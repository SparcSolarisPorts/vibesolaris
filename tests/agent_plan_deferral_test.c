/* SPDX-License-Identifier: Unlicense */
/* Auto-mode planning discipline: a [[VS_PLAN]] proposed with too little
   read-only exploration must be deferred for verification (Claude-Code-style
   planning phase) instead of being adopted immediately.  After enough
   read-only rounds the refined plan is adopted and the do phase runs. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int chats=0,fails=0;
static int defer1_seen=0,defer2_seen=0,adopted_seen=0,runs=0;
static char root[512];

#define CHECK(c,m) do{ if(!(c)){fprintf(stderr,"FAIL: %s\n",m);fails++;} else printf("ok: %s\n",m);}while(0)

static char *d(const char*s){size_t n=strlen(s);char*p=(char*)malloc(n+1);if(p)memcpy(p,s,n+1);return p;}
static char *read_all(const char *p){FILE*f=fopen(p,"rb");long n;char*b;if(!f)return NULL;fseek(f,0,SEEK_END);n=ftell(f);rewind(f);b=(char*)malloc((size_t)n+1);if(!b){fclose(f);return NULL;}if(n>0&&fread(b,1,(size_t)n,f)!=(size_t)n){fclose(f);free(b);return NULL;}b[n]=0;fclose(f);return b;}

char *vs_chat(VSContext*c,const char*prompt){
    char b[1024];(void)c;chats++;
    if(chats==1)return d("[[VS_PLAN title=\"Grounded change\" tasks=\"1|0|-|Make the change\"]]");
    if(chats==2){
        if(strstr(prompt,"has NOT been adopted")&&strstr(prompt,"only 0 read-only")&&strstr(prompt,"DRAFT PLAN (unverified)"))defer1_seen=1;
        snprintf(b,sizeof(b),"[[VS_TOOL list path=\"%s\" depth=\"1\" limit=\"5\"]]",root);return d(b);}
    if(chats==3)return d("[[VS_PLAN title=\"Grounded change\" tasks=\"1|0|-|Make the change\"]]");
    if(chats==4){
        if(strstr(prompt,"has NOT been adopted")&&strstr(prompt,"only 1 read-only")&&strstr(prompt,"deferral 2 of 3"))defer2_seen=1;
        snprintf(b,sizeof(b),"[[VS_TOOL list path=\"%s\" depth=\"1\" limit=\"5\"]]",root);return d(b);}
    if(chats==5)return d("[[VS_PLAN title=\"Grounded change\" tasks=\"1|0|-|Make the change\"]]");
    if(chats==6){
        return d("[[VS_TOOL run task_id=\"1\" cmd=\"printf ok\"]]");}
    if(chats==7)return d("[[VS_TOOL plan_task_complete task_id=\"1\" summary=\"Done\"]]");
    if(chats==8)return d("[[VS_FINAL]]planned and done");
    return d("[[VS_FINAL]]unexpected");}

/* ---- stubs (same set as agent_extended_tools_test.c) ---- */
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
char *vs_run_command_ctx(VSContext*c,const char*cmd,int*st){(void)c;(void)cmd;runs++;*st=0;return d("ok");}
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
    VSContext c;char*out;
    setenv("VIBESOLARIS_AGENT_COMPACT_ROUNDS","0",1); /* deterministic scripted sequence */
    snprintf(root,sizeof(root),"/tmp/vibesolaris-defer-%ld",(long)getpid());
    if(mkdir(root,0700)!=0)return 2;

    memset(&c,0,sizeof(c));
    vs_workflow_init(&c.workflow);
    c.workflow.mode=VS_MODE_AUTO;

    out=vs_agent_turn(&c,"make the change");
    printf("out=%s chats=%d\n",out?out:"(null)",chats);

    CHECK(out&&!strcmp(out,"planned and done"),"turn completes after plan verification and do phase");
    CHECK(defer1_seen,"under-researched plan was deferred with a verification prompt");
    CHECK(defer2_seen,"second proposal after one exploration round was still deferred");
    adopted_seen=runs==1;
    CHECK(adopted_seen,"do phase executed after the verified plan was adopted (task-gated directive passed preflight)");

    rmdir(root);
    free(out);
    if(fails){fprintf(stderr,"%d failure(s)\n",fails);return 1;}
    printf("all plan-deferral tests passed\n");return 0;
}

/* Stub for util.c (agent tests do not link it). */
char *vs_shell_quote(const char *s){const char *p;size_t n=2;char *o,*w;if(!s)s="";for(p=s;*p;p++)n+=(*p=='\'')?4:1;o=(char*)malloc(n+1);if(!o)return NULL;w=o;*w++='\'';for(p=s;*p;p++){if(*p=='\''){memcpy(w,"'\\''",4);w+=4;}else *w++=*p;}*w++='\'';*w=0;return o;}
