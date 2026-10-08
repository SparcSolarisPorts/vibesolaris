/* SPDX-License-Identifier: Unlicense */
/* Checks which credential vs_chat() sends to the Anthropic Messages API.
   Collaborators are stubbed; oauth.c is the real implementation. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
/* ---- stubs for provider.c collaborators ---- */
static char *dupstr_test(const char *x){char *p=malloc(strlen(x)+1);strcpy(p,x);return p;}
static char last_headers[4096];static char last_url[1024];static int calls=0;
char *vs_http_post_ctx(VSContext *c,const char *u,const char *h[],int n,const char *b,long *s){
    int i;(void)c;(void)b;calls++;*s=200;snprintf(last_url,sizeof(last_url),"%s",u);last_headers[0]=0;
    for(i=0;i<n;i++){strcat(last_headers,h[i]);strcat(last_headers,"\n");}
    return dupstr_test("{\"content\":[{\"type\":\"text\",\"text\":\"hi\"}]}");
}
const char *vs_command_shell_name(void){return "sh";}
char *vs_mcp_prompt_fragment(const VSContext *c){(void)c;return NULL;}
void vs_refresh_cache_key(VSContext *c){(void)c;}
void vs_trace(VSContext *c,const char *k,const char *d){(void)c;(void)k;(void)d;}
char *vs_cached_read_file(VSContext *c,const char *p){(void)c;(void)p;return NULL;}
char *vs_cached_base64_file(VSContext *c,const char *p,size_t *n){(void)c;(void)p;(void)n;return NULL;}
char *vs_compact_text_limit(const char *s,size_t l,const char *r){(void)l;(void)r;return s?dupstr_test(s):NULL;}
char *vs_json_escape(const char *s){size_t n=strlen(s),i,j=0;char *o=malloc(n*2+3);o[j++]='\0';o[0]=0;for(i=0;i<n;i++){if(s[i]=='"'||s[i]=='\\'){o[j++]='\\';}o[j++]=s[i];}o[j]=0;return o;}
int vs_cancel_requested(const VSContext *c){(void)c;return 0;}
int vs_persist_settings(VSContext *c){(void)c;return 0;}
int vs_open_url(const char *u){(void)u;return 1;}
const char *vs_protocol_name(VSProtocolKind p){return p==VS_PROTOCOL_ANTHROPIC?"anthropic":"openai";}
/* vs_build_system_prompt and friends live in provider.c and need stubs only for the above */
static int fails=0;
#define CHECK(c,m) do{ if(!(c)){fprintf(stderr,"FAIL: %s\n  headers:\n%s  url: %s\n",m,last_headers,last_url);fails++;} else printf("ok: %s\n",m);}while(0)

static VSContext ctx;
static void setup(const char *base,const char *apikey,int with_oauth)
{
    memset(&ctx,0,sizeof(ctx));
    ctx.provider.kind=VS_PROVIDER_CLAUDE;ctx.provider.protocol=VS_PROTOCOL_ANTHROPIC;
    snprintf(ctx.provider.name,sizeof(ctx.provider.name),"claude");
    snprintf(ctx.provider.base_url,sizeof(ctx.provider.base_url),"%s",base);
    snprintf(ctx.provider.model,sizeof(ctx.provider.model),"claude-sonnet-5");
    snprintf(ctx.provider.api_key,sizeof(ctx.provider.api_key),"%s",apikey);
    if(with_oauth){snprintf(ctx.claude_oauth.access_token,sizeof(ctx.claude_oauth.access_token),"CLAUDE-OAUTH-TOKEN");ctx.claude_oauth.expires_at=4102444800L;}
    /* OpenAI OAuth token must never be used for Claude */
    snprintf(ctx.oauth.access_token,sizeof(ctx.oauth.access_token),"OPENAI-OAUTH-TOKEN");
}
int main(void){
    char *out;
    /* 1. official endpoint + Claude OAuth session => bearer, no x-api-key */
    setup("https://api.anthropic.com/v1/messages","sk-ant-api-key",1);
    out=vs_chat(&ctx,"hello");CHECK(out!=NULL,"chat completes with Claude OAuth");free(out);
    CHECK(strstr(last_headers,"Authorization: Bearer CLAUDE-OAUTH-TOKEN")!=NULL,"official endpoint uses Claude OAuth bearer");
    CHECK(strstr(last_headers,"x-api-key")==NULL,"OAuth session does not also send x-api-key");
    CHECK(strstr(last_headers,"OPENAI-OAUTH-TOKEN")==NULL,"OpenAI OAuth token is never sent to Claude");

    /* 2. custom gateway + Claude OAuth session => OAuth token NOT sent; API key used */
    setup("https://gateway.example.com/v1/messages","sk-ant-api-key",1);
    out=vs_chat(&ctx,"hello");free(out);
    CHECK(strstr(last_headers,"CLAUDE-OAUTH-TOKEN")==NULL,"Claude OAuth token never sent to a third-party gateway");
    CHECK(strstr(last_headers,"x-api-key: sk-ant-api-key")!=NULL,"gateway falls back to the configured API key");

    /* 3. no OAuth session => API key via x-api-key, as before */
    setup("https://api.anthropic.com/v1/messages","sk-ant-api-key",0);
    out=vs_chat(&ctx,"hello");free(out);
    CHECK(strstr(last_headers,"x-api-key: sk-ant-api-key")!=NULL,"API key path unchanged when no OAuth session");
    CHECK(strstr(last_headers,"Authorization: Bearer")==NULL,"no bearer header in API key path");

    /* 4. gateway + OAuth but no API key => clear error, no request */
    setup("https://gateway.example.com/v1/messages","",1);
    calls=0;out=vs_chat(&ctx,"hello");
    CHECK(out&&strstr(out,"only sent to https://api.anthropic.com")!=NULL,"clear error when OAuth cannot be used for this base URL");
    CHECK(calls==0,"no request is sent when OAuth is refused and no key exists");
    free(out);

    /* 5. nothing configured */
    setup("https://api.anthropic.com/v1/messages","",0);
    out=vs_chat(&ctx,"hello");CHECK(out&&strstr(out,"No Claude API key or Claude OAuth session")!=NULL,"clear message when Claude has no credential");free(out);

    if(fails){fprintf(stderr,"%d failure(s)\n",fails);return 1;}
    printf("all provider Claude OAuth tests passed\n");return 0;
}
