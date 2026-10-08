/* SPDX-License-Identifier: Unlicense */
/* Runs the real agent.c + subagent.c loop with a scripted provider.  Tools and
   persistence are stubbed; the delegation protocol, isolation limits, trace
   forwarding and indicator labels are the real code. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails=0;
#define CHECK(c,m) do{ if(!(c)){fprintf(stderr,"FAIL: %s\n",m);fails++;} else printf("ok: %s\n",m);}while(0)

/* ---- scripted provider: behaviour depends on subagent depth ---- */
static int calls_d0=0,calls_d1=0,calls_d2=0;
static int d1_delegates_again=0;
char *vs_chat(VSContext *c,const char *prompt){
    (void)prompt;
    if(c->subagent_depth==0){
        calls_d0++;
        if(calls_d0==1)return strdup("I will look into it.\n[[VS_TOOL subagent task=\"Summarise README\"]]");
        return strdup("Subagent reported back. [[VS_FINAL]]");
    }
    if(c->subagent_depth==1){
        calls_d1++;
        if(d1_delegates_again&&calls_d1==1)return strdup("[[VS_TOOL subagent task=\"nested work\"]]");
        return strdup("README summary: project builds. [[VS_FINAL]]");
    }
    calls_d2++;
    if(calls_d2==1)return strdup("[[VS_TOOL subagent task=\"too deep\"]]");
    return strdup("deep report [[VS_FINAL]]");
}

/* ---- trace capture ---- */
#define MAXT 128
static char tk[MAXT][32];static char td[MAXT][512];static int tn=0;
/* Mirrors the real vs_trace(): every context notifies its live callback; the
   retained record is kept per top-level context (children forward upward). */
void vs_trace(VSContext *c,const char *k,const char *d){
    if(!c)return;
    if(c->trace_callback)c->trace_callback(c->trace_callback_data,tn+1,k,d);
    if(!c->subagent_parent&&tn<MAXT){snprintf(tk[tn],sizeof(tk[tn]),"%s",k?k:"");snprintf(td[tn],sizeof(td[tn]),"%s",d?d:"");tn++;}
}
/* Real vs_trace_clear is per-context; the capture buffer is reset explicitly by the test. */
void vs_trace_clear(VSContext *c){(void)c;}
int vs_cancel_requested(const VSContext *c){
    while(c){ if(c->cancel_requested)return 1; c=(const VSContext*)c->subagent_parent; }
    return 0;
}

/* ---- collaborator stubs ---- */
static char *xdup(const char *s){size_t n=strlen(s?s:"");char *p=malloc(n+1);memcpy(p,s?s:"",n+1);return p;}
char *vs_compact_text_limit(const char *s,size_t l,const char *r){(void)r;if(!s)return xdup("");if(strlen(s)<=l)return xdup(s);{char *p=malloc(l+1);memcpy(p,s,l);p[l]=0;return p;}}
void vs_history_add(VSContext *c,const char *r,const char *x){(void)c;(void)r;(void)x;}
void vs_history_clear(VSContext *c){(void)c;}
void vs_cache_clear(VSContext *c){(void)c;}
void vs_cache_invalidate(VSContext *c,const char *p){(void)c;(void)p;}
char *vs_cached_read_file(VSContext *c,const char *p){(void)c;(void)p;return xdup("");}
int vs_attach(VSContext *c,const char *p){(void)c;(void)p;return 0;}
int vs_is_image_path(const char *p){(void)p;return 0;}
int vs_write_file(const char *p,const char *t){(void)p;(void)t;return 0;}
const char *vs_command_shell_name(void){return "sh";}
char *vs_run_command(const char *c,int *s){(void)c;*s=0;return xdup("");}
char *vs_run_command_ctx(VSContext *c,const char *cmd,int *s){(void)c;return vs_run_command(cmd,s);}
int vs_mcp_refresh_all(VSContext *c,int f){(void)c;(void)f;return 0;}
char *vs_mcp_call(VSContext *c,const char *s,const char *t,const char *a){(void)c;(void)s;(void)t;(void)a;return xdup("");}
char *vs_web_search(VSContext *c,const char *q,int n){(void)c;(void)q;(void)n;return xdup("");}
char *vs_web_fetch(VSContext *c,const char *u,size_t n){(void)c;(void)u;(void)n;return xdup("");}
int vs_graph_build(VSContext *c,const char *r,VSGraphMode m,VSGraph *g){(void)c;(void)r;(void)m;(void)g;return -1;}
char *vs_graph_summary(const VSGraph *g,int n){(void)g;(void)n;return xdup("");}
int vs_write_file_dummy(void){return 0;}

static int has_trace(const char *kind){int i;for(i=0;i<tn;i++)if(!strcmp(tk[i],kind))return 1;return 0;}
static int has_detail_prefix(const char *p){int i;for(i=0;i<tn;i++)if(!strncmp(td[i],p,strlen(p)))return 1;return 0;}
static int has_detail_text(const char *x){int i;for(i=0;i<tn;i++)if(strstr(td[i],x))return 1;return 0;}

int main(void){
    static VSContext parent;
    char *out;char label[96];
    memset(&parent,0,sizeof(parent));

    /* 1. delegation round trip */
    out=vs_agent_turn(&parent,"review the project");
    CHECK(out&&strstr(out,"Subagent reported back"),"parent finishes after receiving the subagent result");
    CHECK(calls_d1>=1,"subagent actually called the provider at depth 1");
    CHECK(has_trace("subagent-start"),"trace records subagent-start");
    CHECK(has_trace("subagent-done"),"trace records subagent-done");
    CHECK(has_detail_prefix("[sub#1 d1]"),"child trace events are forwarded with [sub#1 d1] prefix");
    CHECK(parent.subagent_depth==0,"parent depth unchanged after subagent run");
    free(out);

    /* 2. nested delegation stops at the depth limit */
    memset(&parent,0,sizeof(parent));calls_d0=calls_d1=calls_d2=0;tn=0;d1_delegates_again=1;
    out=vs_agent_turn(&parent,"review the project");
    CHECK(calls_d2>=1,"grandchild (depth 2) runs its own loop");
    CHECK(has_detail_text("[sub#1 d1] [sub#1 d2]"),"depth-2 events nest under the depth-1 prefix when forwarded");
    CHECK(has_detail_text("nesting limit"),"depth-2 agent's delegation is refused at the limit");
    CHECK(!has_detail_text("d3]"),"no depth-3 agent was ever started");
    d1_delegates_again=0;free(out);

    /* 3. per-turn subagent cap */
    memset(&parent,0,sizeof(parent));
    parent.subagent_serial=VS_MAX_SUBAGENTS_PER_TURN;
    out=vs_subagent_run(&parent,"anything");
    CHECK(out&&strstr(out,"limit for this turn"),"subagent count cap is enforced");
    free(out);

    /* 4. cancellation is inherited */
    memset(&parent,0,sizeof(parent));
    parent.cancel_requested=1;
    out=vs_subagent_run(&parent,"anything");
    CHECK(out&&strstr(out,"Stopped by user"),"cancelled parent stops subagent before any work");
    free(out);
    {
        VSContext child;memset(&child,0,sizeof(child));child.subagent_parent=&parent;
        CHECK(vs_cancel_requested(&child),"child reports cancelled when its parent is cancelled");
    }

    /* 5. empty task is rejected */
    memset(&parent,0,sizeof(parent));
    out=vs_subagent_run(&parent,"");
    CHECK(out&&strstr(out,"requires task"),"empty subagent task is rejected");
    free(out);

    /* 6. indicator labels */
    CHECK(vs_trace_phase("model-wait","requesting",label,sizeof(label))&&!strcmp(label,"Thinking"),"model wait shows Thinking");
    CHECK(vs_trace_phase("tool-batch","executing",label,sizeof(label))&&!strcmp(label,"Running tools"),"tool batch shows Running tools");
    CHECK(vs_trace_phase("model-wait","[sub#2 d1] requesting",label,sizeof(label))&&!strcmp(label,"Subagent: Thinking"),"subagent model wait is labelled as subagent");
    CHECK(vs_trace_phase("subagent-start","started",label,sizeof(label))&&!strcmp(label,"Subagent starting"),"subagent start is labelled");
    CHECK(!vs_trace_phase("model-output","answer",label,sizeof(label)),"unrelated events do not change the label");

    /* 7. request prompt used by /subagent */
    {char *p=vs_subagent_request_prompt("count files");CHECK(p&&strstr(p,"subagent task=")&&strstr(p,"count files"),"/subagent request prompt names the task");free(p);}

    if(fails){fprintf(stderr,"%d failure(s)\n",fails);return 1;}
    printf("all subagent tests passed\n");return 0;
}
