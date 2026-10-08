/* SPDX-License-Identifier: Unlicense */
/* Unit tests for provider-scoped OAuth, with the Claude compliance guard rails.
   Network and persistence are stubbed; no real provider is contacted. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static VSContext ctx;               /* static: VSContext is large */
static int http_calls=0;
static char last_url[1024];
static char last_body[2048];
static int persist_calls=0;
static const char *canned_token_json="{\"access_token\":\"AT-NEW\",\"refresh_token\":\"RT-NEW\",\"expires_in\":3600,\"token_type\":\"Bearer\"}";

/* ---- stubs for collaborators outside oauth.c ---- */
char *vs_http_post_ctx(VSContext *c,const char *url,const char *headers[],int nh,const char *body,long *status)
{
    char *r;(void)c;(void)headers;(void)nh;
    http_calls++;
    snprintf(last_url,sizeof(last_url),"%s",url);
    snprintf(last_body,sizeof(last_body),"%s",body);
    *status=200;
    r=(char*)malloc(strlen(canned_token_json)+1);strcpy(r,canned_token_json);return r;
}
int vs_persist_settings(VSContext *c){(void)c;persist_calls++;return 0;}
int vs_open_url(const char *url){(void)url;return 1;}   /* 1 = no browser; not an error for the flow */

static int failures=0;
#define CHECK(cond,msg) do{ if(!(cond)){ fprintf(stderr,"FAIL: %s (%s:%d)\n",msg,__FILE__,__LINE__); failures++; } else printf("ok: %s\n",msg); }while(0)

static void fill_claude(const char *client,const char *auth,const char *token,const char *scopes)
{
    VSOAuthConfig *o=&ctx.claude_oauth;
    memset(o,0,sizeof(*o));
    snprintf(o->client_id,sizeof(o->client_id),"%s",client);
    snprintf(o->authorize_url,sizeof(o->authorize_url),"%s",auth);
    snprintf(o->token_url,sizeof(o->token_url),"%s",token);
    snprintf(o->scopes,sizeof(o->scopes),"%s",scopes);
    snprintf(o->redirect_uri,sizeof(o->redirect_uri),"http://127.0.0.1:14556/callback");
}

static const char *GOOD_CLIENT="11111111-2222-3333-4444-555555555555";
static const char *GOOD_AUTH="https://console.anthropic.com/oauth/authorize";
static const char *GOOD_TOKEN="https://api.anthropic.com/v1/oauth/token";

/* independent base64url (no padding) for checking the PKCE challenge */
static void b64url(const unsigned char *in,size_t n,char *out)
{
    static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t i=0,j=0;
    while(i<n){
        unsigned v=in[i++]<<16; int have=1;
        if(i<n){v|=in[i++]<<8;have++;}
        if(i<n){v|=in[i++];have++;}
        out[j++]=t[(v>>18)&63];out[j++]=t[(v>>12)&63];
        if(have>1)out[j++]=t[(v>>6)&63];
        if(have>2)out[j++]=t[v&63];
    }
    out[j]=0;
}

static const char *param(const char *url,const char *name,char *buf,size_t cap)
{
    const char *p=strstr(url,name);size_t n;
    if(!p)return NULL;
    p+=strlen(name);
    if(*p!='=')return NULL;
    p++;
    n=strcspn(p,"&");if(n>=cap)n=cap-1;memcpy(buf,p,n);buf[n]=0;return buf;
}

int main(void)
{
    char err[512],msg[512],url[4096],b[256];VSOAuthFlow flow;
    const char *cfg_home;
    char tmp_home[]="/tmp/vs-oauth-test-XXXXXX";

    /* Isolate HOME so any plain-text OpenAI profile writes land in a temp dir. */
    if(!mkdtemp(tmp_home)){fprintf(stderr,"mkdtemp failed\n");return 2;}
    setenv("HOME",tmp_home,1);

    vs_oauth_defaults(&ctx);

    /* ---- policy: what is allowed ---- */
    fill_claude(GOOD_CLIENT,GOOD_AUTH,GOOD_TOKEN,"api.read");
    CHECK(vs_claude_oauth_policy_check(&ctx.claude_oauth,err,sizeof(err))==0,"policy accepts an Anthropic-issued client on console.anthropic.com");

    fill_claude(GOOD_CLIENT,"https://api.anthropic.com/oauth/authorize",GOOD_TOKEN,"api.read");
    CHECK(vs_claude_oauth_policy_check(&ctx.claude_oauth,err,sizeof(err))==0,"policy accepts api.anthropic.com endpoints");

    /* ---- policy: what is refused ---- */
    fill_claude("9d1c250a-e61b-44d9-88ed-5944d1962f5e",GOOD_AUTH,GOOD_TOKEN,"user:inference");
    CHECK(vs_claude_oauth_policy_check(&ctx.claude_oauth,err,sizeof(err))!=0,"policy refuses the Claude Code subscription client ID");
    CHECK(strstr(err,"Claude Code")!=NULL,"refusal explains the Claude Code subscription issue");

    fill_claude("9D1C250A-E61B-44D9-88ED-5944D1962F5E",GOOD_AUTH,GOOD_TOKEN,"user:inference");
    CHECK(vs_claude_oauth_policy_check(&ctx.claude_oauth,err,sizeof(err))!=0,"consumer client ID refusal is case-insensitive");

    fill_claude(GOOD_CLIENT,"https://claude.ai/oauth/authorize",GOOD_TOKEN,"api.read");
    CHECK(vs_claude_oauth_policy_check(&ctx.claude_oauth,err,sizeof(err))!=0,"policy refuses claude.ai consumer login host");

    fill_claude(GOOD_CLIENT,GOOD_AUTH,"http://api.anthropic.com/v1/oauth/token","api.read");
    CHECK(vs_claude_oauth_policy_check(&ctx.claude_oauth,err,sizeof(err))!=0,"policy refuses non-https token URL");

    fill_claude(GOOD_CLIENT,GOOD_AUTH,"https://api.anthropic.com.evil.example/token","api.read");
    CHECK(vs_claude_oauth_policy_check(&ctx.claude_oauth,err,sizeof(err))!=0,"policy refuses look-alike host suffix");

    fill_claude(GOOD_CLIENT,"https://api.anthropic.com@evil.example/authorize",GOOD_TOKEN,"api.read");
    CHECK(vs_claude_oauth_policy_check(&ctx.claude_oauth,err,sizeof(err))!=0,"policy refuses userinfo trick in authorisation URL");

    fill_claude(GOOD_CLIENT,GOOD_AUTH,GOOD_TOKEN,"");
    CHECK(vs_claude_oauth_policy_check(&ctx.claude_oauth,err,sizeof(err))!=0,"policy requires explicitly issued scopes");

    /* ---- bearer destination guard ---- */
    CHECK(vs_claude_oauth_bearer_url_ok("https://api.anthropic.com/v1/messages"),"bearer allowed for official messages URL");
    CHECK(vs_claude_oauth_bearer_url_ok("https://api.anthropic.com"),"bearer allowed for bare official origin");
    CHECK(!vs_claude_oauth_bearer_url_ok("https://api.anthropic.com.evil.example/v1/messages"),"bearer refused for look-alike host");
    CHECK(!vs_claude_oauth_bearer_url_ok("http://api.anthropic.com/v1/messages"),"bearer refused over plain http");
    CHECK(!vs_claude_oauth_bearer_url_ok("https://proxy.example/anthropic/v1/messages"),"bearer refused for third-party gateway");
    CHECK(!vs_claude_oauth_bearer_url_ok(NULL),"bearer refused for missing URL");

    /* ---- PKCE flow for Claude uses its own slot and a correct S256 challenge ---- */
    fill_claude(GOOD_CLIENT,GOOD_AUTH,GOOD_TOKEN,"api.read");
    memset(&flow,0,sizeof(flow));flow.listener_fd=-1;
    CHECK(vs_oauth_begin_for(&ctx,VS_OAUTH_CLAUDE,&flow,url,sizeof(url),err,sizeof(err))==0,"Claude begin starts the PKCE flow");
    CHECK(flow.provider==VS_OAUTH_CLAUDE,"flow records the Claude provider");
    {
        unsigned char dig[32];char want[128],got[128];
        vs_sha256((const unsigned char*)flow.verifier,strlen(flow.verifier),dig);
        b64url(dig,32,want);
        param(url,"code_challenge",got,sizeof(got));
        CHECK(strcmp(got,want)==0,"code_challenge equals base64url(SHA256(verifier))");
    }
    CHECK(strstr(url,"code_challenge_method=S256")!=NULL,"authorisation URL uses S256");
    CHECK(strstr(url,"response_type=code")!=NULL,"authorisation URL requests an authorisation code");
    CHECK(strncmp(url,GOOD_AUTH,strlen(GOOD_AUTH))==0,"authorisation URL is on the configured Anthropic host");
    vs_oauth_cancel(&flow);

    /* A refused Claude configuration must not open a listener or a browser. */
    fill_claude("9d1c250a-e61b-44d9-88ed-5944d1962f5e",GOOD_AUTH,GOOD_TOKEN,"user:inference");
    memset(&flow,0,sizeof(flow));flow.listener_fd=-1;
    CHECK(vs_oauth_begin_for(&ctx,VS_OAUTH_CLAUDE,&flow,url,sizeof(url),err,sizeof(err))!=0,"Claude begin refuses the consumer client ID");
    CHECK(flow.active==0,"refused Claude begin leaves no listener active");

    /* OpenAI begin is unaffected by the Claude guard rails (no http check on its URLs). */
    memset(&ctx.oauth,0,sizeof(ctx.oauth));
    snprintf(ctx.oauth.client_id,sizeof(ctx.oauth.client_id),"openai-app");
    snprintf(ctx.oauth.authorize_url,sizeof(ctx.oauth.authorize_url),"https://auth.example/authorize");
    snprintf(ctx.oauth.token_url,sizeof(ctx.oauth.token_url),"https://auth.example/token");
    snprintf(ctx.oauth.redirect_uri,sizeof(ctx.oauth.redirect_uri),"http://127.0.0.1:14555/callback");
    memset(&flow,0,sizeof(flow));flow.listener_fd=-1;
    CHECK(vs_oauth_begin(&ctx,&flow,url,sizeof(url),err,sizeof(err))==0,"OpenAI begin still works");
    CHECK(flow.provider==VS_OAUTH_OPENAI,"OpenAI flow records the OpenAI provider");
    vs_oauth_cancel(&flow);

    /* ---- token handling: refresh for Claude goes only to the configured token URL ---- */
    fill_claude(GOOD_CLIENT,GOOD_AUTH,GOOD_TOKEN,"api.read");
    snprintf(ctx.claude_oauth.refresh_token,sizeof(ctx.claude_oauth.refresh_token),"RT-OLD");
    snprintf(ctx.claude_oauth.access_token,sizeof(ctx.claude_oauth.access_token),"AT-OLD");
    snprintf(ctx.oauth.access_token,sizeof(ctx.oauth.access_token),"OPENAI-AT");
    http_calls=0;persist_calls=0;
    CHECK(vs_oauth_refresh_for(&ctx,VS_OAUTH_CLAUDE,err,sizeof(err))==0,"Claude refresh succeeds against the stub token endpoint");
    CHECK(strcmp(last_url,GOOD_TOKEN)==0,"Claude refresh posts only to the configured token URL");
    CHECK(strstr(last_body,"grant_type=refresh_token")!=NULL,"Claude refresh uses the refresh_token grant");
    CHECK(strcmp(ctx.claude_oauth.access_token,"AT-NEW")==0,"Claude access token is stored in the Claude slot");
    CHECK(strcmp(ctx.claude_oauth.refresh_token,"RT-NEW")==0,"Claude refresh token is rotated");
    CHECK(strcmp(ctx.oauth.access_token,"OPENAI-AT")==0,"OpenAI slot is untouched by a Claude refresh");
    CHECK(persist_calls>0,"Claude tokens are persisted to the encrypted config");
    snprintf(b,sizeof(b),"%s/.vibesolaris/oauth.conf",tmp_home);
    CHECK(access(b,F_OK)!=0,"Claude tokens are never written to the plain-text OAuth profile");

    /* Refused configuration must make no network call at all. */
    fill_claude("9d1c250a-e61b-44d9-88ed-5944d1962f5e",GOOD_AUTH,GOOD_TOKEN,"user:inference");
    snprintf(ctx.claude_oauth.refresh_token,sizeof(ctx.claude_oauth.refresh_token),"RT-STOLEN-LOOKING");
    http_calls=0;
    CHECK(vs_oauth_refresh_for(&ctx,VS_OAUTH_CLAUDE,err,sizeof(err))!=0,"Claude refresh refuses the consumer client ID");
    CHECK(http_calls==0,"refused Claude refresh performs no HTTP request");

    /* Expired-token ensure path refreshes Claude only when allowed. */
    fill_claude(GOOD_CLIENT,GOOD_AUTH,GOOD_TOKEN,"api.read");
    snprintf(ctx.claude_oauth.access_token,sizeof(ctx.claude_oauth.access_token),"AT-EXPIRED");
    snprintf(ctx.claude_oauth.refresh_token,sizeof(ctx.claude_oauth.refresh_token),"RT-NEW");
    ctx.claude_oauth.expires_at=1;   /* long expired */
    http_calls=0;
    CHECK(vs_oauth_ensure_access_token_for(&ctx,VS_OAUTH_CLAUDE,err,sizeof(err))==0,"expired Claude token is refreshed");
    CHECK(http_calls==1,"exactly one refresh request for an expired token");
    CHECK(vs_oauth_is_signed_in_for(&ctx,VS_OAUTH_CLAUDE)==1,"refreshed Claude session reports signed in");
    CHECK(strcmp(ctx.oauth.access_token,"OPENAI-AT")==0,"OpenAI slot still holds its own token after Claude refresh");

    /* ---- logout only clears the requested slot ---- */
    vs_oauth_logout_for(&ctx,VS_OAUTH_CLAUDE);
    CHECK(ctx.claude_oauth.access_token[0]==0&&ctx.claude_oauth.refresh_token[0]==0,"Claude logout clears Claude tokens");
    CHECK(strcmp(ctx.oauth.access_token,"OPENAI-AT")==0,"Claude logout leaves OpenAI tokens alone");
    CHECK(access(b,F_OK)!=0,"Claude logout does not create the plain-text profile");

    /* ---- defaults keep the two callback ports separate ---- */
    vs_oauth_defaults(&ctx);
    CHECK(strcmp(ctx.oauth.redirect_uri,"http://127.0.0.1:14555/callback")==0,"OpenAI default redirect unchanged");
    CHECK(strcmp(ctx.claude_oauth.redirect_uri,"http://127.0.0.1:14556/callback")==0,"Claude default redirect uses a separate port");

    (void)cfg_home;(void)msg;
    if(failures){fprintf(stderr,"%d failure(s)\n",failures);return 1;}
    printf("all Claude OAuth tests passed\n");
    return 0;
}
