/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>

typedef struct { char *p; size_t n; } Buf;
typedef struct { char *session; size_t cap; } HdrCtx;

static size_t wr(void *ptr,size_t sz,size_t nm,void *ud){
    Buf *b=(Buf*)ud;size_t k=sz*nm;char *q=(char*)realloc(b->p,b->n+k+1);if(!q)return 0;b->p=q;memcpy(b->p+b->n,ptr,k);b->n+=k;b->p[b->n]=0;return k;
}
static int ci_prefix(const char *s,size_t n,const char *p){size_t i,m=strlen(p);if(n<m)return 0;for(i=0;i<m;i++)if(tolower((unsigned char)s[i])!=tolower((unsigned char)p[i]))return 0;return 1;}
static size_t hdrwr(void *ptr,size_t sz,size_t nm,void *ud){
    HdrCtx *h=(HdrCtx*)ud;char *s=(char*)ptr;size_t n=sz*nm;const char *key="Mcp-Session-Id:";size_t k=strlen(key),a,b,c;
    if(h&&h->session&&h->cap&&ci_prefix(s,n,key)){a=k;while(a<n&&(s[a]==' '||s[a]=='\t'))a++;b=n;while(b>a&&(s[b-1]=='\r'||s[b-1]=='\n'||s[b-1]==' '||s[b-1]=='\t'))b--;c=b-a;if(c>=h->cap)c=h->cap-1;memcpy(h->session,s+a,c);h->session[c]=0;}
    return n;
}
char *vs_http_post_capture(const char *url,const char *headers[],int nheaders,const char *body,long *status,char *session_id,size_t session_cap){
    CURL *c;struct curl_slist *h=0;Buf b;HdrCtx hc;int i;CURLcode rc;
    b.p=(char*)malloc(1);b.n=0;if(!b.p)return NULL;b.p[0]=0;if(session_id&&session_cap)session_id[0]=0;hc.session=session_id;hc.cap=session_cap;
    c=curl_easy_init();if(!c){free(b.p);return NULL;}
    for(i=0;i<nheaders;i++)h=curl_slist_append(h,headers[i]);
    curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_HTTPHEADER,h);curl_easy_setopt(c,CURLOPT_POSTFIELDS,body?body:"");
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,wr);curl_easy_setopt(c,CURLOPT_WRITEDATA,&b);curl_easy_setopt(c,CURLOPT_HEADERFUNCTION,hdrwr);curl_easy_setopt(c,CURLOPT_HEADERDATA,&hc);curl_easy_setopt(c,CURLOPT_USERAGENT,"vibesolaris/" VS_VERSION);
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);curl_easy_setopt(c,CURLOPT_TIMEOUT,180L);
    rc=curl_easy_perform(c);if(status)curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,status);
    curl_slist_free_all(h);curl_easy_cleanup(c);if(rc!=CURLE_OK){free(b.p);return NULL;}return b.p;
}
char *vs_http_post(const char *url,const char *headers[],int nheaders,const char *body,long *status){return vs_http_post_capture(url,headers,nheaders,body,status,NULL,0);}
