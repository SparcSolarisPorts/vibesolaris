/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int normal_chats=0,compact_chats=0,runs=0,persisted=0,saw_checkpoint=0;
static char *d(const char *s){size_t n=strlen(s);char *p=(char*)malloc(n+1);if(p)memcpy(p,s,n+1);return p;}

char *vs_chat(VSContext *c,const char *p){char b[128];(void)c;
    if(strstr(p,"INTERNAL_CONTEXT_COMPACTION")){compact_chats++;return d("CHECKPOINT_OK: inspected four steps; continue with remaining work.");}
    normal_chats++;if(strstr(p,"CHECKPOINT_OK"))saw_checkpoint=1;
    if(normal_chats<=5){snprintf(b,sizeof(b),"[[VS_TOOL run cmd=\"printf step%d\"]]",normal_chats);return d(b);}
    return d("[[VS_FINAL]]complete");
}
void vs_trace(VSContext*c,const char*k,const char*x){(void)c;(void)k;(void)x;}void vs_trace_clear(VSContext*c){(void)c;}
int vs_mcp_refresh_all(VSContext*c,int f){(void)c;(void)f;return 0;}char*vs_mcp_call(VSContext*c,const char*s,const char*t,const char*a){(void)c;(void)s;(void)t;(void)a;return d("mcp");}
char*vs_compact_text_limit(const char*s,size_t z,const char*r){size_t n;if(!s)return d("");n=strlen(s);(void)r;if(n<=z)return d(s);{char *p=(char*)malloc(z+1);memcpy(p,s,z);p[z]=0;return p;}}
void vs_history_add(VSContext*c,const char*r,const char*x){(void)c;(void)r;(void)x;persisted++;}
char*vs_cached_read_file(VSContext*c,const char*p){(void)c;(void)p;return d("file");}int vs_attach(VSContext*c,const char*p){(void)c;(void)p;return 0;}int vs_is_image_path(const char*p){(void)p;return 1;}void vs_cache_invalidate(VSContext*c,const char*p){(void)c;(void)p;}int vs_write_file(const char*p,const char*t){(void)p;(void)t;return 0;}
const char*vs_command_shell_name(void){return "/usr/xpg4/bin/sh";}char*vs_run_command_ctx(VSContext*c,const char*cmd,int*st){(void)c;runs++;*st=0;return d(cmd);}char*vs_run_command(const char*cmd,int*st){return vs_run_command_ctx(NULL,cmd,st);}int vs_cancel_requested(const VSContext*c){(void)c;return 0;}


/* Stubs for optional agent-integrated project/web tools; these lifecycle tests
   exercise the agent state machine, not the graph or network implementations. */
static char *optional_tool_stub_text(const char *s){size_t n=strlen(s);char *p=(char*)malloc(n+1);if(p)memcpy(p,s,n+1);return p;}
char *vs_web_search(VSContext*c,const char*q,int n){(void)c;(void)q;(void)n;return optional_tool_stub_text("web");}
char *vs_web_fetch(VSContext*c,const char*u,size_t n){(void)c;(void)u;(void)n;return optional_tool_stub_text("page");}
int vs_graph_build(VSContext*c,const char*r,VSGraphMode m,VSGraph*g){(void)c;(void)r;if(g){memset(g,0,sizeof(*g));g->mode=m;}return 0;}
char *vs_graph_summary(const VSGraph*g,int n){(void)g;(void)n;return optional_tool_stub_text("graph");}

int main(void){VSContext c;char *out;memset(&c,0,sizeof(c));putenv("VIBESOLARIS_AGENT_COMPACT_ROUNDS=4");out=vs_agent_turn(&c,"please implement and test all of it");printf("out=%s normal=%d compact=%d runs=%d persisted=%d checkpoint=%d\n",out?out:"(null)",normal_chats,compact_chats,runs,persisted,saw_checkpoint);if(!out||strcmp(out,"complete")||normal_chats!=6||compact_chats<1||runs!=5||persisted!=2||!saw_checkpoint){free(out);return 1;}free(out);return 0;}
