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
static char last_url[1024],last_body[65536],last_headers[4096];
static char plain_response[8192]="{\"content\":[{\"type\":\"text\",\"text\":\"plain\"}]}";
static int stream_calls=0,plain_calls=0;
static int recover_with_text=1;          /* 0 => gateway ignores disabled-thinking */
static char last_trace_tag[64],last_trace_msg[512];
static int saw_shape_retry=0;
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
    /* Simulate a gateway whose thinking-default exhausts budgets: a request
       with thinking explicitly disabled recovers a real text block. */
    if(recover_with_text&&b&&strstr(b,"\"thinking\":{\"type\":\"disabled\"}"))return dupstr_test("{\"content\":[{\"type\":\"text\",\"text\":\"recovered\"}],\"stop_reason\":\"end_turn\",\"usage\":{\"output_tokens\":9}}");
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
void vs_trace(VSContext *c,const char *k,const char *d){
    (void)c;
    snprintf(last_trace_tag,sizeof(last_trace_tag),"%s",k?k:"");
    snprintf(last_trace_msg,sizeof(last_trace_msg),"%s",d?d:"");
    if(k&&!strcmp(k,"provider-shape-retry")&&d&&strstr(d,"text_blocks=1 thinking_blocks=0"))saw_shape_retry=1;
}
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
    stream_calls=plain_calls=0;stream_result=0;feed_slice=0;feed_sse=NULL;feed_len=0;recover_with_text=1;saw_shape_retry=0;
    snprintf(plain_response,sizeof(plain_response),"%s","{\"content\":[{\"type\":\"text\",\"text\":\"plain\"}]}");
    emitted_len=0;emitted[0]=0;
    ctx.stream_callback=collect_stream;ctx.stream_callback_data=NULL;
    ctx.native_tools_enabled=1;   /* runtime default; tests may override */
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
    if(!strstr(last_body,"\"tools\":[")||!strstr(last_body,"\"name\":\"subagent\"")||!strstr(last_body,"\"name\":\"read\"")){fprintf(stderr,"FAIL: custom Anthropic request lacks the native tools array\n");fails++;}
    else printf("ok: custom Anthropic requests expose the core directive set as native tools\n");
    if(!strstr(last_body,"true}}],\"system\"")){fprintf(stderr,"FAIL: native tools array is not balanced before the system field\n");fails++;}
    else printf("ok: native tools array is balanced and well-formed\n");

    /* First-party Anthropic and non-Anthropic protocols stay text-only. */
    setup(VS_PROVIDER_CLAUDE,VS_PROTOCOL_ANTHROPIC,"https://api.anthropic.com/v1/messages");
    snprintf(ctx.provider.api_key,sizeof(ctx.provider.api_key),"%s","sk-test-key");
    feed(ANTHROPIC_SSE);feed_slice=1;
    out=vs_chat(&ctx,"hi");free(out);
    if(strstr(last_body,"\"tools\":[")){fprintf(stderr,"FAIL: first-party Anthropic request unexpectedly carries a tools array\n");fails++;}
    else printf("ok: first-party Anthropic stays on the text-marker protocol\n");
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_OPENAI,"https://gateway.example.test/v1");
    feed(OPENAI_SSE);feed_slice=1;
    out=vs_chat(&ctx,"hi");free(out);
    if(!strstr(last_body,"\"tools\":[")||!strstr(last_body,"\"type\":\"function\"")||!strstr(last_body,"\"name\":\"subagent\"")||!strstr(last_body,"\"name\":\"read\"")||!strstr(last_body,"\"tool_choice\":\"auto\"")){fprintf(stderr,"FAIL: custom OpenAI-protocol request lacks the native function tools array\n");fails++;}
    else printf("ok: custom OpenAI-protocol requests expose the core directive set as native function tools\n");
    if(strstr(last_body,"\"input_schema\"")){fprintf(stderr,"FAIL: OpenAI-protocol request carries an Anthropic-shaped tools array\n");fails++;}
    else printf("ok: OpenAI-protocol tools use the function shape, not input_schema\n");
    if(!strstr(last_body,"true}}}],\"tool_choice\"")){fprintf(stderr,"FAIL: native function tools array is not balanced before tool_choice\n");fails++;}
    else printf("ok: native function tools array is balanced and well-formed\n");

    /* The kill switch removes the tools array on both protocols. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    ctx.native_tools_enabled=0;
    feed(ANTHROPIC_SSE);feed_slice=1;
    out=vs_chat(&ctx,"hi");free(out);
    if(strstr(last_body,"\"tools\":[")){fprintf(stderr,"FAIL: disabled native_tools still emits a tools array (Anthropic)\n");fails++;}
    else printf("ok: native_tools off removes the tools array (Anthropic)\n");
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_OPENAI,"https://gateway.example.test/v1");
    ctx.native_tools_enabled=0;
    feed(OPENAI_SSE);feed_slice=1;
    out=vs_chat(&ctx,"hi");free(out);
    if(strstr(last_body,"\"tools\":[")||strstr(last_body,"\"tool_choice\"")){fprintf(stderr,"FAIL: disabled native_tools still emits a tools array (OpenAI)\n");fails++;}
    else printf("ok: native_tools off removes the tools array (OpenAI)\n");

    /* A gateway rejection naming the tools array points at the kill switch. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    snprintf(plain_response,sizeof(plain_response),"%s","{\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\",\" message\":\"Unknown field: tools\"}}");
    feed("");feed_slice=0;
    out=vs_chat(&ctx,"hi");
    if(!out||!strstr(out,"native_tools")){fprintf(stderr,"FAIL: tools-rejection hint missing (Anthropic), got [%s]\n",out?out:"(null)");fails++;}
    else printf("ok: tools-rejection error points at the native_tools kill switch (Anthropic)\n");
    free(out);
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_OPENAI,"https://gateway.example.test/v1");
    snprintf(plain_response,sizeof(plain_response),"%s","{\"error\":{\"type\":\"invalid_request_error\",\"param\":\"tools\",\" message\":\"Unknown parameter: tools\"}}");
    feed("");feed_slice=0;
    out=vs_chat(&ctx,"hi");
    if(!out||!strstr(out,"native_tools")){fprintf(stderr,"FAIL: tools-rejection hint missing (OpenAI), got [%s]\n",out?out:"(null)");fails++;}
    else printf("ok: tools-rejection error points at the native_tools kill switch (OpenAI)\n");
    free(out);
    /* Errors unrelated to tools must not get the hint. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    snprintf(plain_response,sizeof(plain_response),"%s","{\"type\":\"error\",\"error\":{\"type\":\"overloaded\",\" message\":\"Overloaded\"}}");
    feed("");feed_slice=0;
    out=vs_chat(&ctx,"hi");
    if(out&&strstr(out,"native_tools")){fprintf(stderr,"FAIL: unrelated provider error got the tools hint: [%s]\n",out);fails++;}
    else printf("ok: errors unrelated to tools get no hint\n");
    free(out);
    /* And with the toggle off, even a tools-named error gets no hint. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    ctx.native_tools_enabled=0;
    snprintf(plain_response,sizeof(plain_response),"%s","{\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\",\" message\":\"Unknown field: tools\"}}");
    feed("");feed_slice=0;
    out=vs_chat(&ctx,"hi");
    if(out&&strstr(out,"native_tools")){fprintf(stderr,"FAIL: tools hint shown while native_tools is off\n");fails++;}
    else printf("ok: no tools hint when native_tools is already off\n");
    free(out);
    /* A gateway that ignores the thinking-disabled retry stops being retried:
       two consecutive futile retries set the flag, then one plain call suffices. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    recover_with_text=0;   /* the retry must also come back thinking-only */
    snprintf(plain_response,sizeof(plain_response),"%s","{\"content\":[{\"type\":\"thinking\",\"thinking\":\"plan\"}],\"stop_reason\":\"end_turn\",\"usage\":{\"output_tokens\":7}}");
    feed("");feed_slice=0;
    out=vs_chat(&ctx,"hi");free(out);
    if(ctx.thinking_disabled_ignored){fprintf(stderr,"FAIL: thinking-disabled retry disabled after a single futile retry\n");fails++;}
    else printf("ok: one futile retry does not disable the retry path yet\n");
    {int before=plain_calls;
     out=vs_chat(&ctx,"hi");free(out);
     if(!ctx.thinking_disabled_ignored){fprintf(stderr,"FAIL: two consecutive futile retries did not disable the retry\n");fails++;}
     else printf("ok: two consecutive futile retries disable the thinking-disabled retry\n");
     if(plain_calls-before!=2){fprintf(stderr,"FAIL: second round should still retry once (delta %d)\n",plain_calls-before);fails++;}
     else printf("ok: the disabling round still used its final retry\n");
     before=plain_calls;out=vs_chat(&ctx,"hi");free(out);
     if(plain_calls-before!=1){fprintf(stderr,"FAIL: ignored gateway fired %d plain calls, want 1\n",plain_calls-before);fails++;}
     else printf("ok: once ignored, an empty round costs one plain call, no retry\n");}
    snprintf(plain_response,sizeof(plain_response),"%s","{\"content\":[{\"type\":\"text\",\"text\":\"plain\"}]}");

    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_OPENAI,"https://gateway.example.test/v1");
    feed(OPENAI_SSE);feed_slice=1;
    out=vs_chat(&ctx,"hi");free(out);
    if(!strstr(last_headers,"Accept: text/event-stream")){fprintf(stderr,"FAIL: OpenAI streaming request omits Accept: text/event-stream\n");fails++;}else printf("ok: OpenAI streaming request asks for text/event-stream\n");
    if(strcmp(last_url,"https://gateway.example.test/v1/chat/completions")!=0){fprintf(stderr,"FAIL: OpenAI streaming endpoint was %s\n",last_url);fails++;}else printf("ok: OpenAI streaming request targets chat/completions\n");

    /* Usage arrives separately from text and must be counted once. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    feed("data: {\"type\":\"message_start\",\"message\":{\"usage\":{\"input_tokens\":10,\"cache_read_input_tokens\":20,\"cache_creation_input_tokens\":5}}}\n\ndata: {\"type\":\"message_delta\",\"usage\":{\"output_tokens\":4}}\n\n");feed_slice=1;
    out=vs_chat(&ctx,"hi");free(out);
    if(ctx.provider_input_tokens!=35||ctx.provider_output_tokens!=4||ctx.provider_total_tokens!=39||ctx.provider_cache_read_tokens!=20||ctx.provider_cache_write_tokens!=5){fprintf(stderr,"FAIL: Anthropic streamed usage accounting\n");fails++;}else printf("ok: Anthropic streamed usage accounting\n");

    setup(VS_PROVIDER_OPENAI,VS_PROTOCOL_OPENAI,"https://api.openai.com/v1");
    feed("data: {\"choices\":[{\"delta\":{\"content\":\"ok\"}}]}\n\ndata: {\"choices\":[],\"usage\":{\"prompt_tokens\":100,\"completion_tokens\":10,\"total_tokens\":111,\"prompt_tokens_details\":{\"cached_tokens\":40}}}\n\ndata: [DONE]\n\n");feed_slice=1;
    out=vs_chat(&ctx,"hi");free(out);
    if(ctx.provider_input_tokens!=100||ctx.provider_output_tokens!=10||ctx.provider_total_tokens!=111||ctx.provider_cache_read_tokens!=40){fprintf(stderr,"FAIL: OpenAI streamed usage accounting\n");fails++;}else printf("ok: OpenAI streamed usage accounting\n");
    if(!strstr(last_body,"\"stream_options\":{\"include_usage\":true}")){fprintf(stderr,"FAIL: OpenAI stream does not request usage\n");fails++;}
    if(strstr(last_body,"\"tools\":[")){fprintf(stderr,"FAIL: first-party OpenAI request unexpectedly carries a tools array\n");fails++;}
    else printf("ok: first-party OpenAI stays on the text-marker protocol\n");
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_OPENAI,"https://gateway.example.test/v1");feed(OPENAI_SSE);out=vs_chat(&ctx,"hi");free(out);
    if(strstr(last_body,"\"stream_options\"")){fprintf(stderr,"FAIL: custom gateway received stream_options\n");fails++;}

    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_OPENAI,"https://gateway.example.test/v1");
    feed("data: {\"usage\":{\"prompt_tokens\":100,\"cached_tokens\":40,\"cache_write_tokens\":5}}\n\ndata: {\"usage\":{\"completion_tokens\":10,\"total_tokens\":110}}\n\n");feed_slice=1;
    out=vs_chat(&ctx,"hi");free(out);
    if(ctx.provider_cache_read_tokens!=40||ctx.provider_cache_write_tokens!=5||ctx.provider_total_tokens!=110){fprintf(stderr,"FAIL: partial usage preserves cache counts\n");fails++;}else printf("ok: partial usage preserves cache counts\n");

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

    /* A text-less stream (usage/thinking only) must fall back to the
       non-streaming call instead of returning an empty answer that the
       agent loop would spin on. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    feed("event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n");feed_slice=1;
    out=vs_chat(&ctx,"hi");
    if(!out||strcmp(out,"plain")!=0){fprintf(stderr,"FAIL: a stream with no text deltas should fall back, got [%s]\n",out?out:"(null)");fails++;}else printf("ok: a stream with no text deltas falls back to the non-streaming call\n");
    if(plain_calls!=1){fprintf(stderr,"FAIL: expected exactly one non-streaming fallback, got %d\n",plain_calls);fails++;}
    free(out);

    /* A thinking-only stream (output tokens but zero text deltas) must also
       fall back, not surface an empty answer. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    feed("event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"usage\":{\"input_tokens\":10}}}\n\nevent: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"hmm\"}}\n\nevent: message_delta\ndata: {\"type\":\"message_delta\",\"usage\":{\"output_tokens\":7}}\n\nevent: message_stop\ndata: {\"type\":\"message_stop\"}\n\n");feed_slice=1;
    out=vs_chat(&ctx,"hi");
    if(!out||strcmp(out,"plain")!=0){fprintf(stderr,"FAIL: a thinking-only stream should fall back, got [%s]\n",out?out:"(null)");fails++;}else printf("ok: a thinking-only stream falls back to the non-streaming call\n");
    free(out);

    /* Native tool_use blocks from an aggregating gateway must be converted
       back into [[VS_TOOL ...]] directives.  A thinking-only stream forces the
       non-streaming fallback, which carries the tool_use payload. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    ctx.workflow.current_task_id=7;
    feed("event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"hmm\"}}\n\nevent: message_delta\ndata: {\"type\":\"message_delta\",\"usage\":{\"output_tokens\":40}}\n\nevent: message_stop\ndata: {\"type\":\"message_stop\"}\n\n");feed_slice=1;
    snprintf(plain_response,sizeof(plain_response),"%s",
        "{\"content\":["
        "{\"type\":\"thinking\",\"thinking\":\"let me look\"},"
        "{\"type\":\"tool_use\",\"id\":\"tu_1\",\"name\":\"read\",\"input\":{\"path\":\"/tmp/x.c\",\"start_line\":12}},"
        "{\"type\":\"tool_use\",\"id\":\"tu_2\",\"name\":\"write\",\"input\":{\"path\":\"/tmp/y.c\",\"content\":\"int main(void){return 0;}\\n\",\"task_id\":\"9\"}}"
        "],\"stop_reason\":\"tool_use\",\"usage\":{\"input_tokens\":100,\"output_tokens\":50}}");
    out=vs_chat(&ctx,"hi");
    if(!out){fprintf(stderr,"FAIL: tool_use conversion returned NULL\n");fails++;}
    else {
        if(!strstr(out,"[[VS_TOOL read path=\"/tmp/x.c\" start_line=\"12\"]]")){fprintf(stderr,"FAIL: read tool_use block not converted, got [%s]\n",out);fails++;}
        else if(strstr(out,"read ")&&strstr(out,"task_id=\"7\"")&&strstr(out,"[[VS_TOOL read")){/* read-only op must not inherit task_id */fprintf(stderr,"FAIL: read-only op inherited task_id: [%s]\n",out);fails++;}
        else printf("ok: native tool_use blocks convert to VS_TOOL directives\n");
        if(!strstr(out,"[[VS_TOOL write ")||!strstr(out,"task_id=\"9\"")){fprintf(stderr,"FAIL: write tool_use block lost its explicit task_id: [%s]\n",out);fails++;}
        else if(!strstr(out,"content=\"int main(void){return 0;}\n\"")){fprintf(stderr,"FAIL: write content attribute mangled: [%s]\n",out);fails++;}
        else printf("ok: explicit task_id and escaped content survive conversion\n");
    }
    free(out);

    /* text_blocks=0 + tool_use blocks: no raw JSON may leak into the answer. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    ctx.workflow.current_task_id=4;
    feed("event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n");feed_slice=1;
    snprintf(plain_response,sizeof(plain_response),"%s",
        "{\"content\":[{\"type\":\"thinking\",\"thinking\":\"x\"},{\"type\":\"tool_use\",\"id\":\"tu_9\",\"name\":\"run\",\"input\":{\"cmd\":\"ls -la\"}}],\"stop_reason\":\"tool_use\",\"usage\":{\"output_tokens\":30}}");
    out=vs_chat(&ctx,"hi");
    if(!out||!strstr(out,"[[VS_TOOL run cmd=\"ls -la\" task_id=\"4\"]]")){fprintf(stderr,"FAIL: tool_use-only response should convert cleanly, got [%s]\n",out?out:"(null)");fails++;}
    else if(strstr(out,"\"type\"")||strstr(out,"stop_reason")){fprintf(stderr,"FAIL: raw JSON leaked into answer: [%s]\n",out);fails++;}
    else printf("ok: tool_use-only response converts without leaking raw JSON\n");
    free(out);

    /* MiniMax-style native tool_calls (OpenAI dialect, finish_reason=tool_calls,
       arguments an encoded JSON object) must convert to VS_TOOL directives. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_OPENAI,"https://gateway.example.test/v1");
    ctx.workflow.current_task_id=3;
    feed("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\ndata: [DONE]\n\n");feed_slice=1;
    snprintf(plain_response,sizeof(plain_response),"%s",
        "{\"choices\":[{\"index\":0,\"finish_reason\":\"tool_calls\",\"tool_calls\":["
        "{\"id\":\"c1\",\"type\":\"function\",\"function\":{\"name\":\"run\",\"arguments\":\"{\\\"cmd\\\": \\\"make test\\\", \\\"timeout\\\": 120}\"}},"
        "{\"id\":\"c2\",\"type\":\"function\",\"function\":{\"name\":\"read\",\"arguments\":\"{\\\"path\\\": \\\"/tmp/z.c\\\"}\"}}"
        "]}],\"usage\":{\"prompt_tokens\":50,\"completion_tokens\":20,\"total_tokens\":70}}");
    out=vs_chat(&ctx,"hi");
    if(!out||!strstr(out,"[[VS_TOOL run cmd=\"make test\" timeout=\"120\" task_id=\"3\"]]")){fprintf(stderr,"FAIL: MiniMax run tool_call not converted, got [%s]\n",out?out:"(null)");fails++;}
    else if(!strstr(out,"[[VS_TOOL read path=\"/tmp/z.c\"]]")){fprintf(stderr,"FAIL: MiniMax read tool_call not converted, got [%s]\n",out);fails++;}
    else if(strstr(out,"\"choices\"")||strstr(out,"finish_reason")){fprintf(stderr,"FAIL: raw JSON leaked into MiniMax answer: [%s]\n",out);fails++;}
    else printf("ok: MiniMax native tool_calls convert to VS_TOOL directives\n");
    free(out);

    /* MiniMax raw XML tool calls embedded in the answer text must convert and
       be stripped from the prose. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_OPENAI,"https://gateway.example.test/v1");
    ctx.workflow.current_task_id=5;
    feed("data: {\"choices\":[{\"delta\":{\"content\":\"Let me check.\\n<minimax:tool_call>\\n<invoke name=\\\"run\\\">\\n<parameter name=\\\"cmd\\\">make -j4</parameter>\\n<parameter name=\\\"timeout\\\">120</parameter>\\n</invoke>\\n</minimax:tool_call>\"}}]}\n\ndata: [DONE]\n\n");feed_slice=1;
    out=vs_chat(&ctx,"hi");
    if(!out||!strstr(out,"[[VS_TOOL run cmd=\"make -j4\" timeout=\"120\" task_id=\"5\"]]")){fprintf(stderr,"FAIL: MiniMax XML tool call not converted, got [%s]\n",out?out:"(null)");fails++;}
    else if(strstr(out,"<minimax:tool_call>")||strstr(out,"<invoke")){fprintf(stderr,"FAIL: MiniMax XML leaked into answer: [%s]\n",out);fails++;}
    else printf("ok: MiniMax raw XML tool calls convert to VS_TOOL directives\n");
    free(out);

    /* A thinking-only non-streaming response (content array with no text
       blocks) must never surface a mangled fragment like "type" grabbed
       from [{"type":"thinking"}]; the disabled-thinking retry recovers it. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    feed("event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n");feed_slice=1;
    snprintf(plain_response,sizeof(plain_response),"%s",
        "{\"content\":[{\"type\":\"thinking\",\"thinking\":\"hmm\"}],\"stop_reason\":\"end_turn\",\"usage\":{\"output_tokens\":30}}");
    out=vs_chat(&ctx,"hi");
    if(!out||strcmp(out,"recovered")!=0){fprintf(stderr,"FAIL: thinking-only response should be recovered without a mangled fragment, got [%s]\n",out?out:"(null)");fails++;}
    else if(strstr(out,"\"type\"")){fprintf(stderr,"FAIL: raw JSON leaked into thinking-only answer: [%s]\n",out);fails++;}
    else printf("ok: thinking-only non-streaming response never surfaces a mangled 'type' fragment\n");
    free(out);

    /* A thinking-only non-streaming completion (thinking budget exhausted at
       the gateway) is retried once with thinking disabled; the retry's text
       becomes the answer instead of the empty-response message. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    feed("event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n");feed_slice=1;
    snprintf(plain_response,sizeof(plain_response),"%s",
        "{\"content\":[{\"type\":\"thinking\",\"thinking\":\"hmm\"}],\"stop_reason\":\"end_turn\",\"usage\":{\"output_tokens\":30}}");
    out=vs_chat(&ctx,"hi");
    if(!out||strcmp(out,"recovered")!=0){fprintf(stderr,"FAIL: thinking-only completion should retry with thinking disabled, got [%s]\n",out?out:"(null)");fails++;}
    else if(plain_calls!=2){fprintf(stderr,"FAIL: expected original + one disabled-thinking retry, got %d calls\n",plain_calls);fails++;}
    else if(!saw_shape_retry){fprintf(stderr,"FAIL: retried response shape not traced (tag=%s msg=%s)\n",last_trace_tag,last_trace_msg);fails++;}
    else printf("ok: thinking-only completion retries once with thinking disabled (retry shape traced)\n");
    free(out);

    /* Providers that think internally but return NO thinking block at all
       (Claude often omits the thinking text) still get the disabled-thinking
       retry: an empty content array is enough to trigger it. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    feed("event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n");feed_slice=1;
    snprintf(plain_response,sizeof(plain_response),"%s",
        "{\"content\":[],\"stop_reason\":\"end_turn\",\"usage\":{\"output_tokens\":12}}");
    out=vs_chat(&ctx,"hi");
    if(!out||strcmp(out,"recovered")!=0){fprintf(stderr,"FAIL: empty-content response should retry with thinking disabled, got [%s]\n",out?out:"(null)");fails++;}
    else if(plain_calls!=2){fprintf(stderr,"FAIL: expected original + one disabled-thinking retry, got %d calls\n",plain_calls);fails++;}
    else printf("ok: responses without thinking blocks still retry with thinking disabled\n");
    free(out);

    /* A gateway that IGNORES the disabled-thinking parameter and keeps
       returning thinking-only: the thinking content becomes the answer so
       the agent loop can execute directives embedded in it. */
    setup(VS_PROVIDER_CUSTOM,VS_PROTOCOL_ANTHROPIC,"https://gateway.example.test/v1/messages");
    recover_with_text=0;
    feed("event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n");feed_slice=1;
    snprintf(plain_response,sizeof(plain_response),"%s",
        "{\"content\":[{\"type\":\"thinking\",\"thinking\":\"plan: issue [[VS_TOOL read path=\\\"/tmp\\\"]] now\"}],\"stop_reason\":\"end_turn\",\"usage\":{\"output_tokens\":30}}");
    out=vs_chat(&ctx,"hi");
    if(!out||!strstr(out,"[[VS_TOOL read path=\"/tmp\"]]")){fprintf(stderr,"FAIL: thinking content should become the answer with directives intact, got [%s]\n",out?out:"(null)");fails++;}
    else printf("ok: thinking content becomes the answer when the gateway ignores the retry\n");
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
