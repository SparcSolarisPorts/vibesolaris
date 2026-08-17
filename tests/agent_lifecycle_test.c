/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int chats=0,runs=0;
static char *xdup(const char *s){size_t n=strlen(s);char *p=(char*)malloc(n+1);memcpy(p,s,n+1);return p;}

char *vs_chat(VSContext *ctx,const char *p){(void)ctx;(void)p;chats++;
    if(chats==1)return xdup("I'll inspect it now. [[VS_TOOL run cmd=\"printf inventory\"]]");
    if(chats==2)return xdup("The command runner is operational; the previous command had a shell syntax error. I'll use simpler POSIX-compatible commands. I'm unable to continue because the tool bridge stopped returning results after the initial inventory command.");
    if(chats==3)return xdup("Continuing now. [[VS_TOOL run cmd=\"printf recovered\"]]");
    return xdup("[[VS_FINAL]]Done after recovery.");
}
void vs_trace(VSContext *c,const char *k,const char *d){(void)c;fprintf(stderr,"[%s] %s\n",k,d);}
void vs_trace_clear(VSContext *c){(void)c;}
int vs_mcp_refresh_all(VSContext *c,int f){(void)c;(void)f;return 0;}
char *vs_mcp_call(VSContext *c,const char *s,const char *t,const char *a){(void)c;(void)s;(void)t;(void)a;return xdup("mcp");}
char *vs_compact_text_limit(const char *s,size_t l,const char *r){(void)l;(void)r;return xdup(s?s:"");}
void vs_history_add(VSContext *c,const char *r,const char *x){(void)c;(void)r;(void)x;}
char *vs_cached_read_file(VSContext *c,const char *p){(void)c;(void)p;return xdup("file");}
int vs_attach(VSContext *c,const char *p){(void)c;(void)p;return 0;}
int vs_is_image_path(const char *p){(void)p;return 1;}
void vs_cache_invalidate(VSContext *c,const char *p){(void)c;(void)p;}
int vs_write_file(const char *p,const char *t){(void)p;(void)t;return 0;}
const char *vs_command_shell_name(void){return "/usr/xpg4/bin/sh";}
char *vs_run_command(const char *cmd,int *status){runs++;if(runs==1){*status=3;return xdup("sh: syntax error: unexpected )");}*status=0;return xdup(strstr(cmd,"recovered")?"recovered":"VIBESOLARIS_COMMAND_RUNNER_OK");}

int main(void){VSContext c;char *out;memset(&c,0,sizeof(c));out=vs_agent_turn(&c,"ok that all sounds good, do all that");
    printf("answer=%s\nchats=%d runs=%d\n",out?out:"(null)",chats,runs);
    if(!out||strcmp(out,"Done after recovery.")||chats!=4||runs!=3){free(out);return 1;}free(out);return 0;}
