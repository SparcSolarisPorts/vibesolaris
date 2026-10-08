/* SPDX-License-Identifier: Unlicense */
/* Verifies Claude OAuth tokens round-trip through the encrypted config and are
   excluded from the plain-text export. Collaborators outside config.c are stubbed. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static VSContext src, dst;
static int fails=0;
#define CHECK(c,m) do{ if(!(c)){fprintf(stderr,"FAIL: %s\n",m);fails++;} else printf("ok: %s\n",m);}while(0)
int main(void){
    char *enc,*tmpf="/tmp/vs-plain-export.conf";
    FILE *f;char line[4096];int saw_at_plain=0,saw_claude_cfg_plain=0;
    memset(&src,0,sizeof(src));
    snprintf(src.claude_oauth.client_id,sizeof(src.claude_oauth.client_id),"11111111-2222-3333-4444-555555555555");
    snprintf(src.claude_oauth.authorize_url,sizeof(src.claude_oauth.authorize_url),"https://console.anthropic.com/oauth/authorize");
    snprintf(src.claude_oauth.token_url,sizeof(src.claude_oauth.token_url),"https://api.anthropic.com/v1/oauth/token");
    snprintf(src.claude_oauth.scopes,sizeof(src.claude_oauth.scopes),"api.read");
    snprintf(src.claude_oauth.redirect_uri,sizeof(src.claude_oauth.redirect_uri),"http://127.0.0.1:14556/callback");
    snprintf(src.claude_oauth.access_token,sizeof(src.claude_oauth.access_token),"SECRET-AT-123");
    snprintf(src.claude_oauth.refresh_token,sizeof(src.claude_oauth.refresh_token),"SECRET-RT-456");
    snprintf(src.claude_oauth.token_type,sizeof(src.claude_oauth.token_type),"Bearer");
    src.claude_oauth.expires_at=4102444800L;
    snprintf(src.oauth.client_id,sizeof(src.oauth.client_id),"openai-app");

    /* encrypted-store serialization includes Claude tokens */
    enc=vs_config_serialize(&src);
    CHECK(enc&&strstr(enc,"claude_oauth_access_token=SECRET-AT-123"),"encrypted serialization includes Claude access token");
    CHECK(enc&&strstr(enc,"claude_oauth_refresh_token=SECRET-RT-456"),"encrypted serialization includes Claude refresh token");

    /* plain-text legacy export excludes Claude tokens but keeps settings */
    CHECK(vs_save_config(&src,tmpf)==0,"plain-text export writes");
    f=fopen(tmpf,"r");
    while(f&&fgets(line,sizeof(line),f)){
        if(strstr(line,"SECRET-AT-123")||strstr(line,"SECRET-RT-456"))saw_at_plain=1;
        if(strstr(line,"claude_oauth_client_id="))saw_claude_cfg_plain=1;
    }
    if(f)fclose(f);
    CHECK(!saw_at_plain,"plain-text export contains no Claude tokens");
    CHECK(saw_claude_cfg_plain,"plain-text export keeps Claude non-secret settings");

    /* round trip: encrypted text restores Claude slot exactly and leaves OpenAI slot alone */
    memset(&dst,0,sizeof(dst));
    CHECK(vs_config_apply_text(&dst,enc)==0,"config text applies");
    CHECK(strcmp(dst.claude_oauth.access_token,"SECRET-AT-123")==0,"Claude access token round-trips");
    CHECK(strcmp(dst.claude_oauth.refresh_token,"SECRET-RT-456")==0,"Claude refresh token round-trips");
    CHECK(strcmp(dst.claude_oauth.client_id,"11111111-2222-3333-4444-555555555555")==0,"Claude client ID round-trips");
    CHECK(dst.claude_oauth.expires_at==4102444800L,"Claude expiry round-trips");
    CHECK(strcmp(dst.oauth.client_id,"openai-app")==0,"OpenAI client ID round-trips alongside Claude");
    CHECK(dst.oauth.access_token[0]==0,"OpenAI slot does not inherit Claude tokens");
    free(enc);
    if(fails){fprintf(stderr,"%d failure(s)\n",fails);return 1;}
    printf("all config Claude tests passed\n");return 0;
}

/* ---- stubs ---- */
int vs_global_config_save(const VSContext *c,char *e,size_t n){(void)c;if(e&&n)e[0]=0;return 0;}
char *vs_read_file(const char *p){(void)p;return NULL;}
void vs_refresh_cache_key(VSContext *c){(void)c;}
void vs_trace(VSContext *c,const char *k,const char *d){(void)c;(void)k;(void)d;}
char *vs_http_post_ctx(VSContext *c,const char *u,const char *h[],int n,const char *b,long *s){(void)c;(void)u;(void)h;(void)n;(void)b;*s=0;return NULL;}
int vs_open_url(const char *u){(void)u;return 1;}
