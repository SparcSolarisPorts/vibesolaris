/* SPDX-License-Identifier: Unlicense */
/* Cache usage accounting must be shared across Anthropic and OpenAI protocol
 * modes: response usage fields in either dialect feed the same conversation
 * cache counters, and both request builders advertise cache support. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int http_calls;
static char last_body[32768];
static char *copy(const char *s){size_t n=strlen(s);char *p=malloc(n+1);if(p)memcpy(p,s,n+1);return p;}
static char *json_escape(const char *s){size_t i,n=strlen(s);char *p=malloc(n*2+1),*w=p;if(!p)return NULL;for(i=0;i<n;i++){if(s[i]=='"'||s[i]=='\\')*w++='\\';if(s[i]=='\n'){*w++='\\';*w++='n';}else *w++=s[i];}*w=0;return p;}

char *vs_http_post_ctx(VSContext *c,const char *url,const char *headers[],int count,const char *body,long *status){
    static const char *responses[]={
        /* 1: Anthropic-style usage (native dialect fields). */
        "{\"content\":[{\"type\":\"text\",\"text\":\"anthropic reply one\"}],\"usage\":{\"input_tokens\":100,\"output_tokens\":10,\"cache_read_input_tokens\":50,\"cache_creation_input_tokens\":5}}",
        /* 2: OpenAI-style usage (native dialect fields). */
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"openai reply one\"}}],\"usage\":{\"prompt_tokens\":200,\"completion_tokens\":20,\"total_tokens\":220,\"cached_tokens\":80,\"cache_write_tokens\":7}}",
        /* 3: Anthropic-style usage again. */
        "{\"content\":[{\"type\":\"text\",\"text\":\"anthropic reply two\"}],\"usage\":{\"input_tokens\":110,\"output_tokens\":11,\"cache_read_input_tokens\":70,\"cache_creation_input_tokens\":3}}",
        /* 4: OpenAI-style usage again. */
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"openai reply two\"}}],\"usage\":{\"prompt_tokens\":210,\"completion_tokens\":21,\"total_tokens\":231,\"cached_tokens\":90,\"cache_write_tokens\":9}}"
    };
    (void)c;(void)url;(void)headers;(void)count;
    http_calls++;snprintf(last_body,sizeof(last_body),"%s",body?body:"");
    if(http_calls<1||http_calls>4)return NULL;
    *status=200;return copy(responses[http_calls-1]);
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
char *vs_run_command(const char *cmd,int *status){(void)cmd;*status=0;return copy("");}
char *vs_run_command_ctx(VSContext *c,const char *cmd,int *status){(void)c;return vs_run_command(cmd,status);}
char *vs_web_search(VSContext *c,const char *q,int n){(void)c;(void)q;(void)n;return copy("");}
char *vs_web_fetch(VSContext *c,const char *u,size_t n){(void)c;(void)u;(void)n;return copy("");}
int vs_graph_build(VSContext *c,const char *r,VSGraphMode m,VSGraph *g){(void)c;(void)r;(void)m;(void)g;return -1;}
char *vs_graph_summary(const VSGraph *g,int n){(void)g;(void)n;return copy("");}

static int run_anthropic(VSContext *c){
    c->provider.kind=VS_PROVIDER_CLAUDE;c->provider.protocol=VS_PROTOCOL_ANTHROPIC;
    strcpy(c->provider.name,"claude");strcpy(c->provider.base_url,"https://api.anthropic.test");
    strcpy(c->provider.model,"claude-cache-test");strcpy(c->provider.api_key,"test-key");
    c->provider_anthropic_auth[VS_PROVIDER_CLAUDE]=1;
    {char *out=vs_chat(c,"hello anthropic");if(out)free(out);}
    return strstr(last_body,"\"cache_control\":{\"type\":\"ephemeral\"}")!=NULL;
}
static int run_openai(VSContext *c){
    c->provider.kind=VS_PROVIDER_OPENAI;c->provider.protocol=VS_PROTOCOL_OPENAI;
    strcpy(c->provider.name,"openai");strcpy(c->provider.base_url,"https://api.openai.test/v1");
    strcpy(c->provider.model,"gpt-5.3");strcpy(c->provider.api_key,"test-key");
    {char *out=vs_chat(c,"hello openai");if(out)free(out);}
    return strstr(last_body,"\"prompt_cache_key\":\"provider-cache-test-key\"")!=NULL;
}

int main(void){
    VSContext c;memset(&c,0,sizeof(c));
    c.cache_enabled=1;strcpy(c.cache_key,"provider-cache-test-key");
    /* Round 1: Anthropic mode must advertise ephemeral cache breakpoints. */
    if(!run_anthropic(&c)){fprintf(stderr,"Anthropic request missing cache_control\nbody=%s\n",last_body);return 1;}
    /* Round 2: OpenAI mode must send the shared cache key and cache options. */
    if(!run_openai(&c)){fprintf(stderr,"OpenAI request missing prompt_cache_key\nbody=%s\n",last_body);return 2;}
    if(!strstr(last_body,"\"prompt_cache_options\":{\"mode\":\"implicit\",\"ttl\":\"30m\"}")){fprintf(stderr,"OpenAI request missing prompt_cache_options\nbody=%s\n",last_body);return 3;}
    /* Rounds 3 and 4: accumulate more usage in both dialects. */
    if(!run_anthropic(&c)){fprintf(stderr,"second Anthropic request missing cache_control\n");return 4;}
    if(!run_openai(&c)){fprintf(stderr,"second OpenAI request missing prompt_cache_key\n");return 5;}
    if(http_calls!=4){fprintf(stderr,"expected 4 HTTP calls, got %d\n",http_calls);return 6;}
    /* Shared conversation counters: 50+80+70+90 cached reads, 5+7+3+9 writes. */
    if(c.conversation_cached_tokens!=290){fprintf(stderr,"conversation_cached_tokens=%lu, want 290\n",c.conversation_cached_tokens);return 7;}
    if(c.conversation_cache_write_tokens!=24){fprintf(stderr,"conversation_cache_write_tokens=%lu, want 24\n",c.conversation_cache_write_tokens);return 8;}
    if(c.conversation_usage_responses!=4){fprintf(stderr,"conversation_usage_responses=%lu, want 4\n",c.conversation_usage_responses);return 9;}
    /* Per-response fields reflect only the latest response. */
    if(c.provider_cached_tokens!=90||c.provider_cache_write_tokens!=9){fprintf(stderr,"last provider cache read/write=%ld/%ld, want 90/9\n",c.provider_cached_tokens,c.provider_cache_write_tokens);return 10;}
    puts("provider cache accounting test passed");return 0;
}
