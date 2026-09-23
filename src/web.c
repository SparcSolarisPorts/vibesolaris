/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#define VS_WEB_HTML_MAX (4U*1024U*1024U)

typedef struct { char *p; size_t n; size_t cap; } WBuf;
static char *wdup(const char *s){size_t n=s?strlen(s):0;char *p=(char*)malloc(n+1);if(!p)return NULL;if(s)memcpy(p,s,n);p[n]=0;return p;}

static int wb_init(WBuf *b,size_t cap){b->p=(char*)malloc(cap);if(!b->p)return -1;b->n=0;b->cap=cap;b->p[0]=0;return 0;}
static int wb_grow(WBuf *b,size_t add){char *q;size_t nc;if(b->n+add+1<=b->cap)return 0;nc=b->cap?b->cap:1024;while(nc<b->n+add+1)nc*=2;q=(char*)realloc(b->p,nc);if(!q)return -1;b->p=q;b->cap=nc;return 0;}
static int wb_add(WBuf *b,const char *s){size_t n=s?strlen(s):0;if(wb_grow(b,n)!=0)return -1;if(n)memcpy(b->p+b->n,s,n);b->n+=n;b->p[b->n]=0;return 0;}
static int wb_addf(WBuf *b,const char *fmt,...){char t[2048];va_list ap;va_start(ap,fmt);vsnprintf(t,sizeof(t),fmt,ap);va_end(ap);return wb_add(b,t);}

static int is_unreserved(unsigned char c){return isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~';}
static char *url_encode(const char *s){static const char h[]="0123456789ABCDEF";size_t i,n=0;char *o,*p;if(!s)return NULL;for(i=0;s[i];i++)n+=is_unreserved((unsigned char)s[i])?1:3;o=(char*)malloc(n+1);if(!o)return NULL;p=o;for(i=0;s[i];i++){unsigned char c=(unsigned char)s[i];if(is_unreserved(c))*p++=(char)c;else{*p++='%';*p++=h[c>>4];*p++=h[c&15];}}*p=0;return o;}
static int hx(int c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static void url_decode_inplace(char *s){char *r=s,*w=s;while(r&&*r){if(*r=='%'&&hx((unsigned char)r[1])>=0&&hx((unsigned char)r[2])>=0){*w++=(char)((hx((unsigned char)r[1])<<4)|hx((unsigned char)r[2]));r+=3;}else if(*r=='+'){*w++=' ';r++;}else *w++=*r++;}if(s)*w=0;}
static void html_decode_inplace(char *s){char *r=s,*w=s;while(r&&*r){if(*r=='&'){if(!strncmp(r,"&amp;",5)){*w++='&';r+=5;continue;}if(!strncmp(r,"&quot;",6)){*w++='"';r+=6;continue;}if(!strncmp(r,"&#x27;",6)||!strncmp(r,"&#39;",5)){*w++='\'';r+=r[2]=='x'?6:5;continue;}if(!strncmp(r,"&lt;",4)){*w++='<';r+=4;continue;}if(!strncmp(r,"&gt;",4)){*w++='>';r+=4;continue;}if(!strncmp(r,"&nbsp;",6)){*w++=' ';r+=6;continue;}}*w++=*r++;}if(s)*w=0;}
static void strip_tags_inplace(char *s){char *r=s,*w=s;int tag=0,pending_space=0;while(r&&*r){if(*r=='<'){if(w>s)pending_space=1;tag=1;r++;continue;}if(*r=='>'&&tag){tag=0;r++;continue;}if(!tag){if(pending_space&&w>s&&!isspace((unsigned char)w[-1])&&!isspace((unsigned char)*r))*w++=' ';pending_space=0;*w++=*r;}r++;}if(s)*w=0;html_decode_inplace(s);}
static char *slice_dup(const char *a,const char *b){size_t n;char *o;if(!a||!b||b<a)return NULL;n=(size_t)(b-a);o=(char*)malloc(n+1);if(!o)return NULL;memcpy(o,a,n);o[n]=0;return o;}
static char *attr_value(const char *tag,const char *name){char pat[64];const char *p,*e;char q;snprintf(pat,sizeof(pat),"%s=",name);p=strstr(tag,pat);if(!p)return NULL;p+=strlen(pat);while(*p==' '||*p=='\t')p++;if(*p!='\''&&*p!='"')return NULL;q=*p++;e=strchr(p,q);if(!e)return NULL;return slice_dup(p,e);}
static char *duck_real_url(char *u){char *p;if(!u)return NULL;html_decode_inplace(u);if(!strncmp(u,"//",2)){char *q=(char*)malloc(strlen(u)+7);if(!q)return u;sprintf(q,"https:%s",u);free(u);u=q;}p=strstr(u,"uddg=");if(p){char *v,*amp;p+=5;amp=strchr(p,'&');v=amp?slice_dup(p,amp):wdup(p);if(v){url_decode_inplace(v);free(u);return v;}}return u;}
static void normalize_space(char *s){char *r=s,*w=s;int sp=0;while(r&&*r){unsigned char c=(unsigned char)*r++;if(isspace(c)){sp=1;continue;}if(sp&&w>s)*w++=' ';sp=0;*w++=(char)c;}if(s)*w=0;}

char *vs_web_search(VSContext *ctx,const char *query,int max_results){
    char *enc,*url,*html,*p,*a,*tag_end,*close,*title,*href,*snip=0,*real;long status=0;int count=0;WBuf out;const char *endpoint=getenv("VIBESOLARIS_SEARCH_URL");
    if(!query||!*query)return wdup("ERROR: web search requires a query");
    if(max_results<1)max_results=5;
    if(max_results>12)max_results=12;
    enc=url_encode(query);if(!enc)return NULL;
    if(!endpoint||!*endpoint)endpoint="https://html.duckduckgo.com/html/?q=%s";
    url=(char*)malloc(strlen(endpoint)+strlen(enc)+64);if(!url){free(enc);return NULL;}
    if(strstr(endpoint,"%s"))sprintf(url,endpoint,enc);else sprintf(url,"%s%s",endpoint,enc);free(enc);
    if(ctx)vs_trace(ctx,"web-search",query);
    html=vs_http_get_ctx(ctx,url,&status,VS_WEB_HTML_MAX);free(url);
    if(!html){return wdup("ERROR: web search request failed");}
    if(status<200||status>=400){char b[128];snprintf(b,sizeof(b),"ERROR: web search HTTP status %ld",status);free(html);return wdup(b);}
    if(wb_init(&out,4096)!=0){free(html);return NULL;}
    wb_addf(&out,"WEB_SEARCH query=\"%s\"\n",query);
    p=html;
    while(count<max_results && (a=strstr(p,"result__a"))!=NULL){
        const char *tag_start=a;while(tag_start>html&&*tag_start!='<')tag_start--;if(*tag_start!='<'){p=a+9;continue;}
        tag_end=strchr(a,'>');if(!tag_end)break;
        {char *tag=slice_dup(tag_start,tag_end+1);href=tag?attr_value(tag,"href"):NULL;free(tag);}
        close=strstr(tag_end,"</a>");if(!close){free(href);p=tag_end+1;continue;}
        title=slice_dup(tag_end+1,close);if(title){strip_tags_inplace(title);normalize_space(title);}real=duck_real_url(href);
        {char *ss=strstr(close,"result__snippet");if(ss){char *se=strchr(ss,'>');char *sc=se?strstr(se,"</"):NULL;if(se&&sc&&sc-se<4096){snip=slice_dup(se+1,sc);if(snip){strip_tags_inplace(snip);normalize_space(snip);}}}}
        if(title&&*title&&real&&*real){count++;wb_addf(&out,"%d. %s\n   %s\n",count,title,real);if(snip&&*snip)wb_addf(&out,"   %s\n",snip);}
        free(title);free(real);free(snip);snip=NULL;p=close+4;
    }
    if(count==0)wb_add(&out,"No parseable results were returned. The search endpoint may have changed or blocked automated HTML access. Set VIBESOLARIS_SEARCH_URL to another compatible HTML search endpoint if needed.\n");
    free(html);return out.p;
}


static void remove_html_block(char *s,const char *tag)
{
    char open[48],close[48];char *a,*b;
    if(!s||!tag)return;
    snprintf(open,sizeof(open),"<%s",tag);snprintf(close,sizeof(close),"</%s>",tag);
    for(;;){
        a=strstr(s,open);if(!a)break;b=strstr(a,close);
        if(!b){*a=0;break;}b+=strlen(close);memmove(a,b,strlen(b)+1);
    }
}

char *vs_web_fetch(VSContext *ctx,const char *url,size_t max_text)
{
    char *html,*out;long status=0;size_t n;
    if(!url||(strncmp(url,"http://",7)&&strncmp(url,"https://",8)))return wdup("ERROR: web_fetch requires an absolute http(s) URL");
    if(max_text<4096)max_text=4096;
    if(max_text>512U*1024U)max_text=512U*1024U;
    if(ctx)vs_trace(ctx,"web-fetch",url);
    html=vs_http_get_ctx(ctx,url,&status,2U*1024U*1024U);
    if(!html)return wdup("ERROR: web page request failed");
    if(status<200||status>=400){char b[160];snprintf(b,sizeof(b),"ERROR: web page HTTP status %ld",status);free(html);return wdup(b);}
    remove_html_block(html,"script");remove_html_block(html,"style");remove_html_block(html,"noscript");
    strip_tags_inplace(html);normalize_space(html);n=strlen(html);
    if(n>max_text)out=vs_compact_text_limit(html,max_text,"web page text compacted");else out=wdup(html);
    free(html);return out;
}
