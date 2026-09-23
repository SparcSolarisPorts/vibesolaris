/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *d(const char *s){size_t n=strlen(s);char *p=(char*)malloc(n+1);if(p)memcpy(p,s,n+1);return p;}
void vs_trace(VSContext*c,const char*k,const char*x){(void)c;(void)k;(void)x;}
char *vs_compact_text_limit(const char*s,size_t z,const char*r){size_t n;if(!s)return d("");(void)r;n=strlen(s);if(n<=z)return d(s);{char *p=(char*)malloc(z+1);memcpy(p,s,z);p[z]=0;return p;}}
char *vs_http_get_ctx(VSContext*c,const char*u,long*status,size_t max){(void)c;(void)max;*status=200;if(strstr(u,"duckduckgo")||strstr(u,"search"))return d("<html><a class=\"result__a\" href=\"//duckduckgo.com/l/?uddg=https%3A%2F%2Fexample.com%2Fsolaris\">Solaris &amp; SPARC</a><div class=\"result__snippet\">Useful <b>documentation</b>.</div></html>");return d("<html><style>bad{}</style><script>bad()</script><body><h1>Manual</h1><p>Solaris page text.</p></body></html>");}
int main(void){VSContext c;char *s,*f;memset(&c,0,sizeof(c));putenv("VIBESOLARIS_SEARCH_URL=https://search.invalid/?q=%s");s=vs_web_search(&c,"solaris sparc",5);if(!s||!strstr(s,"Solaris & SPARC")||!strstr(s,"https://example.com/solaris")||!strstr(s,"Useful documentation")){fprintf(stderr,"bad search parse: %s\n",s?s:"null");free(s);return 1;}free(s);f=vs_web_fetch(&c,"https://example.com/solaris",65536);if(!f||!strstr(f,"Manual Solaris page text.")||strstr(f,"bad()")){fprintf(stderr,"bad fetch parse: %s\n",f?f:"null");free(f);return 2;}printf("web parse ok: %s\n",f);free(f);return 0;}
