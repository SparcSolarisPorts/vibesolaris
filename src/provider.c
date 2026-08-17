/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *p; size_t len; size_t cap; int failed; } VSBuf;

static char *dupstr(const char *s){size_t n=s?strlen(s):0;char *p=(char*)malloc(n+1);if(!p)return 0;if(s)memcpy(p,s,n);p[n]=0;return p;}
static int binit(VSBuf *b,size_t cap){if(cap<256)cap=256;if(cap>VS_MAX_REQUEST_BODY)cap=VS_MAX_REQUEST_BODY;b->p=(char*)malloc(cap);if(!b->p)return -1;b->p[0]=0;b->len=0;b->cap=cap;b->failed=0;return 0;}
static int bgrow(VSBuf *b,size_t add){char *n;size_t need,c,max=(size_t)VS_MAX_REQUEST_BODY;if(b->failed)return -1;if(add>max||b->len>max-add-1){b->failed=1;return -1;}need=b->len+add+1;if(need<=b->cap)return 0;c=b->cap?b->cap:256;while(c<need){if(c>max/2){c=max;break;}c*=2;}if(c<need||c>max){b->failed=1;return -1;}n=(char*)realloc(b->p,c);if(!n){b->failed=1;return -1;}b->p=n;b->cap=c;return 0;}
static int badd(VSBuf *b,const char *s){size_t n=s?strlen(s):0;if(bgrow(b,n)!=0)return -1;if(n)memcpy(b->p+b->len,s,n);b->len+=n;b->p[b->len]=0;return 0;}
static int bquoted(VSBuf *b,const char *s){char *e=vs_json_escape(s);int rc;if(!e){b->failed=1;return -1;}rc=badd(b,"\"");if(!rc)rc=badd(b,e);if(!rc)rc=badd(b,"\"");free(e);return rc;}
static char *bfinish(VSContext *c,VSBuf *b){if(!b||!b->p)return NULL;if(b->failed){if(c)vs_trace(c,"limit","request body exceeded the safe in-memory limit or allocation failed");free(b->p);b->p=NULL;return NULL;}return b->p;}
static char *bounded_attachment(VSContext *c,char *t,const char *path){char *q;if(!t)return NULL;q=vs_compact_text_limit(t,VS_MAX_ATTACHMENT_TEXT,"large attachment compacted");if(q&&strlen(q)<strlen(t)){char b[512];snprintf(b,sizeof(b),"attachment %.360s compacted before sending",path?path:"file");vs_trace(c,"limit",b);}free(t);return q;}

static int hex4(const char *p,unsigned long *v){int i;unsigned long n=0;for(i=0;i<4;i++){unsigned char c=(unsigned char)p[i];n<<=4;if(c>='0'&&c<='9')n+=(unsigned long)(c-'0');else if(c>='a'&&c<='f')n+=(unsigned long)(c-'a'+10);else if(c>='A'&&c<='F')n+=(unsigned long)(c-'A'+10);else return -1;}*v=n;return 0;}
static size_t put_utf8(char *o,size_t n,unsigned long cp){if(cp<=0x7fUL)o[n++]=(char)cp;else if(cp<=0x7ffUL){o[n++]=(char)(0xc0U|(cp>>6));o[n++]=(char)(0x80U|(cp&0x3fU));}else if(cp<=0xffffUL){o[n++]=(char)(0xe0U|(cp>>12));o[n++]=(char)(0x80U|((cp>>6)&0x3fU));o[n++]=(char)(0x80U|(cp&0x3fU));}else if(cp<=0x10ffffUL){o[n++]=(char)(0xf0U|(cp>>18));o[n++]=(char)(0x80U|((cp>>12)&0x3fU));o[n++]=(char)(0x80U|((cp>>6)&0x3fU));o[n++]=(char)(0x80U|(cp&0x3fU));}return n;}
static char *json_unescape_string(const char *p){
    size_t cap=strlen(p)+1,n=0;char *o=(char*)malloc(cap);if(!o)return 0;
    while(*p && *p!='"'){
        if(*p=='\\'){
            unsigned long cp=0,lo=0;
            p++;if(!*p)break;
            switch(*p){
                case 'n':o[n++]='\n';break;case 'r':o[n++]='\r';break;case 't':o[n++]='\t';break;
                case 'b':o[n++]='\b';break;case 'f':o[n++]='\f';break;case '"':o[n++]='"';break;
                case '\\':o[n++]='\\';break;case '/':o[n++]='/';break;
                case 'u':
                    if(hex4(p+1,&cp)==0){
                        p+=4;
                        if(cp>=0xd800UL&&cp<=0xdbffUL&&p[1]=='\\'&&p[2]=='u'&&hex4(p+3,&lo)==0&&lo>=0xdc00UL&&lo<=0xdfffUL){cp=0x10000UL+((cp-0xd800UL)<<10)+(lo-0xdc00UL);p+=6;}
                        if(cp>=0xd800UL&&cp<=0xdfffUL)cp=0xfffdUL;
                        n=put_utf8(o,n,cp);
                    } else o[n++]='u';
                    break;
                default:o[n++]=*p;break;
            }
        }else o[n++]=*p;
        p++;
    }
    o[n]=0;return o;
}
static char *extract_after(const char *json,const char *needle){const char *p=strstr(json,needle);if(!p)return 0;p+=strlen(needle);while(*p&&*p!='"')p++;if(*p=='"')p++;return json_unescape_string(p);}
static char *extract_text(const char *json){char *r;
    if((r=extract_after(json,"\"output_text\":")))return r;
    if((r=extract_after(json,"\"text\":")))return r;
    if((r=extract_after(json,"\"content\":")))return r;
    return dupstr(json);
}
static long extract_long(const char *json,const char *needle){const char *p=strstr(json,needle);if(!p)return 0;p+=strlen(needle);while(*p==' '||*p=='\t'||*p==':')p++;return strtol(p,0,10);}
static void capture_usage(VSContext *c,const char *json){
    long cached=0,cachew=0,in=0,out=0,total=0;int have=0,anthropic_style=0;char b[256];
    if(!c||!json)return;
    cached=extract_long(json,"\"cached_tokens\"");
    if(!cached)cached=extract_long(json,"\"cache_read_input_tokens\"");
    if(!cached)cached=extract_long(json,"\"cachedContentTokenCount\"");
    cachew=extract_long(json,"\"cache_write_tokens\"");
    if(!cachew)cachew=extract_long(json,"\"cache_creation_input_tokens\"");
    c->provider_cached_tokens=cached;c->provider_cache_write_tokens=cachew;

    if(strstr(json,"\"prompt_tokens\"")){in=extract_long(json,"\"prompt_tokens\"");have=1;}
    else if(strstr(json,"\"promptTokenCount\"")){in=extract_long(json,"\"promptTokenCount\"");have=1;}
    else if(strstr(json,"\"input_tokens\"")){in=extract_long(json,"\"input_tokens\"");anthropic_style=1;have=1;}

    if(strstr(json,"\"completion_tokens\"")){out=extract_long(json,"\"completion_tokens\"");have=1;}
    else if(strstr(json,"\"candidatesTokenCount\"")){out=extract_long(json,"\"candidatesTokenCount\"");have=1;}
    else if(strstr(json,"\"output_tokens\"")){out=extract_long(json,"\"output_tokens\"");have=1;}

    if(anthropic_style){if(cached>0)in+=cached;if(cachew>0)in+=cachew;}
    if(strstr(json,"\"total_tokens\"")){total=extract_long(json,"\"total_tokens\"");have=1;}
    else if(strstr(json,"\"totalTokenCount\"")){total=extract_long(json,"\"totalTokenCount\"");have=1;}
    else if(have)total=in+out;

    c->provider_input_tokens=in;c->provider_output_tokens=out;c->provider_total_tokens=total;
    if(have){
        if(in>0)c->conversation_input_tokens+=(unsigned long)in;
        if(out>0)c->conversation_output_tokens+=(unsigned long)out;
        if(total>0)c->conversation_total_tokens+=(unsigned long)total;
        else c->conversation_total_tokens+=(unsigned long)((in>0?in:0)+(out>0?out:0));
        c->conversation_usage_responses++;
        snprintf(b,sizeof(b),"tokens input=%ld output=%ld total=%ld cached=%ld",in,out,total,cached);
        vs_trace(c,"usage",b);
    }
}
static int gpt56plus(const char *m){double v;if(!m||strncmp(m,"gpt-",4))return 0;v=atof(m+4);return v>=5.6;}
static const char *mime_for(const char *path){const char *e=strrchr(path,'.');if(!e)return "image/png";e++;if(!strcmp(e,"jpg")||!strcmp(e,"jpeg")||!strcmp(e,"JPG")||!strcmp(e,"JPEG"))return "image/jpeg";if(!strcmp(e,"gif")||!strcmp(e,"GIF"))return "image/gif";if(!strcmp(e,"webp")||!strcmp(e,"WEBP"))return "image/webp";return "image/png";}

static char *request_url(const char *base,VSProtocolKind protocol){
    size_t n;const char *suffix;char *o;
    if(!base)return dupstr("");
    if(protocol==VS_PROTOCOL_GEMINI)return dupstr(base);
    if(protocol==VS_PROTOCOL_ANTHROPIC){
        if(strstr(base,"/v1/messages"))return dupstr(base);
        suffix="/v1/messages";
    }else{
        if(strstr(base,"/chat/completions"))return dupstr(base);
        suffix="/chat/completions";
    }
    n=strlen(base)+strlen(suffix)+2;o=(char*)malloc(n);if(!o)return 0;
    strcpy(o,base);while(strlen(o)>0&&o[strlen(o)-1]=='/')o[strlen(o)-1]=0;strcat(o,suffix);return o;
}

char *vs_build_system_prompt(const VSContext *c){
    const char *base="You are VibeSolaris, a local coding agent. You may ask the host to use local tools by emitting exactly one directive on a line: [[VS_TOOL read path=\"FILE\"]], [[VS_TOOL run cmd=\"COMMAND\"]], [[VS_TOOL write path=\"FILE\" content=\"TEXT_WITH_\\n_ESCAPES\"]], or [[VS_TOOL image path=\"IMAGE_FILE\"]]. The host will execute it and return TOOL_RESULT as ordinary text in the next continuation message; there is no separate hidden TOOL_RESULT transport/channel for the user to restore. The image tool loads a local PNG/JPEG/GIF/WebP into the next model round as real visual input; use it after creating a screenshot, render, plot, CAD viewport capture, or other image that you need to inspect. When the user attaches an image, inspect the visual content directly rather than pretending it is only text. For GUI screenshots, distinguish visible pixel/layout observations from inferred causes; for CAD images, do not invent hidden dimensions or geometry that are not visible. A useful visual-debug loop is: inspect image -> inspect/edit source -> run/build -> capture a new screenshot/render with the run tool -> load it with the image tool -> compare and continue. MCP tools, when available, are listed below and may be called only with the documented VS_MCP directive. Prefer inspecting files before editing. The local command tool remains available throughout the agent turn: a non-zero exit, timeout, compiler error, or malformed command is a result to inspect and correct, NOT evidence that command execution has been disabled. IMPORTANT: merely writing prose such as `I ran pwd` does not run anything. To execute a command you must emit the literal directive [[VS_TOOL run cmd=\"COMMAND\"]] on its own line. Never report that VS_TOOL, TOOL_RESULT, or the workspace/tool bridge failed unless you actually emitted a VS_TOOL directive and the host explicitly returned COMMAND_RUNNER_STATUS: host_error. If a continuation contains TOOL_RESULT_DELIVERED: yes and COMMAND_RUNNER_STATUS: operational, the tool channel is proven healthy. Do not tell the user that VS_TOOL, TOOL_RESULT, or command execution is unavailable merely because a command failed; correct the command and continue. For execution requests, DO NOT stop after saying what you plan or intend to do and DO NOT ask the user to send another prompt just to continue. Actually execute the work with VS_TOOL/VS_MCP, continuing through build/test/verification as appropriate. Ask the user only when genuinely blocked by missing information, credentials, destructive-action permission, or a decision that cannot safely be inferred; then emit [[VS_NEED_USER question=\"YOUR QUESTION\"]]. When an execution request is genuinely complete, emit [[VS_FINAL]] before the concise final result. NEVER place [[VS_FINAL]] in the same response as a VS_TOOL or VS_MCP directive: a tool/MCP directive means work is still pending and the host will execute it before any final result is accepted. Ordinary informational questions may be answered normally without tools or markers. Never assume Linux; honour the detected OS and CPU architecture.\n";
    char *mcp=vs_mcp_prompt_fragment(c);size_t n=strlen(base)+strlen(c->agent_md)+strlen(c->os_name)+strlen(c->os_release)+strlen(c->arch)+strlen(c->cwd)+(mcp?strlen(mcp):0)+768;char *o=(char*)malloc(n);
    if(!o){if(mcp)free(mcp);return NULL;}
    snprintf(o,n,"%sHost OS: %s %s\nCPU architecture: %s\nWorking directory: %s\n\nAGENT.MD:\n%s\n\nMCP TOOLS:\n%s\n",base,c->os_name,c->os_release,c->arch,c->cwd,c->agent_md[0]?c->agent_md:"(none)",mcp?mcp:"(none)");if(mcp)free(mcp);return o;
}

static int add_openai_msg(VSBuf *b,const char *role,const char *content,int *first){
    if(!*first)badd(b,",");
    *first=0;badd(b,"{\"role\":");bquoted(b,role);badd(b,",\"content\":");bquoted(b,content);badd(b,"}");return 0;
}

static char *build_openai_body(VSContext *c,const char *sys,const char *user){
    VSBuf b;int i,first=1;char *x,*t,*e;const char *mime;
    if(binit(&b,8192)!=0)return NULL;
    badd(&b,"{\"model\":");bquoted(&b,c->provider.model);badd(&b,",\"messages\":[");
    if(c->provider.kind==VS_PROVIDER_OPENAI && c->cache_enabled && gpt56plus(c->provider.model)){
        first=0;badd(&b,"{\"role\":\"system\",\"content\":[{\"type\":\"text\",\"text\":");bquoted(&b,sys);badd(&b,",\"prompt_cache_breakpoint\":{\"mode\":\"explicit\"}}]}");
    } else add_openai_msg(&b,"system",sys,&first);
    for(i=0;i<c->history_count;i++)add_openai_msg(&b,c->history[i].role,c->history[i].content,&first);
    if(!first)badd(&b,",");
    badd(&b,"{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":");bquoted(&b,user);badd(&b,"}");
    for(i=0;i<c->attachment_count;i++){
        if(c->attachments[i].is_image){x=vs_cached_base64_file(c,c->attachments[i].path,0);if(x){char *label=(char*)malloc(strlen(c->attachments[i].path)+32);mime=mime_for(c->attachments[i].path);if(label){sprintf(label,"Attached image: %s",c->attachments[i].path);badd(&b,",{\"type\":\"text\",\"text\":");bquoted(&b,label);badd(&b,"}");free(label);}badd(&b,",{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:");badd(&b,mime);badd(&b,";base64,");badd(&b,x);badd(&b,"\"}}");free(x);}}
        else {t=vs_cached_read_file(c,c->attachments[i].path);if(t){t=bounded_attachment(c,t,c->attachments[i].path);if(!t)continue;e=vs_json_escape(t);badd(&b,",{\"type\":\"text\",\"text\":\"Attached file: ");x=vs_json_escape(c->attachments[i].path);badd(&b,x);free(x);badd(&b,"\\n");badd(&b,e);badd(&b,"\"}");free(e);free(t);}}
    }
    badd(&b,"]}]");
    if(c->provider.kind==VS_PROVIDER_OPENAI && c->cache_enabled){badd(&b,",\"prompt_cache_key\":");bquoted(&b,c->cache_key);if(gpt56plus(c->provider.model))badd(&b,",\"prompt_cache_options\":{\"mode\":\"implicit\",\"ttl\":\"30m\"}");}
    badd(&b,",\"temperature\":0.2}");return bfinish(c,&b);
}

static char *build_claude_body(VSContext *c,const char *sys,const char *user){
    VSBuf b;int i,first=1;char *x,*t;const char *mime;
    if(binit(&b,8192)!=0)return NULL;
    badd(&b,"{\"model\":");bquoted(&b,c->provider.model);badd(&b,",\"max_tokens\":8192");
    if(c->cache_enabled)badd(&b,",\"cache_control\":{\"type\":\"ephemeral\"}");
    badd(&b,",\"system\":[{\"type\":\"text\",\"text\":");bquoted(&b,sys);if(c->cache_enabled)badd(&b,",\"cache_control\":{\"type\":\"ephemeral\"}");badd(&b,"}],\"messages\":[");
    for(i=0;i<c->history_count;i++){if(!first)badd(&b,",");first=0;badd(&b,"{\"role\":");bquoted(&b,!strcmp(c->history[i].role,"assistant")?"assistant":"user");badd(&b,",\"content\":");bquoted(&b,c->history[i].content);badd(&b,"}");}
    if(!first)badd(&b,",");
    badd(&b,"{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":");bquoted(&b,user);badd(&b,"}");
    for(i=0;i<c->attachment_count;i++){
        if(c->attachments[i].is_image){x=vs_cached_base64_file(c,c->attachments[i].path,0);if(x){char *label=(char*)malloc(strlen(c->attachments[i].path)+32);mime=mime_for(c->attachments[i].path);if(label){sprintf(label,"Attached image: %s",c->attachments[i].path);badd(&b,",{\"type\":\"text\",\"text\":");bquoted(&b,label);badd(&b,"}");free(label);}badd(&b,",{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":");bquoted(&b,mime);badd(&b,",\"data\":");bquoted(&b,x);badd(&b,"}}");free(x);}}
        else {t=vs_cached_read_file(c,c->attachments[i].path);if(t){t=bounded_attachment(c,t,c->attachments[i].path);if(!t)continue;badd(&b,",{\"type\":\"text\",\"text\":");x=(char*)malloc(strlen(c->attachments[i].path)+strlen(t)+32);if(x){sprintf(x,"Attached file: %s\n%s",c->attachments[i].path,t);bquoted(&b,x);free(x);}else bquoted(&b,t);badd(&b,"}");free(t);}}
    }
    badd(&b,"]}]}");return bfinish(c,&b);
}

static char *build_gemini_body(VSContext *c,const char *sys,const char *user){
    VSBuf b;int i,first=1;char *x,*t;const char *mime;
    if(binit(&b,8192)!=0)return NULL;
    badd(&b,"{\"system_instruction\":{\"parts\":[{\"text\":");bquoted(&b,sys);badd(&b,"}]},\"contents\":[");
    for(i=0;i<c->history_count;i++){if(!first)badd(&b,",");first=0;badd(&b,"{\"role\":");bquoted(&b,!strcmp(c->history[i].role,"assistant")?"model":"user");badd(&b,",\"parts\":[{\"text\":");bquoted(&b,c->history[i].content);badd(&b,"}]}");}
    if(!first)badd(&b,",");
    badd(&b,"{\"role\":\"user\",\"parts\":[{\"text\":");bquoted(&b,user);badd(&b,"}");
    for(i=0;i<c->attachment_count;i++){
        if(c->attachments[i].is_image){x=vs_cached_base64_file(c,c->attachments[i].path,0);if(x){char *label=(char*)malloc(strlen(c->attachments[i].path)+32);mime=mime_for(c->attachments[i].path);if(label){sprintf(label,"Attached image: %s",c->attachments[i].path);badd(&b,",{\"text\":");bquoted(&b,label);badd(&b,"}");free(label);}badd(&b,",{\"inlineData\":{\"mimeType\":");bquoted(&b,mime);badd(&b,",\"data\":");bquoted(&b,x);badd(&b,"}}");free(x);}}
        else {t=vs_cached_read_file(c,c->attachments[i].path);if(t){t=bounded_attachment(c,t,c->attachments[i].path);if(!t)continue;badd(&b,",{\"text\":");x=(char*)malloc(strlen(c->attachments[i].path)+strlen(t)+32);if(x){sprintf(x,"Attached file: %s\n%s",c->attachments[i].path,t);bquoted(&b,x);free(x);}else bquoted(&b,t);badd(&b,"}");free(t);}}
    }
    badd(&b,"]}]}");return bfinish(c,&b);
}

char *vs_chat(VSContext *c,const char *user){
    char *sys=vs_build_system_prompt(c),*body,*resp,*out,*u,*endpoint;
    const char *h[5];
    char auth[VS_OAUTH_TOKEN_MAX+64],apiheader[VS_OAUTH_TOKEN_MAX+64],oauth_err[512];
    const char *credential;long status=0;int nh=0;
    c->provider_cached_tokens=0;c->provider_cache_write_tokens=0;c->provider_input_tokens=0;c->provider_output_tokens=0;c->provider_total_tokens=0;vs_refresh_cache_key(c);
    { char tb[512]; snprintf(tb,sizeof(tb),"provider=%s protocol=%s model=%s",c->provider.name,vs_protocol_name(c->provider.protocol),c->provider.model); vs_trace(c,"model-request",tb); }

    if(c->provider.protocol==VS_PROTOCOL_GEMINI){
        if(!c->provider.api_key[0]){free(sys);return dupstr("No API key configured for Gemini.");}
        body=build_gemini_body(c,sys,user);
        if(!body){free(sys);return dupstr("Request is too large or memory is exhausted; reduce attachments/context and retry.");}
        u=(char*)malloc(strlen(c->provider.base_url)+strlen(c->provider.model)+strlen(c->provider.api_key)+64);
        if(!u){free(body);free(sys);return dupstr("Out of memory");}
        sprintf(u,"%s/%s:generateContent?key=%s",c->provider.base_url,c->provider.model,c->provider.api_key);
        h[0]="Content-Type: application/json";
        resp=vs_http_post_ctx(c,u,h,1,body,&status);free(u);free(body);free(sys);
        if(!resp)return dupstr("HTTP request failed");
        capture_usage(c,resp);out=extract_text(resp);free(resp);return out;
    }

    credential=c->provider.api_key;
    if(c->provider.kind==VS_PROVIDER_OPENAI && c->oauth.access_token[0]){
        oauth_err[0]=0;
        if(vs_oauth_ensure_access_token(c,oauth_err,sizeof(oauth_err))==0) credential=c->oauth.access_token;
        else if(!credential[0]){free(sys);return dupstr(oauth_err[0]?oauth_err:"OAuth access token is unavailable");}
    }
    if(!credential[0]){
        free(sys);
        return dupstr(c->provider.kind==VS_PROVIDER_OPENAI?"No OpenAI credential configured. Enter an API key or complete OAuth login.":"No API key configured for the selected provider.");
    }

    endpoint=request_url(c->provider.base_url,c->provider.protocol);
    if(!endpoint||!endpoint[0]){free(endpoint);free(sys);return dupstr("No API URL configured for the selected provider/protocol.");}

    if(c->provider.protocol==VS_PROTOCOL_ANTHROPIC){
        body=build_claude_body(c,sys,user);
        if(!body){free(endpoint);free(sys);return dupstr("Request is too large or memory is exhausted; reduce attachments/context and retry.");}
        h[nh++]="Content-Type: application/json";
        if(c->provider.kind==VS_PROVIDER_CLAUDE || c->provider.kind==VS_PROVIDER_DEEPSEEK){
            snprintf(apiheader,sizeof(apiheader),"x-api-key: %s",credential);h[nh++]=apiheader;
        }else{
            snprintf(auth,sizeof(auth),"Authorization: Bearer %s",credential);h[nh++]=auth;
        }
        h[nh++]="anthropic-version: 2023-06-01";
        resp=vs_http_post_ctx(c,endpoint,h,nh,body,&status);
    }else{
        body=build_openai_body(c,sys,user);
        if(!body){free(endpoint);free(sys);return dupstr("Request is too large or memory is exhausted; reduce attachments/context and retry.");}
        snprintf(auth,sizeof(auth),"Authorization: Bearer %s",credential);
        h[0]="Content-Type: application/json";h[1]=auth;
        resp=vs_http_post_ctx(c,endpoint,h,2,body,&status);
    }
    free(endpoint);free(body);free(sys);
    if(!resp)return dupstr("HTTP request failed");
    capture_usage(c,resp);out=extract_text(resp);free(resp);return out;
}
