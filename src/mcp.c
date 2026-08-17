/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <unistd.h>

#define VS_MCP_VERSION "2026-07-28"
#define VS_MCP_LEGACY_VERSION "2025-11-25"
#define VS_MCP_READ_TIMEOUT 45

typedef struct { char *p; size_t n; size_t cap; } MBuf;

static char *mdup(const char *s){size_t n=s?strlen(s):0;char *p=(char*)malloc(n+1);if(!p)return NULL;if(s)memcpy(p,s,n);p[n]=0;return p;}
static void mcopy(char *d,size_t c,const char *s){size_t n;if(!d||c==0)return;if(!s)s="";n=strlen(s);if(n>=c)n=c-1;memcpy(d,s,n);d[n]=0;}
static int mb_init(MBuf *b,size_t c){b->p=(char*)malloc(c);if(!b->p)return -1;b->p[0]=0;b->n=0;b->cap=c;return 0;}
static int mb_addn(MBuf *b,const char *s,size_t n){char *p;size_t c;if(b->n+n+1>b->cap){c=b->cap?b->cap:1024;while(b->n+n+1>c)c*=2;p=(char*)realloc(b->p,c);if(!p)return -1;b->p=p;b->cap=c;}memcpy(b->p+b->n,s,n);b->n+=n;b->p[b->n]=0;return 0;}
static int mb_add(MBuf *b,const char *s){return mb_addn(b,s,strlen(s));}

void vs_trace(VSContext *c,const char *kind,const char *detail)
{
    int i,step;
    const char *k=kind?kind:"step", *d=detail?detail:"";
    if(!c)return;
    if(c->trace_count>=VS_MAX_TRACE){for(i=1;i<c->trace_count;i++)c->trace[i-1]=c->trace[i];c->trace_count--;c->trace_dropped++;}
    mcopy(c->trace[c->trace_count].kind,sizeof(c->trace[c->trace_count].kind),k);
    mcopy(c->trace[c->trace_count].detail,sizeof(c->trace[c->trace_count].detail),d);
    c->trace_count++;
    step=(int)(c->trace_dropped+(unsigned long)c->trace_count);
    if(c->trace_callback)c->trace_callback(c->trace_callback_data,step,k,d);
}
void vs_trace_clear(VSContext *c){if(!c)return;memset(c->trace,0,sizeof(c->trace));c->trace_count=0;c->trace_dropped=0;}
void vs_set_trace_callback(VSContext *c,VSTraceCallback callback,void *userdata){if(!c)return;c->trace_callback=callback;c->trace_callback_data=userdata;}

static int server_index(VSContext *c,const char *name){int i;for(i=0;i<c->mcp_server_count;i++)if(!strcmp(c->mcp_servers[i].name,name))return i;return -1;}

static void server_stop(VSMcpServer *s)
{
    int st;
    if(!s)return;
    if(s->in_fd>=0){close(s->in_fd);s->in_fd=-1;}
    if(s->out_fd>=0){close(s->out_fd);s->out_fd=-1;}
    if(s->pid>0){kill((pid_t)s->pid,SIGTERM);(void)waitpid((pid_t)s->pid,&st,WNOHANG);s->pid=0;}
    s->io_pos=s->io_len=0;s->initialized=0;s->session_id[0]=0;
}
void vs_mcp_shutdown(VSContext *c){int i;if(!c)return;for(i=0;i<c->mcp_server_count;i++)server_stop(&c->mcp_servers[i]);}
static void init_server_runtime(VSMcpServer *s){s->pid=0;s->in_fd=-1;s->out_fd=-1;s->next_id=1;s->io_pos=s->io_len=0;s->tools_loaded=0;s->legacy=0;s->initialized=0;s->protocol_version[0]=0;s->session_id[0]=0;}

int vs_mcp_add_stdio(VSContext *c,const char *name,const char *cmd)
{
    int i;
    if(!c||!name||!*name||!cmd||!*cmd)return -1;
    i=server_index(c,name);
    if(i<0){if(c->mcp_server_count>=VS_MAX_MCP_SERVERS)return -1;i=c->mcp_server_count++;memset(&c->mcp_servers[i],0,sizeof(c->mcp_servers[i]));init_server_runtime(&c->mcp_servers[i]);}
    else {server_stop(&c->mcp_servers[i]);init_server_runtime(&c->mcp_servers[i]);}
    mcopy(c->mcp_servers[i].name,sizeof(c->mcp_servers[i].name),name);c->mcp_servers[i].transport=VS_MCP_STDIO;mcopy(c->mcp_servers[i].target,sizeof(c->mcp_servers[i].target),cmd);c->mcp_servers[i].auth[0]=0;c->mcp_servers[i].enabled=1;
    vs_trace(c,"mcp-config","added/updated stdio MCP server");(void)vs_persist_settings(c);return 0;
}
int vs_mcp_add_http(VSContext *c,const char *name,const char *url,const char *bearer)
{
    int i;
    if(!c||!name||!*name||!url||!*url)return -1;
    i=server_index(c,name);
    if(i<0){if(c->mcp_server_count>=VS_MAX_MCP_SERVERS)return -1;i=c->mcp_server_count++;memset(&c->mcp_servers[i],0,sizeof(c->mcp_servers[i]));init_server_runtime(&c->mcp_servers[i]);}
    else {server_stop(&c->mcp_servers[i]);init_server_runtime(&c->mcp_servers[i]);}
    mcopy(c->mcp_servers[i].name,sizeof(c->mcp_servers[i].name),name);c->mcp_servers[i].transport=VS_MCP_HTTP;mcopy(c->mcp_servers[i].target,sizeof(c->mcp_servers[i].target),url);mcopy(c->mcp_servers[i].auth,sizeof(c->mcp_servers[i].auth),bearer?bearer:"");c->mcp_servers[i].enabled=1;
    vs_trace(c,"mcp-config","added/updated remote MCP server");(void)vs_persist_settings(c);return 0;
}
int vs_mcp_remove(VSContext *c,const char *name)
{
    int i,j;if(!c)return -1;i=server_index(c,name);if(i<0)return -1;server_stop(&c->mcp_servers[i]);for(j=i+1;j<c->mcp_server_count;j++)c->mcp_servers[j-1]=c->mcp_servers[j];c->mcp_server_count--;memset(&c->mcp_servers[c->mcp_server_count],0,sizeof(c->mcp_servers[0]));c->mcp_tool_count=0;for(j=0;j<c->mcp_server_count;j++)c->mcp_servers[j].tools_loaded=0;vs_trace(c,"mcp-config","removed MCP server");(void)vs_persist_settings(c);return 0;
}

static int start_stdio(VSContext *c,VSMcpServer *s)
{
    int tochild[2],fromchild[2];pid_t p;char b[512];
    if(s->pid>0&&s->in_fd>=0&&s->out_fd>=0)return 0;
    if(pipe(tochild)!=0||pipe(fromchild)!=0){vs_trace(c,"mcp-error","pipe() failed starting stdio MCP server");return -1;}
    p=fork();if(p<0){close(tochild[0]);close(tochild[1]);close(fromchild[0]);close(fromchild[1]);return -1;}
    if(p==0){dup2(tochild[0],0);dup2(fromchild[1],1);close(tochild[0]);close(tochild[1]);close(fromchild[0]);close(fromchild[1]);execl("/bin/sh","sh","-c",s->target,(char*)0);_exit(127);}
    close(tochild[0]);close(fromchild[1]);s->pid=(long)p;s->in_fd=tochild[1];s->out_fd=fromchild[0];s->io_pos=s->io_len=0;s->initialized=0;s->session_id[0]=0;snprintf(b,sizeof(b),"started stdio MCP server %s (pid %ld)",s->name,s->pid);vs_trace(c,"mcp-start",b);return 0;
}
static int write_all(int fd,const char *p,size_t n){size_t o=0;ssize_t w;while(o<n){w=write(fd,p+o,n-o);if(w<0){if(errno==EINTR)continue;return -1;}if(w==0)return -1;o+=(size_t)w;}return 0;}
static char *read_stdio_line(VSMcpServer *s)
{
    MBuf b;fd_set rf;struct timeval tv;ssize_t r;char *nl;size_t take;
    if(mb_init(&b,4096)!=0)return NULL;
    for(;;){
        if(s->io_pos<s->io_len){nl=(char*)memchr(s->io_buf+s->io_pos,'\n',s->io_len-s->io_pos);if(nl){take=(size_t)(nl-(s->io_buf+s->io_pos));if(mb_addn(&b,s->io_buf+s->io_pos,take)!=0){free(b.p);return NULL;}s->io_pos+=(take+1);return b.p;}if(mb_addn(&b,s->io_buf+s->io_pos,s->io_len-s->io_pos)!=0){free(b.p);return NULL;}s->io_pos=s->io_len=0;if(b.n>VS_MAX_TEXT*4){free(b.p);return NULL;}}
        FD_ZERO(&rf);FD_SET(s->out_fd,&rf);tv.tv_sec=VS_MCP_READ_TIMEOUT;tv.tv_usec=0;r=select(s->out_fd+1,&rf,NULL,NULL,&tv);if(r<=0){free(b.p);return NULL;}r=read(s->out_fd,s->io_buf,sizeof(s->io_buf));if(r<=0){free(b.p);return NULL;}s->io_pos=0;s->io_len=(size_t)r;
    }
}
static char *stdio_request_raw(VSMcpServer *s,const char *req,unsigned long id)
{
    char *line;char pat[64];int tries=0;size_t n=strlen(req);if(write_all(s->in_fd,req,n)||write_all(s->in_fd,"\n",1))return NULL;snprintf(pat,sizeof(pat),"\"id\":%lu",id);
    while(tries++<64){line=read_stdio_line(s);if(!line)return NULL;if(strstr(line,pat))return line;free(line);}return NULL;
}
static int stdio_notify(VSMcpServer *s,const char *json){return write_all(s->in_fd,json,strlen(json))||write_all(s->in_fd,"\n",1)?-1:0;}

static char *extract_sse_json(char *r)
{
    char *p,*best=NULL,*e;size_t n;char *out;if(!r)return NULL;p=r;while((p=strstr(p,"data:"))!=NULL){p+=5;while(*p==' '||*p=='\t')p++;if(*p=='{')best=p;}if(!best)return r;e=strchr(best,'\n');n=e?(size_t)(e-best):strlen(best);while(n&&best[n-1]=='\r')n--;out=(char*)malloc(n+1);if(!out)return r;memcpy(out,best,n);out[n]=0;free(r);return out;
}
static char *make_meta_request(unsigned long id,const char *method,const char *name,const char *args)
{
    MBuf b;char num[64];char *en=NULL;if(mb_init(&b,2048)!=0)return NULL;snprintf(num,sizeof(num),"%lu",id);mb_add(&b,"{\"jsonrpc\":\"2.0\",\"id\":");mb_add(&b,num);mb_add(&b,",\"method\":\"");mb_add(&b,method);mb_add(&b,"\",\"params\":{");if(name){en=vs_json_escape(name);mb_add(&b,"\"name\":\"");mb_add(&b,en?en:"");mb_add(&b,"\",");if(en)free(en);}if(args){mb_add(&b,"\"arguments\":");mb_add(&b,args);mb_add(&b,",");}mb_add(&b,"\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"" VS_MCP_VERSION "\",\"io.modelcontextprotocol/clientInfo\":{\"name\":\"VibeSolaris\",\"version\":\"" VS_VERSION "\"},\"io.modelcontextprotocol/clientCapabilities\":{}}}}");return b.p;
}
static char *make_legacy_request(unsigned long id,const char *method,const char *name,const char *args)
{
    MBuf b;char num[64];char *en=NULL;if(mb_init(&b,1024)!=0)return NULL;snprintf(num,sizeof(num),"%lu",id);mb_add(&b,"{\"jsonrpc\":\"2.0\",\"id\":");mb_add(&b,num);mb_add(&b,",\"method\":\"");mb_add(&b,method);mb_add(&b,"\",\"params\":{");if(name){en=vs_json_escape(name);mb_add(&b,"\"name\":\"");mb_add(&b,en?en:"");mb_add(&b,"\"");if(en)free(en);if(args)mb_add(&b,",");}if(args){mb_add(&b,"\"arguments\":");mb_add(&b,args);}mb_add(&b,"}}");return b.p;
}
static void extract_protocol_version(const char *json,char *out,size_t cap)
{
    const char *p,*q;size_t n;if(!out||cap==0)return;out[0]=0;if(!json)return;p=strstr(json,"\"protocolVersion\"");if(!p)return;p=strchr(p,':');if(!p)return;p++;while(*p==' '||*p=='\t')p++;if(*p!='\"')return;p++;q=strchr(p,'\"');if(!q)return;n=(size_t)(q-p);if(n>=cap)n=cap-1;memcpy(out,p,n);out[n]=0;
}
static int legacy_initialize_stdio(VSContext *c,VSMcpServer *s)
{
    unsigned long id;char req[768];char *r;char pv[32];char b[256];if(start_stdio(c,s)!=0)return -1;if(s->initialized&&s->legacy)return 0;
    id=s->next_id++;snprintf(req,sizeof(req),"{\"jsonrpc\":\"2.0\",\"id\":%lu,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"%s\",\"capabilities\":{},\"clientInfo\":{\"name\":\"VibeSolaris\",\"version\":\"" VS_VERSION "\"}}}",id,VS_MCP_LEGACY_VERSION);vs_trace(c,"mcp-negotiate","trying legacy MCP initialize over stdio");r=stdio_request_raw(s,req,id);if(!r)return -1;if(strstr(r,"\"error\"")){free(r);return -1;}extract_protocol_version(r,pv,sizeof(pv));free(r);mcopy(s->protocol_version,sizeof(s->protocol_version),pv[0]?pv:VS_MCP_LEGACY_VERSION);if(stdio_notify(s,"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}")!=0)return -1;s->legacy=1;s->initialized=1;snprintf(b,sizeof(b),"%s negotiated legacy MCP %s",s->name,s->protocol_version);vs_trace(c,"mcp-negotiate",b);return 0;
}
static int legacy_initialize_http(VSContext *c,VSMcpServer *s)
{
    unsigned long id;char req[768];char *r,*nr;const char *h[8];char auth[640],vh[96],sh[320];int n=0;long st=0;char pv[32],sess[256],b[320];
    if(s->initialized&&s->legacy)return 0;
    id=s->next_id++;snprintf(req,sizeof(req),"{\"jsonrpc\":\"2.0\",\"id\":%lu,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"%s\",\"capabilities\":{},\"clientInfo\":{\"name\":\"VibeSolaris\",\"version\":\"" VS_VERSION "\"}}}",id,VS_MCP_LEGACY_VERSION);h[n++]="Content-Type: application/json";h[n++]="Accept: application/json, text/event-stream";if(s->auth[0]){snprintf(auth,sizeof(auth),"Authorization: Bearer %s",s->auth);h[n++]=auth;}vs_trace(c,"mcp-negotiate","trying legacy MCP initialize over Streamable HTTP");sess[0]=0;r=vs_http_post_capture_ctx(c,s->target,h,n,req,&st,sess,sizeof(sess));if(!r||st<200||st>=300){if(r)free(r);return -1;}r=extract_sse_json(r);if(strstr(r,"\"error\"")){free(r);return -1;}extract_protocol_version(r,pv,sizeof(pv));free(r);mcopy(s->protocol_version,sizeof(s->protocol_version),pv[0]?pv:VS_MCP_LEGACY_VERSION);mcopy(s->session_id,sizeof(s->session_id),sess);
    n=0;h[n++]="Content-Type: application/json";h[n++]="Accept: application/json, text/event-stream";snprintf(vh,sizeof(vh),"MCP-Protocol-Version: %s",s->protocol_version);h[n++]=vh;if(s->session_id[0]){snprintf(sh,sizeof(sh),"Mcp-Session-Id: %s",s->session_id);h[n++]=sh;}if(s->auth[0]){snprintf(auth,sizeof(auth),"Authorization: Bearer %s",s->auth);h[n++]=auth;}nr=vs_http_post_ctx(c,s->target,h,n,"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}",&st);if(nr)free(nr);if(st<200||st>=300)return -1;s->legacy=1;s->initialized=1;snprintf(b,sizeof(b),"%s negotiated legacy MCP %s%s",s->name,s->protocol_version,s->session_id[0]?" with HTTP session":"");vs_trace(c,"mcp-negotiate",b);return 0;
}
static int legacy_initialize(VSContext *c,VSMcpServer *s){if(s->transport==VS_MCP_STDIO)return legacy_initialize_stdio(c,s);return legacy_initialize_http(c,s);}

static char *request_stdio(VSContext *c,VSMcpServer *s,const char *method,const char *name,const char *args)
{
    char *req,*line;unsigned long id;if(start_stdio(c,s)!=0)return mdup("{\"error\":\"could not start stdio MCP server\"}");if(s->legacy&&!s->initialized&&legacy_initialize_stdio(c,s)!=0)return mdup("{\"error\":\"legacy stdio MCP initialize failed\"}");id=s->next_id++;req=s->legacy?make_legacy_request(id,method,name,args):make_meta_request(id,method,name,args);if(!req)return NULL;line=stdio_request_raw(s,req,id);free(req);if(!line){server_stop(s);return mdup("{\"error\":\"stdio MCP response timeout or EOF\"}");}return line;
}
static char *request_http(VSContext *c,VSMcpServer *s,const char *method,const char *name,const char *args)
{
    char *req,*resp;const char *h[10];char auth[640],mh[256],nh[384],vh[96],sh[320];int n=0;long st=0;unsigned long id;if(s->legacy&&!s->initialized&&legacy_initialize_http(c,s)!=0)return mdup("{\"error\":\"legacy HTTP MCP initialize failed\"}");id=s->next_id++;req=s->legacy?make_legacy_request(id,method,name,args):make_meta_request(id,method,name,args);if(!req)return NULL;h[n++]="Content-Type: application/json";h[n++]="Accept: application/json, text/event-stream";
    if(s->legacy){snprintf(vh,sizeof(vh),"MCP-Protocol-Version: %s",s->protocol_version[0]?s->protocol_version:VS_MCP_LEGACY_VERSION);h[n++]=vh;if(s->session_id[0]){snprintf(sh,sizeof(sh),"Mcp-Session-Id: %s",s->session_id);h[n++]=sh;}}
    else {h[n++]="MCP-Protocol-Version: " VS_MCP_VERSION;snprintf(mh,sizeof(mh),"Mcp-Method: %s",method);h[n++]=mh;if(name&&*name){snprintf(nh,sizeof(nh),"Mcp-Name: %s",name);h[n++]=nh;}}
    if(s->auth[0]){snprintf(auth,sizeof(auth),"Authorization: Bearer %s",s->auth);h[n++]=auth;}resp=vs_http_post_ctx(c,s->target,h,n,req,&st);free(req);if(!resp)return mdup("{\"error\":\"remote MCP HTTP request failed\"}");if(st<200||st>=300){char b[128];snprintf(b,sizeof(b),"remote MCP HTTP status %ld",st);vs_trace(c,"mcp-error",b);}return extract_sse_json(resp);
}
static char *request_server(VSContext *c,VSMcpServer *s,const char *method,const char *name,const char *args)
{
    char b[512];snprintf(b,sizeof(b),"%s -> %s%s%s [%s]",s->name,method,name?" / ":"",name?name:"",s->legacy?(s->protocol_version[0]?s->protocol_version:"legacy"):VS_MCP_VERSION);vs_trace(c,"mcp-request",b);if(s->transport==VS_MCP_STDIO)return request_stdio(c,s,method,name,args);return request_http(c,s,method,name,args);
}

static const char *skip_ws(const char *p){while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;return p;}
static const char *skip_string(const char *p){if(*p!='\"')return p;p++;while(*p){if(*p=='\\'&&p[1])p+=2;else if(*p=='\"')return p+1;else p++;}return p;}
static const char *balanced_end(const char *p,char open,char close){int d=0;const char *q=p;if(*q!=open)return NULL;while(*q){if(*q=='\"'){q=skip_string(q);continue;}if(*q==open)d++;else if(*q==close){d--;if(d==0)return q+1;}q++;}return NULL;}
static char *json_string_field(const char *obj,const char *end,const char *key)
{
    char pat[160];const char *p,*q;MBuf b;snprintf(pat,sizeof(pat),"\"%s\"",key);p=obj;while((p=strstr(p,pat))&&p<end){p+=strlen(pat);p=skip_ws(p);if(*p!=':')continue;p=skip_ws(p+1);if(*p!='\"')continue;p++;if(mb_init(&b,128)!=0)return NULL;while(p<end&&*p&&*p!='\"'){if(*p=='\\'&&p+1<end){p++;switch(*p){case 'n':mb_addn(&b,"\n",1);break;case 'r':mb_addn(&b,"\r",1);break;case 't':mb_addn(&b,"\t",1);break;default:mb_addn(&b,p,1);break;}p++;}else{q=p+1;mb_addn(&b,p,(size_t)(q-p));p=q;}}return b.p;}return NULL;
}
static char *json_object_field(const char *obj,const char *end,const char *key)
{
    char pat[160];const char *p,*q;size_t n;char *o;snprintf(pat,sizeof(pat),"\"%s\"",key);p=strstr(obj,pat);if(!p||p>=end)return NULL;p+=strlen(pat);p=skip_ws(p);if(*p!=':')return NULL;p=skip_ws(p+1);if(*p!='{')return NULL;q=balanced_end(p,'{','}');if(!q||q>end)return NULL;n=(size_t)(q-p);o=(char*)malloc(n+1);if(!o)return NULL;memcpy(o,p,n);o[n]=0;return o;
}
static int parse_tools_for_server(VSContext *c,VSMcpServer *s,const char *json)
{
    const char *p,*arr,*end,*oe;char *name,*desc,*schema;VSMcpTool *t;int count=0;p=strstr(json,"\"tools\"");if(!p)return -1;p=strchr(p,'[');if(!p)return -1;arr=p+1;end=balanced_end(p,'[',']');if(!end)return -1;p=arr;while(p<end&&c->mcp_tool_count<VS_MAX_MCP_TOOLS){p=skip_ws(p);while(p<end&&*p!='{')p++;if(p>=end)break;oe=balanced_end(p,'{','}');if(!oe||oe>end)break;name=json_string_field(p,oe,"name");if(name&&*name){desc=json_string_field(p,oe,"description");schema=json_object_field(p,oe,"inputSchema");t=&c->mcp_tools[c->mcp_tool_count++];memset(t,0,sizeof(*t));mcopy(t->server,sizeof(t->server),s->name);mcopy(t->name,sizeof(t->name),name);mcopy(t->description,sizeof(t->description),desc?desc:"");mcopy(t->input_schema,sizeof(t->input_schema),schema?schema:"{\"type\":\"object\"}");count++;if(desc)free(desc);if(schema)free(schema);}if(name)free(name);p=oe;}s->tools_loaded=1;return count;
}
int vs_mcp_refresh_server(VSContext *c,int index)
{
    char *r,b[320];int rc;if(!c||index<0||index>=c->mcp_server_count)return -1;if(!c->mcp_servers[index].enabled)return 0;r=request_server(c,&c->mcp_servers[index],"tools/list",NULL,NULL);if(!r)return -1;rc=parse_tools_for_server(c,&c->mcp_servers[index],r);free(r);
    if(rc<0&&!c->mcp_servers[index].legacy){vs_trace(c,"mcp-negotiate","modern MCP request was not accepted; trying 2025-11-25 initialize compatibility");if(legacy_initialize(c,&c->mcp_servers[index])==0){r=request_server(c,&c->mcp_servers[index],"tools/list",NULL,NULL);if(r){rc=parse_tools_for_server(c,&c->mcp_servers[index],r);free(r);}}}
    snprintf(b,sizeof(b),"%s exposed %d tool(s) via %s",c->mcp_servers[index].name,rc<0?0:rc,c->mcp_servers[index].legacy?(c->mcp_servers[index].protocol_version[0]?c->mcp_servers[index].protocol_version:"legacy"):VS_MCP_VERSION);vs_trace(c,rc<0?"mcp-error":"mcp-discover",b);return rc;
}
int vs_mcp_refresh_all(VSContext *c,int force)
{
    int i,total=0,need=force;if(!c)return -1;if(!force){for(i=0;i<c->mcp_server_count;i++)if(c->mcp_servers[i].enabled&&!c->mcp_servers[i].tools_loaded){need=1;break;}if(!need)return c->mcp_tool_count;}c->mcp_tool_count=0;for(i=0;i<c->mcp_server_count;i++){c->mcp_servers[i].tools_loaded=0;if(c->mcp_servers[i].enabled){int n=vs_mcp_refresh_server(c,i);if(n>0)total+=n;}}return total;
}
char *vs_mcp_call(VSContext *c,const char *server,const char *tool,const char *args)
{
    int i;char *r;char b[512];if(!c||!server||!tool)return mdup("ERROR: invalid MCP call");i=server_index(c,server);if(i<0)return mdup("ERROR: MCP server not configured");if(!args||!*args)args="{}";snprintf(b,sizeof(b),"calling %s.%s args=%.300s",server,tool,args);vs_trace(c,"mcp-call",b);r=request_server(c,&c->mcp_servers[i],"tools/call",tool,args);if(!r)return mdup("ERROR: MCP call failed");snprintf(b,sizeof(b),"%s.%s result %.360s",server,tool,r);vs_trace(c,"mcp-result",b);return r;
}
char *vs_mcp_prompt_fragment(const VSContext *c)
{
    MBuf b;int i;if(!c||c->mcp_tool_count<=0)return mdup("(no MCP tools configured/discovered)\n");if(mb_init(&b,4096)!=0)return NULL;mb_add(&b,"Available MCP tools. To call one, emit exactly one directive: [[VS_MCP server=\"SERVER\" tool=\"TOOL\" args=\"{\\\"key\\\":\\\"value\\\"}\"]]. The host will execute it and return MCP_RESULT.\n");for(i=0;i<c->mcp_tool_count;i++){mb_add(&b,"- ");mb_add(&b,c->mcp_tools[i].server);mb_add(&b,".");mb_add(&b,c->mcp_tools[i].name);if(c->mcp_tools[i].description[0]){mb_add(&b," — ");mb_add(&b,c->mcp_tools[i].description);}mb_add(&b,"\n  inputSchema: ");mb_add(&b,c->mcp_tools[i].input_schema);mb_add(&b,"\n");}return b.p;
}
