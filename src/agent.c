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
    char *p,*summary,*bounded,*fallback;size_t n;int interval=compact_interval(),saved_attachment_from;char trace[256];
    if(!c||c->agent_history_count<=0||interval==0)return;
    if((completed_rounds%interval)!=0&&c->agent_history_bytes<(VS_AGENT_HISTORY_BUDGET*3)/4)return;
    n=strlen(task?task:"")+(c->agent_checkpoint?strlen(c->agent_checkpoint):0)+1200;p=(char*)malloc(n);if(!p)return;
    snprintf(p,n,
        "INTERNAL_CONTEXT_COMPACTION. Return ONLY a dense working-state summary for another coding-agent round. Do not call tools, do not answer the user, and do not emit VS_* markers. Preserve: objective/constraints; host OS/toolchain facts; files inspected or changed; exact important errors; commands/tests and outcomes; design decisions; what has already been tried; unresolved issues; and the next concrete steps. Drop repeated protocol instructions, filler, duplicate logs, superseded plans, and unimportant raw output.\n\nACTIVE_TASK:\n%s\n\nPREVIOUS_CHECKPOINT:\n%s\n",
        task?task:"",c->agent_checkpoint?c->agent_checkpoint:"(none)");
    vs_trace(c,"compact","compacting autonomous working context");
    /* A newly loaded image is intended for the next normal agent round, not
       for this housekeeping request.  Suppress attachments while compacting
       and restore the pending one-shot attachment range afterwards. */
    saved_attachment_from=c->attachment_send_from;
    c->attachment_send_from=c->attachment_count;
    summary=vs_chat(c,p);
    c->attachment_send_from=saved_attachment_from;
    free(p);
    if(bad_checkpoint(summary)){if(summary)free(summary);fallback=fallback_checkpoint(c,task);summary=fallback;vs_trace(c,"compact","provider compaction was unusable; kept a bounded mechanical checkpoint instead");}
    if(!summary)return;
    bounded=vs_compact_text_limit(summary,VS_AGENT_CHECKPOINT_MAX,"semantic working-state checkpoint compacted");free(summary);if(!bounded)return;
    agent_checkpoint_clear(c);c->agent_checkpoint=bounded;c->agent_checkpoint_bytes=strlen(bounded);agent_history_clear(c);
    snprintf(trace,sizeof(trace),"working context compacted after %d rounds to %lu bytes",completed_rounds,(unsigned long)c->agent_checkpoint_bytes);vs_trace(c,"compact",trace);
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
    if(!strncmp(d+10,"read ",5))return native_read(c,d);
    if(!strncmp(d+10,"list ",5))return native_list(c,d);
    if(!strncmp(d+10,"search ",7))return native_search(c,d);
    if(!strncmp(d+10,"graph ",6)){char *m=attr(d,"mode"),*p=attr(d,"path"),*sum=0;VSGraph g;VSGraphMode mode=VS_GRAPH_FILES;if(m){unesc(m);if(!strcmp(m,"fields")||!strcmp(m,"field")||!strcmp(m,"symbols"))mode=VS_GRAPH_FIELDS;}if(p)unesc(p);if(vs_graph_build(c,p&&*p?p:c->cwd,mode,&g)>=0)sum=vs_graph_summary(&g,100);free(m);free(p);return sum?sum:dupstr("ERROR: graph build failed");}
    if(!strncmp(d+10,"web_search ",11)){char *q=attr(d,"query"),*cnt=attr(d,"count"),*r;int n=5;if(q)unesc(q);if(cnt){n=atoi(cnt);if(n<1)n=1;if(n>12)n=12;}r=q&&*q?vs_web_search(c,q,n):dupstr("ERROR: web_search requires query=\"...\"");free(q);free(cnt);return r;}
    if(!strncmp(d+10,"web_fetch ",10)){char *u=attr(d,"url"),*r;if(u)unesc(u);r=u&&*u?vs_web_fetch(c,u,96U*1024U):dupstr("ERROR: web_fetch requires url=\"https://...\"");free(u);return r;}
    if(!strncmp(d+10,"run ",4)){char *cmd=attr(d,"cmd"),*raw=0,*bounded=0;int st=0;if(cmd)unesc(cmd);raw=cmd?vs_run_command_ctx(c,cmd,&st):0;if(raw){size_t z;bounded=vs_compact_text_limit(raw,VS_AGENT_COMMAND_RESULT_MAX,"command output compacted; narrow the command or inspect a specific file/range");free(raw);raw=bounded;*command_runner_confirmed=1;z=strlen(raw?raw:"")+1024;result=(char*)malloc(z);if(result)snprintf(result,z,"COMMAND_RUNNER_STATUS: operational\nCOMMAND_SHELL: %s\nCOMMAND_EXIT_STATUS: %d\nOUTPUT:\n%s%s",vs_command_shell_name(),st,raw?raw:"",(solaris_host(c)&&st!=0&&solaris_command_mismatch(raw))?"\nSOLARIS_HINT: This looks like a GNU/Linux-vs-Solaris command mismatch. Use Solaris/POSIX syntax or VibeSolaris read/list/search; do not retry the same unsupported option unchanged.\n":"");free(raw);}else{*command_runner_confirmed=0;result=dupstr("COMMAND_RUNNER_STATUS: host_error\nCOMMAND_EXIT_STATUS: -1\nERROR: host could not start or communicate with the command process.");}free(cmd);return result?result:dupstr("ERROR: command result allocation failed");}
    if(!strncmp(d+10,"write ",6)){char *p=attr(d,"path"),*ct=attr(d,"content");int rc=-1;if(p)unesc(p);if(ct)unesc(ct);if(p&&ct)rc=vs_write_file(p,ct);if(rc==0&&p)vs_cache_invalidate(c,p);result=dupstr(rc==0?"OK: file written":"ERROR: write failed");free(p);free(ct);return result;}
    return dupstr("ERROR: unknown VS_TOOL operation");
}

static const char *earliest_directive(const char *s,int *is_mcp){const char *t=strstr(s,"[[VS_TOOL "),*m=strstr(s,"[[VS_MCP ");if(!t&&!m)return 0;if(m&&(!t||m<t)){*is_mcp=1;return m;}*is_mcp=0;return t;}
static int directive_count(const char *s){int n=0,m;const char *p=s,*d,*e;while((d=earliest_directive(p,&m))!=0){e=strstr(d,"]]" );if(!e)break;n++;p=e+2;}return n;}
static char *next_directive(const char **cursor,int *is_mcp){const char *d,*e;char *o;size_t n;if(!cursor||!*cursor)return 0;d=earliest_directive(*cursor,is_mcp);if(!d)return 0;e=strstr(d,"]]" );if(!e){*cursor=d+strlen(d);return 0;}n=(size_t)(e+2-d);o=(char*)malloc(n+1);if(!o)return 0;memcpy(o,d,n);o[n]=0;*cursor=e+2;return o;}
static char *execute_batch(VSContext *c,const char *answer,int *command_runner_confirmed){int total=directive_count(answer),original_total=total,i=0,is_mcp;const char *cur=answer;char *d,*r,*server,*name,*args,*single=0;ABuffer b;if(total<=0)return 0;if(total>VS_MAX_BATCH_TOOLS)total=VS_MAX_BATCH_TOOLS;if(total==1){d=next_directive(&cur,&is_mcp);if(!d)return dupstr("ERROR: malformed directive");if(is_mcp){server=attr(d,"server");name=attr(d,"tool");args=attr(d,"args");if(args)unesc(args);single=(server&&name)?vs_mcp_call(c,server,name,args&&*args?args:"{}"):dupstr("ERROR: malformed VS_MCP directive");free(server);free(name);free(args);}else single=execute_tool(c,d,command_runner_confirmed);free(d);return single;}
    if(ab_init(&b,VS_AGENT_TOOL_RESULT_MAX)!=0)return 0;
    ab_addf(&b,"BATCH_TOOL_RESULT: %d directive(s) executed in order\n",total);
    while(i<total&&(d=next_directive(&cur,&is_mcp))!=0){i++;ab_addf(&b,"\n--- directive %d: %.220s ---\n",i,d);if(is_mcp){server=attr(d,"server");name=attr(d,"tool");args=attr(d,"args");if(args)unesc(args);r=(server&&name)?vs_mcp_call(c,server,name,args&&*args?args:"{}"):dupstr("ERROR: malformed VS_MCP directive");free(server);free(name);free(args);}else r=execute_tool(c,d,command_runner_confirmed);if(r){char *q=vs_compact_text_limit(r,VS_AGENT_TOOL_RESULT_MAX/(size_t)(total?total:1),"batched tool result compacted");if(q){ab_add(&b,q);free(q);}free(r);}free(d);}
    if(original_total>VS_MAX_BATCH_TOOLS)ab_addf(&b,"\n...[only the first %d directives were executed; issue remaining dependent work next round]...\n",VS_MAX_BATCH_TOOLS);
    return ab_finish(&b);}

char *vs_agent_turn(VSContext *c,const char *user){
    char *prompt=dupstr(user),*ans=0,*original=dupstr(user),*task_anchor=0,*last_tool_result=0;int round,plan_continues=0,tool_recoveries=0,command_runner_confirmed=0,actual_tool_seen=0,execution_mode=0,cancelled=0,max_rounds=VS_MAX_TOOL_ROUNDS;char step[512];
    {const char *mr=getenv("VIBESOLARIS_MAX_AGENT_ROUNDS");if(mr&&*mr){char *ep=0;long v=strtol(mr,&ep,10);if(ep&&*ep==0&&v>=8&&v<=256)max_rounds=(int)v;}}
    if(!prompt||!original){free(prompt);free(original);return dupstr("out of memory");}
    task_anchor=build_task_anchor(c,original);if(!task_anchor)task_anchor=dupstr(original);if(!task_anchor){free(prompt);free(original);return dupstr("out of memory");}
    agent_context_clear(c);c->attachment_send_from=0;execution_mode=execution_intent(original);vs_trace_clear(c);vs_trace(c,"agent","starting agent turn");
    if(vs_cancel_requested(c)){cancelled=1;vs_trace(c,"cancel","agent turn stopped by user before work began");}
    if(!cancelled&&c->mcp_server_count>0){int n=vs_mcp_refresh_all(c,0);if(vs_cancel_requested(c)){cancelled=1;vs_trace(c,"cancel","agent turn stopped while preparing MCP tools");}else{snprintf(step,sizeof(step),"MCP catalogue ready: %d tool(s)",n<0?0:n);vs_trace(c,"mcp",step);}}
    for(round=0;!cancelled&&round<max_rounds;round++){
        char *tool,*mcp,*result=0,*next;
        if(vs_cancel_requested(c)){cancelled=1;vs_trace(c,"cancel","agent turn stopped by user");break;}
        if(round>0&&(round%VS_LONG_TASK_CHECKPOINT)==0){snprintf(step,sizeof(step),"long-task checkpoint: %d rounds completed; working context=%lu bytes, checkpoint=%lu bytes",round,(unsigned long)c->agent_history_bytes,(unsigned long)c->agent_checkpoint_bytes);vs_trace(c,"checkpoint",step);}
        snprintf(step,sizeof(step),"model round %d",round+1);vs_trace(c,"model",step);vs_trace(c,"model-input",prompt);vs_trace(c,"model-wait","requesting provider response");
        free(ans);ans=vs_chat(c,prompt);c->attachment_send_from=c->attachment_count;if(!ans){free(prompt);free(original);free(task_anchor);free(last_tool_result);agent_context_clear(c);return dupstr("provider error");}
        if(vs_cancel_requested(c)){cancelled=1;free(ans);ans=dupstr("Stopped by user.");vs_trace(c,"cancel","agent turn stopped while waiting for the provider");break;}vs_trace(c,"model-output",ans);vs_trace(c,"model-result","model response received");
        tool=strstr(ans,"[[VS_TOOL ");mcp=strstr(ans,"[[VS_MCP ");
        if((tool||mcp)&&strstr(ans,"[[VS_FINAL]]")){char *clean=strip_marker(ans,"[[VS_FINAL]]");vs_trace(c,"protocol","deferred premature VS_FINAL because executable directives are pending");if(clean){free(ans);ans=clean;tool=strstr(ans,"[[VS_TOOL ");mcp=strstr(ans,"[[VS_MCP ");}}
        if(!tool&&!mcp&&(execution_mode||actual_tool_seen)&&false_tool_unavailable_claim(ans)&&tool_recoveries<VS_MAX_TOOL_RECOVERIES){char *probe=0,*recover=0;int pst=-1;tool_recoveries++;snprintf(step,sizeof(step),"model claimed tool channel unavailable; probing command runner (%d/%d)",tool_recoveries,VS_MAX_TOOL_RECOVERIES);vs_trace(c,"tool-health",step);probe=vs_run_command_ctx(c,"printf 'VIBESOLARIS_COMMAND_RUNNER_OK\\n'",&pst);if(probe&&pst==0&&strstr(probe,"VIBESOLARIS_COMMAND_RUNNER_OK")){command_runner_confirmed=1;vs_trace(c,"tool-health","command runner health probe passed; false blocker suppressed");agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);agent_compact(c,task_anchor,round+1);recover=tool_channel_recovery_prompt(task_anchor,last_tool_result,ans,actual_tool_seen,c->agent_checkpoint);free(probe);free(prompt);prompt=recover;if(!prompt){vs_trace(c,"memory-error","could not allocate recovery prompt");break;}continue;}if(probe)free(probe);command_runner_confirmed=0;vs_trace(c,"tool-health","command runner health probe failed; treating availability as a host problem");}
        if(!tool&&!mcp&&strstr(ans,"[[VS_NEED_USER")){char *q=attr(strstr(ans,"[[VS_NEED_USER"),"question");vs_trace(c,"agent","paused because model needs user input");if(q&&*q){free(ans);ans=q;}else free(q);break;}
        if(!tool&&!mcp&&strstr(ans,"[[VS_FINAL]]")){char *clean=strip_marker(ans,"[[VS_FINAL]]");if(clean){free(ans);ans=clean;}vs_trace(c,"agent","completed with explicit final marker");break;}
        if(!tool&&!mcp){
            if(execution_mode||actual_tool_seen){plan_continues++;snprintf(step,sizeof(step),"execution turn returned no directive/final marker; continuing (%d, ceiling %d)",plan_continues,max_rounds);vs_trace(c,"auto-continue",step);agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);agent_compact(c,task_anchor,round+1);next=continuation_prompt(task_anchor,last_tool_result,1,c->agent_checkpoint);free(prompt);prompt=next;if(!prompt){vs_trace(c,"memory-error","could not allocate continuation prompt");break;}continue;}
            if(plan_continues<VS_MAX_PLAN_CONTINUES&&should_auto_continue(original,ans)){plan_continues++;snprintf(step,sizeof(step),"planning-only response detected; continuing (%d/%d)",plan_continues,VS_MAX_PLAN_CONTINUES);vs_trace(c,"auto-continue",step);agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);agent_compact(c,task_anchor,round+1);next=continuation_prompt(task_anchor,0,1,c->agent_checkpoint);free(prompt);prompt=next;if(!prompt){vs_trace(c,"memory-error","could not allocate continuation prompt");break;}continue;}
            vs_trace(c,"agent","completed informational turn without tool work");break;
        }
        plan_continues=0;actual_tool_seen=1;execution_mode=1;if(vs_cancel_requested(c)){cancelled=1;vs_trace(c,"cancel","agent turn stopped before tool action");break;}
        {int dc=directive_count(ans);snprintf(step,sizeof(step),"executing %d directive(s)%s",dc>VS_MAX_BATCH_TOOLS?VS_MAX_BATCH_TOOLS:dc,dc>1?" as a batch":"");vs_trace(c,"tool-batch",step);result=execute_batch(c,ans,&command_runner_confirmed);}
        if(result){char *bounded=vs_compact_text_limit(result,VS_AGENT_TOOL_RESULT_MAX,"tool result compacted for model context");if(bounded){if(strlen(bounded)<strlen(result))vs_trace(c,"limit","tool result compacted to keep autonomous context bounded");free(result);result=bounded;}vs_trace(c,"tool-output",result);}
        if(vs_cancel_requested(c)){cancelled=1;free(result);result=0;vs_trace(c,"cancel","agent turn stopped after tool returned");break;}
        free(last_tool_result);last_tool_result=result?dupstr(result):0;agent_history_add(c,"user",prompt);agent_history_add(c,"assistant",ans);agent_compact(c,task_anchor,round+1);next=continuation_prompt(task_anchor,result,0,c->agent_checkpoint);if(!next){free(result);vs_trace(c,"memory-error","could not allocate next tool-round prompt");break;}free(result);free(prompt);prompt=next;
    }
    if(!cancelled&&round>=max_rounds){snprintf(step,sizeof(step),"maximum autonomous agent rounds reached (%d); tools remain available next turn",max_rounds);vs_trace(c,"limit",step);if(execution_mode||(ans&&(strstr(ans,"[[VS_TOOL ")||strstr(ans,"[[VS_MCP ")))){free(ans);ans=dupstr("The single-turn autonomous ceiling was reached while work was still in progress. The task was not marked complete; continue from the existing files and conversation state, or raise VIBESOLARIS_MAX_AGENT_ROUNDS (up to 256).");}}
    if(cancelled){free(ans);ans=dupstr("Stopped by user.");}
    free(prompt);free(task_anchor);free(last_tool_result);agent_context_clear(c);
    if(ans&&original){vs_history_add(c,"user",original);vs_history_add(c,"assistant",ans);}free(original);return ans;
}
