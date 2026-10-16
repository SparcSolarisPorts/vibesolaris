/* SPDX-License-Identifier: Unlicense */
/* End-to-end custom Anthropic API -> agent directive -> host command runner. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int http_calls, command_calls;
static char last_url[512], last_headers[2048], last_body[32768];
static char *copy(const char *s){size_t n=strlen(s);char *p=malloc(n+1);if(p)memcpy(p,s,n+1);return p;}
static char *json_escape(const char *s){size_t i,n=strlen(s);char *p=malloc(n*2+1),*w=p;if(!p)return NULL;for(i=0;i<n;i++){if(s[i]=='"'||s[i]=='\\')*w++='\\';if(s[i]=='\n'){*w++='\\';*w++='n';}else *w++=s[i];}*w=0;return p;}

char *vs_http_post_ctx(VSContext *c,const char *url,const char *headers[],int count,const char *body,long *status){
    static const char *plan="[[VS_PLAN title=\"Safe command\" tasks=\"1|0|-|Run requested command\"]]";
    static const char *run="[[VS_TOOL run task_id=\"1\" cmd=\"printf custom-anthropic-ok\"]]";
    static const char *done="[[VS_FINAL]]done";
    const char *text;char *escaped,*response;int i;(void)c;
    http_calls++;snprintf(last_url,sizeof(last_url),"%s",url);last_headers[0]=0;snprintf(last_body,sizeof(last_body),"%s",body?body:"");
    for(i=0;i<count;i++){strncat(last_headers,headers[i],sizeof(last_headers)-strlen(last_headers)-2);strncat(last_headers,"\n",sizeof(last_headers)-strlen(last_headers)-1);}
    text=http_calls==1?plan:(http_calls==2?run:done);escaped=json_escape(text);if(!escaped)return NULL;
    response=malloc(strlen(escaped)+96);if(response)sprintf(response,"{\"content\":[{\"type\":\"text\",\"text\":\"%s\"}]}",escaped);free(escaped);*status=200;return response;
}
const char *vs_command_shell_name(void){return "sh";}
char *vs_mcp_prompt_fragment(const VSContext *c){(void)c;return NULL;}
void vs_refresh_cache_key(VSContext *c){(void)c;}
void vs_trace(VSContext *c,const char *k,const char *d){(void)c;(void)k;(void)d;}
void vs_trace_clear(VSContext *c){(void)c;}
int vs_persist_settings(VSContext *c){(void)c;return 0;}
int vs_open_url(const char *u){(void)u;return 1;}
const char *vs_protocol_name(VSProtocolKind p){return p==VS_PROTOCOL_ANTHROPIC?"anthropic":"openai";}
char *vs_cached_read_file(VSContext *c,const char *p){(void)c;(void)p;return NULL;}
char *vs_cached_base64_file(VSContext *c,const char *p,size_t *n){(void)c;(void)p;(void)n;return NULL;}
char *vs_json_escape(const char *s){return json_escape(s);}
int vs_cancel_requested(const VSContext *c){(void)c;return 0;}
char *vs_compact_text_limit(const char *s,size_t n,const char *r){(void)n;(void)r;return copy(s?s:"");}
void vs_history_add(VSContext *c,const char *r,const char *x){(void)c;(void)r;(void)x;}
void vs_history_clear(VSContext *c){(void)c;}
void vs_cache_clear(VSContext *c){(void)c;}
void vs_cache_invalidate(VSContext *c,const char *p){(void)c;(void)p;}
int vs_mcp_refresh_all(VSContext *c,int f){(void)c;(void)f;return 0;}
char *vs_mcp_call(VSContext *c,const char *s,const char *t,const char *a){(void)c;(void)s;(void)t;(void)a;return copy("mcp");}
int vs_write_file(const char *p,const char *t){(void)p;(void)t;return 0;}
int vs_attach(VSContext *c,const char *p){(void)c;(void)p;return 0;}
int vs_is_image_path(const char *p){(void)p;return 0;}
char *vs_run_command(const char *cmd,int *status){(void)cmd;command_calls++;*status=0;return copy("custom-anthropic-ok");}
char *vs_run_command_ctx(VSContext *c,const char *cmd,int *status){(void)c;return vs_run_command(cmd,status);}
char *vs_web_search(VSContext *c,const char *q,int n){(void)c;(void)q;(void)n;return copy("");}
char *vs_web_fetch(VSContext *c,const char *u,size_t n){(void)c;(void)u;(void)n;return copy("");}
int vs_graph_build(VSContext *c,const char *r,VSGraphMode m,VSGraph *g){(void)c;(void)r;(void)m;(void)g;return -1;}
char *vs_graph_summary(const VSGraph *g,int n){(void)g;(void)n;return copy("");}

int main(void){
    VSContext c;char *out;memset(&c,0,sizeof(c));
    c.provider.kind=VS_PROVIDER_CUSTOM;c.provider.protocol=VS_PROTOCOL_ANTHROPIC;
    strcpy(c.provider.name,"custom");strcpy(c.provider.base_url,"https://gateway.example.test/v1/messages");
    strcpy(c.provider.model,"Claude-Compatible-Model");strcpy(c.provider.api_key,"custom-secret");
    c.provider_anthropic_auth[VS_PROVIDER_CUSTOM]=0;vs_workflow_init(&c.workflow);
    out=vs_agent_turn(&c,"run the requested command");
    if(!out||c.workflow.phase!=VS_WORKFLOW_AWAITING_ACCEPT||http_calls!=1||command_calls!=0){fprintf(stderr,"plan was not safely paused: out=%s phase=%d http=%d commands=%d\n",out?out:"(null)",c.workflow.phase,http_calls,command_calls);free(out);return 1;}free(out);
    if(strcmp(last_url,"https://gateway.example.test/v1/messages")||!strstr(last_headers,"Authorization: Bearer custom-secret")||!strstr(last_body,"\"model\":\"Claude-Compatible-Model\"")){fprintf(stderr,"custom Anthropic request was not formed correctly\nurl=%s\nheaders=%s\nbody=%s\n",last_url,last_headers,last_body);return 2;}
    if(vs_workflow_accept(&c.workflow)!=0)return 3;
    out=vs_agent_turn(&c,"continue accepted work");
    if(!out||strcmp(out,"done")||http_calls!=3||command_calls!=1){fprintf(stderr,"custom Anthropic command flow failed: out=%s http=%d commands=%d\n",out?out:"(null)",http_calls,command_calls);free(out);return 4;}
    free(out);puts("custom Anthropic agent command test passed");return 0;
}
