/* SPDX-License-Identifier: Unlicense */
/* SSE reassembly for the Anthropic and OpenAI streaming dialects.
 *
 * provider.c's streaming transport (vs_http_post_stream_ctx) is stubbed so this
 * test owns every chunk boundary: one SSE document is delivered in fixed-size
 * slices, including slices that cut through the "data:" prefix, through \r\n,
 * through the JSON payload, and through multi-byte UTF-8 sequences.
 */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- state owned by the streaming stub ---------------- */
static char last_url[1024],last_body[16384],last_headers[4096];
static const char *plain_response="{\"content\":[{\"type\":\"text\",\"text\":\"plain\"}]}";
static int stream_calls=0,plain_calls=0;
static int stream_result=0;              /* 0 => deliver, <0 => transport failure */
static const char *feed_sse=NULL;
static size_t feed_len=0,feed_slice=0;   /* 0 => deliver the whole document at once */

static char *dupstr_test(const char *x){char *p=malloc(strlen(x)+1);if(p)strcpy(p,x);return p;}

/* ---------------- stubs for provider.c collaborators ---------------- */
char *vs_http_post_ctx(VSContext *c,const char *u,const char *h[],int n,const char *b,long *s){
    int i;(void)c;plain_calls++;*s=200;
    snprintf(last_url,sizeof(last_url),"%s",u);
    snprintf(last_body,sizeof(last_body),"%s",b?b:"");
    last_headers[0]=0;
    for(i=0;i<n;i++){strcat(last_headers,h[i]);strcat(last_headers,"\n");}
    return dupstr_test(plain_response);
}
int vs_http_post_stream_ctx(VSContext *ctx,const char *url,const char *headers[],int nheaders,const char *body,long *status,VSHTTPChunkCallback callback,void *userdata){
    size_t delivered=0;int i;(void)ctx;
    stream_calls++;*status=200;
    snprintf(last_url,sizeof(last_url),"%s",url);
    snprintf(last_body,sizeof(last_body),"%s",body?body:"");
    last_headers[0]=0;
    for(i=0;i<nheaders;i++){strcat(last_headers,headers[i]);strcat(last_headers,"\n");}
    if(stream_result<0)return -1;
    if(!feed_sse||!callback)return 0;
    while(delivered<feed_len){
        size_t n=feed_slice?feed_slice:(feed_len-delivered);
        if(n>feed_len-delivered)n=feed_len-delivered;
        if(callback(userdata,feed_sse+delivered,n)!=0)return -1;
        delivered+=n;
    }
    return 0;
}
const char *vs_command_shell_name(void){return "sh";}
char *vs_mcp_prompt_fragment(const VSContext *c){(void)c;return NULL;}
void vs_refresh_cache_key(VSContext *c){(void)c;}
void vs_trace(VSContext *c,const char *k,const char *d){(void)c;(void)k;(void)d;}
char *vs_cached_read_file(VSContext *c,const char *p){(void)c;(void)p;return NULL;}
char *vs_cached_base64_file(VSContext *c,const char *p,size_t *n){(void)c;(void)p;(void)n;return NULL;}
char *vs_compact_text_limit(const char *s,size_t l,const char *r){(void)l;(void)r;return s?dupstr_test(s):NULL;}
char *vs_json_escape(const char *s){size_t n=strlen(s),i,j=0;char *o=malloc(n*2+3);if(!o)return NULL;for(i=0;i<n;i++){if(s[i]=='"'||s[i]=='\\')o[j++]='\\';o[j++]=s[i];}o[j]=0;return o;}
int vs_cancel_requested(const VSContext *c){(void)c;return 0;}
int vs_persist_settings(VSContext *c){(void)c;return 0;}
int vs_open_url(const char *u){(void)u;return 1;}
const char *vs_protocol_name(VSProtocolKind p){return p==VS_PROTOCOL_ANTHROPIC?"anthropic":"openai";}
/* The real src/oauth.c supplies the vs_oauth_* and vs_claude_oauth_bearer_url_ok
   symbols; this test configures no OAuth session, so its API-key path is used. */

/* ---------------- harness ---------------- */
static VSContext ctx;
static int fails=0;
static char emitted[16384];              /* text delivered to ctx.stream_callback */
static size_t emitted_len;

static int collect_stream(void *ud,const char *d,size_t n){
    (void)ud;
    if(emitted_len+n<sizeof(emitted)){memcpy(emitted+emitted_len,d,n);emitted_len+=n;emitted[emitted_len]=0;}
    return 0;
}
static int abort_stream(void *ud,const char *d,size_t n){(void)ud;(void)d;(void)n;return 1;}

static void setup(VSProviderKind kind,VSProtocolKind protocol,const char *base){
    memset(&ctx,0,sizeof(ctx));
    ctx.provider.kind=kind;ctx.provider.protocol=protocol;
    snprintf(ctx.provider.name,sizeof(ctx.provider.name),"%s",kind==VS_PROVIDER_CUSTOM?"custom":"provider");
    snprintf(ctx.provider.base_url,sizeof(ctx.provider.base_url),"%s",base);
    snprintf(ctx.provider.model,sizeof(ctx.provider.model),"claude-sonnet-5");
    snprintf(ctx.provider.api_key,sizeof(ctx.provider.api_key),"sk-test-key");
    stream_calls=plain_calls=0;stream_result=0;feed_slice=0;feed_sse=NULL;feed_len=0;
    emitted_len=0;emitted[0]=0;
    ctx.stream_callback=collect_stream;ctx.stream_callback_data=NULL;
}
static void feed(const char *sse){feed_sse=sse;feed_len=strlen(sse);}

static const char *const SLICES_DESC="slices 1,2,3,7,64,whole";

/* Deliver one document at several chunk boundaries and require the assembled
   answer, and the live callback text, to be identical every time. */
static void run_slices(VSProviderKind kind,VSProtocolKind protocol,const char *base,const char *sse,const char *expected,const char *label){
    static const size_t slices[]={1,2,3,7,64,0};
    size_t i;int bad=0;
    for(i=0;i<sizeof(slices)/sizeof(slices[0]);i++){
        char *out;
        setup(kind,protocol,base);
        feed(sse);feed_slice=slices[i];
        out=vs_chat(&ctx,"hi");
        if(!out){fprintf(stderr,"FAIL: %s (slice=%lu): vs_chat returned NULL\n",label,(unsigned long)slices[i]);bad=1;continue;}
        if(strcmp(out,expected)!=0)fprintf(stderr,"FAIL: %s (slice=%lu)\n  expected: [%s]\n  actual:   [%s]\n",label,(unsigned long)slices[i],expected,out),bad=1;
        if(stream_calls!=1||plain_calls!=0)fprintf(stderr,"FAIL: %s (slice=%lu): streaming path not used (stream=%d plain=%d)\n",label,(unsigned long)slices[i],stream_calls,plain_calls),bad=1;
        if(emitted_len!=(size_t)strlen(expected)||strcmp(emitted,expected)!=0)fprintf(stderr,"FAIL: %s (slice=%lu): live callback text differs: [%s]\n",label,(unsigned long)slices[i],emitted),bad=1;
        free(out);
    }
    if(bad){fails++;return;}
    printf("ok: %s (%s)\n",label,SLICES_DESC);
}

/* Anthropic dialect: only content_block_delta/text_delta carries answer text. */
static const char *const ANTHROPIC_SSE=
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_1\",\"role\":\"assistant\"}}\n"
    "\n"
    "event: content_block_start\n"
    "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello\"}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\", world\"}}\n"
    "\n"
    "event: content_block_stop\n"
    "data: {\"type\":\"content_block_stop\",\"index\":0}\n"
    "\n"
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},\"usage\":{\"output_tokens\":4}}\n"
    "\n"
    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n"
    "\n";

/* Same dialect over CRLF, with the optional space after "data:" omitted. */
static const char *const ANTHROPIC_CRLF_SSE=
    "event: content_block_delta\r\n"
    "data:{\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"CRLF\"}}\r\n"
    "\r\n"
    "event: message_stop\r\n"
    "data: {\"type\":\"message_stop\"}\r\n";

/* Tool-use deltas must not leak into the answer as visible text, and the final
   line has no trailing newline so it is only seen by provider_stream_finish(). */
static const char *const ANTHROPIC_TOOL_SSE=
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"cmd\\\":\"}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"tail no newline\"}}";

/* OpenAI dialect: choices[].delta.content, terminated by [DONE]. */
static const char *const OPENAI_SSE=
    "data: {\"id\":\"c1\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\",\"content\":\"\"}}]}\n"
    "\n"
    "data: {\"id\":\"c1\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hel\"}}]}\n"
    "\n"
    "data: {\"id\":\"c1\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"lo\"}}]}\n"
    "\n"
    "data: {\"id\":\"c1\",\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n"
    "\n"
    "data: [DONE]\n"
    "\n";

/* Reasoning-only fields must not be echoed as answer text. */
static const char *const OPENAI_REASONING_SSE=
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"reasoning_content\":\"thinking hard\"}}]}\n"
    "\n"
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"answer\"}}]}\n"
    "\n"
    "data: [DONE]\n";

#define UTF8_TEXT "caf\xc3\xa9 \xe4\xb8\x96\xe7\x95\x8c"
static const char *const ANTHROPIC_UTF8_SSE=
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"caf\xc3\xa9 \xe4\xb8\x96\xe7\x95\x8c\"}}\n";

int main(void){
    char *out;
    setvbuf(stdout,NULL,_IONBF,0);
    printf("-- Anthropic SSE --\n");
    run_slices(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages",ANTHROPIC_SSE,"Hello, world","Anthropic text deltas assemble in order");
    run_slices(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages",ANTHROPIC_CRLF_SSE,"CRLF","Anthropic CRLF framing and 'data:' without space");
    run_slices(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages",ANTHROPIC_TOOL_SSE,"tail no newline","tool-use deltas are ignored; unterminated final line still parsed");
    run_slices(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages",ANTHROPIC_UTF8_SSE,UTF8_TEXT,"Anthropic multi-byte UTF-8 split across chunk boundaries");

    printf("-- OpenAI SSE --\n");
    run_slices(VS_PROVIDER_CUSTOM,VS_PROTOCOL_OPENAI,"https://gateway.example.test/v1",OPENAI_SSE,"Hello","OpenAI delta.content assembles and stops at [DONE]");
    run_slices(VS_PROVIDER_CUSTOM,VS_PROTOCOL_OPENAI,"https://gateway.example.test/v1",OPENAI_REASONING_SSE,"answer","OpenAI reasoning_content is not emitted as answer text");

    /* The streaming attempt must actually be the streaming request. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    feed(ANTHROPIC_SSE);feed_slice=1;
    out=vs_chat(&ctx,"hi");free(out);
    if(!strstr(last_headers,"Accept: text/event-stream")){fprintf(stderr,"FAIL: streaming request omits Accept: text/event-stream\n");fails++;}else printf("ok: streaming request asks for text/event-stream\n");
    if(!strstr(last_body,"\"stream\":true")){fprintf(stderr,"FAIL: streaming request body omits \"stream\":true\n");fails++;}else printf("ok: streaming request body sets \"stream\":true\n");
    if(strcmp(last_url,"https://gateway.example.test/v1/messages")!=0){fprintf(stderr,"FAIL: streaming endpoint was %s\n",last_url);fails++;}else printf("ok: streaming request targets the Anthropic messages endpoint\n");

    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_OPENAI,"https://gateway.example.test/v1");
    feed(OPENAI_SSE);feed_slice=1;
    out=vs_chat(&ctx,"hi");free(out);
    if(!strstr(last_headers,"Accept: text/event-stream")){fprintf(stderr,"FAIL: OpenAI streaming request omits Accept: text/event-stream\n");fails++;}else printf("ok: OpenAI streaming request asks for text/event-stream\n");
    if(strcmp(last_url,"https://gateway.example.test/v1/chat/completions")!=0){fprintf(stderr,"FAIL: OpenAI streaming endpoint was %s\n",last_url);fails++;}else printf("ok: OpenAI streaming request targets chat/completions\n");

    printf("-- degradation and edge cases --\n");
    /* An SSE error event aborts the streaming read and retries without streaming. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    feed("event: error\ndata: {\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\",\"message\":\"Overloaded\"}}\n\n");feed_slice=3;
    out=vs_chat(&ctx,"hi");
    if(!out||strcmp(out,"plain")!=0){fprintf(stderr,"FAIL: SSE error event should fall back to the non-streaming call, got [%s]\n",out?out:"(null)");fails++;}else printf("ok: SSE error event falls back to the non-streaming call\n");
    if(plain_calls!=1){fprintf(stderr,"FAIL: expected exactly one non-streaming retry after SSE error, got %d\n",plain_calls);fails++;}
    if(emitted_len!=0){fprintf(stderr,"FAIL: SSE error event leaked [%s] to the live callback\n",emitted);fails++;}else printf("ok: SSE error event emits no partial text\n");
    free(out);

    /* Transport failure of the streaming call also degrades to non-streaming. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    stream_result=-1;
    out=vs_chat(&ctx,"hi");
    if(!out||strcmp(out,"plain")!=0){fprintf(stderr,"FAIL: streaming transport failure should fall back, got [%s]\n",out?out:"(null)");fails++;}else printf("ok: streaming transport failure falls back to the non-streaming call\n");
    if(stream_calls!=1||plain_calls!=1){fprintf(stderr,"FAIL: fallback call counts wrong (stream=%d plain=%d)\n",stream_calls,plain_calls);fails++;}
    free(out);

    /* A live callback that aborts (cancelled TUI/GUI paint) also degrades, and
       the partial text is not returned as the answer. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    ctx.stream_callback=abort_stream;feed(ANTHROPIC_SSE);feed_slice=1;
    out=vs_chat(&ctx,"hi");
    if(!out||strcmp(out,"plain")!=0){fprintf(stderr,"FAIL: aborted live callback should fall back, got [%s]\n",out?out:"(null)");fails++;}else printf("ok: aborted live callback falls back to the non-streaming call\n");
    if(plain_calls!=1){fprintf(stderr,"FAIL: expected a non-streaming retry after abort, got %d\n",plain_calls);fails++;}
    free(out);

    /* An empty stream must not be reported as a transport failure. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    feed("event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n");feed_slice=1;
    out=vs_chat(&ctx,"hi");
    if(!out){fprintf(stderr,"FAIL: a stream with no text deltas returned NULL\n");fails++;}else printf("ok: a stream with no text deltas returns an empty answer (no error)\n");
    free(out);

    /* Long answer split across many small chunks: every byte delivered once. */
    {
        static char big[65536];size_t i;char *w=big;
        memset(&ctx,0,sizeof(ctx));
        setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
        w+=sprintf(w,"event: content_block_delta\n");
        for(i=0;i<400;i++){
            w+=sprintf(w,"data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"chunk%03lu\"}}\n\n",(unsigned long)i);
        }
        feed(big);feed_slice=1;
        out=vs_chat(&ctx,"hi");
        {
            int ok=1;size_t off=0;
            if(!out)ok=0;
            else for(i=0;i<400&&ok;i++){
                char want[16];snprintf(want,sizeof(want),"chunk%03lu",(unsigned long)i);
                if(strncmp(out+off,want,8)!=0)ok=0;
                off+=8;
            }
            if(ok&&out[off]!=0)ok=0;
            if(!ok){fprintf(stderr,"FAIL: 400 single-byte-sliced deltas did not reassemble exactly (got %lu bytes)\n",(unsigned long)(out?strlen(out):0));fails++;}
            else printf("ok: 400 deltas reassemble exactly when sliced one byte at a time\n");
        }
        free(out);
    }

    if(fails){fprintf(stderr,"%d failure(s)\n",fails);return 1;}
    printf("all provider SSE streaming tests passed\n");
    return 0;
}
