#include <pthread.h>
/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static char *dupstr(const char *s){size_t n=s?strlen(s):0;char *p=(char*)malloc(n+1);if(!p)return 0;if(s)memcpy(p,s,n);p[n]=0;return p;}
static char *attr(const char *s,const char *name){char pat[64];const char *p,*e;char *o;size_t n;snprintf(pat,sizeof(pat),"%s=\"",name);p=strstr(s,pat);if(!p)return 0;p+=strlen(pat);e=p;while(*e && !(*e=='"' && (e==p || e[-1]!='\\')))e++;n=(size_t)(e-p);o=(char*)malloc(n+1);if(!o)return 0;memcpy(o,p,n);o[n]=0;return o;}
static void unesc(char *s){char *r=s,*w=s;while(r&&*r){if(*r=='\\'&&r[1]){r++;if(*r=='n')*w++='\n';else if(*r=='t')*w++='\t';else if(*r=='r')*w++='\r';else *w++=*r;r++;}else *w++=*r++;}if(s)*w=0;}

/* Models occasionally narrate a plan instead of executing it.  Keep this
   deliberately small and ASCII-only for old C/Solaris toolchains. */
static int ascii_contains_ci(const char *s,const char *needle){
    size_t i,n;if(!s||!needle||!*needle)return 0;n=strlen(needle);
    for(;*s;s++){
        for(i=0;i<n;i++){
            unsigned char a=(unsigned char)s[i],b=(unsigned char)needle[i];
            if(!a)return 0;
            if(a>='A'&&a<='Z')a=(unsigned char)(a-'A'+'a');
            if(b>='A'&&b<='Z')b=(unsigned char)(b-'A'+'a');
            if(a!=b)break;
        }
        if(i==n)return 1;
    }
    return 0;
}
static int execution_intent(const char *s){
    static const char *k[]={" add "," make "," fix "," edit "," update "," modify "," implement "," build "," create "," write "," change "," run "," test "," debug "," compile "," install "," remove "," replace "," refactor "," convert "," do all "," do that "," do it "," go ahead "," proceed "," continue "," keep going "," carry on ",0};
    char *q;size_t n;int i;if(!s)return 0;n=strlen(s);q=(char*)malloc(n+3);if(!q)return 0;q[0]=' ';memcpy(q+1,s,n);q[n+1]=' ';q[n+2]=0;
    for(i=0;k[i];i++)if(ascii_contains_ci(q,k[i])){free(q);return 1;}
    free(q);return 0;
}
static int prospective_language(const char *s){
    static const char *k[]={"i'll ","i will ","i am going to ","i'm going to ","going to inspect","going to check","going to edit","going to update","going to fix","going to run","let me inspect","let me check","let me edit","let me update","let me fix","next i will","next, i will","first i will","first, i will","i will now","will now inspect","will now edit","will now run",0};int i;
    if(!s)return 0;
    for(i=0;k[i];i++)if(ascii_contains_ci(s,k[i]))return 1;
    return 0;
}
static int false_tool_unavailable_claim(const char *s){
    static const char *k[]={
        "no tool_result","no tool result","tool_result channel","tool result channel","vs_tool result channel","restore the vs_tool","restore vs_tool",
        "cannot run commands","can't run commands","unable to run commands","cannot execute commands","can't execute commands","unable to execute commands",
        "can no longer run commands","command execution is unavailable","command tool is unavailable","local command tool is unavailable","command runner is unavailable",
        "tooling is unavailable","command directives are currently returning no","not returning any tool_result","not returning tool_result","not returning any tool result",
        "not returning tool result","re-enable the workspace tool bridge","reenable the workspace tool bridge","re-enable the tool bridge","reenable the tool bridge",
        "workspace tool bridge","tool bridge is broken","tool bridge unavailable","tool bridge stopped","tool bridge has stopped","bridge stopped returning",
        "tool stopped returning","tools stopped returning","command results stopped","no results after","results are not coming back","results are no longer",
        "please restore the tool bridge","provide the command results so i can continue",0
    };int i,toolish,blocked;if(!s)return 0;for(i=0;k[i];i++)if(ascii_contains_ci(s,k[i]))return 1;
    toolish=ascii_contains_ci(s,"tool")||ascii_contains_ci(s,"command")||ascii_contains_ci(s,"vs_tool")||ascii_contains_ci(s,"tool_result")||ascii_contains_ci(s,"workspace bridge");
    blocked=ascii_contains_ci(s,"not returning")||ascii_contains_ci(s,"stopped returning")||ascii_contains_ci(s,"no result")||ascii_contains_ci(s,"no results")||ascii_contains_ci(s,"unavailable")||ascii_contains_ci(s,"cannot continue")||ascii_contains_ci(s,"can't continue")||ascii_contains_ci(s,"unable to continue")||ascii_contains_ci(s,"restore")||ascii_contains_ci(s,"re-enable")||ascii_contains_ci(s,"reenable")||ascii_contains_ci(s,"broken");
    return toolish&&blocked;
}
static int inspection_request(const char *s){
    static const char *k[]={"look at this project","look at the project","look into this project","inspect this project","inspect the project","look at the files","look into the files","read the project","review the project","review this repo","inspect the repo","look at this repo","explore the codebase",0};int i;
    if(!s)return 0;for(i=0;k[i];i++)if(ascii_contains_ci(s,k[i]))return 1;return 0;
}
static int should_auto_continue(const char *original,const char *answer){if(!execution_intent(original)||!prospective_language(answer))return 0;if(ascii_contains_ci(answer,"if you want")||ascii_contains_ci(answer,"if you'd like"))return 0;return 1;}
static char *strip_marker(const char *s,const char *marker){const char *p;char *o;size_t a,b;if(!s)return dupstr("");p=strstr(s,marker);if(!p)return dupstr(s);a=(size_t)(p-s);b=strlen(p+strlen(marker));o=(char*)malloc(a+b+1);if(!o)return 0;memcpy(o,s,a);memcpy(o+a,p+strlen(marker),b+1);return o;}

/* ---------- bounded autonomous-turn working memory ---------- */
static void agent_history_drop_one(VSContext *c){int i;if(!c||c->agent_history_count<=0)return;if(c->agent_history[0].content)free(c->agent_history[0].content);if(c->agent_history_bytes>=c->agent_history[0].bytes)c->agent_history_bytes-=c->agent_history[0].bytes;else c->agent_history_bytes=0;for(i=1;i<c->agent_history_count;i++)c->agent_history[i-1]=c->agent_history[i];memset(&c->agent_history[c->agent_history_count-1],0,sizeof(c->agent_history[0]));c->agent_history_count--;}
static void agent_history_drop_oldest(VSContext *c){if(c->agent_history_count>=2&&!strcmp(c->agent_history[0].role,"user")&&!strcmp(c->agent_history[1].role,"assistant")){agent_history_drop_one(c);agent_history_drop_one(c);}else agent_history_drop_one(c);}
static void agent_history_clear(VSContext *c){int i;if(!c)return;for(i=0;i<c->agent_history_count;i++)if(c->agent_history[i].content)free(c->agent_history[i].content);memset(c->agent_history,0,sizeof(c->agent_history));c->agent_history_count=0;c->agent_history_bytes=0;}
static void agent_history_add(VSContext *c,const char *role,const char *content){char *q;size_t n;if(!c||!content)return;q=vs_compact_text_limit(content,VS_AGENT_HISTORY_MESSAGE_MAX,"older autonomous-turn message compacted");if(!q)return;n=strlen(q);while(c->agent_history_count>=VS_MAX_AGENT_HISTORY||(c->agent_history_count>0&&c->agent_history_bytes+n>VS_AGENT_HISTORY_BUDGET))agent_history_drop_oldest(c);strncpy(c->agent_history[c->agent_history_count].role,role?role:"user",sizeof(c->agent_history[c->agent_history_count].role)-1);c->agent_history[c->agent_history_count].content=q;c->agent_history[c->agent_history_count].bytes=n;c->agent_history_bytes+=n;c->agent_history_count++;}
static void agent_checkpoint_clear(VSContext *c){if(!c)return;if(c->agent_checkpoint)free(c->agent_checkpoint);c->agent_checkpoint=0;c->agent_checkpoint_bytes=0;}
static void agent_context_clear(VSContext *c){if(!c)return;agent_history_clear(c);agent_checkpoint_clear(c);c->attachment_send_from=0;}
static void append_snprintf(char *o,size_t cap,size_t *n,const char *fmt,...);

static char *build_task_anchor(VSContext *c,const char *original){char *base,*out;size_t cap,n;int i;size_t user_limit=(c&&c->attachment_count>0)?(12U*1024U):VS_ACTIVE_TASK_MAX;base=vs_compact_text_limit(original,user_limit,"active task anchor compacted");if(!base)return 0;if(!c||c->attachment_count<=0)return base;cap=strlen(base)+512+(size_t)c->attachment_count*320U;out=(char*)malloc(cap);if(!out){free(base);return 0;}n=(size_t)snprintf(out,cap,"%s\n\nATTACHMENT_MANIFEST (content is sent on the first model round only; use VS_TOOL read/image to revisit a local attachment):\n",base);free(base);for(i=0;i<c->attachment_count&&n<cap-64;i++)append_snprintf(out,cap,&n,"- %.260s%s\n",c->attachments[i].path,c->attachments[i].is_image?" [image]":"");return out;}

static int compact_interval(void){const char *e=getenv("VIBESOLARIS_AGENT_COMPACT_ROUNDS");char *ep=0;long v;if(!e||!*e)return VS_AGENT_COMPACT_ROUNDS;v=strtol(e,&ep,10);if(!ep||*ep)return VS_AGENT_COMPACT_ROUNDS;if(v==0)return 0;if(v<4)v=4;if(v>64)v=64;return (int)v;}

static void append_snprintf(char *o,size_t cap,size_t *n,const char *fmt,...){va_list ap;int w;if(!o||!n||*n>=cap)return;va_start(ap,fmt);w=vsnprintf(o+*n,cap-*n,fmt,ap);va_end(ap);if(w<0)return;if((size_t)w>=cap-*n)*n=cap-1;else *n+=(size_t)w;}
static char *fallback_checkpoint(VSContext *c,const char *task){size_t cap=VS_AGENT_CHECKPOINT_MAX*2+1024,n=0;char *o,*q;int i;o=(char*)malloc(cap);if(!o)return 0;o[0]=0;append_snprintf(o,cap,&n,"ACTIVE TASK:\n%s\n\nWORKING STATE (mechanically compacted):\n",task?task:"");if(c->agent_checkpoint&&n<cap){q=vs_compact_text_limit(c->agent_checkpoint,VS_AGENT_CHECKPOINT_MAX/2,"older checkpoint compacted");if(q){append_snprintf(o,cap,&n,"Previous checkpoint:\n%s\n",q);free(q);}}
    for(i=0;i<c->agent_history_count&&n<cap-128;i++){q=vs_compact_text_limit(c->agent_history[i].content,3072,"message excerpt");if(q){append_snprintf(o,cap,&n,"\n%s: %s\n",c->agent_history[i].role,q);free(q);}}
    q=vs_compact_text_limit(o,VS_AGENT_CHECKPOINT_MAX,"autonomous working-state checkpoint");free(o);return q;
}

static int bad_checkpoint(const char *s){if(!s||!*s)return 1;if(strstr(s,"[[VS_TOOL")||strstr(s,"[[VS_MCP")||strstr(s,"[[VS_FINAL")||strstr(s,"[[VS_NEED_USER"))return 1;if(!strncmp(s,"HTTP request failed",19)||!strncmp(s,"provider error",14)||!strncmp(s,"No API",6))return 1;return 0;}

static void agent_compact_impl(VSContext *c,const char *task,int completed_rounds,int force){
    char *p,*summary,*bounded,*fallback;size_t n;int interval=compact_interval(),saved_attachment_from;VSHTTPChunkCallback saved_stream;void *saved_stream_data;char trace[256];
    if(!c||c->agent_history_count<=0)return;
    if(!force){
        if(interval==0)return;
        if((completed_rounds%interval)!=0&&c->agent_history_bytes<(VS_AGENT_HISTORY_BUDGET*3)/4)return;
    }
    n=strlen(task?task:"")+(c->agent_checkpoint?strlen(c->agent_checkpoint):0)+1200;p=(char*)malloc(n);if(!p)return;
    snprintf(p,n,
        "INTERNAL_CONTEXT_COMPACTION. Return ONLY a dense working-state summary for another coding-agent round. Do not call tools, do not answer the user, and do not emit VS_* markers. Preserve: objective/constraints; host OS/toolchain facts; files inspected or changed; exact important errors; commands/tests and outcomes; design decisions; what has already been tried; unresolved issues; and the next concrete steps. Drop repeated protocol instructions, filler, duplicate logs, superseded plans, and unimportant raw output.\n\nACTIVE_TASK:\n%s\n\nPREVIOUS_CHECKPOINT:\n%s\n",
        task?task:"",c->agent_checkpoint?c->agent_checkpoint:"(none)");
    vs_trace(c,"compact","compacting autonomous working context");
    /* Compaction is an internal provider round; never expose its token stream
       through the active UI callback. */
    {
        saved_stream=c->stream_callback;
        saved_stream_data=c->stream_callback_data;
        c->stream_callback=NULL;c->stream_callback_data=NULL;
    /* A newly loaded image is intended for the next normal agent round, not
       for this housekeeping request.  Suppress attachments while compacting
       and restore the pending one-shot attachment range afterwards. */
    saved_attachment_from=c->attachment_send_from;
    c->attachment_send_from=c->attachment_count;
    summary=vs_chat(c,p);
    c->attachment_send_from=saved_attachment_from;
    c->stream_callback=saved_stream;c->stream_callback_data=saved_stream_data;
    free(p);
    if(bad_checkpoint(summary)){if(summary)free(summary);fallback=fallback_checkpoint(c,task);summary=fallback;vs_trace(c,"compact","provider compaction was unusable; kept a bounded mechanical checkpoint instead");}
    if(!summary)return;
    bounded=vs_compact_text_limit(summary,VS_AGENT_CHECKPOINT_MAX,"semantic working-state checkpoint compacted");free(summary);if(!bounded)return;
    agent_checkpoint_clear(c);c->agent_checkpoint=bounded;c->agent_checkpoint_bytes=strlen(bounded);agent_history_clear(c);
    snprintf(trace,sizeof(trace),"working context compacted after %d rounds to %lu bytes",completed_rounds,(unsigned long)c->agent_checkpoint_bytes);vs_trace(c,"compact",trace);
    }
}

static void agent_compact(VSContext *c,const char *task,int completed_rounds){
    agent_compact_impl(c,task,completed_rounds,0);
}

/* Public forced compaction used by the interactive /compact command. */
void agent_compact_force(VSContext *ctx,const char *task){
    agent_compact_impl(ctx,task,0,1);
}

/* Release per-turn working memory (history + checkpoint) owned by a context.
   Turn boundaries keep this state now, so contexts that are destroyed rather
   than reused — subagent children in particular — must release it here. */
void vs_agent_context_release(VSContext *ctx){
    if(!ctx)return;
    agent_history_clear(ctx);
    agent_checkpoint_clear(ctx);
}

static char *continuation_prompt(const char *task,const char *result,int planning_only,const char *checkpoint){
    const char *lead=planning_only?"The previous response did not complete the execution lifecycle. Continue now with concrete work.":"TOOL_RESULT:";
    size_t n=strlen(task?task:"")+strlen(result?result:"")+strlen(checkpoint?checkpoint:"")+strlen(lead)+512;char *o=(char*)malloc(n);if(!o)return 0;
    snprintf(o,n,"ACTIVE_TASK:\n%s\n\nCOMPACTED_WORKING_STATE:\n%s\n\n%s\n%s\n\nContinue efficiently from this state. Use one or more VS_TOOL/VS_MCP directives, VS_NEED_USER for a genuine blocker, or VS_FINAL only when complete. Do not redo completed inspection.\n",task?task:"",checkpoint?checkpoint:"(none)",lead,result?result:"(no result)");return o;
}
/* Auto-mode plan deepening: sent back when a [[VS_PLAN]] arrives with too
   little read-only investigation behind it.  Asks the model to verify the
   draft against the real codebase before the plan is adopted, so the do
   phase executes a grounded plan instead of guesses. */
static char *plan_deepen_prompt(const char *task,const char *draft,int explore_rounds,int deferrals,int max_deferrals,const char *checkpoint){
    size_t n=strlen(task?task:"")+strlen(draft?draft:"")+strlen(checkpoint?checkpoint:"")+1600;char *o=(char*)malloc(n);if(!o)return 0;
    snprintf(o,n,
        "PLANNING PHASE (Auto mode): a draft plan arrived after only %d read-only exploration round(s), so it has NOT been adopted yet (deferral %d of %d). Ground the plan in the actual codebase before it is accepted: for every task in the draft, read the files it will touch, grep/glob for the symbols, functions, and paths it references and confirm they exist as assumed, and look for what the draft missed (callers, error handling, existing tests that cover the area). Use ONLY read-only tools (read, list, search, glob, grep, graph, web_search, web_fetch) - do not modify anything yet. When the plan matches reality, re-emit the refined [[VS_PLAN ...]] and it will be adopted automatically.\n\nDRAFT PLAN (unverified):\n%.*s\n\nACTIVE_TASK:\n%s\n\nCOMPACTED_WORKING_STATE:\n%s\n",
        explore_rounds,deferrals,max_deferrals,1200,draft?draft:"",task?task:"",checkpoint?checkpoint:"(none)");
    return o;
}
static char *tool_channel_recovery_prompt(const char *task,const char *last_result,const char *claim,int actual_tool_seen,const char *checkpoint){
    size_t n=strlen(task?task:"")+strlen(last_result?last_result:"")+strlen(claim?claim:"")+strlen(checkpoint?checkpoint:"")+900;char *o=(char*)malloc(n);if(!o)return 0;
    snprintf(o,n,"ACTIVE_TASK:\n%s\n\nCOMPACTED_WORKING_STATE:\n%s\n\nTOOL_CHANNEL_RECOVERY: the host independently verified that the command runner works. %s Do not ask the user to restore VS_TOOL or TOOL_RESULT.\nLAST_TOOL_RESULT:\n%s\n\nINCORRECT_PREVIOUS_CLAIM:\n%s\n\nContinue with an actual VS_TOOL/VS_MCP directive, or VS_FINAL only if the task is complete.\n",task?task:"",checkpoint?checkpoint:"(none)",actual_tool_seen?"A real tool request was already executed in this turn.":"The previous response did not actually issue a tool directive.",last_result?last_result:"(none)",claim?claim:"");return o;
}

/* ---------- native, OS-independent repository inspection tools ---------- */
typedef struct {char *p;size_t n,cap;int truncated;} ABuffer;
static int ab_init(ABuffer *b,size_t cap){b->p=(char*)malloc(cap);if(!b->p)return -1;b->p[0]=0;b->n=0;b->cap=cap;b->truncated=0;return 0;}
static void ab_add(ABuffer *b,const char *s){size_t m,room;if(!b||!b->p||!s||b->truncated)return;m=strlen(s);room=b->cap-b->n;if(room<=96){b->truncated=1;return;}if(m>=room-96){m=room-96;b->truncated=1;}memcpy(b->p+b->n,s,m);b->n+=m;b->p[b->n]=0;}
static void ab_addf(ABuffer *b,const char *fmt,...){char t[2048];va_list ap;va_start(ap,fmt);vsnprintf(t,sizeof(t),fmt,ap);va_end(ap);ab_add(b,t);}
static char *ab_finish(ABuffer *b){if(b->truncated&&b->p&&b->cap>b->n+48){const char *m="\n...[native tool output truncated]...\n";size_t z=strlen(m);if(z<b->cap-b->n){memcpy(b->p+b->n,m,z+1);b->n+=z;}}return b->p;}
static int skip_meta_dir(const char *n){return !strcmp(n,".git")||!strcmp(n,".hg")||!strcmp(n,".svn");}
static int join_path(char *out,size_t cap,const char *a,const char *b){size_t na=strlen(a);if(na+strlen(b)+2>cap)return -1;strcpy(out,a);if(na&&a[na-1]!='/')strcat(out,"/");strcat(out,b);return 0;}
static long long_attr(const char *d,const char *name,long def,long lo,long hi){char *s=attr(d,name),*ep=0;long v;int ok;if(!s)return def;v=strtol(s,&ep,10);ok=(ep&&*ep==0);free(s);if(!ok)return def;if(v<lo)v=lo;if(v>hi)v=hi;return v;}

static char *native_read(VSContext *c,const char *d){char *p=attr(d,"path"),*raw,*start_s,*max_s,*out;long start=1,max_lines=240,total=0,line=1,last;const char *r,*ls,*le;ABuffer b;if(!p)return dupstr("ERROR: read requires path=\"FILE\"");unesc(p);raw=vs_cached_read_file(c,p);if(!raw){free(p);return dupstr("ERROR: unable to read file");}start_s=attr(d,"start_line");max_s=attr(d,"max_lines");if(!start_s&&!max_s){out=vs_compact_text_limit(raw,VS_AGENT_READ_RESULT_MAX,"file read compacted; use start_line/max_lines for a precise range");free(raw);free(p);return out;}if(start_s){start=strtol(start_s,0,10);if(start<1)start=1;}if(max_s){max_lines=strtol(max_s,0,10);if(max_lines<1)max_lines=1;if(max_lines>4000)max_lines=4000;}free(start_s);free(max_s);
    for(r=raw;*r;r++)if(*r=='\n')total++;
    if(raw[0]&&r>raw&&r[-1]!='\n')total++;
    last=start+max_lines-1;
    if(ab_init(&b,VS_AGENT_READ_RESULT_MAX+512)!=0){free(raw);free(p);return 0;}
    ab_addf(&b,"[read %s lines %ld-%ld of %ld]\n",p,start,last<total?last:total,total);ls=raw;line=1;
    while(*ls&&line<start){le=strchr(ls,'\n');if(!le){ls+=strlen(ls);break;}ls=le+1;line++;}
    while(*ls&&line<=last&&!b.truncated){size_t m;le=strchr(ls,'\n');if(!le)le=ls+strlen(ls);m=(size_t)(le-ls);if(m>0){char *tmp=(char*)malloc(m+2);if(!tmp)break;memcpy(tmp,ls,m);tmp[m]='\n';tmp[m+1]=0;ab_add(&b,tmp);free(tmp);}else ab_add(&b,"\n");if(!*le)break;ls=le+1;line++;}
    out=ab_finish(&b);free(raw);free(p);return out;
}

static void native_list_rec(VSContext *c,const char *path,int depth,int max_depth,long limit,long *count,ABuffer *b){DIR *dp;struct dirent *de;char full[VS_MAX_PATH];struct stat st;if(*count>=limit||b->truncated||vs_cancel_requested(c))return;dp=opendir(path);if(!dp){ab_addf(b,"! cannot open %s\n",path);return;}while((de=readdir(dp))!=0&&*count<limit&&!b->truncated){if(!strcmp(de->d_name,".")||!strcmp(de->d_name,".."))continue;if(depth>0&&skip_meta_dir(de->d_name))continue;if(join_path(full,sizeof(full),path,de->d_name)!=0)continue;if(lstat(full,&st)!=0)continue;if(S_ISDIR(st.st_mode)){ab_addf(b,"d %s/\n",full);(*count)++;if(depth<max_depth)native_list_rec(c,full,depth+1,max_depth,limit,count,b);}else if(S_ISLNK(st.st_mode)){ab_addf(b,"l %s\n",full);(*count)++;}else if(S_ISREG(st.st_mode)){ab_addf(b,"f %s\n",full);(*count)++;}if(vs_cancel_requested(c))break;}closedir(dp);}
static char *native_list(VSContext *c,const char *d){char *p=attr(d,"path"),*out;long depth=long_attr(d,"depth",2,0,8),limit=long_attr(d,"limit",300,1,2000),count=0;ABuffer b;struct stat st;if(!p)p=dupstr(".");if(!p)return 0;unesc(p);if(ab_init(&b,VS_AGENT_TOOL_RESULT_MAX)!=0){free(p);return 0;}if(lstat(p,&st)!=0)ab_addf(&b,"ERROR: cannot stat %s\n",p);else if(S_ISDIR(st.st_mode)){ab_addf(&b,"[list %s depth=%ld limit=%ld; VCS metadata directories skipped]\n",p,depth,limit);native_list_rec(c,p,0,(int)depth,limit,&count,&b);}else ab_addf(&b,"f %s\n",p);out=ab_finish(&b);free(p);return out;}

static int contains_text(const char *hay,const char *needle,int ci){size_t i,n;if(!ci)return strstr(hay,needle)!=0;n=strlen(needle);if(!n)return 1;for(;*hay;hay++){for(i=0;i<n;i++){unsigned char a=(unsigned char)hay[i],bb=(unsigned char)needle[i];if(!a)return 0;if(a>='A'&&a<='Z')a=(unsigned char)(a-'A'+'a');if(bb>='A'&&bb<='Z')bb=(unsigned char)(bb-'A'+'a');if(a!=bb)break;}if(i==n)return 1;}return 0;}
static void search_file(VSContext *c,const char *path,const char *q,int ci,long limit,long *hits,ABuffer *b){FILE *f;unsigned char sample[4096];size_t n,i;char line[8192];long ln=0;if(*hits>=limit||b->truncated||vs_cancel_requested(c))return;f=fopen(path,"rb");if(!f)return;n=fread(sample,1,sizeof(sample),f);for(i=0;i<n;i++)if(sample[i]==0){fclose(f);return;}rewind(f);while(fgets(line,sizeof(line),f)&&*hits<limit&&!b->truncated){char snippet[640];size_t m;ln++;if(!contains_text(line,q,ci))continue;m=strlen(line);while(m&& (line[m-1]=='\n'||line[m-1]=='\r'))m--;if(m>560)m=560;memcpy(snippet,line,m);snippet[m]=0;ab_addf(b,"%s:%ld: %s%s\n",path,ln,snippet,strlen(line)>m?" ...":"");(*hits)++;if(vs_cancel_requested(c))break;}fclose(f);}
static void native_search_rec(VSContext *c,const char *path,const char *q,int ci,long limit,long *hits,ABuffer *b){DIR *dp;struct dirent *de;char full[VS_MAX_PATH];struct stat st;if(*hits>=limit||b->truncated||vs_cancel_requested(c))return;dp=opendir(path);if(!dp)return;while((de=readdir(dp))!=0&&*hits<limit&&!b->truncated){if(!strcmp(de->d_name,".")||!strcmp(de->d_name,"..")||skip_meta_dir(de->d_name))continue;if(join_path(full,sizeof(full),path,de->d_name)!=0)continue;if(lstat(full,&st)!=0)continue;if(S_ISDIR(st.st_mode))native_search_rec(c,full,q,ci,limit,hits,b);else if(S_ISREG(st.st_mode))search_file(c,full,q,ci,limit,hits,b);if(vs_cancel_requested(c))break;}closedir(dp);}
static char *native_search(VSContext *c,const char *d){char *p=attr(d,"path"),*q=attr(d,"query"),*ci_s=attr(d,"case_insensitive"),*out;int ci=ci_s&&atoi(ci_s)!=0;long limit=long_attr(d,"limit",100,1,1000),hits=0;struct stat st;ABuffer b;free(ci_s);if(!p)p=dupstr(".");if(!p||!q){free(p);free(q);return dupstr("ERROR: search requires query=\"LITERAL\"");}unesc(p);unesc(q);if(ab_init(&b,VS_AGENT_TOOL_RESULT_MAX)!=0){free(p);free(q);return 0;}ab_addf(&b,"[literal search path=%s query=%s limit=%ld%s]\n",p,q,limit,ci?" case-insensitive":"");if(lstat(p,&st)!=0)ab_addf(&b,"ERROR: cannot stat %s\n",p);else if(S_ISDIR(st.st_mode))native_search_rec(c,p,q,ci,limit,&hits,&b);else if(S_ISREG(st.st_mode))search_file(c,p,q,ci,limit,&hits,&b);ab_addf(&b,"[matches=%ld]\n",hits);out=ab_finish(&b);free(p);free(q);return out;}

static int solaris_host(const VSContext *c){return c&&(!strcmp(c->os_name,"SunOS")||strstr(c->os_name,"Solaris"));}
static int solaris_command_mismatch(const char *raw){return raw&&(ascii_contains_ci(raw,"illegal option")||ascii_contains_ci(raw,"unknown option")||ascii_contains_ci(raw,"not found")||ascii_contains_ci(raw,"usage:"));}
static int find_attachment(const VSContext *c,const char *p){int i;if(!c||!p)return -1;for(i=0;i<c->attachment_count;i++)if(!strcmp(c->attachments[i].path,p))return i;return -1;}

/* Extended tool set advertised in the system prompt.  Each branch maps a
   VS_TOOL directive onto the real module implementation.  Returns NULL when
   the operation is not recognised here, so execute_tool can keep its
   original fallthrough. */
static char *execute_extended_tool(VSContext *c,const char *d){
    char *p,*q,*r,*s;int st;
    if(!strncmp(d+10,"glob ",5)){
        char results[VS_MAX_GLOB_RESULTS][VS_MAX_PATH];int count=0;
        p=attr(d,"path");q=attr(d,"pattern");
        if(p)unesc(p);if(q)unesc(q);
        if(!q||!*q){free(p);free(q);return dupstr("ERROR: glob requires pattern=\"...\"");}
        r=vs_glob(p&&*p?p:".",q,&count,results);
        free(p);free(q);return r?r:dupstr("ERROR: glob failed");
    }
    if(!strncmp(d+10,"grep ",5)){
        int ci=0,ctx=0,lim=100;
        p=attr(d,"path");q=attr(d,"pattern");r=attr(d,"case_insensitive");s=attr(d,"context");
        {char *l=attr(d,"limit");
        if(p)unesc(p);if(q)unesc(q);
        if(r&&atoi(r))ci=1;if(s)ctx=atoi(s);if(l){lim=atoi(l);if(lim<1)lim=1;if(lim>VS_MAX_GREP_RESULTS)lim=VS_MAX_GREP_RESULTS;}
        free(r);free(s);free(l);
        if(!q||!*q){free(p);free(q);return dupstr("ERROR: grep requires pattern=\"...\"");}
        {char *out=vs_grep(p&&*p?p:".",q,ci,ctx,lim);free(p);free(q);return out?out:dupstr("ERROR: grep failed");}}
    }
    if(!strncmp(d+10,"edit ",5)){
        int reps=0;
        p=attr(d,"path");q=attr(d,"search");r=attr(d,"replace");
        if(p)unesc(p);if(q)unesc(q);if(r)unesc(r);
        if(!p||!*p||!q||!*q){free(p);free(q);free(r);return dupstr("ERROR: edit requires path, search and replace attributes");}
        s=vs_edit_file(p,q,r,&reps);
        if(s&&!strncmp(s,"ERROR:",6)){free(p);free(q);free(r);return s;}
        if(s)vs_cache_invalidate(c,p);
        free(p);free(q);free(r);return s?s:dupstr("ERROR: edit failed");
    }
    if(!strncmp(d+10,"multi_edit ",11)){
        char *edits,*cur,*semi;const char *sr[16],*rp[16];int n=0;int total=0;
        p=attr(d,"path");edits=attr(d,"edits");
        if(p)unesc(p);if(edits)unesc(edits);
        if(!p||!*p||!edits||!*edits){free(p);free(edits);return dupstr("ERROR: multi_edit requires path and edits=\"search|replace;...\"");}
        {char *cur=edits,*semi;
        while(cur&&*cur&&n<16){char *bar;
            semi=strchr(cur,';');if(semi)*semi=0;
            bar=strchr(cur,'|');
            if(!bar){free(p);free(edits);return dupstr("ERROR: multi_edit edits must be search|replace pairs separated by ;");}
            *bar=0;sr[n]=cur;rp[n]=bar+1;n++;
            cur=semi?semi+1:NULL;}
        }
        if(n==0){free(p);free(edits);return dupstr("ERROR: multi_edit requires at least one edit");}
        r=vs_multi_edit(p,sr,rp,n,&total);
        if(r&&!strncmp(r,"ERROR:",6)){free(p);free(edits);return r;}
        if(r)vs_cache_invalidate(c,p);
        free(p);free(edits);return r?r:dupstr("ERROR: multi_edit failed");
    }
    if(!strncmp(d+10,"notebook_read ",14)){p=attr(d,"path");if(p)unesc(p);if(!p||!*p){free(p);return dupstr("ERROR: notebook_read requires path");}r=vs_notebook_read(p);free(p);return r?r:dupstr("ERROR: notebook_read failed");}
    if(!strncmp(d+10,"notebook_edit ",14)){
        int cell;
        p=attr(d,"path");q=attr(d,"cell");r=attr(d,"source");
        if(p)unesc(p);if(r)unesc(r);
        cell=q?atoi(q):-1;
        if(!p||!*p||!q||cell<0||!r){free(p);free(q);free(r);return dupstr("ERROR: notebook_edit requires path, cell and source");}
        s=vs_notebook_edit(p,cell,r);free(p);free(q);free(r);return s?s:dupstr("ERROR: notebook_edit failed");
    }
    if(!strncmp(d+10,"monitor ",8)){p=attr(d,"cmd");if(p)unesc(p);if(!p||!*p){free(p);return dupstr("ERROR: monitor requires cmd");}{int id=vs_monitor_start(p);size_t z=64;char *o=(char*)malloc(z);if(o)snprintf(o,z,id>=0?"OK: monitor started with id %d":"ERROR: could not start monitor",id);free(p);return o?o:dupstr("ERROR: monitor result allocation failed");}}
    if(!strncmp(d+10,"monitor_output ",15)){long id=long_attr(d,"monitor_id",-1,0,VS_MAX_MONITORS);if(id<0)return dupstr("ERROR: monitor_output requires monitor_id");return vs_monitor_output((int)id);}
    if(!strncmp(d+10,"monitor_kill ",13)){long id=long_attr(d,"monitor_id",-1,0,VS_MAX_MONITORS);if(id<0)return dupstr("ERROR: monitor_kill requires monitor_id");return vs_monitor_kill((int)id)==0?dupstr("OK: monitor stopped"):dupstr("ERROR: no such monitor or already stopped");}
    if(!strncmp(d+10,"task_create ",12)){
        int prio=VS_TASK_PRIORITY_MEDIUM;
        p=attr(d,"title");q=attr(d,"description");r=attr(d,"priority");
        if(!p||!*p){free(p);free(q);free(r);return dupstr("ERROR: task_create requires title=\"...\"");}
        if(r){if(!strcmp(r,"low"))prio=VS_TASK_PRIORITY_LOW;else if(!strcmp(r,"high"))prio=VS_TASK_PRIORITY_HIGH;}
        if(c->tasks.next_id<=0)vs_task_init(&c->tasks,NULL);
        st=vs_task_create(&c->tasks,p,q&&*q?q:"",prio);
        free(p);free(q);free(r);
        if(st<=0)return dupstr("ERROR: could not create task");
        {size_t z=64;char *o=(char*)malloc(z);if(o)snprintf(o,z,"OK: task %d created",st);return o?o:dupstr("ERROR: task result allocation failed");}
    }
    if(!strncmp(d+10,"task_update ",12)){
        long id;char *id_s=attr(d,"id"),*status=attr(d,"status"),*title=attr(d,"title"),*desc=attr(d,"description"),*ep=NULL;
        id=id_s?strtol(id_s,&ep,10):0;
        if(!id_s||ep==id_s||id<=0){free(id_s);free(status);free(title);free(desc);return dupstr("ERROR: task_update requires id=\"N\"");}
        st=vs_task_update(&c->tasks,(int)id,status&&*status?status:NULL,title,desc);
        free(id_s);free(status);free(title);free(desc);
        return st==0?dupstr("OK: task updated"):dupstr("ERROR: no task with that id");
    }
    if(!strncmp(d+10,"task_list ",10))return vs_task_list(&c->tasks,50);
    if(!strncmp(d+10,"task_search ",12)){p=attr(d,"query");if(p)unesc(p);if(!p||!*p){free(p);return dupstr("ERROR: task_search requires query");}r=vs_task_search(&c->tasks,p,-1,20);free(p);return r?r:dupstr("ERROR: task_search failed");}
    if(!strncmp(d+10,"task_delete ",12)){long id=long_attr(d,"id",0,0,2147483647L);if(id<=0)return dupstr("ERROR: task_delete requires id=\"N\"");return vs_task_delete(&c->tasks,(int)id)==0?dupstr("OK: task deleted"):dupstr("ERROR: no task with that id");}
    if(!strncmp(d+10,"checkpoint ",11)){
        char *action=attr(d,"action"),*desc2=attr(d,"description");char *o;
        if(!action||!*action){free(action);free(desc2);return dupstr("ERROR: checkpoint requires action=\"create|rollback|list\"");}
        if(!strcmp(action,"create")){st=vs_checkpoint_create(&c->checkpoints,desc2&&*desc2?desc2:"agent checkpoint");o=dupstr(st==0?"OK: checkpoint created":"ERROR: could not create checkpoint");}
        else if(!strcmp(action,"rollback")){st=vs_checkpoint_rollback(&c->checkpoints,NULL);o=dupstr(st==0?"OK: rolled back to previous checkpoint":"ERROR: no checkpoint to rollback");}
        else if(!strcmp(action,"list")){o=vs_checkpoint_list(&c->checkpoints,20);if(!o)o=dupstr("ERROR: could not list checkpoints");}
        else o=dupstr("ERROR: checkpoint action must be create, rollback or list");
        free(action);free(desc2);return o;
    }
    if(!strncmp(d+10,"lsp ",4)){
        char *cmd=attr(d,"command");p=attr(d,"path");
        if(p)unesc(p);
        if(!cmd||!p||!*p){free(cmd);free(p);return dupstr("ERROR: lsp requires command=\"diagnostics|definition|hover|rename\" and path");}
        if(!strcmp(cmd,"diagnostics"))r=vs_lsp_diagnostics_compat(p);
        else if(!strcmp(cmd,"definition")){long line=long_attr(d,"line",1,1,1000000),col=long_attr(d,"col",1,1,1000000);r=vs_lsp_definition_compat(p,(int)line,(int)col);}
        else if(!strcmp(cmd,"hover")){long line=long_attr(d,"line",1,1,1000000),col=long_attr(d,"col",1,1,1000000);r=vs_lsp_hover(c->lsp_servers,&c->lsp_server_count,p,(int)line,(int)col);}
        else if(!strcmp(cmd,"rename")){long line=long_attr(d,"line",1,1,1000000),col=long_attr(d,"col",1,1,1000000);char *nn=attr(d,"new_name");if(nn)unesc(nn);r=(nn&&*nn)?vs_lsp_rename(c->lsp_servers,&c->lsp_server_count,p,(int)line,(int)col,nn):dupstr("ERROR: lsp rename requires new_name=\"...\"");free(nn);}
        else{r=0; }
        free(cmd);free(p);
        if(!r)return dupstr("ERROR: unsupported lsp command (use diagnostics, definition, hover or rename)");
        return r;
    }
    if(!strncmp(d+10,"permission ",11)){
        char *action=attr(d,"action"),*tool=attr(d,"tool"),*path=attr(d,"path");char rule[320];char *o;
        if(!action||!*action){free(action);free(tool);free(path);return dupstr("ERROR: permission requires action=\"allow|deny|list\"");}
        if(!strcmp(action,"list")){o=vs_permission_list(&c->permissions,50);if(!o)o=dupstr("ERROR: could not list permissions");}
        else if((!strcmp(action,"allow")||!strcmp(action,"deny"))&&tool&&*tool){
            if(path&&*path)snprintf(rule,sizeof(rule),"%s:%s",tool,path);else snprintf(rule,sizeof(rule),"%s",tool);
            st=vs_permission_add_rule(&c->permissions,rule,!strcmp(action,"allow"));
            o=dupstr(st==0?"OK: permission rule added":"ERROR: could not add permission rule");
        }
        else o=dupstr("ERROR: permission allow/deny requires tool=\"...\"");
        free(action);free(tool);free(path);return o;
    }
    if(!strncmp(d+10,"auto_mode ",10)){
        char *action=attr(d,"action");char *o;
        if(!action||!*action){free(action);return dupstr("ERROR: auto_mode requires action=\"on|off|status|rules\"");}
        if(!strcmp(action,"on")){c->auto_mode.enabled=1;o=dupstr("OK: auto mode enabled");}
        else if(!strcmp(action,"off")){c->auto_mode.enabled=0;o=dupstr("OK: auto mode disabled");}
        else if(!strcmp(action,"status")){size_t z=96;o=(char*)malloc(z);if(o)snprintf(o,z,"auto_mode=%s (%d rules)",c->auto_mode.enabled?"on":"off",c->auto_mode.rule_count);}
        else if(!strcmp(action,"rules")){o=vs_auto_mode_list_rules(&c->auto_mode,50);if(!o)o=dupstr("ERROR: could not list auto mode rules");}
        else o=dupstr("ERROR: auto_mode action must be on, off, status or rules");
        free(action);return o?o:dupstr("ERROR: auto_mode result allocation failed");
    }
    if(!strncmp(d+10,"team_message ",13)||!strncmp(d+10,"team_read ",10)||!strncmp(d+10,"team_broadcast ",15)){
        char *team=attr(d,"team");int i,found=-1;VSTeam *t=NULL;
        if(!team||!*team){free(team);return dupstr("ERROR: team directive requires team=\"NAME\"");}
        for(i=0;i<c->team_count&&i<VS_MAX_TEAMS;i++)if(!strcmp(c->teams[i].id,team)){found=i;break;}
        if(found<0){char *o2;size_t z=strlen(team)+96;o2=(char*)malloc(z);if(o2)snprintf(o2,z,"ERROR: no active team '%s'; create one with team_create first",team);free(team);return o2?o2:dupstr("ERROR: no such team");}
        t=&c->teams[found];
        if(!strncmp(d+10,"team_message ",13)){char *from=attr(d,"from"),*to=attr(d,"to"),*msg=attr(d,"message");if(msg)unesc(msg);r=(from&&to&&msg)?vs_team_message(t,from,to,msg):dupstr("ERROR: team_message requires from, to and message");free(from);free(to);free(msg);}
        else if(!strncmp(d+10,"team_read ",10)){char *agent=attr(d,"agent");r=agent?vs_team_read_messages(t,agent):dupstr("ERROR: team_read requires agent=\"...\"");free(agent);}
        else{char *from=attr(d,"from"),*msg=attr(d,"message");if(msg)unesc(msg);r=(from&&msg)?vs_team_broadcast(t,from,msg):dupstr("ERROR: team_broadcast requires from and message");free(from);free(msg);}
        free(team);return r?r:dupstr("ERROR: team operation failed");
    }
    if(!strncmp(d+10,"code_review ",12)){p=attr(d,"path");q=attr(d,"scope");r=attr(d,"focus");if(p)unesc(p);s=vs_code_review_start(p&&*p?p:".",q&&*q?q:"staged",r&&*r?r:"general");free(p);free(q);free(r);return s?s:dupstr("ERROR: could not start code review");}
    if(!strncmp(d+10,"plugin ",7)){
        char *action=attr(d,"action");char *o;
        if(!action||!*action){free(action);return dupstr("ERROR: plugin requires action=\"install|list|uninstall\"");}
        if(!strcmp(action,"install")){p=attr(d,"url");st=p&&*p?vs_plugin_install(c->plugins,&c->plugin_count,p):-1;o=dupstr(st==0?"OK: plugin installed":"ERROR: plugin install failed");free(p);}
        else if(!strcmp(action,"list")){o=vs_plugin_list(c->plugins,c->plugin_count,50);if(!o)o=dupstr("ERROR: could not list plugins");}
        else if(!strcmp(action,"uninstall")){p=attr(d,"name");st=p&&*p?vs_plugin_uninstall(c->plugins,&c->plugin_count,p):-1;o=dupstr(st==0?"OK: plugin removed":"ERROR: no such plugin");free(p);}
        else o=dupstr("ERROR: plugin action must be install, list or uninstall");
        free(action);return o;
    }
    if(!strncmp(d+10,"init ",5)){p=attr(d,"path");if(p)unesc(p);r=vs_init_repo(p&&*p?p:".");free(p);return r?r:dupstr("ERROR: init failed");}
    if(!strncmp(d+10,"skill ",6)){char *name=attr(d,"name"),*input=attr(d,"input");if(input)unesc(input);r=(name&&*name)?vs_skill_invoke(c->skills,&c->skill_count,name,input):dupstr("ERROR: skill requires name=\"...\"");free(name);free(input);return r?r:dupstr("ERROR: skill invocation failed");}
    if(!strncmp(d+10,"slash_command ",14)){char *name=attr(d,"name"),*args=attr(d,"args");if(args)unesc(args);r=(name&&*name)?vs_slash_execute(&c->slashes,name,args):dupstr("ERROR: slash_command requires name=\"...\"");free(name);free(args);return r?r:dupstr("ERROR: slash command failed");}
    if(!strncmp(d+10,"tool_search ",12)){p=attr(d,"query");if(p)unesc(p);if(!p||!*p){free(p);return dupstr("ERROR: tool_search requires query=\"...\"");}r=vs_tool_search(p);free(p);return r?r:dupstr("ERROR: tool_search failed");}
    if(!strncmp(d+10,"enter_worktree ",15)){p=attr(d,"path");if(p)unesc(p);st=vs_worktree_enter(p&&*p?p:c->cwd,c->worktree_path,sizeof(c->worktree_path));free(p);if(st!=0)return dupstr("ERROR: could not enter worktree (is the directory a git work tree?)");c->worktree_active=1;return dupstr("OK: entered worktree");}
    if(!strncmp(d+10,"exit_worktree ",14)){if(!c->worktree_active)return dupstr("ERROR: not inside a worktree");st=vs_worktree_exit(c->worktree_path);c->worktree_active=0;c->worktree_path[0]=0;return dupstr(st==0?"OK: left worktree":"ERROR: could not leave worktree");}
    if(!strncmp(d+10,"ask_user_question ",18))return dupstr("ERROR: ask_user_question is host-interactive; emit [[VS_NEED_USER question=\"...\" option1=\"...\" option2=\"...\"]] instead and the host will ask the user");
    if(!strncmp(d+10,"list_mcp_resources ",19)){p=attr(d,"server");r=(p&&*p)?vs_mcp_list_resources(c,p):dupstr("ERROR: list_mcp_resources requires server=\"...\"");free(p);return r?r:dupstr("ERROR: could not list MCP resources");}
    if(!strncmp(d+10,"read_mcp_resource ",18)){p=attr(d,"server");q=attr(d,"uri");r=(p&&*p&&q&&*q)?vs_mcp_read_resource(c,p,q):dupstr("ERROR: read_mcp_resource requires server and uri");free(p);free(q);return r?r:dupstr("ERROR: could not read MCP resource");}
    if(!strncmp(d+10,"end_conversation ",17))return dupstr("ERROR: end_conversation is host-controlled; finish the task with [[VS_FINAL]] and the host will close the session when appropriate");
    if(!strncmp(d+10,"exit_plan_mode ",15))return dupstr("ERROR: exit_plan_mode is host-controlled; propose [[VS_PLAN title=\"...\" tasks=\"...\"]] and the host will present the approval control");
    return NULL;
}

static char *execute_tool_inner(VSContext *c,const char *d,int *command_runner_confirmed){char *result=0;if(!d)return dupstr("ERROR: empty tool directive");
    if(!strncmp(d+10,"image ",6)){char *p=attr(d,"path");int rc=-1,idx=find_attachment(c,p),before=c->attachment_count;if(p)unesc(p);if(p&&vs_is_image_path(p))rc=vs_attach(c,p);if(rc==0){if(idx<0)idx=before;c->attachment_send_from=idx>=0?idx:c->attachment_count;{size_t z=strlen(p?p:"")+96;result=(char*)malloc(z);if(result)snprintf(result,z,"OK: image loaded for the next model round: %s",p?p:"");}}if(!result)result=dupstr((p&&!vs_is_image_path(p))?"ERROR: image tool requires PNG, JPEG, GIF, or WebP":"ERROR: unable to load image file");free(p);return result;}
    if(!strncmp(d+10,"subagent ",9)){char *t=attr(d,"task"),*r;if(t)unesc(t);r=vs_subagent_run(c,t);free(t);return r;}
    if(!strncmp(d+10,"read ",5))return native_read(c,d);
    if(!strncmp(d+10,"list ",5))return native_list(c,d);
    if(!strncmp(d+10,"search ",7))return native_search(c,d);
    if(!strncmp(d+10,"graph ",6)){char *m=attr(d,"mode"),*p=attr(d,"path"),*sum=0;VSGraph g;VSGraphMode mode=VS_GRAPH_FILES;if(m){unesc(m);if(!strcmp(m,"fields")||!strcmp(m,"field")||!strcmp(m,"symbols"))mode=VS_GRAPH_FIELDS;}if(p)unesc(p);if(vs_graph_build(c,p&&*p?p:c->cwd,mode,&g)>=0)sum=vs_graph_summary(&g,100);free(m);free(p);return sum?sum:dupstr("ERROR: graph build failed");}
    if(!strncmp(d+10,"web_search ",11)){char *q=attr(d,"query"),*cnt=attr(d,"count"),*r;int n=5;if(q)unesc(q);if(cnt){n=atoi(cnt);if(n<1)n=1;if(n>12)n=12;}r=q&&*q?vs_web_search(c,q,n):dupstr("ERROR: web_search requires query=\"...\"");free(q);free(cnt);return r;}
    if(!strncmp(d+10,"web_fetch ",10)){char *u=attr(d,"url"),*r;if(u)unesc(u);r=u&&*u?vs_web_fetch(c,u,96U*1024U):dupstr("ERROR: web_fetch requires url=\"https://...\"");free(u);return r;}
    if(!strncmp(d+10,"run ",4)){char *cmd=attr(d,"cmd"),*raw=0,*bounded=0;int st=0;if(cmd)unesc(cmd);raw=cmd?vs_run_command_ctx(c,cmd,&st):0;if(raw){size_t z;bounded=vs_compact_text_limit(raw,VS_AGENT_COMMAND_RESULT_MAX,"command output compacted; narrow the command or inspect a specific file/range");free(raw);raw=bounded;*command_runner_confirmed=1;z=strlen(raw?raw:"")+1024;result=(char*)malloc(z);if(result)snprintf(result,z,"COMMAND_RUNNER_STATUS: operational\nCOMMAND_SHELL: %s\nCOMMAND_EXIT_STATUS: %d\nOUTPUT:\n%s%s",vs_command_shell_name(),st,raw?raw:"",(solaris_host(c)&&st!=0&&solaris_command_mismatch(raw))?"\nSOLARIS_HINT: This looks like a GNU/Linux-vs-Solaris command mismatch. Use Solaris/POSIX syntax or VibeSolaris read/list/search; do not retry the same unsupported option unchanged.\n":"");free(raw);}else{*command_runner_confirmed=0;result=dupstr("COMMAND_RUNNER_STATUS: host_error\nCOMMAND_EXIT_STATUS: -1\nERROR: host could not start or communicate with the command process.");}free(cmd);return result?result:dupstr("ERROR: command result allocation failed");}
    if(!strncmp(d+10,"write ",6)){char *p=attr(d,"path"),*ct=attr(d,"content");int rc=-1;if(p)unesc(p);if(ct)unesc(ct);if(p&&ct)rc=vs_write_file(p,ct);if(rc==0&&p)vs_cache_invalidate(c,p);if(rc==0&&p&&ct){
        /* Read-after-write verification catches silent truncation/write races. */
        FILE *check=fopen(p,"rb");int verified=0;
        if(check){size_t expected=strlen(ct),got=0;char tmp[4096];int equal=1;size_t take;
            while((take=fread(tmp,1,sizeof(tmp),check))>0){
                if(got+take>expected||memcmp(tmp,ct+got,take)!=0){equal=0;break;}
                got+=take;
            }
            if(ferror(check))equal=0;
            verified=equal&&got==expected;
            fclose(check);
        }
        result=dupstr(verified?"OK: file written; read-back verification passed":"ERROR: file write returned success but read-back verification failed");
    }else result=dupstr("ERROR: write failed");
    free(p);free(ct);return result;}
    if(!strncmp(d+10,"todo ",5)){char *items=attr(d,"items"),*idx_s=attr(d,"index"),*st_s=attr(d,"status");
        if(items){const char *arr[VS_MAX_TODO_ITEMS];int n=0;char *p,*sep;unesc(items);p=items;
            while(p&&*p&&n<VS_MAX_TODO_ITEMS){sep=strchr(p,'|');if(sep)*sep=0;if(*p)arr[n++]=p;p=sep?sep+1:(char*)0;}
            if(vs_todo_set_all(&c->todo,arr,n)==0){size_t z=64;result=(char*)malloc(z);if(result)snprintf(result,z,"OK: todo list updated (%d items)",n);}
            if(!result)result=dupstr("ERROR: unable to update todo list");
        }else if(idx_s&&st_s){int idx=atoi(idx_s);VSTodoStatus st;int st_ok=1;
            if(!strcmp(st_s,"pending"))st=VS_TODO_STATUS_PENDING;
            else if(!strcmp(st_s,"in_progress"))st=VS_TODO_STATUS_IN_PROGRESS;
            else if(!strcmp(st_s,"completed"))st=VS_TODO_STATUS_COMPLETED;
            else if(!strcmp(st_s,"cancelled"))st=VS_TODO_STATUS_CANCELLED;
            else st_ok=0;
            if(!st_ok)result=dupstr("ERROR: todo status must be pending, in_progress, completed, or cancelled");
            else if(vs_todo_update_status(&c->todo,idx-1,st)==0)result=dupstr("OK: todo item updated");
            else result=dupstr("ERROR: todo index out of range");
        }else result=dupstr("ERROR: todo requires items=\"A|B|C\" or index=\"N\" status=\"STATUS\"");
        free(items);free(idx_s);free(st_s);return result;}
    if(!strncmp(d+10,"plan_task_complete ",19)){char *id_s=attr(d,"task_id"),*summary=attr(d,"summary"),*ep=0;long id=0;
        if(summary)unesc(summary);
        if(id_s){id=strtol(id_s,&ep,10);if(ep==id_s||*ep)id=0;}
        if(id<=0)result=dupstr("ERROR: plan_task_complete requires a numeric task_id from the accepted plan");
        else if(vs_workflow_set_task_status(&c->workflow,(int)id,VS_TASK_COMPLETED,summary&&*summary?summary:0)!=0)result=dupstr("ERROR: task_id is not part of the accepted plan");
        else {size_t z=80+(summary?strlen(summary):0);result=(char*)malloc(z);if(result)snprintf(result,z,"OK: plan task %ld marked completed%s%s",id,summary&&*summary?": ":"",summary&&*summary?summary:"");}
        free(id_s);free(summary);return result?result:dupstr("ERROR: tool result allocation failed");}
    if(!strncmp(d+10,"computer_use_screenshot ",22))return vs_computer_use_screenshot();
    if(!strncmp(d+10,"computer_use_click ",19))return vs_computer_use_click((int)long_attr(d,"x",0,-100000,100000),(int)long_attr(d,"y",0,-100000,100000));
    if(!strncmp(d+10,"computer_use_move ",18))return vs_computer_use_move((int)long_attr(d,"x",0,-100000,100000),(int)long_attr(d,"y",0,-100000,100000));
    if(!strncmp(d+10,"computer_use_type ",18)){char *t=attr(d,"text");char *r;if(t)unesc(t);r=t?vs_computer_use_type(t):dupstr("ERROR: computer_use_type requires text=\"...\"");free(t);return r;}
    {char *ext=execute_extended_tool(c,d);if(ext)return ext;}
    return dupstr("ERROR: unknown VS_TOOL operation");
}
/* Dispatch wrapper that fires configured PreToolUse/PostToolUse hooks around
   every tool execution.  The hook store lives on the context; with no hooks
   configured this is two cheap no-op scans. */
static char *execute_tool(VSContext *c,const char *d,int *command_runner_confirmed){
    char op[64];const char *p=d?d+10:"";size_t i=0;char *r;
    while(p[i]&&i<sizeof(op)-1&&!isspace((unsigned char)p[i])){op[i]=p[i];i++;}
    op[i]=0;
    if(c&&c->hooks.hook_count)vs_hooks_run_pre(&c->hooks,op);
    r=execute_tool_inner(c,d,command_runner_confirmed);
    if(c&&c->hooks.hook_count)vs_hooks_run_post(&c->hooks,op,r?r:"");
    return r;
}

static const char *earliest_directive(const char *s,int *is_mcp){const char *t=strstr(s,"[[VS_TOOL "),*m=strstr(s,"[[VS_MCP ");if(!t&&!m)return 0;if(m&&(!t||m<t)){*is_mcp=1;return m;}*is_mcp=0;return t;}
static int directive_count(const char *s){int n=0,m;const char *p=s,*d,*e;while((d=earliest_directive(p,&m))!=0){e=strstr(d,"]]" );if(!e)break;n++;p=e+2;}return n;}
static char *directive_signature(const char *s){const char *p=s,*d,*e;int m;size_t n=0;char *out;while((d=earliest_directive(p,&m))!=0){e=strstr(d,"]]" );if(!e)break;n+=(size_t)(e+2-d)+1;p=e+2;}if(!n)return dupstr("");out=(char*)malloc(n+1);if(!out)return 0;out[0]=0;p=s;while((d=earliest_directive(p,&m))!=0){size_t z;e=strstr(d,"]]" );if(!e)break;z=(size_t)(e+2-d);strncat(out,d,z);strcat(out,"\n");p=e+2;}return out;}
static char *next_directive(const char **cursor,int *is_mcp){const char *d,*e;char *o;size_t n;if(!cursor||!*cursor)return 0;d=earliest_directive(*cursor,is_mcp);if(!d)return 0;e=strstr(d,"]]" );if(!e){*cursor=d+strlen(d);return 0;}n=(size_t)(e+2-d);o=(char*)malloc(n+1);if(!o)return 0;memcpy(o,d,n);o[n]=0;*cursor=e+2;return o;}
static int directive_known(const char *d,int is_mcp){
    static const char *ops[]={"read ","list ","search ","graph ","web_search ","web_fetch ","run ","write ","image ","subagent ","todo ","plan_task_complete ","computer_use_click ","computer_use_move ","computer_use_type ",
        "glob ","grep ","edit ","multi_edit ","notebook_read ","notebook_edit ","monitor ","monitor_output ","monitor_kill ",
        "task_create ","task_update ","task_list ","task_search ","task_delete ",
        "checkpoint ","lsp ","permission ","auto_mode ","team_message ","team_read ","team_broadcast ",
        "code_review ","plugin ","init ","skill ","slash_command ","tool_search ",
        "enter_worktree ","exit_worktree ","ask_user_question ","list_mcp_resources ","read_mcp_resource ","end_conversation ","exit_plan_mode ","computer_use_screenshot ",NULL};
    int i;if(is_mcp)return 1;if(strncmp(d,"[[VS_TOOL ",10))return 0;
    for(i=0;ops[i];i++)if(!strncmp(d+10,ops[i],strlen(ops[i])))return 1;
    return 0;
}
static char *next_directive(const char **cursor,int *is_mcp);
static int directive_read_only(const char *d,int is_mcp){
    if(is_mcp)return 0;
    return !strncmp(d,"[[VS_TOOL read ",15)||!strncmp(d,"[[VS_TOOL list ",15)||
           !strncmp(d,"[[VS_TOOL search ",17)||!strncmp(d,"[[VS_TOOL graph ",16)||
           !strncmp(d,"[[VS_TOOL web_search ",21)||!strncmp(d,"[[VS_TOOL web_fetch ",20)||
           !strncmp(d,"[[VS_TOOL glob ",15)||!strncmp(d,"[[VS_TOOL grep ",15)||
           !strncmp(d,"[[VS_TOOL notebook_read ",21)||!strncmp(d,"[[VS_TOOL monitor_output ",24)||
           !strncmp(d,"[[VS_TOOL task_list ",19)||!strncmp(d,"[[VS_TOOL task_search ",21)||
           !strncmp(d,"[[VS_TOOL lsp ",14)||!strncmp(d,"[[VS_TOOL tool_search ",21)||
           !strncmp(d,"[[VS_TOOL list_mcp_resources ",28)||!strncmp(d,"[[VS_TOOL read_mcp_resource ",29)||
           !strncmp(d,"[[VS_TOOL computer_use_screenshot ",33);
}

/* A plan marker only needs to pause the host when the response also describes
   work that can change state.  This deliberately treats MCP as side-effecting
   because the host cannot infer safety from an MCP tool's name. */
static int plan_has_side_effects(const char *answer)
{
    const char *cur=answer;int is_mcp;char *d;int side_effect=0,count=0;
    while((d=next_directive(&cur,&is_mcp))!=NULL){
        count++;if(!directive_read_only(d,is_mcp))side_effect=1;
        free(d);
    }
    /* A standalone plan is itself a side-effecting proposal. */
    return count==0?1:side_effect;
}
static int name_in_list(const char *name,const char *const *list){int i;for(i=0;list[i];i++)if(!strcmp(name,list[i]))return 1;return 0;}
static const char *json_ws(const char *p){while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;return p;}
static const char *json_value_end(const char *p,int depth);
static const char *json_string_end(const char *p){if(*p++!='\"')return NULL;while(*p){if((unsigned char)*p<0x20)return NULL;if(*p=='\\'){if(!p[1])return NULL;p+=2;continue;}if(*p++=='\"')return p;}return NULL;}
static const char *json_value_end(const char *p,int depth){const char *q;if(depth>32)return NULL;p=json_ws(p);if(*p=='\"')return json_string_end(p);if(*p=='{'||*p=='['){char close=*p=='{'?'}':']';int object=*p=='{';p=json_ws(p+1);if(*p==close)return p+1;for(;;){if(object){q=json_string_end(p);if(!q)return NULL;p=json_ws(q);if(*p++!=':')return NULL;p=json_ws(p);}q=json_value_end(p,depth+1);if(!q)return NULL;p=json_ws(q);if(*p==close)return p+1;if(*p++!=',')return NULL;p=json_ws(p);}}if(!strncmp(p,"true",4))return p+4;if(!strncmp(p,"false",5))return p+5;if(!strncmp(p,"null",4))return p+4;if(*p=='-'||isdigit((unsigned char)*p)){char *e;strtod(p,&e);return e==p?NULL:e;}return NULL;}
static int valid_json_object(const char *s){const char *p,*e;if(!s)return 0;p=json_ws(s);if(*p!='{')return 0;e=json_value_end(p,0);return e&&!*json_ws(e);}

static int has_attr(const char *d,const char *name){char *v=attr(d,name);int ok=v!=NULL;if(v)free(v);return ok;}
static void schema_why(char *why,size_t cap,const char *fmt,...){va_list ap;if(!why||!cap)return;va_start(ap,fmt);vsnprintf(why,cap,fmt,ap);va_end(ap);}
static int directive_schema_valid(VSContext *c,const char *d,int is_mcp,char *why,size_t why_cap){
    int mcp_found=-1;
    static const char *common[] = {"task_id",NULL};
    static const char *read_attrs[] = {"path","start_line","max_lines",NULL};
    static const char *list_attrs[] = {"path","depth","limit",NULL};
    static const char *search_attrs[] = {"path","query","case_insensitive","limit",NULL};
    static const char *graph_attrs[] = {"path","mode",NULL};
    static const char *web_search_attrs[] = {"query","count",NULL};
    static const char *web_fetch_attrs[] = {"url",NULL};
    static const char *run_attrs[] = {"cmd",NULL};
    static const char *write_attrs[] = {"path","content",NULL};
    static const char *image_attrs[] = {"path",NULL};
    static const char *subagent_attrs[] = {"task",NULL};
    static const char *todo_attrs[] = {"items","index","status",NULL};
    static const char *plan_complete_attrs[] = {"summary",NULL};
    static const char *cu_point_attrs[] = {"x","y",NULL};
    static const char *cu_type_attrs[] = {"text",NULL};
    static const char *mcp_attrs[] = {"server","tool","args","task_id",NULL};
    static const char *glob_attrs[] = {"path","pattern",NULL};
    static const char *grep_attrs[] = {"path","pattern","case_insensitive","context","limit",NULL};
    static const char *edit_attrs[] = {"path","search","replace",NULL};
    static const char *multi_edit_attrs[] = {"path","edits",NULL};
    static const char *notebook_attrs[] = {"path","cell","source",NULL};
    static const char *monitor_attrs[] = {"cmd",NULL};
    static const char *monitor_id_attrs[] = {"monitor_id",NULL};
    static const char *task_create_attrs[] = {"title","description","priority",NULL};
    static const char *task_update_attrs[] = {"id","status","title","description",NULL};
    static const char *task_id_attrs[] = {"id",NULL};
    static const char *task_search_attrs[] = {"query","status",NULL};
    static const char *checkpoint_attrs[] = {"action","description",NULL};
    static const char *lsp_attrs[] = {"command","path","line","col",NULL};
    static const char *permission_attrs[] = {"action","tool","path",NULL};
    static const char *auto_mode_attrs[] = {"action",NULL};
    static const char *team_msg_attrs[] = {"team","from","to","message",NULL};
    static const char *team_read_attrs[] = {"team","agent",NULL};
    static const char *team_bcast_attrs[] = {"team","from","message",NULL};
    static const char *code_review_attrs[] = {"path","scope","focus",NULL};
    static const char *plugin_attrs[] = {"action","url","name",NULL};
    static const char *init_attrs[] = {"path",NULL};
    static const char *skill_attrs[] = {"name","input",NULL};
    static const char *slash_attrs[] = {"name","args",NULL};
    static const char *tool_search_attrs2[] = {"query",NULL};
    static const char *worktree_attrs[] = {"path",NULL};
    static const char *ask_attrs[] = {"question","options",NULL};
    static const char *mcp_res_attrs[] = {"server","uri",NULL};
    static const char *end_conv_attrs[] = {"message",NULL};
    static const char *exit_plan_attrs[] = {NULL};
    static const char *no_attrs[] = {NULL};
    const char *p,*end;char op[32],name[64],seen[16][64];int i,j,seen_count=0,seen_task=0;const char *const *allowed=0;char *value;
    if(!d){schema_why(why,why_cap,"directive is empty");return 0;}end=strstr(d,"]]" );if(!end){schema_why(why,why_cap,"missing closing ]] delimiter");return 0;}p=d+2;
    if(is_mcp){if(strncmp(p,"VS_MCP ",7)){schema_why(why,why_cap,"expected \"[[VS_MCP \" prefix");return 0;}p+=7;strcpy(op,"MCP");allowed=mcp_attrs;}
    else {if(strncmp(p,"VS_TOOL ",8)){schema_why(why,why_cap,"expected \"[[VS_TOOL \" prefix");return 0;}p+=8;for(i=0;i<(int)sizeof(op)-1&&p<end&&!isspace((unsigned char)*p);i++,p++)op[i]=*p;op[i]=0;
        if(!strcmp(op,"read"))allowed=read_attrs;else if(!strcmp(op,"list"))allowed=list_attrs;else if(!strcmp(op,"search"))allowed=search_attrs;else if(!strcmp(op,"graph"))allowed=graph_attrs;else if(!strcmp(op,"web_search"))allowed=web_search_attrs;else if(!strcmp(op,"web_fetch"))allowed=web_fetch_attrs;else if(!strcmp(op,"run"))allowed=run_attrs;else if(!strcmp(op,"write"))allowed=write_attrs;else if(!strcmp(op,"image"))allowed=image_attrs;else if(!strcmp(op,"subagent"))allowed=subagent_attrs;else if(!strcmp(op,"todo"))allowed=todo_attrs;else if(!strcmp(op,"plan_task_complete"))allowed=plan_complete_attrs;else if(!strcmp(op,"computer_use_click")||!strcmp(op,"computer_use_move"))allowed=cu_point_attrs;else if(!strcmp(op,"computer_use_type"))allowed=cu_type_attrs;
        else if(!strcmp(op,"glob"))allowed=glob_attrs;else if(!strcmp(op,"grep"))allowed=grep_attrs;else if(!strcmp(op,"edit"))allowed=edit_attrs;else if(!strcmp(op,"multi_edit"))allowed=multi_edit_attrs;else if(!strcmp(op,"notebook_read"))allowed=notebook_attrs;else if(!strcmp(op,"notebook_edit"))allowed=notebook_attrs;else if(!strcmp(op,"monitor"))allowed=monitor_attrs;else if(!strcmp(op,"monitor_output")||!strcmp(op,"monitor_kill"))allowed=monitor_id_attrs;else if(!strcmp(op,"task_create"))allowed=task_create_attrs;else if(!strcmp(op,"task_update"))allowed=task_update_attrs;else if(!strcmp(op,"task_list"))allowed=0;else if(!strcmp(op,"task_search"))allowed=task_search_attrs;else if(!strcmp(op,"task_delete"))allowed=task_id_attrs;else if(!strcmp(op,"checkpoint"))allowed=checkpoint_attrs;else if(!strcmp(op,"lsp"))allowed=lsp_attrs;else if(!strcmp(op,"permission"))allowed=permission_attrs;else if(!strcmp(op,"auto_mode"))allowed=auto_mode_attrs;else if(!strcmp(op,"team_message"))allowed=team_msg_attrs;else if(!strcmp(op,"team_read"))allowed=team_read_attrs;else if(!strcmp(op,"team_broadcast"))allowed=team_bcast_attrs;else if(!strcmp(op,"code_review"))allowed=code_review_attrs;else if(!strcmp(op,"plugin"))allowed=plugin_attrs;else if(!strcmp(op,"init"))allowed=init_attrs;else if(!strcmp(op,"skill"))allowed=skill_attrs;else if(!strcmp(op,"slash_command"))allowed=slash_attrs;else if(!strcmp(op,"tool_search"))allowed=tool_search_attrs2;else if(!strcmp(op,"enter_worktree")||!strcmp(op,"exit_worktree"))allowed=worktree_attrs;else if(!strcmp(op,"ask_user_question"))allowed=ask_attrs;else if(!strcmp(op,"list_mcp_resources"))allowed=mcp_res_attrs;else if(!strcmp(op,"read_mcp_resource"))allowed=mcp_res_attrs;else if(!strcmp(op,"end_conversation"))allowed=end_conv_attrs;else if(!strcmp(op,"exit_plan_mode"))allowed=exit_plan_attrs;else if(!strcmp(op,"computer_use_screenshot"))allowed=no_attrs;else{schema_why(why,why_cap,"unknown operation \"%s\"",op);return 0;}}
    while(p<end){while(p<end&&isspace((unsigned char)*p))p++;if(p==end)break;j=0;while(p<end&&(isalnum((unsigned char)*p)||*p=='_')){if(j>=(int)sizeof(name)-1){schema_why(why,why_cap,"attribute name too long in %s",op);return 0;}name[j++]=*p++;}if(!j){schema_why(why,why_cap,"malformed attribute syntax in %s (expected name=\\\"value\\\")",op);return 0;}name[j]=0;if(*p++!='='){schema_why(why,why_cap,"expected '=' after attribute \"%s\" in %s",name,op);return 0;}if(p>=end||*p++!='\"'){schema_why(why,why_cap,"expected quoted value for attribute \"%s\" in %s",name,op);return 0;}
        {const char *v=p;while(p<end){if(*p=='\\'&&p+1<end){p+=2;continue;}if(*p=='\"')break;p++;}if(p>=end){schema_why(why,why_cap,"unterminated value for attribute \"%s\" in %s",name,op);return 0;}value=(char*)malloc((size_t)(p-v)+1);if(!value)return 0;memcpy(value,v,(size_t)(p-v));value[p-v]=0;p++;}
        if(!name_in_list(name,allowed)&&!name_in_list(name,common)){char alist[256];size_t an=0;alist[0]=0;for(i=0;allowed&&allowed[i];i++)an+=(size_t)snprintf(alist+an,sizeof(alist)-an,"%s%s",an?", ":"",allowed[i]);schema_why(why,why_cap,"attribute \"%s\" is not allowed for %s (allowed: %s, task_id)",name,op,alist);free(value);return 0;}
        for(i=0;i<seen_count;i++)if(!strcmp(name,seen[i])){schema_why(why,why_cap,"duplicate attribute \"%s\" in %s",name,op);free(value);return 0;}
        if(seen_count>=16){schema_why(why,why_cap,"too many attributes in %s (max 16)",op);free(value);return 0;}strcpy(seen[seen_count++],name);
        if(!strcmp(name,"task_id")){if(seen_task++){schema_why(why,why_cap,"duplicate attribute \"task_id\" in %s",op);free(value);return 0;}}
        free(value);
    }
    if(is_mcp){char *server=attr(d,"server"),*tool=attr(d,"tool"),*args=attr(d,"args");int found=0;size_t z;for(i=0;c&&i<c->mcp_tool_count;i++)if(server&&tool&&!strcmp(c->mcp_tools[i].server,server)&&!strcmp(c->mcp_tools[i].name,tool)){found=1;mcp_found=i;break;}if(args)unesc(args);z=args?strlen(args):0;while(z&&isspace((unsigned char)args[z-1]))args[--z]=0;while(args&&*args&&isspace((unsigned char)*args)){memmove(args,args+1,strlen(args));z--;}if(!args||!*args){schema_why(why,why_cap,"MCP directive is missing args");free(server);free(tool);free(args);return 0;}if(!server||!tool||!found||z>VS_MCP_SCHEMA_MAX){schema_why(why,why_cap,!server||!tool?"MCP directive requires server and tool attributes":(!found?"MCP tool \"%s.%s\" is not in the discovered tool catalogue":"MCP args exceed %d bytes"),server?server:"",tool?tool:"",VS_MCP_SCHEMA_MAX);free(server);free(tool);free(args);return 0;}free(server);free(tool);free(args);return mcp_found;}
    if(!strcmp(op,"read")||!strcmp(op,"run")||!strcmp(op,"image"))return has_attr(d,!strcmp(op,"run")?"cmd":"path")?1:(schema_why(why,why_cap,"%s requires a \"%s\" attribute",op,!strcmp(op,"run")?"cmd":"path"),0);
    if(!strcmp(op,"search")||!strcmp(op,"web_search"))return has_attr(d,"query")?1:(schema_why(why,why_cap,"%s requires a \"query\" attribute",op),0);
    if(!strcmp(op,"web_fetch"))return has_attr(d,"url")?1:(schema_why(why,why_cap,"%s requires a \"url\" attribute",op),0);
    if(!strcmp(op,"write"))return(has_attr(d,"path")&&has_attr(d,"content"))?1:(schema_why(why,why_cap,"write requires \"path\" and \"content\" attributes"),0);
    if(!strcmp(op,"subagent"))return has_attr(d,"task")?1:(schema_why(why,why_cap,"%s requires a \"task\" attribute",op),0);
    if(!strcmp(op,"todo"))return(has_attr(d,"items")||(has_attr(d,"index")&&has_attr(d,"status")))?1:(schema_why(why,why_cap,"todo requires \"items\" or both \"index\" and \"status\""),0);
    if(!strcmp(op,"plan_task_complete"))return has_attr(d,"task_id")?1:(schema_why(why,why_cap,"%s requires a \"task_id\" attribute",op),0);
    if(!strcmp(op,"computer_use_click")||!strcmp(op,"computer_use_move"))return(has_attr(d,"x")&&has_attr(d,"y"))?1:(schema_why(why,why_cap,"%s requires \"x\" and \"y\" attributes",op),0);
    if(!strcmp(op,"computer_use_type"))return has_attr(d,"text")?1:(schema_why(why,why_cap,"%s requires a \"text\" attribute",op),0);
    return 1;
}
static int preflight_batch_ex(VSContext *c,const char *answer,int *count_out,char *why,size_t why_cap){
    const char *cur=answer,*scan=answer;int n=0,is_mcp,authorized=c&&vs_workflow_authorized(&c->workflow),staged_task=c?c->workflow.current_task_id:0;char *d;
    if(why&&why_cap)why[0]=0;
    while((d=next_directive(&cur,&is_mcp))!=NULL){
        n++;
        if(n>VS_MAX_BATCH_TOOLS){schema_why(why,why_cap,"directive %d: batch too large (%d directives, max %d per batch)",n,n,VS_MAX_BATCH_TOOLS);free(d);return -1;}
        if(!directive_known(d,is_mcp)){if(!is_mcp&&!strncmp(d,"[[VS_TOOL ",10)){char badop[32];const char *bp=d+10;size_t bi=0;while(bp[bi]&&bi<sizeof(badop)-1&&!isspace((unsigned char)bp[bi])){badop[bi]=bp[bi];bi++;}badop[bi]=0;schema_why(why,why_cap,"directive %d: unknown operation \"%s\"",n,badop);}else schema_why(why,why_cap,"directive %d: malformed directive prefix",n);free(d);return -1;}
        {char swhy[256];int schema=directive_schema_valid(c,d,is_mcp,swhy,sizeof(swhy));if(schema<0||(!is_mcp&&schema!=1)){schema_why(why,why_cap,"directive %d: %s",n,swhy[0]?swhy:"invalid directive schema");free(d);return -1;}if(is_mcp){char *args=attr(d,"args");int call_ok=0;if(args)unesc(args);call_ok=valid_json_object(args)&&c&&schema<c->mcp_tool_count;free(args);if(!call_ok){schema_why(why,why_cap,"directive %d: MCP args is not a valid JSON object or the tool is not in the catalogue",n);free(d);return -1;}}}
        if(!authorized&&!directive_read_only(d,is_mcp)){schema_why(why,why_cap,"directive %d (%.*s): side-effecting tool requires an accepted plan first",n,40,d);free(d);return -2;}
        if(authorized){char *id_s=attr(d,"task_id"),*ep=NULL;long id=0;int valid=0,k;
            if(!id_s){schema_why(why,why_cap,"directive %d: every action must name a task_id from the accepted plan",n);free(d);return -6;}
            id=strtol(id_s,&ep,10);if(ep==id_s||*ep||id<=0||id>2147483647L){schema_why(why,why_cap,"directive %d: task_id \"%s\" is not a positive integer",n,id_s);free(id_s);free(d);return -6;}
            if(!vs_workflow_task_ready(&c->workflow,(int)id) && strncmp(d+10,"plan_task_complete ",strlen("plan_task_complete "))){schema_why(why,why_cap,"directive %d: task_id %ld is blocked, complete, or not ready in the accepted plan",n,id);free(id_s);free(d);return -7;}
            for(k=0;k<c->workflow.task_count;k++)if(c->workflow.tasks[k].id==(int)id){valid=1;break;}
            if(valid)staged_task=(int)id;
            free(id_s);
        }
        if(is_mcp){char *server=attr(d,"server"),*tool=attr(d,"tool");int found=0,i;
            if(!server||!tool){schema_why(why,why_cap,"directive %d: MCP directive requires server and tool attributes",n);free(server);free(tool);free(d);return -1;}
            if(!authorized){free(server);free(tool);free(d);return -2;}
            for(i=0;i<c->mcp_tool_count;i++)if(!strcmp(c->mcp_tools[i].server,server)&&!strcmp(c->mcp_tools[i].name,tool)){found=1;break;}
            free(server);free(tool);if(!found){schema_why(why,why_cap,"directive %d: MCP tool is not in the discovered tool catalogue",n);free(d);return -3;}
        } else if(!directive_read_only(d,0)&&!authorized){schema_why(why,why_cap,"directive %d: side-effecting tool requires an accepted plan first",n);free(d);return -2;}
        free(d);
    }
    /* A marker without a closing delimiter is malformed; do not execute a valid
       prefix.  This case is reported separately (-8) because the model can
       usually repair a truncated directive on the next round. */
    while((scan=earliest_directive(scan,&is_mcp))!=NULL){const char *end=strstr(scan,"]]" );if(!end){char frag[80];size_t fi=0;const char *fp=scan;while(fp[fi]&&fi<sizeof(frag)-1&&fi<(size_t)(end?0:60)){frag[fi]=fp[fi];fi++;}frag[fi]=0;schema_why(why,why_cap,"directive marker starting with \"%s\" was truncated before its closing ]]",frag);return -8;}scan=end+2;}
    if(count_out)*count_out=n;
    if(c&&c->workflow.mode==VS_MODE_MANUAL&&authorized&&n>1){schema_why(why,why_cap,"Manual mode approves one action at a time; the batch has %d directives",n);return -4;}
    if(c&&c->workflow.mode==VS_MODE_MANUAL&&authorized&&c->workflow.action_approved){
        char *sig=directive_signature(answer);
        int same=sig&&c->workflow.pending_request[0]&&!strcmp(sig,c->workflow.pending_request);
        free(sig);
        if(!same){schema_why(why,why_cap,"Manual mode: this action does not match the pending approved action; request approval again");return -5;}
    }
    if(c&&authorized&&n>0)c->workflow.current_task_id=staged_task;
    return 0;
}
static int preflight_batch(VSContext *c,const char *answer,int *count_out){return preflight_batch_ex(c,answer,count_out,NULL,0);}
static int parse_plan_marker(VSContext *c,const char *answer)
{
    const char *mark=strstr(answer?answer:"","[[VS_PLAN ");
    char *title=NULL,*raw=NULL,*item,*bar,*dep,*end;
    VSTaskRecord tasks[VS_MAX_PLAN_TASKS];int count=0,rc=-1;
    if(!mark)return 0;
    title=attr(mark,"title");raw=attr(mark,"tasks");
    if(!title||!raw||!*title||!*raw)goto done;
    memset(tasks,0,sizeof(tasks));
    item=raw;
    while(item&&*item){
        VSTaskRecord *t;long id,parent;char *ep;
        if(count>=VS_MAX_PLAN_TASKS)goto done;
        t=&tasks[count];
        id=strtol(item,&ep,10);if(ep==item||*ep!='|')goto done;t->id=(int)id;item=ep+1;
        parent=strtol(item,&ep,10);if(ep==item||*ep!='|')goto done;t->parent_id=(int)parent;item=ep+1;
        bar=strchr(item,'|');if(!bar)goto done;*bar=0;dep=item;item=bar+1;
        if(strcmp(dep,"-")){char *p=dep;while(*p){long d;if(t->dependency_count>=VS_MAX_TASK_DEPS)goto done;d=strtol(p,&ep,10);if(ep==p||d<=0)goto done;t->dependencies[t->dependency_count++]=(int)d;if(*ep==','){p=ep+1;if(!*p)goto done;}else if(!*ep)break;else goto done;}}
        end=strchr(item,';');if(end)*end=0;
        if(!*item||strlen(item)>=sizeof(t->title))goto done;
        strcpy(t->title,item);t->status=VS_TASK_PLANNED;count++;
        if(!end)item=NULL;else item=end+1;
    }
    if(item!=NULL||count<=0)goto done;
    rc=vs_workflow_set_plan(&c->workflow,title,tasks,count)==0?1:-1;
done:
    free(title);free(raw);return rc;
}

/* Run independent delegated tasks at once. Non-subagent batches retain
   their original ordered execution semantics. */
typedef struct { VSContext *parent; char *task; char *result; pthread_t thread; int started; } ParallelSubagent;
static void *parallel_subagent_worker(void *v)
{
    ParallelSubagent *job=(ParallelSubagent*)v;
    job->result=vs_subagent_run(job->parent,job->task);
    return NULL;
}
static char *parallel_subagent_batch(VSContext *c,const char *answer,int total)
{
    ParallelSubagent jobs[VS_MAX_BATCH_TOOLS];
    const char *cur=answer;ABuffer b;int i,mcp;char *d,*out=NULL;
    memset(jobs,0,sizeof(jobs));
    /* Parse every task before launching any worker. */
    for(i=0;i<total;i++){
        d=next_directive(&cur,&mcp);
        if(!d||mcp){free(d);goto cleanup;}
        jobs[i].task=attr(d,"task");
        if(jobs[i].task)unesc(jobs[i].task);
        jobs[i].parent=c;free(d);
        if(!jobs[i].task)goto cleanup;
    }
    for(i=0;i<total;i++){
        if(!pthread_create(&jobs[i].thread,NULL,parallel_subagent_worker,&jobs[i]))jobs[i].started=1;
        else jobs[i].result=dupstr("ERROR: unable to start subagent thread");
    }
    for(i=0;i<total;i++)if(jobs[i].started)pthread_join(jobs[i].thread,NULL);
    if(ab_init(&b,VS_AGENT_TOOL_RESULT_MAX)!=0)goto cleanup;
    ab_addf(&b,"PARALLEL_SUBAGENT_RESULT: %d independent task(s), all joined\n",total);
    for(i=0;i<total;i++){
        char *q=vs_compact_text_limit(jobs[i].result?jobs[i].result:"ERROR: missing result",VS_AGENT_TOOL_RESULT_MAX/(size_t)total,"parallel subagent result compacted");
        ab_addf(&b,"\n--- subagent task %d ---\n",i+1);
        if(q){ab_add(&b,q);free(q);}
    }
    out=ab_finish(&b);
cleanup:
    for(i=0;i<total;i++){free(jobs[i].task);free(jobs[i].result);}
    return out;
}
static int all_subagent_directives(const char *answer,int total)
{
    const char *cur=answer;int i,mcp;char *d;
    for(i=0;i<total;i++){
        d=next_directive(&cur,&mcp);
        if(!d)return 0;
        if(mcp||strncmp(d+10,"subagent ",9)){free(d);return 0;}
        free(d);
    }
    return total>1;
}

/* Batches made only of independent read-only tools and/or subagent tasks can
   run concurrently: they do not mutate shared agent state, so ordering is
   irrelevant and the wall-clock win on slow providers/filesystems is large.
   Results are still reported in the original directive order. */
static int all_parallel_safe_directives(const char *answer,int total)
{
    const char *cur=answer;int i,mcp;char *d;
    for(i=0;i<total;i++){
        d=next_directive(&cur,&mcp);
        if(!d)return 0;
        if(mcp||(!directive_read_only(d,0)&&strncmp(d+10,"subagent ",9))){free(d);return 0;}
        free(d);
    }
    return total>1;
}

typedef struct { VSContext *c; char *directive; char *result; pthread_t thread; int started; int confirmed; } ParallelToolJob;
static void *parallel_tool_worker(void *v)
{
    ParallelToolJob *j=(ParallelToolJob*)v;
    j->result=execute_tool(j->c,j->directive,&j->confirmed);
    return NULL;
}
static char *parallel_readonly_batch(VSContext *c,const char *answer,int total)
{
    ParallelToolJob jobs[VS_MAX_BATCH_TOOLS];
    const char *cur=answer;ABuffer b;int i,mcp;char *out=NULL;
    memset(jobs,0,sizeof(jobs));
    for(i=0;i<total;i++){
        jobs[i].directive=next_directive(&cur,&mcp);
        if(!jobs[i].directive||mcp)goto cleanup;
        jobs[i].c=c;
    }
    for(i=0;i<total;i++){
        if(!pthread_create(&jobs[i].thread,NULL,parallel_tool_worker,&jobs[i]))jobs[i].started=1;
        else jobs[i].result=execute_tool(c,jobs[i].directive,&jobs[i].confirmed); /* serial fallback */
    }
    for(i=0;i<total;i++)if(jobs[i].started)pthread_join(jobs[i].thread,NULL);
    if(ab_init(&b,VS_AGENT_TOOL_RESULT_MAX)!=0)goto cleanup;
    ab_addf(&b,"BATCH_TOOL_RESULT: %d independent directive(s) executed in parallel\n",total);
    for(i=0;i<total;i++){
        ab_addf(&b,"\n--- directive %d: %.220s ---\n",i+1,jobs[i].directive);
        if(jobs[i].result){char *q=vs_compact_text_limit(jobs[i].result,VS_AGENT_TOOL_RESULT_MAX/(size_t)total,"batched tool result compacted");if(q){ab_add(&b,q);free(q);}}
    }
    out=ab_finish(&b);
cleanup:
    for(i=0;i<total;i++){free(jobs[i].directive);free(jobs[i].result);}
    return out;
}
static char *execute_batch(VSContext *c,const char *answer,int *command_runner_confirmed){int total=directive_count(answer),original_total=total,i=0,is_mcp;const char *cur=answer;char *d,*r,*server,*name,*args,*single=0;ABuffer b;if(total<=0)return 0;if(total>VS_MAX_BATCH_TOOLS)total=VS_MAX_BATCH_TOOLS;if(all_subagent_directives(answer,total)){char *parallel=parallel_subagent_batch(c,answer,total);if(parallel)return parallel;}if(all_parallel_safe_directives(answer,total)){char *parallel=parallel_readonly_batch(c,answer,total);if(parallel)return parallel;}if(total==1){d=next_directive(&cur,&is_mcp);if(!d)return dupstr("ERROR: malformed directive");if(is_mcp){server=attr(d,"server");name=attr(d,"tool");args=attr(d,"args");if(args)unesc(args);single=(server&&name)?vs_mcp_call(c,server,name,args&&*args?args:"{}"):dupstr("ERROR: malformed VS_MCP directive");free(server);free(name);free(args);}else single=execute_tool(c,d,command_runner_confirmed);free(d);return single;}
    if(ab_init(&b,VS_AGENT_TOOL_RESULT_MAX)!=0)return 0;
    ab_addf(&b,"BATCH_TOOL_RESULT: %d directive(s) executed in order\n",total);
    while(i<total&&(d=next_directive(&cur,&is_mcp))!=0){i++;ab_addf(&b,"\n--- directive %d: %.220s ---\n",i,d);if(is_mcp){server=attr(d,"server");name=attr(d,"tool");args=attr(d,"args");if(args)unesc(args);r=(server&&name)?vs_mcp_call(c,server,name,args&&*args?args:"{}"):dupstr("ERROR: malformed VS_MCP directive");free(server);free(name);free(args);}else r=execute_tool(c,d,command_runner_confirmed);if(r){char *q=vs_compact_text_limit(r,VS_AGENT_TOOL_RESULT_MAX/(size_t)(total?total:1),"batched tool result compacted");if(q){ab_add(&b,q);free(q);}free(r);}free(d);}
    if(original_total>VS_MAX_BATCH_TOOLS)ab_addf(&b,"\n...[only the first %d directives were executed; issue remaining dependent work next round]...\n",VS_MAX_BATCH_TOOLS);
    return ab_finish(&b);}

char *vs_agent_turn(VSContext *c,const char *user){
    char *prompt=dupstr(user),*ans=0,*original=dupstr(user),*task_anchor=0,*last_tool_result=0,*last_directives=0;int round,plan_continues=0,no_progress=0,tool_recoveries=0,malformed_recoveries=0,command_runner_confirmed=0,actual_tool_seen=0,execution_mode=0,cancelled=0,max_rounds=VS_MAX_TOOL_ROUNDS,explore_rounds=0,plan_deferrals=0;char step[512];
    {const char *mr=getenv("VIBESOLARIS_MAX_AGENT_ROUNDS");if(mr&&*mr){char *ep=0;long v=strtol(mr,&ep,10);if(ep&&*ep==0&&v>=8&&v<=256)max_rounds=(int)v;}}
    if(!prompt||!original){free(prompt);free(original);return dupstr("out of memory");}
    task_anchor=build_task_anchor(c,original);if(!task_anchor)task_anchor=dupstr(original);if(!task_anchor){free(prompt);free(original);return dupstr("out of memory");}
    /* When a plan has been accepted, remind the model which tasks exist and
       which of them are currently unblocked, so directives reference real
       task_ids instead of hallucinated ones. */
    if(vs_workflow_authorized(&c->workflow)&&c->workflow.task_count>0){size_t z=strlen(prompt)+256+(size_t)c->workflow.task_count*(VS_TASK_TITLE_MAX+96);char *np=(char*)malloc(z);size_t n=0;int i;
        if(!np){free(prompt);free(task_anchor);free(original);return dupstr("out of memory");}
        n=(size_t)snprintf(np,z,"ACCEPTED PLAN: %s\n",c->workflow.title);
        for(i=0;i<c->workflow.task_count;i++){VSTaskRecord *t=&c->workflow.tasks[i];int ready=vs_workflow_task_ready(&c->workflow,t->id);n+=(size_t)snprintf(np+n,z-n,"task_id=%d title=\"%s\" status=%d ready=%s\n",t->id,t->title,(int)t->status,ready?"yes":"no");}
        snprintf(np+n,z-n,"\n%s",prompt);
        free(prompt);prompt=np;}
    /* Carry the compacted working state across turns of the same session so
       long tasks do not silently restart from zero.  The block is bounded by
       VS_AGENT_CHECKPOINT_MAX and clearly labelled as possibly stale. */
    if(c->subagent_depth==0&&!c->subagent_parent&&c->agent_checkpoint&&*c->agent_checkpoint){size_t z=strlen(prompt)+strlen(c->agent_checkpoint)+128;char *np=(char*)malloc(z);
        if(!np){free(prompt);free(task_anchor);free(original);return dupstr("out of memory");}
        snprintf(np,z,"WORKING STATE FROM EARLIER TURN (may be stale; verify before relying on it):\n%s\n\n%s",c->agent_checkpoint,prompt);
        free(prompt);prompt=np;}
    /* Claude-Code-style planning: in Auto mode, invest model rounds in
       read-only investigation up front and adopt only a plan grounded in
       verified file contents.  The do phase then executes a plan instead of
       guesses. */
    if(c->workflow.mode==VS_MODE_AUTO&&!vs_workflow_authorized(&c->workflow)){static const char plan_guide[]=
        "PLANNING DISCIPLINE (Auto mode): before proposing [[VS_PLAN ...]], investigate with read-only tools (read, list, search, glob, grep) until the plan is grounded in the actual codebase - confirm the files you will touch exist, the symbols you will change are where you think they are, and the steps are each verifiable. Spend model rounds on this exploration phase; a plan proposed with too little evidence will be sent back for deeper verification. After the plan is adopted, execute it efficiently in the do phase.";
        size_t z=strlen(prompt)+sizeof(plan_guide)+8;char *np=(char*)malloc(z);
        if(!np){free(prompt);free(task_anchor);free(original);return dupstr("out of memory");}
        snprintf(np,z,"%s\n\n%s",plan_guide,prompt);
        free(prompt);prompt=np;}
    if(c->pending_question){c->pending_question=0;c->question_option_count=0;c->question[0]=0;memset(c->question_options,0,sizeof(c->question_options));}
    /* Per-turn working history is reset, but the compacted checkpoint is
       deliberately retained (see above). */
    agent_history_clear(c);c->attachment_send_from=0;c->subagent_serial=0;if(c->subagent_depth==0&&!c->subagent_parent){c->subagent_record_count=0;memset(c->subagents,0,sizeof(c->subagents));}execution_mode=execution_intent(original);vs_trace_clear(c);vs_trace(c,"agent","starting agent turn");
    if(vs_cancel_requested(c)){cancelled=1;vs_trace(c,"cancel","agent turn stopped by user before work began");}
    if(!cancelled&&vs_workflow_authorized(&c->workflow)&&c->mcp_server_count>0){int n=vs_mcp_refresh_all(c,0);if(vs_cancel_requested(c)){cancelled=1;vs_trace(c,"cancel","agent turn stopped while preparing MCP tools");}else{snprintf(step,sizeof(step),"MCP catalogue ready: %d tool(s)",n<0?0:n);vs_trace(c,"mcp",step);}}
    for(round=0;!cancelled&&round<max_rounds;round++){
        char *tool,*mcp,*result=0,*next;
        if(vs_cancel_requested(c)){cancelled=1;vs_trace(c,"cancel","agent turn stopped by user");break;}
        if(round>0&&(round%VS_LONG_TASK_CHECKPOINT)==0){snprintf(step,sizeof(step),"long-task checkpoint: %d rounds completed; working context=%lu bytes, checkpoint=%lu bytes",round,(unsigned long)c->agent_history_bytes,(unsigned long)c->agent_checkpoint_bytes);vs_trace(c,"checkpoint",step);}
        snprintf(step,sizeof(step),"model round %d",round+1);vs_trace(c,"model",step);vs_trace(c,"model-input",prompt);vs_trace(c,"model-wait","requesting provider response");
        if(c->hooks.hook_count){size_t hz=strlen(prompt)+65536;char *mod=(char*)malloc(hz);if(mod){vs_hooks_run_prompt(&c->hooks,prompt,mod,hz);if(*mod){free(prompt);prompt=mod;}else free(mod);}}
        free(ans);ans=vs_chat(c,prompt);c->attachment_send_from=c->attachment_count;if(!ans){
            free(prompt);free(task_anchor);free(last_tool_result);free(last_directives);agent_context_clear(c);
            free(original);return dupstr("provider error");
        }
        if(vs_cancel_requested(c)){cancelled=1;free(ans);ans=dupstr("Stopped by user.");vs_trace(c,"cancel","agent turn stopped while waiting for the provider");break;}vs_trace(c,"model-output",ans);vs_trace(c,"model-result","model response received");
        {
            int plan_rc,plan_side_effects=plan_has_side_effects(ans);
            const char *marker_end=strstr(ans,"[[VS_PLAN ");char *clean=0;
            if(marker_end){const char *e=strstr(marker_end,"]]" );if(e){size_t a=(size_t)(marker_end-ans),b=strlen(e+2);clean=(char*)malloc(a+b+1);if(clean){memcpy(clean,ans,a);memcpy(clean+a,e+2,b+1);}}}
            /* Read-only discovery is allowed to run without promoting the
               response into an approval gate.  The marker is presentation,
               not authorization, so remove it and continue normally. */
            if(marker_end&&!plan_side_effects){
                if(clean){free(ans);ans=clean;clean=NULL;}
                vs_trace(c,"plan-suppressed","read-only response did not require plan approval");
            } else {
                plan_rc=parse_plan_marker(c,ans);
                if(plan_rc!=0){
                    if(plan_rc>0){
                        if(c->workflow.mode==VS_MODE_AUTO){
                            if(explore_rounds<VS_MIN_PLAN_EXPLORE_ROUNDS&&plan_deferrals<VS_MAX_PLAN_DEFERRALS){
                                /* The draft arrived with too little read-only
                                   evidence behind it.  Send it back for
                                   verification instead of adopting guesses. */
                                plan_deferrals++;
                                snprintf(step,sizeof(step),"plan deferred for deeper verification (%d read-only round(s) so far, deferral %d/%d)",explore_rounds,plan_deferrals,VS_MAX_PLAN_DEFERRALS);
                                vs_trace(c,"plan-deferred",step);
                                agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);
                                next=plan_deepen_prompt(task_anchor,ans,explore_rounds,plan_deferrals,VS_MAX_PLAN_DEFERRALS,c->agent_checkpoint);
                                if(clean){free(ans);ans=clean;clean=NULL;}
                                free(prompt);prompt=next;
                                if(!prompt){vs_trace(c,"memory-error","could not allocate plan verification prompt");break;}
                                continue;
                            }
                            if(vs_workflow_accept(&c->workflow)==0){
                                vs_trace(c,"plan-auto-accepted","plan accepted automatically in Auto mode");
                                agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);agent_compact(c,task_anchor,round+1);
                                next=continuation_prompt(task_anchor,0,1,c->agent_checkpoint);
                                if(clean){free(ans);ans=clean;clean=NULL;}
                                free(prompt);prompt=next;
                                if(!prompt){vs_trace(c,"memory-error","could not allocate plan continuation prompt");break;}
                                continue;
                            }
                            vs_trace(c,"plan-invalid","automatic plan acceptance failed");
                        } else {
                            snprintf(c->pending_task,sizeof(c->pending_task),"%s",original);
                            c->pending_task_revision=c->workflow.revision;
                            c->pending_task_valid=1;
                            vs_trace(c,"plan-awaiting-accept","plan proposed; no task actions executed");
                        }
                    } else vs_trace(c,"plan-invalid","malformed or invalid plan proposal rejected");
                    if(clean){free(ans);ans=clean;clean=NULL;}
                    if(plan_rc>0&&(!ans||!*ans))ans=dupstr("Plan is ready for review.");
                    break;
                }
            }
            if(clean)free(clean);
        }
        tool=strstr(ans,"[[VS_TOOL ");mcp=strstr(ans,"[[VS_MCP ");
        if(tool||mcp){int batch_count=0;char why_detail[768];int gate=preflight_batch_ex(c,ans,&batch_count,why_detail,sizeof(why_detail));
            if(gate==-8){vs_trace(c,"directive-rejected","directive marker was truncated before its closing delimiter; asking the model to re-emit it");if(why_detail[0])vs_trace(c,"directive-detail",why_detail);agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);{char tfix[1024];snprintf(tfix,sizeof(tfix),"ERROR: the previous response contained a VS_TOOL/VS_MCP directive that was truncated before its closing ]]. Nothing was executed.%s%s Re-emit the intended directive(s) in full.",why_detail[0]?" Details: ":"",why_detail);next=continuation_prompt(task_anchor,tfix,1,c->agent_checkpoint);}free(prompt);prompt=next;if(!prompt){vs_trace(c,"memory-error","could not allocate truncation recovery prompt");break;}continue;}
            if(gate<0&&gate!=-2){const char *why=gate==-4?"Manual mode approves one action at a time; request a single directive.":(gate==-3?"The requested MCP tool is not in the discovered tool catalogue.":(gate==-5?"This manual approval does not match the pending action; request approval again.":(gate==-7?"The requested task is blocked, complete, or not ready in the accepted plan.":(gate==-6?"Every action must name an accepted task_id from the reviewed plan.":"The directive batch was malformed and was rejected before any action ran."))));
                char detail[1024];detail[0]=0;
                if(why_detail[0])snprintf(detail,sizeof(detail)," Details: %s",why_detail);else if(gate==-1)snprintf(detail,sizeof(detail)," Details: no directive in the batch passed validation.");
                /* A malformed batch (-1) is usually a recoverable model slip
                   (bad attribute, oversize batch, unparseable marker).  Give
                   the model the precise error and a chance to re-emit, like
                   the truncation recovery above, but cap the retries so a
                   provider that keeps producing garbage still ends the turn. */
                if(gate==-1&&malformed_recoveries<VS_MAX_MALFORMED_RECOVERIES){
                    char *fixmsg;
                    malformed_recoveries++;
                    if(c->workflow.mode==VS_MODE_MANUAL){c->workflow.action_approved=0;c->workflow.pending_request[0]=0;}
                    vs_trace(c,"directive-rejected",why);if(why_detail[0])vs_trace(c,"directive-detail",why_detail);
                    fixmsg=(char*)malloc(strlen(why)+strlen(detail)+512);
                    if(!fixmsg){free(ans);ans=dupstr(why);break;}
                    snprintf(fixmsg,strlen(why)+strlen(detail)+512,
                        "ERROR: %s%s Supported operations: read, list, search, glob, grep, graph, web_search, web_fetch, run, write, edit, multi_edit, image, notebook_read, notebook_edit, monitor, monitor_output, monitor_kill, subagent, todo, plan_task_complete, task_create, task_update, task_list, task_search, task_delete, checkpoint, lsp, permission, auto_mode, team_message, team_read, team_broadcast, code_review, plugin, init, skill, slash_command, tool_search, enter_worktree, exit_worktree, computer_use_screenshot, computer_use_click, computer_use_move, computer_use_type. Fix the directive(s) and re-emit them in full.",
                        why,detail);
                    agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);
                    next=continuation_prompt(task_anchor,fixmsg,1,c->agent_checkpoint);
                    free(fixmsg);free(ans);ans=NULL;free(prompt);prompt=next;
                    if(!prompt){vs_trace(c,"memory-error","could not allocate malformed-batch recovery prompt");break;}
                    continue;
                }
                if(c->workflow.mode==VS_MODE_MANUAL){c->workflow.action_approved=0;c->workflow.pending_request[0]=0;}
                vs_trace(c,"directive-rejected",why);if(why_detail[0])vs_trace(c,"directive-detail",why_detail);
                {size_t zl=strlen(why)+strlen(detail)+2;char *full=(char*)malloc(zl);if(full){snprintf(full,zl,"%s%s",why,detail);free(ans);ans=full;}else{free(ans);ans=dupstr(why);}}
                break;}
        }
        if((tool||mcp)&&!vs_workflow_authorized(&c->workflow)){
            int read_only=1,check_mcp;const char *check=ans;char *item;
            while((item=next_directive(&check,&check_mcp))!=NULL){if(!directive_read_only(item,check_mcp))read_only=0;free(item);}
            if(!read_only){
                vs_trace(c,"approval-blocked","side-effecting directive withheld until the user accepts a proposed plan");
                free(ans);ans=dupstr("Plan approval is required before side-effecting tools can run. Review the proposal with /plan, then use /plan accept.");
                break;
            }
        }
        if((tool||mcp)&&vs_workflow_needs_action_approval(&c->workflow)){
            c->workflow.phase=VS_WORKFLOW_AWAITING_ACTION;
            c->workflow.action_approved=0;
            {char *sig=directive_signature(ans);if(sig){snprintf(c->workflow.pending_request,sizeof(c->workflow.pending_request),"%s",sig);free(sig);}}
            vs_trace(c,"action-awaiting-approval","manual mode requires approval before the next action");
            free(ans);ans=dupstr("Manual mode paused before an action. Use /plan approve-action, then ask the agent to continue.");
            break;
        }
        if((tool||mcp)&&strstr(ans,"[[VS_FINAL]]")){char *clean=strip_marker(ans,"[[VS_FINAL]]");vs_trace(c,"protocol","deferred premature VS_FINAL because executable directives are pending");if(clean){free(ans);ans=clean;tool=strstr(ans,"[[VS_TOOL ");mcp=strstr(ans,"[[VS_MCP ");}}
        if(!tool&&!mcp&&(execution_mode||actual_tool_seen)&&false_tool_unavailable_claim(ans)&&tool_recoveries<VS_MAX_TOOL_RECOVERIES){char *probe=0,*recover=0;int pst=-1;tool_recoveries++;snprintf(step,sizeof(step),"model claimed tool channel unavailable; probing command runner (%d/%d)",tool_recoveries,VS_MAX_TOOL_RECOVERIES);vs_trace(c,"tool-health",step);probe=vs_run_command_ctx(c,"printf 'VIBESOLARIS_COMMAND_RUNNER_OK\\n'",&pst);if(probe&&pst==0&&strstr(probe,"VIBESOLARIS_COMMAND_RUNNER_OK")){command_runner_confirmed=1;vs_trace(c,"tool-health","command runner health probe passed; false blocker suppressed");agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);agent_compact(c,task_anchor,round+1);recover=tool_channel_recovery_prompt(task_anchor,last_tool_result,ans,actual_tool_seen,c->agent_checkpoint);free(probe);free(prompt);prompt=recover;if(!prompt){vs_trace(c,"memory-error","could not allocate recovery prompt");break;}continue;}if(probe)free(probe);command_runner_confirmed=0;vs_trace(c,"tool-health","command runner health probe failed; treating availability as a host problem");}
        if(!tool&&!mcp&&strstr(ans,"[[VS_NEED_USER")){char *marker=strstr(ans,"[[VS_NEED_USER"),*q=attr(marker,"question");int oi;c->pending_question=0;c->question_option_count=0;c->question[0]=0;memset(c->question_options,0,sizeof(c->question_options));if(q&&*q){unesc(q);strncpy(c->question,q,sizeof(c->question)-1);c->pending_question=1;c->pending_question_revision++;for(oi=0;oi<VS_NEED_USER_MAX_OPTIONS;oi++){char key[16];char *option;snprintf(key,sizeof(key),"option%d",oi+1);option=attr(marker,key);if(!option)break;unesc(option);if(!*option||strlen(option)>=VS_NEED_USER_OPTION_MAX){free(option);c->question_option_count=0;memset(c->question_options,0,sizeof(c->question_options));break;}strncpy(c->question_options[oi],option,sizeof(c->question_options[oi])-1);c->question_option_count++;free(option);}free(ans);ans=q;}else{free(q);free(ans);ans=dupstr("The agent needs clarification before it can continue.");}vs_trace(c,"agent","paused because model needs user input");break;}
        if(!tool&&!mcp&&strstr(ans,"[[VS_FINAL]]")){char *clean=strip_marker(ans,"[[VS_FINAL]]");int i,all_complete=c->workflow.task_count>0;if(clean){free(ans);ans=clean;}for(i=0;i<c->workflow.task_count;i++)if(c->workflow.tasks[i].status!=VS_TASK_COMPLETED)all_complete=0;if(all_complete)c->workflow.phase=VS_WORKFLOW_COMPLETED;vs_trace(c,"agent",all_complete?"completed with explicit final marker":"turn ended with explicit final marker; remaining tasks retain host-observed status");break;}
        if(!tool&&!mcp){
            /* Read-only repository inspection is actionable even in Plan mode.
               Haiku/small models sometimes mistake custom tool directives for
               unavailable function calling; retry with an explicit read action. */
            if(!actual_tool_seen&&inspection_request(original)&&tool_recoveries<2){
                char *retry;size_t sz=strlen(original)+strlen(c->cwd)+512;
                retry=(char*)malloc(sz);
                if(retry){
                    snprintf(retry,sz,"USER_REQUEST: %s\n\nLOCAL_FILESYSTEM_ACCESS_IS_AVAILABLE. Your current working directory is %s. This is an inspection request; issue [[VS_TOOL list path=\".\" depth=\"2\" limit=\"100\"]] on its own line NOW. Read-only inspection does not require plan approval or a task_id. Do not claim you cannot access the files; do not ask the user to paste anything.",original,c->cwd);
                    tool_recoveries++;
                    vs_trace(c,"tool-recovery","inspection answer omitted file tools; requesting explicit local directory listing");
                    agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);
                    free(prompt);prompt=retry;continue;
                }
            }
            if(execution_mode||actual_tool_seen){plan_continues++;no_progress++;snprintf(step,sizeof(step),"execution response had no directive/final marker; continuing (%d consecutive) until the round ceiling",no_progress);vs_trace(c,"auto-continue",step);agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);next=continuation_prompt(task_anchor,last_tool_result,1,c->agent_checkpoint);free(prompt);prompt=next;if(!prompt){vs_trace(c,"memory-error","could not allocate continuation prompt");break;}continue;}
            if(plan_continues<(c->workflow.mode==VS_MODE_AUTO?VS_MAX_PLAN_CONTINUES*2:VS_MAX_PLAN_CONTINUES)&&should_auto_continue(original,ans)){plan_continues++;snprintf(step,sizeof(step),"planning-only response detected; continuing (%d/%d)",plan_continues,c->workflow.mode==VS_MODE_AUTO?VS_MAX_PLAN_CONTINUES*2:VS_MAX_PLAN_CONTINUES);vs_trace(c,"auto-continue",step);agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);next=continuation_prompt(task_anchor,0,1,c->agent_checkpoint);free(prompt);prompt=next;if(!prompt){vs_trace(c,"memory-error","could not allocate continuation prompt");break;}continue;}
            vs_trace(c,"agent","completed informational turn without tool work");break;
        }
        plan_continues=0;actual_tool_seen=1;execution_mode=1;if(!vs_workflow_authorized(&c->workflow))explore_rounds++;if(vs_cancel_requested(c)){cancelled=1;vs_trace(c,"cancel","agent turn stopped before tool action");break;}
        {char *sig=directive_signature(ans);if(sig&&last_directives&&!strcmp(sig,last_directives)){free(sig);vs_trace(c,"no-progress","same tool directive repeated after its previous result; refusing to execute it again");result=dupstr("ERROR: this exact directive batch was already executed in this turn and its result is shown above; it was not run again. Continue with a different concrete action, VS_NEED_USER for a genuine blocker, or VS_FINAL when complete.");if(!result){vs_trace(c,"memory-error","could not allocate refusal result");break;}vs_trace(c,"tool-output",result);free(last_tool_result);last_tool_result=dupstr(result);agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);next=continuation_prompt(task_anchor,result,0,c->agent_checkpoint);free(result);free(prompt);prompt=next;if(!prompt){vs_trace(c,"memory-error","could not allocate next tool-round prompt");break;}continue;}if(sig){free(last_directives);last_directives=sig;}}
        {int dc=directive_count(ans),active=c->workflow.current_task_id;snprintf(step,sizeof(step),"executing %d directive(s)%s serially",dc,dc>1?" as a batch":"");vs_trace(c,"tool-batch",step);if(active)vs_workflow_set_task_status(&c->workflow,active,VS_TASK_RUNNING,"running serially");if(c->workflow.mode==VS_MODE_MANUAL)(void)vs_workflow_consume_action_approval(&c->workflow);result=execute_batch(c,ans,&command_runner_confirmed);if(active&&result&&strstr(result,"COMMAND_RUNNER_STATUS: host_error"))vs_workflow_set_task_status(&c->workflow,active,VS_TASK_FAILED,result);}no_progress=0;
        if(result){char *bounded=vs_compact_text_limit(result,VS_AGENT_TOOL_RESULT_MAX,"tool result compacted for model context");if(bounded){if(strlen(bounded)<strlen(result))vs_trace(c,"limit","tool result compacted to keep autonomous context bounded");free(result);result=bounded;}vs_trace(c,"tool-output",result);}
        if(vs_cancel_requested(c)){cancelled=1;free(result);result=0;vs_trace(c,"cancel","agent turn stopped after tool returned");break;}
        free(last_tool_result);last_tool_result=result?dupstr(result):0;agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);agent_compact(c,task_anchor,round+1);next=continuation_prompt(task_anchor,result,0,c->agent_checkpoint);if(!next){free(result);vs_trace(c,"memory-error","could not allocate next tool-round prompt");break;}free(result);free(prompt);prompt=next;
    }
    if(!cancelled&&round>=max_rounds){snprintf(step,sizeof(step),"maximum autonomous agent rounds reached (%d); tools remain available next turn",max_rounds);vs_trace(c,"limit",step);if(execution_mode||(ans&&(strstr(ans,"[[VS_TOOL ")||strstr(ans,"[[VS_MCP ")))){free(ans);ans=dupstr("The single-turn autonomous ceiling was reached while work was still in progress. The task was not marked complete; continue from the existing files and conversation state, or raise VIBESOLARIS_MAX_AGENT_ROUNDS (up to 256).");}}
    if(cancelled){vs_workflow_clear_pending_task(c);free(ans);ans=dupstr("Stopped by user.");}
    if(c&&c->workflow.mode==VS_MODE_MANUAL&&c->workflow.action_approved){c->workflow.action_approved=0;c->workflow.pending_request[0]=0;if(c->workflow.phase==VS_WORKFLOW_DOING)c->workflow.phase=VS_WORKFLOW_AWAITING_ACTION;}
    /* Working history and the compacted checkpoint deliberately survive the
       turn boundary: the next turn's start resets the history, and /compact
       (agent_compact_force) needs the retained state to be meaningful when
       the user invokes it between turns. */
    free(prompt);free(task_anchor);free(last_tool_result);free(last_directives);
    if(ans&&original){
        if(!c->suppress_history_once){vs_history_add(c,"user",original);vs_history_add(c,"assistant",ans);}
        else c->suppress_history_once=0;
    }
    free(original);return ans;
}
