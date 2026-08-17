/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <curl/curl.h>

typedef struct { char *p; size_t n; size_t cap; size_t max; int overflow; } Buf;
typedef struct { char *session; size_t cap; } HdrCtx;

static size_t wr(void *ptr,size_t sz,size_t nm,void *ud){
    Buf *b=(Buf*)ud;size_t k,need,newcap;char *q;
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
    }else if(host&&hcap){size_t z=strlen(value);if(z>=hcap)z=hcap-1;memcpy(host,value,z);host[z]=0;}
}

char *vs_http_post_capture_ctx(VSContext *ctx,const char *url,const char *headers[],int nheaders,const char *body,long *status,char *session_id,size_t session_cap){
    CURL *c;struct curl_slist *h=0;Buf b;HdrCtx hc;int i;CURLcode rc;char phost[1024],pauth[1024],redacted[1024],tracebuf[1200];
    b.cap=8192;b.max=(size_t)VS_MAX_HTTP_RESPONSE;b.n=0;b.overflow=0;b.p=(char*)malloc(b.cap);if(!b.p)return NULL;b.p[0]=0;
    if(status)*status=0;
    if(session_id&&session_cap)session_id[0]=0;
    hc.session=session_id;hc.cap=session_cap;
    c=curl_easy_init();if(!c){free(b.p);return NULL;}
    for(i=0;i<nheaders;i++){struct curl_slist *nh=curl_slist_append(h,headers[i]);if(!nh){curl_slist_free_all(h);curl_easy_cleanup(c);free(b.p);return NULL;}h=nh;}
    curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_HTTPHEADER,h);curl_easy_setopt(c,CURLOPT_POSTFIELDS,body?body:"");
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,wr);curl_easy_setopt(c,CURLOPT_WRITEDATA,&b);curl_easy_setopt(c,CURLOPT_HEADERFUNCTION,hdrwr);curl_easy_setopt(c,CURLOPT_HEADERDATA,&hc);curl_easy_setopt(c,CURLOPT_USERAGENT,"vibesolaris/" VS_VERSION);
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,30L);curl_easy_setopt(c,CURLOPT_TIMEOUT,600L);curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
#ifdef CURLOPT_TCP_KEEPALIVE
    curl_easy_setopt(c,CURLOPT_TCP_KEEPALIVE,1L);
#endif
    if(ctx&&ctx->proxy_enabled&&ctx->proxy[0]){
        proxy_parts(ctx->proxy,phost,sizeof(phost),pauth,sizeof(pauth));
        if(phost[0]){curl_easy_setopt(c,CURLOPT_PROXY,phost);curl_easy_setopt(c,CURLOPT_NOPROXY,"");}
        if(pauth[0]){curl_easy_setopt(c,CURLOPT_PROXYUSERPWD,pauth);curl_easy_setopt(c,CURLOPT_PROXYAUTH,(long)CURLAUTH_BASIC);}
        vs_proxy_redacted(ctx,redacted,sizeof(redacted));snprintf(tracebuf,sizeof(tracebuf),"routing HTTP request via %s",redacted);vs_trace(ctx,"proxy",tracebuf);
    }
    rc=curl_easy_perform(c);if(status)curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,status);
    curl_slist_free_all(h);curl_easy_cleanup(c);
    if(rc!=CURLE_OK){
        if(ctx){if(b.overflow)snprintf(tracebuf,sizeof(tracebuf),"HTTP response exceeded %lu MiB safety limit",(unsigned long)(VS_MAX_HTTP_RESPONSE/(1024U*1024U)));else snprintf(tracebuf,sizeof(tracebuf),"HTTP transport failed: %s",curl_easy_strerror(rc));vs_trace(ctx,b.overflow?"limit":"http-error",tracebuf);}
        free(b.p);return NULL;
    }
    return b.p;
}
char *vs_http_post_ctx(VSContext *ctx,const char *url,const char *headers[],int nheaders,const char *body,long *status){return vs_http_post_capture_ctx(ctx,url,headers,nheaders,body,status,NULL,0);}
char *vs_http_post_capture(const char *url,const char *headers[],int nheaders,const char *body,long *status,char *session_id,size_t session_cap){return vs_http_post_capture_ctx(NULL,url,headers,nheaders,body,status,session_id,session_cap);}
char *vs_http_post(const char *url,const char *headers[],int nheaders,const char *body,long *status){return vs_http_post_ctx(NULL,url,headers,nheaders,body,status);}
