/* SPDX-License-Identifier: Unlicense */
/* Synthetic streaming benchmark: drives the REAL production SSE pipeline
 * (provider.c's provider_stream_chunk/stream_line/stream_event/stream_emit)
 * through a strong vs_http_post_stream_ctx override that feeds synthetic
 * provider bytes in network-like chunk sizes.
 *
 * Link recipe mirrors provider_cache_test (src/provider.c + AGENT_SRCS +
 * AGENT_MCP_STUB + oauth/sha256). Not part of `make test`; run explicitly via
 * `make bench-stream`.
 *
 * Scenarios:
 *   openai-small       20k tokens x ~5B text, 1500-4000B chunks (typical)
 *   openai-small-64b   same stream fed in 64B chunks (worst-case granularity)
 *   openai-large       2k tokens x 2KB text (codegen-like payload)
 *   anthropic-small    20k Anthropic content_block_delta events
 *   giant-line         one 512KB single-line SSE event
 *   tui-full-path      openai-small behind the real vs_tui_stream_append
 *                      (stream_callback side, like the TUI live_stream hook)
 *   overhead           immediate [DONE] — per-call baseline to subtract
 *   tui-append         vs_tui_stream_append micro (TUI buffer side)
 */
#include "vibesolaris.h"
#include "../src/tui_stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- stubs for util.c services not linked here (mirrors provider_cache_test) ---- */
static char *copy(const char *s){size_t n=strlen(s);char *p=malloc(n+1);if(p)memcpy(p,s,n+1);return p;}
static char *json_escape(const char *s){size_t i,n=strlen(s);char *p=malloc(n*2+1),*w=p;if(!p)return NULL;for(i=0;i<n;i++){if(s[i]=='"'||s[i]=='\\')*w++='\\';if(s[i]=='\n'){*w++='\\';*w++='n';}else *w++=s[i];}*w=0;return p;}
const char *vs_command_shell_name(void){return "sh";}
char *vs_http_post_ctx(VSContext *c,const char *url,const char *headers[],int count,const char *body,long *status){(void)c;(void)url;(void)headers;(void)count;(void)body;if(status)*status=200;return copy("{\"choices\":[{\"message\":{\"content\":\"\"}}]}");}
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
char *vs_shell_quote(const char *s){const char *p;size_t n=2;char *o,*w;if(!s)s="";for(p=s;*p;p++)n+=(*p=='\'')?4:1;o=(char*)malloc(n+1);if(!o)return NULL;w=o;*w++='\'';for(p=s;*p;p++){if(*p=='\''){memcpy(w,"'\\''",4);w+=4;}else *w++=*p;}*w++='\'';*w=0;return o;}

/* ---- synthetic provider ---- */
static struct {
    char *payload; size_t len, cap;
    long tokens;                 /* content deltas generated */
    size_t chunk_min, chunk_max; /* callback segmentation */
} feed;

static void feed_put(const char *s){size_t n=strlen(s);if(feed.len+n+1>feed.cap){size_t c=feed.cap?feed.cap:65536;while(c<feed.len+n+1)c*=2;feed.payload=realloc(feed.payload,c);feed.cap=c;}memcpy(feed.payload+feed.len,s,n);feed.len+=n;feed.payload[feed.len]=0;}
static void feed_putn(const char *s,size_t n){if(feed.len+n+1>feed.cap){size_t c=feed.cap?feed.cap:65536;while(c<feed.len+n+1)c*=2;feed.payload=realloc(feed.payload,c);feed.cap=c;}memcpy(feed.payload+feed.len,s,n);feed.len+=n;feed.payload[feed.len]=0;}

static char *feed_reset(void){feed.len=0;feed.tokens=0;if(feed.payload)feed.payload[0]=0;return feed.payload;}

static void build_openai(long tokens,size_t text_len){
    static char *big=NULL;static size_t big_cap=0;
    char line[256];
    long i;
    if(text_len+1>big_cap){big=realloc(big,text_len+1);big_cap=text_len+1;}
    memset(big,'a',text_len);big[text_len]=0;
    feed_reset();
    for(i=0;i<tokens;i++){
        size_t n=text_len-(size_t)(i%3);
        feed_put("data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"");
        feed_putn(big,n);
        feed_put("\"}}]}\n\n");
    }
    feed.tokens=tokens;
    snprintf(line,sizeof(line),"data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}],\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":%ld,\"total_tokens\":%ld,\"cached_tokens\":5,\"cache_write_tokens\":2}}\n\n",feed.tokens,feed.tokens+10);
    feed_put(line);
    feed_put("data: [DONE]\n\n");
}

static void build_anthropic(long tokens){
    char line[256];
    long i;
    feed_reset();
    feed_put("event: message_start\n");
    feed_put("data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_1\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"bench\",\"usage\":{\"input_tokens\":10,\"cache_read_input_tokens\":5,\"cache_creation_input_tokens\":2}}}\n\n");
    for(i=0;i<tokens;i++)
        feed_put("event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"word \"}}\n\n");
    feed.tokens=tokens;
    feed_put("event: message_delta\n");
    snprintf(line,sizeof(line),"data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":%ld}}\n\n",feed.tokens);
    feed_put(line);
    feed_put("data: [DONE]\n\n");
}

/* strong override: provider.c's weak vs_http_post_stream_ctx loses */
int vs_http_post_stream_ctx(VSContext *ctx,const char *url,const char *headers[],int nheaders,const char *body,long *status,VSHTTPChunkCallback callback,void *userdata){
    size_t pos=0;
    unsigned rng=12345;
    (void)ctx;(void)url;(void)headers;(void)nheaders;(void)body;
    if(status)*status=200;
    if(!feed.payload||!feed.len)return -1;
    while(pos<feed.len){
        size_t n;
        if(feed.chunk_max<=feed.chunk_min)n=feed.chunk_min;
        else{rng=rng*1103515245u+12345u;n=feed.chunk_min+(size_t)(rng%(unsigned)(feed.chunk_max-feed.chunk_min));}
        if(n>feed.len-pos)n=feed.len-pos;
        if(callback(userdata,feed.payload+pos,n)!=0)return -1;
        pos+=n;
    }
    return 0;
}

/* stream_callback side: append into the real TUI stream buffer */
static int bench_stream_cb(void *userdata,const char *data,size_t len){
    return vs_tui_stream_append((VSTUIStream *)userdata,data,len);
}

/* ---- timing ---- */
static double now_ns(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (double)ts.tv_sec*1e9+(double)ts.tv_nsec;}
static int cmp_d(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return x<y?-1:x>y?1:0;}

static double run_median(VSContext *c,int iters,long *tokens_out,size_t *bytes_out){
    double *ts=malloc(sizeof(double)*(size_t)iters);
    int i;
    for(i=0;i<iters;i++){
        char *out;double t0=now_ns();
        out=vs_chat(c,"benchmark prompt");
        ts[i]=now_ns()-t0;
        if(out){
            if((size_t)strlen(out)<(size_t)feed.tokens*2){fprintf(stderr,"sanity: answer too short (%zu for %ld tokens)\n",strlen(out),feed.tokens);exit(2);}
            free(out);
        }
    }
    qsort(ts,(size_t)iters,sizeof(double),cmp_d);
    {double med=ts[iters/2];free(ts);
    if(tokens_out)*tokens_out=feed.tokens;
    if(bytes_out)*bytes_out=feed.len;
    return med;}
}

static void report(const char *name,double med,long tokens,size_t bytes){
    printf("  %-18s %10.0f ns/tok %8.2f MB/s %8.3f%% CPU@200tps\n",name,
        tokens?med/(double)tokens:med,
        ((double)bytes/(med/1e9))/1048576.0,
        tokens?((med/(double)tokens)/5000000.0)*100.0:0.0);
}

static void set_openai(VSContext *c){c->provider.kind=VS_PROVIDER_OPENAI;c->provider.protocol=VS_PROTOCOL_OPENAI;strcpy(c->provider.name,"custom");strcpy(c->provider.model,"bench-model");strcpy(c->provider.api_key,"test-key");}
static void set_anthropic(VSContext *c){c->provider.kind=VS_PROVIDER_CLAUDE;c->provider.protocol=VS_PROTOCOL_ANTHROPIC;strcpy(c->provider.name,"claude");strcpy(c->provider.model,"claude-bench");strcpy(c->provider.api_key,"test-key");c->provider_anthropic_auth[VS_PROVIDER_CLAUDE]=1;}

int main(void){
    VSContext c;
    static VSTUIStream tuis;
    const int iters=5;
    printf("vibesolaris streaming benchmark (median of %d runs)\n",iters);
    printf("budget: at 200 tokens/s each token has 5000 us of wall time\n\n");

    memset(&c,0,sizeof(c));
    c.cache_enabled=1;strcpy(c.cache_key,"bench-key");
    strcpy(c.provider.base_url,"https://bench.test/v1");
    set_openai(&c);

    /* per-call baseline: empty stream (system prompt + request build) */
    feed.chunk_min=1500;feed.chunk_max=4000;build_openai(0,0);
    {double med=run_median(&c,iters,NULL,NULL);
     printf("  overhead/call      %10.0f ns/call (request build, no tokens)\n",med);}

    feed.chunk_min=1500;feed.chunk_max=4000;build_openai(20000,5);
    {long t;size_t b;double med=run_median(&c,iters,&t,&b);report("openai-small",med,t,b);}

    feed.chunk_min=64;feed.chunk_max=65;build_openai(20000,5);
    {long t;size_t b;double med=run_median(&c,iters,&t,&b);report("openai-small-64b",med,t,b);}

    feed.chunk_min=1500;feed.chunk_max=4000;build_openai(2000,2048);
    {long t;size_t b;double med=run_median(&c,iters,&t,&b);report("openai-large",med,t,b);}

    feed.chunk_min=1500;feed.chunk_max=4000;build_anthropic(20000);
    strcpy(c.provider.base_url,"https://api.anthropic.test");
    set_anthropic(&c);
    {long t;size_t b;double med=run_median(&c,iters,&t,&b);report("anthropic-small",med,t,b);}
    strcpy(c.provider.base_url,"https://bench.test/v1");
    set_openai(&c);

    feed.chunk_min=1500;feed.chunk_max=4000;build_openai(1,512*1024);
    {long t;size_t b;double med=run_median(&c,iters,&t,&b);
     printf("  giant-line         %10.0f ns/event %8.2f MB/s (%zu B in one line)\n",med,((double)b/(med/1e9))/1048576.0,b);}

    /* full path: stream_callback -> real TUI append; reset buffer per iteration */
    memset(&tuis,0,sizeof(tuis));
    c.stream_callback=bench_stream_cb;c.stream_callback_data=&tuis;
    feed.chunk_min=1500;feed.chunk_max=4000;build_openai(20000,5);
    {double *ts=malloc(sizeof(double)*(size_t)iters);int i;
     for(i=0;i<iters;i++){char *out;double t0;vs_tui_stream_reset(&tuis);
        t0=now_ns();out=vs_chat(&c,"benchmark prompt");ts[i]=now_ns()-t0;if(out)free(out);}
     qsort(ts,(size_t)iters,sizeof(double),cmp_d);
     report("tui-full-path",ts[iters/2],feed.tokens,feed.len);
     free(ts);}
    c.stream_callback=NULL;free(tuis.data);

    /* TUI append micro: 20k x 6B (stays under the 128KB stream cap) */
    {const long n=20000;double *ts=malloc(sizeof(double)*(size_t)iters);int i;
     for(i=0;i<iters;i++){
        VSTUIStream s;double t0;long k;
        memset(&s,0,sizeof(s));
        t0=now_ns();
        for(k=0;k<n;k++)vs_tui_stream_append(&s,"word  ",6);
        ts[i]=now_ns()-t0;
        free(s.data);
     }
     qsort(ts,(size_t)iters,sizeof(double),cmp_d);
     printf("  tui-append         %10.0f ns/tok (vs_tui_stream_append, 6B tokens)\n",ts[iters/2]/(double)n);
     free(ts);}

    free(feed.payload);
    return 0;
}
