/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int chats=0,batch_seen=0,range_seen=0,persisted=0;
static char root[512],file1[640],file2[640];
static char *d(const char*s){size_t n=strlen(s);char*p=(char*)malloc(n+1);if(p)memcpy(p,s,n+1);return p;}
static char *read_all(const char *p){FILE*f=fopen(p,"rb");long n;char*b;if(!f)return NULL;fseek(f,0,SEEK_END);n=ftell(f);rewind(f);b=(char*)malloc((size_t)n+1);if(!b){fclose(f);return NULL;}if(n)fread(b,1,(size_t)n,f);b[n]=0;fclose(f);return b;}
char *vs_chat(VSContext*c,const char*p){char b[2048];(void)c;chats++;
    if(chats==1){snprintf(b,sizeof(b),"[[VS_TOOL list path=\"%s\" depth=\"1\" limit=\"20\"]]\n[[VS_TOOL search path=\"%s\" query=\"needle\" limit=\"20\"]]",root,root);return d(b);}
    if(chats==2){if(strstr(p,"BATCH_TOOL_RESULT")&&strstr(p,"needle")&&strstr(p,"a.txt")&&strstr(p,"b.txt"))batch_seen=1;snprintf(b,sizeof(b),"[[VS_TOOL read path=\"%s\" start_line=\"2\" max_lines=\"1\"]]",file1);return d(b);}
    if(chats==3){if(strstr(p,"lines 2-2")&&strstr(p,"needle is here")&&!strstr(p,"first line\nthird line"))range_seen=1;return d("[[VS_FINAL]]native tools ok");}
    return d("[[VS_FINAL]]unexpected");}
void vs_trace(VSContext*c,const char*k,const char*x){(void)c;(void)k;(void)x;}void vs_trace_clear(VSContext*c){(void)c;}int vs_mcp_refresh_all(VSContext*c,int f){(void)c;(void)f;return 0;}char*vs_mcp_call(VSContext*c,const char*s,const char*t,const char*a){(void)c;(void)s;(void)t;(void)a;return d("mcp");}
char*vs_compact_text_limit(const char*s,size_t z,const char*r){size_t n;if(!s)return d("");(void)r;n=strlen(s);if(n<=z)return d(s);{char*p=(char*)malloc(z+1);memcpy(p,s,z);p[z]=0;return p;}}
void vs_history_add(VSContext*c,const char*r,const char*x){(void)c;(void)r;(void)x;persisted++;}char*vs_cached_read_file(VSContext*c,const char*p){(void)c;return read_all(p);}int vs_attach(VSContext*c,const char*p){(void)c;(void)p;return 0;}int vs_is_image_path(const char*p){(void)p;return 1;}void vs_cache_invalidate(VSContext*c,const char*p){(void)c;(void)p;}int vs_write_file(const char*p,const char*t){FILE*f=fopen(p,"wb");if(!f)return -1;fputs(t,f);fclose(f);return 0;}const char*vs_command_shell_name(void){return "/bin/sh";}char*vs_run_command_ctx(VSContext*c,const char*cmd,int*st){(void)c;(void)cmd;*st=0;return d("");}char*vs_run_command(const char*cmd,int*st){return vs_run_command_ctx(NULL,cmd,st);}int vs_cancel_requested(const VSContext*c){(void)c;return 0;}

/* Stubs for optional agent-integrated project/web tools; these lifecycle tests
   exercise the agent state machine, not the graph or network implementations. */
static char *optional_tool_stub_text(const char *s){size_t n=strlen(s);char *p=(char*)malloc(n+1);if(p)memcpy(p,s,n+1);return p;}
char *vs_web_search(VSContext*c,const char*q,int n){(void)c;(void)q;(void)n;return optional_tool_stub_text("web");}
char *vs_web_fetch(VSContext*c,const char*u,size_t n){(void)c;(void)u;(void)n;return optional_tool_stub_text("page");}
int vs_graph_build(VSContext*c,const char*r,VSGraphMode m,VSGraph*g){(void)c;(void)r;if(g){memset(g,0,sizeof(*g));g->mode=m;}return 0;}
char *vs_graph_summary(const VSGraph*g,int n){(void)g;(void)n;return optional_tool_stub_text("graph");}

int main(void){VSContext c;char*out;FILE*f;snprintf(root,sizeof(root),"/tmp/vibesolaris-native-%ld",(long)getpid());mkdir(root,0700);snprintf(file1,sizeof(file1),"%s/a.txt",root);snprintf(file2,sizeof(file2),"%s/b.txt",root);f=fopen(file1,"w");fputs("first line\nneedle is here\nthird line\n",f);fclose(f);f=fopen(file2,"w");fputs("other needle\n",f);fclose(f);memset(&c,0,sizeof(c));out=vs_agent_turn(&c,"inspect these files and finish the task");printf("out=%s chats=%d batch=%d range=%d persisted=%d\n",out?out:"(null)",chats,batch_seen,range_seen,persisted);unlink(file1);unlink(file2);rmdir(root);if(!out||strcmp(out,"native tools ok")||chats!=3||!batch_seen||!range_seen||persisted!=2){free(out);return 1;}free(out);return 0;}
