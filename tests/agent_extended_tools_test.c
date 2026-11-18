/* SPDX-License-Identifier: Unlicense */
/* Regression test for the recurring "directive batch was malformed and was
   rejected" dead-end.  Covers two things:
   1. The extended tool set advertised in the system prompt (glob, grep,
      edit, task_*, ...) is actually dispatched to the real module
      implementations instead of being rejected as unknown operations.
   2. A genuinely malformed batch (unknown op, truncated marker) no longer
      ends the agent turn: the model receives a precise ERROR and a re-emit
      request, and the turn continues.
   3. An illustrative [[VS_TOOL ...]] placeholder mentioned inside prose is
      skipped, not rejected as an unknown operation, and the real directives
      in the same response still execute. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int chats=0,fails=0;
static char root[512],file1[640],file2[640],filec[640];
static int grep_seen=0,glob_seen=0,edit_seen=0,task_seen=0;
static int recovery_unknown_seen=0,recovery_trunc_seen=0,recovery_badattr_seen=0,ask_seen=0,placeholder_seen=0;

#define CHECK(c,m) do{ if(!(c)){fprintf(stderr,"FAIL: %s\n",m);fails++;} else printf("ok: %s\n",m);}while(0)

static char *d(const char*s){size_t n=strlen(s);char*p=(char*)malloc(n+1);if(p)memcpy(p,s,n+1);return p;}
static char *read_all(const char *p){FILE*f=fopen(p,"rb");long n;char*b;if(!f)return NULL;fseek(f,0,SEEK_END);n=ftell(f);rewind(f);b=(char*)malloc((size_t)n+1);if(!b){fclose(f);return NULL;}if(n>0&&fread(b,1,(size_t)n,f)!=(size_t)n){fclose(f);free(b);return NULL;}b[n]=0;fclose(f);return b;}

char *vs_chat(VSContext*c,const char*prompt){
    char b[4096];(void)c;chats++;
    if(chats==1){snprintf(b,sizeof(b),"[[VS_TOOL grep task_id=\"1\" path=\"%s\" pattern=\"needle\" case_insensitive=\"0\" context=\"0\" limit=\"20\"]]",root);return d(b);}
    if(chats==2){
        if(strstr(prompt,"needle is here")&&strstr(prompt,"[matches="))grep_seen=1;
        snprintf(b,sizeof(b),"[[VS_TOOL glob task_id=\"1\" path=\"%s\" pattern=\"*.c\"]]",root);return d(b);}
    if(chats==3){
        if(strstr(prompt,"c1.c"))glob_seen=1;
        snprintf(b,sizeof(b),"[[VS_TOOL edit task_id=\"1\" path=\"%s\" search=\"first line\" replace=\"edited first line\"]]",file1);return d(b);}
    if(chats==4){
        if(strstr(prompt,"edit")||strstr(prompt,"replacements")||strstr(prompt,"OK"))edit_seen=1;
        snprintf(b,sizeof(b),"[[VS_TOOL task_create task_id=\"1\" title=\"Extended dispatch task\" description=\"verify\" priority=\"high\"]]");return d(b);}
    if(chats==5){
        if(strstr(prompt,"OK: task")||strstr(prompt,"task 1 created"))task_seen=1;
        /* genuinely unknown operation: preflight gate -1, must recover not die */
        return d("[[VS_TOOL frobnicate task_id=\"1\" x=\"1\"]]");}
    if(chats==6){
        if(strstr(prompt,"ERROR:")&&strstr(prompt,"Supported operations")&&strstr(prompt,"unknown operation \"frobnicate\""))recovery_unknown_seen=1;
        /* truncated marker: preflight gate -8 recovery path */
        return d("I will read the file now.\n[[VS_TOOL read task_id=\"1\" path=\"x");}
    if(chats==7){
        if(strstr(prompt,"ERROR:")&&strstr(prompt,"truncated")&&strstr(prompt,"Details:"))recovery_trunc_seen=1;
        /* misspelled attribute: schema gate -1 with a precise diagnostic */
        return d("[[VS_TOOL read task_id=\"1\" pah=\"x\"]]");}
    if(chats==8){
        if(strstr(prompt,"ERROR:")&&strstr(prompt,"attribute \"pah\" is not allowed for read")&&strstr(prompt,"allowed:"))recovery_badattr_seen=1;
        /* host-interactive tool must return guidance, not kill the turn */
        return d("[[VS_TOOL ask_user_question task_id=\"1\" question=\"pick\" options=\"a|b\"]]");}
    if(chats==9){
        if(strstr(prompt,"VS_NEED_USER"))ask_seen=1;
        /* an illustrative directive mention in prose ("[[VS_TOOL read ...]]")
           must be skipped, not fail the batch; "..." inside a QUOTED value
           (cmd="echo ...") must stay a real directive; the real read must run */
        return d("Then I will emit [[VS_TOOL read ...]] like so, or run [[VS_TOOL run task_id=\"1\" cmd=\"echo ...\"]] instead.\n[[VS_TOOL read task_id=\"1\" path=\"definitely-missing-file.txt\"]]");}
    if(chats==10){
        if(strstr(prompt,"unable to read file")&&strstr(prompt,"directive 2")&&!strstr(prompt,"unknown operation"))placeholder_seen=1;
        return d("[[VS_FINAL]]extended tools ok");}
    return d("[[VS_FINAL]]unexpected");}

/* ---- stubs mirroring agent_native_tools_test.c ---- */
void vs_trace(VSContext*c,const char*k,const char*x){(void)c;(void)k;(void)x;}
void vs_trace_clear(VSContext*c){(void)c;}
int vs_mcp_refresh_all(VSContext*c,int f){(void)c;(void)f;return 0;}
char *vs_mcp_call(VSContext*c,const char*s,const char*t,const char*a){(void)c;(void)s;(void)t;(void)a;return d("mcp");}
char *vs_mcp_list_resources(VSContext *ctx,const char *s){(void)ctx;(void)s;return d("{\"resources\":[]}");}
char *vs_mcp_read_resource(VSContext *ctx,const char *s,const char *u){(void)ctx;(void)s;(void)u;return d("resource");}
char *vs_compact_text_limit(const char*s,size_t z,const char*r){size_t n;if(!s)return d("");(void)r;n=strlen(s);if(n<=z)return d(s);{char*p=(char*)malloc(z+1);memcpy(p,s,z);p[z]=0;return p;}}
void vs_history_add(VSContext*c,const char*r,const char*x){(void)c;(void)r;(void)x;}
char *vs_cached_read_file(VSContext*c,const char*p){(void)c;return read_all(p);}
int vs_attach(VSContext*c,const char*p){(void)c;(void)p;return 0;}
int vs_is_image_path(const char*p){(void)p;return 1;}
void vs_cache_invalidate(VSContext*c,const char*p){(void)c;(void)p;}
int vs_write_file(const char*p,const char*t){FILE*f=fopen(p,"wb");if(!f)return -1;fputs(t,f);fclose(f);return 0;}
const char *vs_command_shell_name(void){return "/bin/sh";}
char *vs_run_command_ctx(VSContext*c,const char*cmd,int*st){(void)c;(void)cmd;*st=0;return d("");}
char *vs_run_command(const char*cmd,int*st){return vs_run_command_ctx(NULL,cmd,st);}
int vs_cancel_requested(const VSContext*c){(void)c;return 0;}
static char *stub_text(const char *s){return d(s);}
char *vs_web_search(VSContext*c,const char*q,int n){(void)c;(void)q;(void)n;return stub_text("web");}
char *vs_web_fetch(VSContext*c,const char*u,size_t n){(void)c;(void)u;(void)n;return stub_text("page");}
int vs_graph_build(VSContext*c,const char*r,VSGraphMode m,VSGraph*g){(void)c;(void)r;if(g){memset(g,0,sizeof(*g));g->mode=m;}return 0;}
char *vs_graph_summary(const VSGraph*g,int n){(void)g;(void)n;return stub_text("graph");}
void vs_history_clear(VSContext *c){(void)c;}
void vs_cache_clear(VSContext *c){(void)c;}

int main(void){
    VSContext c;char*out,*body;FILE*f;
    setenv("VIBESOLARIS_AGENT_COMPACT_ROUNDS","0",1); /* keep the scripted chat sequence deterministic */
    snprintf(root,sizeof(root),"/tmp/vibesolaris-ext-%ld",(long)getpid());
    if(mkdir(root,0700)!=0)return 2;
    snprintf(file1,sizeof(file1),"%s/a.txt",root);
    snprintf(file2,sizeof(file2),"%s/b.txt",root);
    snprintf(filec,sizeof(filec),"%s/c1.c",root);
    f=fopen(file1,"w");fputs("first line\nneedle is here\nthird line\n",f);fclose(f);
    f=fopen(file2,"w");fputs("other needle\n",f);fclose(f);
    f=fopen(filec,"w");fputs("int main(void){return 0;}\n",f);fclose(f);

    memset(&c,0,sizeof(c));
    vs_workflow_init(&c.workflow);
    c.workflow.mode=VS_MODE_AUTO;
    {VSTaskRecord task;memset(&task,0,sizeof(task));task.id=1;strcpy(task.title,"Extended dispatch test");
     if(vs_workflow_set_plan(&c.workflow,"Extended tools",&task,1)!=0||vs_workflow_accept(&c.workflow)!=0)return 2;}

    out=vs_agent_turn(&c,"exercise the extended tool set");
    printf("out=%s chats=%d\n",out?out:"(null)",chats);

    CHECK(out&&!strcmp(out,"extended tools ok"),"agent turn completes with the extended tool set");
    CHECK(grep_seen,"grep directive dispatched and results fed back to the model");
    CHECK(glob_seen,"glob directive dispatched and results fed back to the model");
    CHECK(edit_seen,"edit directive dispatched and result fed back to the model");
    body=read_all(file1);
    CHECK(body&&strstr(body,"edited first line"),"edit directive actually modified the file");
    free(body);
    CHECK(task_seen,"task_create dispatched and the created task id was reported");
    CHECK(c.tasks.task_count==1,"task store received the created task");
    CHECK(recovery_unknown_seen,"unknown operation produced a recovery ERROR instead of ending the turn");
    CHECK(recovery_trunc_seen,"truncated marker produced the truncation recovery instead of ending the turn");
    CHECK(recovery_badattr_seen,"misspelled attribute produced a precise schema diagnostic instead of ending the turn");
    CHECK(ask_seen,"ask_user_question returned host guidance instead of killing the turn");
    CHECK(placeholder_seen,"illustrative [[VS_TOOL ...]] mention was skipped while the real directive still ran");

    unlink(file1);unlink(file2);unlink(filec);rmdir(root);
    free(out);
    if(fails){fprintf(stderr,"%d failure(s)\n",fails);return 1;}
    printf("all extended tool dispatch tests passed\n");return 0;
}

/* Stub for util.c (agent tests do not link it). */
char *vs_shell_quote(const char *s){const char *p;size_t n=2;char *o,*w;if(!s)s="";for(p=s;*p;p++)n+=(*p=='\'')?4:1;o=(char*)malloc(n+1);if(!o)return NULL;w=o;*w++='\'';for(p=s;*p;p++){if(*p=='\''){memcpy(w,"'\\''",4);w+=4;}else *w++=*p;}*w++='\'';*w=0;return o;}
