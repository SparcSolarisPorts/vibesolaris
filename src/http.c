/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <curl/curl.h>

typedef struct { char *p; size_t n; size_t cap; size_t max; int overflow; } Buf;
typedef struct { char *session; size_t cap; } HdrCtx;

/* Transient transport-level failures: no HTTP response was received, so a
   fresh attempt cannot duplicate server-side work.  Errors after a response
   status arrived are never retried. */
#define VS_HTTP_MAX_ATTEMPTS 3
static int retryable_transport_error(CURLcode rc){
    switch(rc){
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY:
    case CURLE_COULDNT_CONNECT:
    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_GOT_NOTHING:
    case CURLE_RECV_ERROR:
    case CURLE_SEND_ERROR:
    case CURLE_PARTIAL_FILE:
    case CURLE_SSL_CONNECT_ERROR:
        return 1;
    default:
        return 0;
    }
}
static long env_long(const char *name,long def){const char *e=getenv(name);char *ep;long v;if(!e||!*e)return def;v=strtol(e,&ep,10);if(!ep||*ep||v<1)return def;return v;}
static long http_total_timeout(void){return env_long("VIBESOLARIS_HTTP_TIMEOUT",600);}
static long http_low_speed_time(void){return env_long("VIBESOLARIS_HTTP_LOW_SPEED_TIME",240);}
static long http_attempt_pause(int attempt){return attempt==1?1:3;}

/* CURLOPT_TCP_KEEPALIVE is an enum constant, not a macro, so the historical
   "#ifdef" guard was always false and keepalive was never enabled.  Gate on
   the libcurl version instead; half-dead connections across NATs are then
   detected by the kernel rather than burning the whole transfer timeout. */
static void set_keepalive(CURL *c){
#if LIBCURL_VERSION_NUM >= 0x071900
    curl_easy_setopt(c,CURLOPT_TCP_KEEPALIVE,1L);
#endif
#if LIBCURL_VERSION_NUM >= 0x072500
    curl_easy_setopt(c,CURLOPT_TCP_KEEPIDLE,60L);
    curl_easy_setopt(c,CURLOPT_TCP_KEEPINTVL,30L);
#endif
}

/* Phase-level failure report: which curl stage consumed the time and how many
   bytes arrived, so a stall is visible as dns/connect/ttfb/total instead of a
   bare "timed out". */
static void trace_transport_diagnostics(CURL *c,VSContext *ctx,const char *what,CURLcode rc){
    char buf[1200];
    double total=0,nl=0,ct=0,st=0;
    long code=0;
    curl_off_t dl=0;
    if(!ctx||!c)return;
    curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);
    curl_easy_getinfo(c,CURLINFO_TOTAL_TIME,&total);
    curl_easy_getinfo(c,CURLINFO_NAMELOOKUP_TIME,&nl);
    curl_easy_getinfo(c,CURLINFO_CONNECT_TIME,&ct);
    curl_easy_getinfo(c,CURLINFO_STARTTRANSFER_TIME,&st);
#if LIBCURL_VERSION_NUM >= 0x073700
    curl_easy_getinfo(c,CURLINFO_SIZE_DOWNLOAD_T,&dl);
#else
    {double d=0;curl_easy_getinfo(c,CURLINFO_SIZE_DOWNLOAD,&d);dl=(curl_off_t)d;}
#endif
    snprintf(buf,sizeof(buf),
        "transport failure during %s: %s (curl %d); dns=%.2fs connect=%.2fs ttfb=%.2fs total=%.2fs received=%lld bytes http_status=%ld",
        what?what:"request",curl_easy_strerror(rc),(int)rc,(double)nl,(double)ct,(double)st,(double)total,(long long)dl,code);
    vs_trace(ctx,"http-error",buf);
}

#if LIBCURL_VERSION_NUM >= 0x072000
static int cancel_xfer(void *ud,curl_off_t dltotal,curl_off_t dlnow,curl_off_t ultotal,curl_off_t ulnow){
    VSContext *ctx=(VSContext*)ud;(void)dltotal;(void)dlnow;(void)ultotal;(void)ulnow;return vs_cancel_requested(ctx);
}
#else
static int cancel_progress(void *ud,double dltotal,double dlnow,double ultotal,double ulnow){
    VSContext *ctx=(VSContext*)ud;(void)dltotal;(void)dlnow;(void)ultotal;(void)ulnow;return vs_cancel_requested(ctx);
}
#endif

static size_t wr(void *ptr,size_t sz,size_t nm,void *ud){
    Buf *b=(void*)ud;size_t k,need,newcap;char *q;
    if(!b||sz==0||nm==0)return 0;
    if(nm>((size_t)-1)/sz){b->overflow=1;return 0;}k=sz*nm;
    if(k>b->max||b->n>b->max-k){b->overflow=1;return 0;}
    need=b->n+k+1;
    if(need>b->cap){
        newcap=b->cap?b->cap:8192;
        while(newcap<need){if(newcap>b->max/2){newcap=b->max+1;break;}newcap*=2;}
        if(newcap<need||newcap>b->max+1){b->overflow=1;return 0;}
        q=(char*)realloc(b->p,newcap);if(!q)return 0;b->p=q;b->cap=newcap;
    }
    memcpy(b->p+b->n,ptr,k);b->n+=k;b->p[b->n]=0;return k;
}

typedef struct { VSHTTPChunkCallback callback; void *userdata; size_t received; } StreamCtx;
static size_t stream_wr(void *ptr,size_t sz,size_t nm,void *ud){
    StreamCtx *s=(StreamCtx*)ud;size_t n;
    if(!s||!s->callback||sz==0||nm==0)return 0;
    if(nm>((size_t)-1)/sz)return 0;
    n=sz*nm;
    if(s->callback(s->userdata,(const char*)ptr,n)!=0)return 0;
    s->received+=n;
    return n;
}
static int ci_prefix(const char *s,size_t n,const char *p){size_t i,m=strlen(p);if(n<m)return 0;for(i=0;i<m;i++)if(tolower((unsigned char)s[i])!=tolower((unsigned char)p[i]))return 0;return 1;}
static size_t hdrwr(void *ptr,size_t sz,size_t nm,void *ud){
    HdrCtx *h=(HdrCtx*)ud;char *s=(char*)ptr;size_t n=sz*nm;const char *key="Mcp-Session-Id:";size_t k=strlen(key),a,b,c;
    if(h&&h->session&&h->cap&&ci_prefix(s,n,key)){a=k;while(a<n&&(s[a]==' '||s[a]=='\t'))a++;b=n;while(b>a&&(s[b-1]=='\r'||s[b-1]=='\n'||s[b-1]==' '||s[b-1]=='\t'))b--;c=b-a;if(c>=h->cap)c=h->cap-1;memcpy(h->session,s+a,c);h->session[c]=0;}
    return n;
}

/* Split the documented VibeSolaris form user:pass@host:port without ever
   copying credentials into the activity trace.  A scheme prefix is accepted
   as a convenience, although the GUI documents the simpler forms. */
static void proxy_parts(const char *value,char *host,size_t hcap,char *auth,size_t acap){
    const char *p,*at,*scheme;size_t prefix=0,n;
    if(host&&hcap)host[0]=0;
    if(auth&&acap)auth[0]=0;
    if(!value||!*value)return;
    p=value;scheme=strstr(value,"://");if(scheme){prefix=(size_t)(scheme-value)+3;p=scheme+3;}at=strrchr(p,'@');
    if(at){
        if(auth&&acap){n=(size_t)(at-p);if(n>=acap)n=acap-1;memcpy(auth,p,n);auth[n]=0;}
        if(host&&hcap){if(prefix){n=prefix;if(n>=hcap)n=hcap-1;memcpy(host,value,n);host[n]=0;strncat(host,at+1,hcap-strlen(host)-1);}else{strncpy(host,at+1,hcap-1);host[hcap-1]=0;}}
    }else if(host&&hcap){size_t z=strlen(p);if(z>=hcap)z=hcap-1;memcpy(host,p,z);host[z]=0;}
}

static void apply_common_opts(CURL *c,VSContext *ctx){
    curl_easy_setopt(c,CURLOPT_USERAGENT,"vibesolaris/" VS_VERSION);
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,30L);curl_easy_setopt(c,CURLOPT_TIMEOUT,http_total_timeout());
    curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
    /* Abort transfers that deliver nothing for a long stretch: catches
       half-dead connections that otherwise sit until the total timeout.
       A healthy reasoning stream stays well above 1 byte/s. */
    curl_easy_setopt(c,CURLOPT_LOW_SPEED_LIMIT,1L);curl_easy_setopt(c,CURLOPT_LOW_SPEED_TIME,http_low_speed_time());
    set_keepalive(c);
    if(ctx){curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
#if LIBCURL_VERSION_NUM >= 0x072000
        curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,cancel_xfer);curl_easy_setopt(c,CURLOPT_XFERINFODATA,ctx);
#else
        curl_easy_setopt(c,CURLOPT_PROGRESSFUNCTION,cancel_progress);curl_easy_setopt(c,CURLOPT_PROGRESSDATA,ctx);
#endif
    }
}
static void apply_proxy_opts(CURL *c,VSContext *ctx){
    char phost[1024],pauth[1024],redacted[1024],tracebuf[1200];
    if(!ctx||!ctx->proxy_enabled||!ctx->proxy[0])return;
    proxy_parts(ctx->proxy,phost,sizeof(phost),pauth,sizeof(pauth));
    if(phost[0]){curl_easy_setopt(c,CURLOPT_PROXY,phost);curl_easy_setopt(c,CURLOPT_NOPROXY,"");}
    if(pauth[0]){curl_easy_setopt(c,CURLOPT_PROXYUSERPWD,pauth);curl_easy_setopt(c,CURLOPT_PROXYAUTH,(long)CURLAUTH_BASIC);}
    vs_proxy_redacted(ctx,redacted,sizeof(redacted));snprintf(tracebuf,sizeof(tracebuf),"routing HTTP request via %s",redacted);vs_trace(ctx,"proxy",tracebuf);
}

char *vs_http_post_capture_ctx(VSContext *ctx,const char *url,const char *headers[],int nheaders,const char *body,long *status,char *session_id,size_t session_cap){
    int attempt;
    if(status)*status=0;
    if(session_id&&session_cap)session_id[0]=0;
    for(attempt=1;attempt<=VS_HTTP_MAX_ATTEMPTS;attempt++){
        CURL *c;struct curl_slist *h=0;Buf b;HdrCtx hc;int i;CURLcode rc;
        b.cap=8192;b.max=(size_t)VS_MAX_HTTP_RESPONSE;b.n=0;b.overflow=0;b.p=(char*)malloc(b.cap);if(!b.p)return NULL;b.p[0]=0;
        hc.session=session_id;hc.cap=session_cap;
        c=curl_easy_init();if(!c){free(b.p);return NULL;}
        for(i=0;i<nheaders;i++){struct curl_slist *nh=curl_slist_append(h,headers[i]);if(!nh){curl_slist_free_all(h);curl_easy_cleanup(c);free(b.p);return NULL;}h=nh;}
        curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_HTTPHEADER,h);curl_easy_setopt(c,CURLOPT_POSTFIELDS,body?body:"");
        curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,wr);curl_easy_setopt(c,CURLOPT_WRITEDATA,&b);curl_easy_setopt(c,CURLOPT_HEADERFUNCTION,hdrwr);curl_easy_setopt(c,CURLOPT_HEADERDATA,&hc);
        apply_common_opts(c,ctx);apply_proxy_opts(c,ctx);
        if(ctx&&vs_cancel_requested(ctx)){curl_slist_free_all(h);curl_easy_cleanup(c);free(b.p);vs_trace(ctx,"cancel","HTTP request cancelled before transfer");return NULL;}
        rc=curl_easy_perform(c);if(status)curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,status);
        if(rc==CURLE_OK){curl_slist_free_all(h);curl_easy_cleanup(c);return b.p;}
        if(ctx){
            long code=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);
            if(attempt<VS_HTTP_MAX_ATTEMPTS&&code==0&&b.overflow==0&&retryable_transport_error(rc)&&!(vs_cancel_requested(ctx)&&rc==CURLE_ABORTED_BY_CALLBACK)){
                char tracebuf[1200];
                trace_transport_diagnostics(c,ctx,"request",rc);
                snprintf(tracebuf,sizeof(tracebuf),"no response received; retrying (%d/%d) after %lds",attempt+1,VS_HTTP_MAX_ATTEMPTS,(long)http_attempt_pause(attempt));
                vs_trace(ctx,"http-retry",tracebuf);
                curl_slist_free_all(h);curl_easy_cleanup(c);free(b.p);
                sleep((unsigned)http_attempt_pause(attempt));
                if(vs_cancel_requested(ctx)){vs_trace(ctx,"cancel","HTTP retry aborted by user");return NULL;}
                continue;
            }
            if(vs_cancel_requested(ctx)&&rc==CURLE_ABORTED_BY_CALLBACK){char tracebuf[1200];snprintf(tracebuf,sizeof(tracebuf),"HTTP request cancelled by user");vs_trace(ctx,"cancel",tracebuf);}
            else if(b.overflow){char tracebuf[1200];snprintf(tracebuf,sizeof(tracebuf),"HTTP response exceeded %lu MiB safety limit",(unsigned long)(VS_MAX_HTTP_RESPONSE/(1024U*1024U)));vs_trace(ctx,"limit",tracebuf);}
            else trace_transport_diagnostics(c,ctx,"request",rc);
        }
        curl_slist_free_all(h);curl_easy_cleanup(c);free(b.p);
        return NULL;
    }
    return NULL;
}
int vs_http_post_stream_ctx(VSContext *ctx,const char *url,const char *headers[],int nheaders,const char *body,long *status,VSHTTPChunkCallback callback,void *userdata){
    int attempt;
    if(!url||!callback)return -1;if(status)*status=0;
    for(attempt=1;attempt<=VS_HTTP_MAX_ATTEMPTS;attempt++){
        CURL *c;struct curl_slist *h=0;StreamCtx s;int i;CURLcode rc;
        s.callback=callback;s.userdata=userdata;s.received=0;
        c=curl_easy_init();if(!c)return -1;
        for(i=0;i<nheaders;i++){struct curl_slist *nh=curl_slist_append(h,headers[i]);if(!nh){curl_slist_free_all(h);curl_easy_cleanup(c);return -1;}h=nh;}
        curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_HTTPHEADER,h);curl_easy_setopt(c,CURLOPT_POSTFIELDS,body?body:"");curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,stream_wr);curl_easy_setopt(c,CURLOPT_WRITEDATA,&s);
        apply_common_opts(c,ctx);apply_proxy_opts(c,ctx);
        if(ctx&&vs_cancel_requested(ctx)){curl_slist_free_all(h);curl_easy_cleanup(c);return -2;}
        rc=curl_easy_perform(c);if(status)curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,status);
        if(rc==CURLE_OK){curl_slist_free_all(h);curl_easy_cleanup(c);return 0;}
        if(ctx){
            long code=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);
            /* A mid-stream retry would duplicate content the caller has
               already consumed, so only retry when no byte arrived. */
            if(attempt<VS_HTTP_MAX_ATTEMPTS&&code==0&&s.received==0&&retryable_transport_error(rc)&&!(vs_cancel_requested(ctx)&&rc==CURLE_ABORTED_BY_CALLBACK)){
                char tracebuf[1200];
                trace_transport_diagnostics(c,ctx,"streaming request",rc);
                snprintf(tracebuf,sizeof(tracebuf),"stream produced no data; retrying (%d/%d) after %lds",attempt+1,VS_HTTP_MAX_ATTEMPTS,(long)http_attempt_pause(attempt));
                vs_trace(ctx,"http-retry",tracebuf);
                curl_slist_free_all(h);curl_easy_cleanup(c);
                sleep((unsigned)http_attempt_pause(attempt));
                if(vs_cancel_requested(ctx)){vs_trace(ctx,"cancel","streaming HTTP retry aborted by user");return -2;}
                continue;
            }
            if(vs_cancel_requested(ctx)&&rc==CURLE_ABORTED_BY_CALLBACK)vs_trace(ctx,"cancel","streaming HTTP request cancelled by user");
            else trace_transport_diagnostics(c,ctx,"streaming request",rc);
        }
        curl_slist_free_all(h);curl_easy_cleanup(c);
        return -1;
    }
    return -1;
}
char *vs_http_post_ctx(VSContext *ctx,const char *url,const char *headers[],int nheaders,const char *body,long *status){return vs_http_post_capture_ctx(ctx,url,headers,nheaders,body,status,NULL,0);}
char *vs_http_post(const char *url,const char *headers[],int nheaders,const char *body,long *status){return vs_http_post_ctx(NULL,url,headers,nheaders,body,status);}
char *vs_http_get_ctx(VSContext *ctx,const char *url,long *status,size_t max_bytes){
    CURL *c;Buf b;CURLcode rc;char phost[1024],pauth[1024],redacted[1024],tracebuf[1200];
    if(!url||!*url)return NULL;
    if(max_bytes<4096)max_bytes=4096;
    if(max_bytes>(size_t)VS_MAX_HTTP_RESPONSE)max_bytes=(size_t)VS_MAX_HTTP_RESPONSE;
    b.cap=8192;if(b.cap>max_bytes+1)b.cap=max_bytes+1;b.max=max_bytes;b.n=0;b.overflow=0;
    b.p=(char*)malloc(b.cap);if(!b.p)return NULL;b.p[0]=0;
    if(status)*status=0;
    c=curl_easy_init();if(!c){free(b.p);return NULL;}
    curl_easy_setopt(c,CURLOPT_URL,url);
    curl_easy_setopt(c,CURLOPT_HTTPGET,1L);
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,wr);curl_easy_setopt(c,CURLOPT_WRITEDATA,&b);
    curl_easy_setopt(c,CURLOPT_USERAGENT,"Mozilla/5.0 (X11; SunOS) VibeSolaris/" VS_VERSION);
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,20L);curl_easy_setopt(c,CURLOPT_TIMEOUT,60L);curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_MAXREDIRS,5L);
    set_keepalive(c);
#ifdef CURLOPT_ACCEPT_ENCODING
    curl_easy_setopt(c,CURLOPT_ACCEPT_ENCODING,"");
#endif
    if(ctx){
        curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
#if LIBCURL_VERSION_NUM >= 0x072000
        curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,cancel_xfer);curl_easy_setopt(c,CURLOPT_XFERINFODATA,ctx);
#else
        curl_easy_setopt(c,CURLOPT_PROGRESSFUNCTION,cancel_progress);curl_easy_setopt(c,CURLOPT_PROGRESSDATA,ctx);
#endif
    }
    if(ctx&&ctx->proxy_enabled&&ctx->proxy[0]){proxy_parts(ctx->proxy,phost,sizeof(phost),pauth,sizeof(pauth));if(phost[0]){curl_easy_setopt(c,CURLOPT_PROXY,phost);curl_easy_setopt(c,CURLOPT_NOPROXY,"");}if(pauth[0]){curl_easy_setopt(c,CURLOPT_PROXYUSERPWD,pauth);curl_easy_setopt(c,CURLOPT_PROXYAUTH,(long)CURLAUTH_BASIC);}vs_proxy_redacted(ctx,redacted,sizeof(redacted));snprintf(tracebuf,sizeof(tracebuf),"routing HTTP request via %s",redacted);vs_trace(ctx,"proxy",tracebuf);}
    if(ctx&&vs_cancel_requested(ctx)){curl_easy_cleanup(c);free(b.p);return NULL;}
    rc=curl_easy_perform(c);if(status)curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,status);curl_easy_cleanup(c);
    if(rc!=CURLE_OK){
        if(ctx){
            if(vs_cancel_requested(ctx)&&rc==CURLE_ABORTED_BY_CALLBACK)vs_trace(ctx,"cancel","HTTP request cancelled by user");
            else if(b.overflow)vs_trace(ctx,"limit","HTTP response exceeded safety limit");
            else vs_trace(ctx,"http-error","HTTP transport failed");
        }
        free(b.p);return NULL;
    }
    return b.p;
}
