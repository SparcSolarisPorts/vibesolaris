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

static void agent_compact(VSContext *c,const char *task,int completed_rounds){
    char *p,*summary,*bounded,*fallback;size_t n;int interval=compact_interval(),saved_attachment_from;VSHTTPChunkCallback saved_stream;void *saved_stream_data;char trace[256];
    if(!c||c->agent_history_count<=0||interval==0)return;
    if((completed_rounds%interval)!=0&&c->agent_history_bytes<(VS_AGENT_HISTORY_BUDGET*3)/4)return;
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

static char *continuation_prompt(const char *task,const char *result,int planning_only,const char *checkpoint){
    const char *lead=planning_only?"The previous response did not complete the execution lifecycle. Continue now with concrete work.":"TOOL_RESULT:";
    size_t n=strlen(task?task:"")+strlen(result?result:"")+strlen(checkpoint?checkpoint:"")+strlen(lead)+512;char *o=(char*)malloc(n);if(!o)return 0;
    snprintf(o,n,"ACTIVE_TASK:\n%s\n\nCOMPACTED_WORKING_STATE:\n%s\n\n%s\n%s\n\nContinue efficiently from this state. Use one or more VS_TOOL/VS_MCP directives, VS_NEED_USER for a genuine blocker, or VS_FINAL only when complete. Do not redo completed inspection.\n",task?task:"",checkpoint?checkpoint:"(none)",lead,planning_only?"":(result?result:"(no result)"));return o;
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

static char *execute_tool(VSContext *c,const char *d,int *command_runner_confirmed){char *result=0;if(!d)return dupstr("ERROR: empty tool directive");
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
    return dupstr("ERROR: unknown VS_TOOL operation");
}

static const char *earliest_directive(const char *s,int *is_mcp){const char *t=strstr(s,"[[VS_TOOL "),*m=strstr(s,"[[VS_MCP ");if(!t&&!m)return 0;if(m&&(!t||m<t)){*is_mcp=1;return m;}*is_mcp=0;return t;}
static int directive_count(const char *s){int n=0,m;const char *p=s,*d,*e;while((d=earliest_directive(p,&m))!=0){e=strstr(d,"]]" );if(!e)break;n++;p=e+2;}return n;}
static char *directive_signature(const char *s){const char *p=s,*d,*e;int m;size_t n=0;char *out;while((d=earliest_directive(p,&m))!=0){e=strstr(d,"]]" );if(!e)break;n+=(size_t)(e+2-d)+1;p=e+2;}if(!n)return dupstr("");out=(char*)malloc(n+1);if(!out)return 0;out[0]=0;p=s;while((d=earliest_directive(p,&m))!=0){size_t z;e=strstr(d,"]]" );if(!e)break;z=(size_t)(e+2-d);strncat(out,d,z);strcat(out,"\n");p=e+2;}return out;}
static char *next_directive(const char **cursor,int *is_mcp){const char *d,*e;char *o;size_t n;if(!cursor||!*cursor)return 0;d=earliest_directive(*cursor,is_mcp);if(!d)return 0;e=strstr(d,"]]" );if(!e){*cursor=d+strlen(d);return 0;}n=(size_t)(e+2-d);o=(char*)malloc(n+1);if(!o)return 0;memcpy(o,d,n);o[n]=0;*cursor=e+2;return o;}
static int directive_known(const char *d,int is_mcp){
    static const char *ops[]={"read ","list ","search ","graph ","web_search ","web_fetch ","run ","write ","image ","subagent ",NULL};
    int i;if(is_mcp)return 1;if(strncmp(d,"[[VS_TOOL ",10))return 0;
    for(i=0;ops[i];i++)if(!strncmp(d+10,ops[i],strlen(ops[i])))return 1;
    return 0;
}
static char *next_directive(const char **cursor,int *is_mcp);
static int directive_read_only(const char *d,int is_mcp){
    if(is_mcp)return 0;
    return !strncmp(d,"[[VS_TOOL read ",15)||!strncmp(d,"[[VS_TOOL list ",15)||
           !strncmp(d,"[[VS_TOOL search ",17)||!strncmp(d,"[[VS_TOOL graph ",16)||
           !strncmp(d,"[[VS_TOOL web_search ",21)||!strncmp(d,"[[VS_TOOL web_fetch ",20);
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
static int directive_schema_valid(VSContext *c,const char *d,int is_mcp){
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
    static const char *mcp_attrs[] = {"server","tool","args","task_id",NULL};
    const char *p,*end;char op[32],name[64],seen[16][64];int i,j,seen_count=0,seen_task=0;const char *const *allowed=0;char *value;
    if(!d)return 0;end=strstr(d,"]]" );if(!end)return 0;p=d+2;
    if(is_mcp){if(strncmp(p,"VS_MCP ",7))return 0;p+=7;strcpy(op,"MCP");allowed=mcp_attrs;}
    else {if(strncmp(p,"VS_TOOL ",8))return 0;p+=8;for(i=0;i<(int)sizeof(op)-1&&p<end&&!isspace((unsigned char)*p);i++,p++)op[i]=*p;op[i]=0;
        if(!strcmp(op,"read"))allowed=read_attrs;else if(!strcmp(op,"list"))allowed=list_attrs;else if(!strcmp(op,"search"))allowed=search_attrs;else if(!strcmp(op,"graph"))allowed=graph_attrs;else if(!strcmp(op,"web_search"))allowed=web_search_attrs;else if(!strcmp(op,"web_fetch"))allowed=web_fetch_attrs;else if(!strcmp(op,"run"))allowed=run_attrs;else if(!strcmp(op,"write"))allowed=write_attrs;else if(!strcmp(op,"image"))allowed=image_attrs;else if(!strcmp(op,"subagent"))allowed=subagent_attrs;else return 0;}
    while(p<end){while(p<end&&isspace((unsigned char)*p))p++;if(p==end)break;j=0;while(p<end&&(isalnum((unsigned char)*p)||*p=='_')){if(j>=(int)sizeof(name)-1)return 0;name[j++]=*p++;}if(!j)return 0;name[j]=0;if(*p++!='=')return 0;if(p>=end||*p++!='\"')return 0;
        {const char *v=p;while(p<end){if(*p=='\\'&&p+1<end){p+=2;continue;}if(*p=='\"')break;p++;}if(p>=end)return 0;value=(char*)malloc((size_t)(p-v)+1);if(!value)return 0;memcpy(value,v,(size_t)(p-v));value[p-v]=0;p++;}
        if(!name_in_list(name,allowed)&&!name_in_list(name,common)){free(value);return 0;}
        for(i=0;i<seen_count;i++)if(!strcmp(name,seen[i])){free(value);return 0;}
        if(seen_count>=16){free(value);return 0;}strcpy(seen[seen_count++],name);
        if(!strcmp(name,"task_id")){if(seen_task++){free(value);return 0;}}
        free(value);
    }
    if(is_mcp){char *server=attr(d,"server"),*tool=attr(d,"tool"),*args=attr(d,"args");int found=0;size_t z;for(i=0;c&&i<c->mcp_tool_count;i++)if(server&&tool&&!strcmp(c->mcp_tools[i].server,server)&&!strcmp(c->mcp_tools[i].name,tool)){found=1;mcp_found=i;break;}if(args)unesc(args);z=args?strlen(args):0;while(z&&isspace((unsigned char)args[z-1]))args[--z]=0;while(args&&*args&&isspace((unsigned char)*args)){memmove(args,args+1,strlen(args));z--;}if(!args||!*args){free(server);free(tool);free(args);return 0;}if(!server||!tool||!found||z>VS_MCP_SCHEMA_MAX){free(server);free(tool);free(args);return 0;}free(server);free(tool);free(args);return mcp_found;}
    if(!strcmp(op,"read")||!strcmp(op,"run")||!strcmp(op,"image"))return has_attr(d,!strcmp(op,"run")?"cmd":"path");
    if(!strcmp(op,"search")||!strcmp(op,"web_search"))return has_attr(d,"query");
    if(!strcmp(op,"web_fetch"))return has_attr(d,"url");
    if(!strcmp(op,"write"))return has_attr(d,"path")&&has_attr(d,"content");
    if(!strcmp(op,"subagent"))return has_attr(d,"task");
    return 1;
}
static int preflight_batch(VSContext *c,const char *answer,int *count_out){
    const char *cur=answer,*scan=answer;int n=0,is_mcp,authorized=c&&vs_workflow_authorized(&c->workflow),staged_task=c?c->workflow.current_task_id:0;char *d;
    while((d=next_directive(&cur,&is_mcp))!=NULL){
        n++;
        if(n>VS_MAX_BATCH_TOOLS){free(d);return -1;}
        {int schema=directive_schema_valid(c,d,is_mcp);if(!directive_known(d,is_mcp)||schema<0){free(d);return -1;}if(is_mcp){char *args=attr(d,"args");int call_ok=0;if(args)unesc(args);call_ok=valid_json_object(args)&&c&&schema<c->mcp_tool_count;free(args);if(!call_ok){free(d);return -1;}}}
        if(!authorized&&!directive_read_only(d,is_mcp)){free(d);return -2;}
        if(authorized){char *id_s=attr(d,"task_id"),*ep=NULL;long id=0;int valid=0,k;
            if(!id_s){free(d);return -6;}
            id=strtol(id_s,&ep,10);if(ep==id_s||*ep||id<=0||id>2147483647L){free(id_s);free(d);return -6;}
            if(!vs_workflow_task_ready(&c->workflow,(int)id)){free(id_s);free(d);return -7;}
            for(k=0;k<c->workflow.task_count;k++)if(c->workflow.tasks[k].id==(int)id){valid=1;break;}
            if(valid)staged_task=(int)id;
            free(id_s);
        }
        if(is_mcp){char *server=attr(d,"server"),*tool=attr(d,"tool");int found=0,i;
            if(!server||!tool){free(server);free(tool);free(d);return -1;}
            if(!authorized){free(server);free(tool);free(d);return -2;}
            for(i=0;i<c->mcp_tool_count;i++)if(!strcmp(c->mcp_tools[i].server,server)&&!strcmp(c->mcp_tools[i].name,tool)){found=1;break;}
            free(server);free(tool);if(!found){free(d);return -3;}
        } else if(!directive_read_only(d,0)&&!authorized){free(d);return -2;}
        free(d);
    }
    /* A marker without a closing delimiter is malformed; do not execute a valid prefix. */
    while((scan=earliest_directive(scan,&is_mcp))!=NULL){const char *end=strstr(scan,"]]" );if(!end)return -1;scan=end+2;}
    if(count_out)*count_out=n;
    if(c&&c->workflow.mode==VS_MODE_MANUAL&&authorized&&n>1)return -4;
    if(c&&c->workflow.mode==VS_MODE_MANUAL&&authorized&&c->workflow.action_approved){
        char *sig=directive_signature(answer);
        int same=sig&&c->workflow.pending_request[0]&&!strcmp(sig,c->workflow.pending_request);
        free(sig);
        if(!same)return -5;
    }
    if(c&&authorized&&n>0)c->workflow.current_task_id=staged_task;
    return 0;
}
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
static char *execute_batch(VSContext *c,const char *answer,int *command_runner_confirmed){int total=directive_count(answer),original_total=total,i=0,is_mcp;const char *cur=answer;char *d,*r,*server,*name,*args,*single=0;ABuffer b;if(total<=0)return 0;if(total>VS_MAX_BATCH_TOOLS)total=VS_MAX_BATCH_TOOLS;if(all_subagent_directives(answer,total)){char *parallel=parallel_subagent_batch(c,answer,total);if(parallel)return parallel;}if(total==1){d=next_directive(&cur,&is_mcp);if(!d)return dupstr("ERROR: malformed directive");if(is_mcp){server=attr(d,"server");name=attr(d,"tool");args=attr(d,"args");if(args)unesc(args);single=(server&&name)?vs_mcp_call(c,server,name,args&&*args?args:"{}"):dupstr("ERROR: malformed VS_MCP directive");free(server);free(name);free(args);}else single=execute_tool(c,d,command_runner_confirmed);free(d);return single;}
    if(ab_init(&b,VS_AGENT_TOOL_RESULT_MAX)!=0)return 0;
    ab_addf(&b,"BATCH_TOOL_RESULT: %d directive(s) executed in order\n",total);
    while(i<total&&(d=next_directive(&cur,&is_mcp))!=0){i++;ab_addf(&b,"\n--- directive %d: %.220s ---\n",i,d);if(is_mcp){server=attr(d,"server");name=attr(d,"tool");args=attr(d,"args");if(args)unesc(args);r=(server&&name)?vs_mcp_call(c,server,name,args&&*args?args:"{}"):dupstr("ERROR: malformed VS_MCP directive");free(server);free(name);free(args);}else r=execute_tool(c,d,command_runner_confirmed);if(r){char *q=vs_compact_text_limit(r,VS_AGENT_TOOL_RESULT_MAX/(size_t)(total?total:1),"batched tool result compacted");if(q){ab_add(&b,q);free(q);}free(r);}free(d);}
    if(original_total>VS_MAX_BATCH_TOOLS)ab_addf(&b,"\n...[only the first %d directives were executed; issue remaining dependent work next round]...\n",VS_MAX_BATCH_TOOLS);
    return ab_finish(&b);}

char *vs_agent_turn(VSContext *c,const char *user){
    char *prompt=dupstr(user),*ans=0,*original=dupstr(user),*task_anchor=0,*last_tool_result=0,*last_directives=0;int round,plan_continues=0,no_progress=0,tool_recoveries=0,command_runner_confirmed=0,actual_tool_seen=0,execution_mode=0,cancelled=0,stalled=0,max_rounds=VS_MAX_TOOL_ROUNDS;char step[512];
    {const char *mr=getenv("VIBESOLARIS_MAX_AGENT_ROUNDS");if(mr&&*mr){char *ep=0;long v=strtol(mr,&ep,10);if(ep&&*ep==0&&v>=8&&v<=256)max_rounds=(int)v;}}
    if(!prompt||!original){free(prompt);free(original);return dupstr("out of memory");}
    task_anchor=build_task_anchor(c,original);if(!task_anchor)task_anchor=dupstr(original);if(!task_anchor){free(prompt);free(original);return dupstr("out of memory");}
    if(c->pending_question){c->pending_question=0;c->question_option_count=0;c->question[0]=0;memset(c->question_options,0,sizeof(c->question_options));}
    agent_context_clear(c);c->attachment_send_from=0;c->subagent_serial=0;if(c->subagent_depth==0&&!c->subagent_parent){c->subagent_record_count=0;memset(c->subagents,0,sizeof(c->subagents));}execution_mode=execution_intent(original);vs_trace_clear(c);vs_trace(c,"agent","starting agent turn");
    if(vs_cancel_requested(c)){cancelled=1;vs_trace(c,"cancel","agent turn stopped by user before work began");}
    if(!cancelled&&vs_workflow_authorized(&c->workflow)&&c->mcp_server_count>0){int n=vs_mcp_refresh_all(c,0);if(vs_cancel_requested(c)){cancelled=1;vs_trace(c,"cancel","agent turn stopped while preparing MCP tools");}else{snprintf(step,sizeof(step),"MCP catalogue ready: %d tool(s)",n<0?0:n);vs_trace(c,"mcp",step);}}
    for(round=0;!cancelled&&round<max_rounds;round++){
        char *tool,*mcp,*result=0,*next;
        if(vs_cancel_requested(c)){cancelled=1;vs_trace(c,"cancel","agent turn stopped by user");break;}
        if(round>0&&(round%VS_LONG_TASK_CHECKPOINT)==0){snprintf(step,sizeof(step),"long-task checkpoint: %d rounds completed; working context=%lu bytes, checkpoint=%lu bytes",round,(unsigned long)c->agent_history_bytes,(unsigned long)c->agent_checkpoint_bytes);vs_trace(c,"checkpoint",step);}
        snprintf(step,sizeof(step),"model round %d",round+1);vs_trace(c,"model",step);vs_trace(c,"model-input",prompt);vs_trace(c,"model-wait","requesting provider response");
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
        if(tool||mcp){int batch_count=0,gate=preflight_batch(c,ans,&batch_count);
            if(gate<0&&gate!=-2){const char *why=gate==-4?"Manual mode approves one action at a time; request a single directive.":(gate==-3?"The requested MCP tool is not in the discovered tool catalogue.":(gate==-5?"This manual approval does not match the pending action; request approval again.":(gate==-7?"The requested task is blocked, complete, or not ready in the accepted plan.":(gate==-6?"Every action must name an accepted task_id from the reviewed plan.":"The directive batch was malformed and was rejected before any action ran."))));
                if(c->workflow.mode==VS_MODE_MANUAL){c->workflow.action_approved=0;c->workflow.pending_request[0]=0;}
                vs_trace(c,"directive-rejected",why);free(ans);ans=dupstr(why);break;}
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
            if(execution_mode||actual_tool_seen){plan_continues++;no_progress++;if(no_progress>=2){stalled=1;snprintf(step,sizeof(step),"stopped after %d consecutive execution responses without a tool action or completion marker",no_progress);vs_trace(c,"no-progress",step);break;}snprintf(step,sizeof(step),"execution response had no directive/final marker; one recovery continuation remains (%d/2)",no_progress);vs_trace(c,"auto-continue",step);agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);agent_compact(c,task_anchor,round+1);next=continuation_prompt(task_anchor,last_tool_result,1,c->agent_checkpoint);free(prompt);prompt=next;if(!prompt){vs_trace(c,"memory-error","could not allocate continuation prompt");break;}continue;}
            if(plan_continues<VS_MAX_PLAN_CONTINUES&&should_auto_continue(original,ans)){plan_continues++;snprintf(step,sizeof(step),"planning-only response detected; continuing (%d/%d)",plan_continues,VS_MAX_PLAN_CONTINUES);vs_trace(c,"auto-continue",step);agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);agent_compact(c,task_anchor,round+1);next=continuation_prompt(task_anchor,0,1,c->agent_checkpoint);free(prompt);prompt=next;if(!prompt){vs_trace(c,"memory-error","could not allocate continuation prompt");break;}continue;}
            vs_trace(c,"agent","completed informational turn without tool work");break;
        }
        plan_continues=0;actual_tool_seen=1;execution_mode=1;if(vs_cancel_requested(c)){cancelled=1;vs_trace(c,"cancel","agent turn stopped before tool action");break;}
        {char *sig=directive_signature(ans);if(sig&&last_directives&&!strcmp(sig,last_directives)){free(sig);stalled=1;vs_trace(c,"no-progress","same tool directive repeated after its previous result; refusing to execute it again");break;}if(sig){free(last_directives);last_directives=sig;}}
        {int dc=directive_count(ans),active=c->workflow.current_task_id;snprintf(step,sizeof(step),"executing %d directive(s)%s serially",dc,dc>1?" as a batch":"");vs_trace(c,"tool-batch",step);if(active)vs_workflow_set_task_status(&c->workflow,active,VS_TASK_RUNNING,"running serially");if(c->workflow.mode==VS_MODE_MANUAL)(void)vs_workflow_consume_action_approval(&c->workflow);result=execute_batch(c,ans,&command_runner_confirmed);if(active&&result&&strstr(result,"COMMAND_RUNNER_STATUS: host_error"))vs_workflow_set_task_status(&c->workflow,active,VS_TASK_FAILED,result);}no_progress=0;
        if(result){char *bounded=vs_compact_text_limit(result,VS_AGENT_TOOL_RESULT_MAX,"tool result compacted for model context");if(bounded){if(strlen(bounded)<strlen(result))vs_trace(c,"limit","tool result compacted to keep autonomous context bounded");free(result);result=bounded;}vs_trace(c,"tool-output",result);}
        if(vs_cancel_requested(c)){cancelled=1;free(result);result=0;vs_trace(c,"cancel","agent turn stopped after tool returned");break;}
        free(last_tool_result);last_tool_result=result?dupstr(result):0;agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);agent_compact(c,task_anchor,round+1);next=continuation_prompt(task_anchor,result,0,c->agent_checkpoint);if(!next){free(result);vs_trace(c,"memory-error","could not allocate next tool-round prompt");break;}free(result);free(prompt);prompt=next;
    }
    if(stalled&&!cancelled){free(ans);ans=dupstr("Paused because the agent stopped making progress. The task was not marked complete; review the latest trace/result, then retry with a different approach or clarify the blocker.");}
    else if(no_progress>=2&&!cancelled){free(ans);ans=dupstr("Paused after repeated responses without a tool action or completion marker. The task was not marked complete; retry with a concrete next step or clarify the blocker.");}
    if(!cancelled&&round>=max_rounds){snprintf(step,sizeof(step),"maximum autonomous agent rounds reached (%d); tools remain available next turn",max_rounds);vs_trace(c,"limit",step);if(execution_mode||(ans&&(strstr(ans,"[[VS_TOOL ")||strstr(ans,"[[VS_MCP ")))){free(ans);ans=dupstr("The single-turn autonomous ceiling was reached while work was still in progress. The task was not marked complete; continue from the existing files and conversation state, or raise VIBESOLARIS_MAX_AGENT_ROUNDS (up to 256).");}}
    if(cancelled){vs_workflow_clear_pending_task(c);free(ans);ans=dupstr("Stopped by user.");}
    if(c&&c->workflow.mode==VS_MODE_MANUAL&&c->workflow.action_approved){c->workflow.action_approved=0;c->workflow.pending_request[0]=0;if(c->workflow.phase==VS_WORKFLOW_DOING)c->workflow.phase=VS_WORKFLOW_AWAITING_ACTION;}
    free(prompt);free(task_anchor);free(last_tool_result);free(last_directives);agent_context_clear(c);
    if(ans&&original){
        if(!c->suppress_history_once){vs_history_add(c,"user",original);vs_history_add(c,"assistant",ans);}
        else c->suppress_history_once=0;
    }
    free(original);return ans;
}
