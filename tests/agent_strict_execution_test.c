/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int chats=0,runs=0;
static char *d(const char*s){size_t n=strlen(s);char*p=(char*)malloc(n+1);memcpy(p,s,n+1);return p;}
char *vs_chat(VSContext*c,const char*p){(void)c;(void)p;chats++;
 if(chats==1)return d("[[VS_TOOL run cmd=\"printf first\"]]");
 if(chats==2)return d("I am still investigating this state and have not finished yet.");
 if(chats==3)return d("[[VS_TOOL run cmd=\"printf second\"]]");
 return d("[[VS_FINAL]]complete");}
void vs_trace(VSContext*c,const char*k,const char*x){(void)c;fprintf(stderr,"[%s] %s\n",k,x);}void vs_trace_clear(VSContext*c){(void)c;}
int vs_mcp_refresh_all(VSContext*c,int f){(void)c;(void)f;return 0;}char*vs_mcp_call(VSContext*c,const char*s,const char*t,const char*a){(void)c;(void)s;(void)t;(void)a;return d("mcp");}
char*vs_compact_text_limit(const char*s,size_t z,const char*r){(void)z;(void)r;return d(s?s:"");}void vs_history_add(VSContext*c,const char*r,const char*x){(void)c;(void)r;(void)x;}
char*vs_cached_read_file(VSContext*c,const char*p){(void)c;(void)p;return d("file");}int vs_attach(VSContext*c,const char*p){(void)c;(void)p;return 0;}int vs_is_image_path(const char*p){(void)p;return 1;}void vs_cache_invalidate(VSContext*c,const char*p){(void)c;(void)p;}int vs_write_file(const char*p,const char*t){(void)p;(void)t;return 0;}
const char *vs_command_shell_name(void){return "/bin/sh";}char*vs_run_command(const char*cmd,int*st){runs++;*st=0;return d(strstr(cmd,"second")?"second":"first");}
int main(void){VSContext c;char*out;memset(&c,0,sizeof(c));out=vs_agent_turn(&c,"ok sounds good, do all that");printf("%s chats=%d runs=%d\n",out,chats,runs);if(strcmp(out,"complete")||chats!=4||runs!=2)return 1;free(out);return 0;}
