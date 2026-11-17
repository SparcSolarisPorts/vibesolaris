/* SPDX-License-Identifier: Unlicense */
/* Extended agent robustness scenarios: dependency gating, plan-task
   completion edge cases, todo validation, parallel read-only and mixed
   subagent batches, forced compaction, and cross-turn working-state
   retention.  Tools and persistence are stubbed exactly like the other
   agent lifecycle tests; the state machine under test is the real one. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int scenario=0,chats=0,runs=0,compact_chats=0;
static int parallel_seen=0,mixed_seen=0,prior_state_seen=0;
static char root[512],file1[640];
static char *d(const char*s){size_t n=strlen(s);char*p=(char*)malloc(n+1);memcpy(p,s,n+1);return p;}
static char *read_all(const char *p){FILE*f=fopen(p,"rb");long n;char*b;if(!f)return NULL;fseek(f,0,SEEK_END);n=ftell(f);rewind(f);b=(char*)malloc((size_t)n+1);if(!b){fclose(f);return NULL;}if(n)fread(b,1,(size_t)n,f);b[n]=0;fclose(f);return b;}

char *vs_chat(VSContext *c,const char *p){
    if(c->subagent_depth==1)return d("[[VS_FINAL]]research complete");
    if(strstr(p,"INTERNAL_CONTEXT_COMPACTION")){compact_chats++;return d("FORCED_STATE: investigated the failure thoroughly");}
    chats++;
    if(scenario==1)return d("[[VS_TOOL run task_id=\"2\" cmd=\"printf early\"]]");
    if(scenario==2){
        if(chats==1)return d("[[VS_TOOL plan_task_complete task_id=\"99\" summary=\"ghost\"]]");
        if(chats==2){if(!strstr(p,"not part of the accepted plan"))return d("[[VS_FINAL]]missing unknown-task feedback");return d("[[VS_TOOL plan_task_complete task_id=\"1\" summary=\"done\"]]");}
        return d("[[VS_FINAL]]ok");}
    if(scenario==3){
        if(chats==1)return d("[[VS_TOOL todo task_id=\"1\" items=\"alpha|beta\"]]");
        if(chats==2)return d("[[VS_TOOL todo task_id=\"1\" index=\"9\" status=\"completed\"]]");
        if(chats==3){if(!strstr(p,"out of range"))return d("[[VS_FINAL]]missing range error");return d("[[VS_TOOL todo task_id=\"1\" index=\"1\" status=\"bogus\"]]");}
        if(chats==4){if(!strstr(p,"must be pending"))return d("[[VS_FINAL]]missing status error");return d("[[VS_TOOL todo task_id=\"1\" index=\"2\" status=\"completed\"]]");}
        return d("[[VS_FINAL]]ok");}
    if(scenario==4){char b[1024];
        if(chats==1){snprintf(b,sizeof(b),"[[VS_TOOL list task_id=\"1\" path=\"%s\" depth=\"1\" limit=\"20\"]]\n[[VS_TOOL search task_id=\"1\" path=\"%s\" query=\"needle\" limit=\"20\"]]\n[[VS_TOOL read task_id=\"1\" path=\"%s\"]]",root,root,file1);return d(b);}
        if(strstr(p,"executed in parallel")&&strstr(p,"alpha needle")&&strstr(p,"beta needle"))parallel_seen=1;
        return d("[[VS_FINAL]]ok");}
    if(scenario==5){char b[640];
        if(chats==1){snprintf(b,sizeof(b),"[[VS_TOOL subagent task_id=\"1\" task=\"research topic\"]]\n[[VS_TOOL list task_id=\"1\" path=\"%s\" depth=\"1\" limit=\"20\"]]",root);return d(b);}
        if(strstr(p,"research complete")&&strstr(p,"executed in parallel"))mixed_seen=1;
        return d("[[VS_FINAL]]ok");}
    if(scenario==6){
        if(chats==1)return d("[[VS_TOOL run task_id=\"1\" cmd=\"printf step\"]]");
        return d("[[VS_FINAL]]ok");}
    if(scenario==7){
        if(strstr(p,"WORKING STATE FROM EARLIER TURN")&&strstr(p,"FORCED_STATE"))prior_state_seen=1;
        return d("[[VS_FINAL]]noted");}
    if(scenario==8)return d("[[VS_TOOL run task_id=\"1\" cmd=\"printf a\"]]\n[[VS_TOOL run task_id=\"1\" cmd=\"printf b\"]]");
    return d("[[VS_FINAL]]unexpected");
}
void vs_trace(VSContext*c,const char*k,const char*x){(void)c;(void)k;(void)x;}
void vs_trace_clear(VSContext*c){(void)c;}
int vs_mcp_refresh_all(VSContext*c,int f){(void)c;(void)f;return 0;}
char *vs_mcp_call(VSContext*c,const char*s,const char*t,const char*a){(void)c;(void)s;(void)t;(void)a;return d("mcp");}
char *vs_compact_text_limit(const char*s,size_t z,const char*r){size_t n;if(!s)return d("");(void)r;n=strlen(s);if(n<=z)return d(s);{char*p=(char*)malloc(z+1);memcpy(p,s,z);p[z]=0;return p;}}
void vs_history_add(VSContext*c,const char*r,const char*x){(void)c;(void)r;(void)x;}
char *vs_cached_read_file(VSContext*c,const char*p){(void)c;return read_all(p);}
int vs_attach(VSContext*c,const char*p){(void)c;(void)p;return 0;}
int vs_is_image_path(const char*p){(void)p;return 1;}
void vs_cache_invalidate(VSContext*c,const char*p){(void)c;(void)p;}
int vs_write_file(const char*p,const char*t){(void)p;(void)t;return 0;}
const char *vs_command_shell_name(void){return "/bin/sh";}
char *vs_run_command_ctx(VSContext*c,const char*cmd,int*st){(void)c;(void)cmd;runs++;*st=0;return d("out");}
char *vs_run_command(const char*cmd,int*st){return vs_run_command_ctx(NULL,cmd,st);}
int vs_cancel_requested(const VSContext*c){(void)c;return 0;}
char *vs_web_search(VSContext*c,const char*q,int n){(void)c;(void)q;(void)n;return d("web");}
char *vs_web_fetch(VSContext*c,const char*u,size_t n){(void)c;(void)u;(void)n;return d("page");}
int vs_graph_build(VSContext*c,const char*r,VSGraphMode m,VSGraph*g){(void)c;(void)r;if(g){memset(g,0,sizeof(*g));g->mode=m;}return 0;}
char *vs_graph_summary(const VSGraph*g,int n){(void)g;(void)n;return d("graph");}
void vs_history_clear(VSContext*c){(void)c;}
void vs_cache_clear(VSContext*c){(void)c;}

static int setup_plan(VSContext *c,VSTaskRecord *tasks,int count,const char *title)
{
    memset(c,0,sizeof(*c));
    vs_workflow_init(&c->workflow);
    if(vs_workflow_set_plan(&c->workflow,title,tasks,count)!=0)return -1;
    return vs_workflow_accept(&c->workflow);
}

int main(void)
{
    VSContext c;char *out;VSTaskRecord tasks[2];

    /* 1: a task whose dependency is incomplete must be gated, not executed. */
    memset(tasks,0,sizeof(tasks));
    tasks[0].id=1;strcpy(tasks[0].title,"First");
    tasks[1].id=2;strcpy(tasks[1].title,"Second");tasks[1].dependency_count=1;tasks[1].dependencies[0]=1;
    if(setup_plan(&c,tasks,2,"Dependency gate")!=0)return 1;
    scenario=1;chats=runs=0;
    out=vs_agent_turn(&c,"do all the tasks");
    if(!out||!strstr(out,"blocked")||chats!=1||runs!=0){fprintf(stderr,"scenario 1 (dependency gate) failed: %s chats=%d runs=%d\n",out?out:"(null)",chats,runs);free(out);return 1;}
    free(out);puts("ok: blocked dependency is gated, not executed");

    /* 2: completing an unknown plan task reports an error and the turn recovers. */
    memset(tasks,0,sizeof(tasks));tasks[0].id=1;strcpy(tasks[0].title,"Only task");
    if(setup_plan(&c,tasks,1,"Unknown completion")!=0)return 2;
    scenario=2;chats=0;
    out=vs_agent_turn(&c,"finish the task");
    if(!out||strcmp(out,"ok")||chats!=3||c.workflow.tasks[0].status!=VS_TASK_COMPLETED){fprintf(stderr,"scenario 2 (unknown completion) failed: %s chats=%d\n",out?out:"(null)",chats);free(out);return 2;}
    free(out);puts("ok: unknown plan_task_complete reports an error and recovers");

    /* 3: todo index/status validation errors are fed back without crashing. */
    if(setup_plan(&c,tasks,1,"Todo validation")!=0)return 3;
    scenario=3;chats=0;
    out=vs_agent_turn(&c,"track and finish");
    if(!out||strcmp(out,"ok")||chats!=5||c.todo.item_count!=2||c.todo.items[1].status!=VS_TODO_STATUS_COMPLETED){fprintf(stderr,"scenario 3 (todo validation) failed: %s chats=%d\n",out?out:"(null)",chats);free(out);return 3;}
    free(out);puts("ok: todo index and status are validated with feedback");

    /* 4: a batch of independent read-only directives runs in parallel. */
    snprintf(root,sizeof(root),"/tmp/vibesolaris-ext-%ld",(long)getpid());
    mkdir(root,0700);
    snprintf(file1,sizeof(file1),"%s/a.txt",root);
    {FILE*f=fopen(file1,"w");fputs("alpha needle here\n",f);fclose(f);}
    {char f2[640];FILE*f;snprintf(f2,sizeof(f2),"%s/b.txt",root);f=fopen(f2,"w");fputs("beta needle\n",f);fclose(f);}
    if(setup_plan(&c,tasks,1,"Parallel batch")!=0)return 4;
    scenario=4;chats=0;
    out=vs_agent_turn(&c,"inspect the files");
    if(!out||strcmp(out,"ok")||chats!=2||!parallel_seen){fprintf(stderr,"scenario 4 (parallel read-only batch) failed: %s chats=%d parallel=%d\n",out?out:"(null)",chats,parallel_seen);free(out);return 4;}
    free(out);puts("ok: independent read-only directives execute in parallel");

    /* 5: subagent tasks and read-only tools share the parallel path. */
    if(setup_plan(&c,tasks,1,"Mixed parallel")!=0)return 5;
    scenario=5;chats=0;
    out=vs_agent_turn(&c,"delegate and inspect");
    if(!out||strcmp(out,"ok")||chats!=2||!mixed_seen){fprintf(stderr,"scenario 5 (mixed parallel) failed: %s chats=%d mixed=%d\n",out?out:"(null)",chats,mixed_seen);free(out);return 5;}
    free(out);puts("ok: subagent and read-only directives run concurrently");

    /* 6: /compact-style forced compaction replaces working state on demand. */
    if(setup_plan(&c,tasks,1,"Forced compaction")!=0)return 6;
    scenario=6;chats=runs=0;
    out=vs_agent_turn(&c,"do one step");
    if(!out||strcmp(out,"ok")){fprintf(stderr,"scenario 6 setup failed: %s\n",out?out:"(null)");free(out);return 6;}
    free(out);
    if(c.agent_history_count<=0){fprintf(stderr,"scenario 6: no working history to compact\n");return 6;}
    agent_compact_force(&c,"manual compaction");
    if(compact_chats!=1||!c.agent_checkpoint||!strstr(c.agent_checkpoint,"FORCED_STATE")||c.agent_history_count!=0){fprintf(stderr,"scenario 6 (forced compaction) failed: compact_chats=%d checkpoint=%s history=%d\n",compact_chats,c.agent_checkpoint?c.agent_checkpoint:"(null)",c.agent_history_count);return 6;}
    puts("ok: forced compaction replaces working state on demand");

    /* 7: the compacted working state survives into the next turn's prompt. */
    scenario=7;chats=0;
    out=vs_agent_turn(&c,"continue the earlier work");
    if(!out||strcmp(out,"noted")||!prior_state_seen){fprintf(stderr,"scenario 7 (cross-turn retention) failed: %s prior=%d\n",out?out:"(null)",prior_state_seen);free(out);return 7;}
    free(out);puts("ok: compacted working state is carried into the next turn");

    /* 8: manual mode rejects multi-directive batches before any action runs. */
    if(setup_plan(&c,tasks,1,"Manual gate")!=0)return 8;
    c.workflow.mode=VS_MODE_MANUAL;
    scenario=8;chats=runs=0;
    out=vs_agent_turn(&c,"do both steps");
    if(!out||!strstr(out,"one action at a time")||runs!=0){fprintf(stderr,"scenario 8 (manual gate) failed: %s runs=%d\n",out?out:"(null)",runs);free(out);return 8;}
    free(out);puts("ok: manual mode rejects multi-directive batches atomically");

    puts("extended agent scenarios passed");
    return 0;
}

/* Stub for util.c (agent tests do not link it). */
char *vs_shell_quote(const char *s){const char *p;size_t n=2;char *o,*w;if(!s)s="";for(p=s;*p;p++)n+=(*p=='\'')?4:1;o=(char*)malloc(n+1);if(!o)return NULL;w=o;*w++='\'';for(p=s;*p;p++){if(*p=='\''){memcpy(w,"'\\''",4);w+=4;}else *w++=*p;}*w++='\'';*w=0;return o;}
