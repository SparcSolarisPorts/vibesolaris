/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif

#define OAUTH_TIMEOUT_SECONDS 300
#define OAUTH_DEFAULT_REDIRECT "http://127.0.0.1:14555/callback"

typedef struct {
    char host[64];
    int port;
    char path[256];
} RedirectParts;

static void copyv(char *dst,size_t cap,const char *src)
{
    size_t n;
    if(!dst||cap==0)return;
    if(!src)src="";
    n=strlen(src);if(n>=cap)n=cap-1;
    memcpy(dst,src,n);dst[n]=0;
}

static void setmsg(char *dst,size_t cap,const char *s)
{
    copyv(dst,cap,s?s:"");
}


static int random_bytes(unsigned char *out,size_t n)
{
    const char *names[3];
    int fd,i;size_t got;ssize_t r;
    names[0]="/dev/urandom";names[1]="/dev/random";names[2]=NULL;
    for(i=0;names[i];i++){
        fd=open(names[i],O_RDONLY);
        if(fd<0)continue;
        got=0;
        while(got<n){r=read(fd,out+got,n-got);if(r<=0)break;got+=(size_t)r;}
        close(fd);
        if(got==n)return 0;
    }
    return -1;
}

static char *base64url_bytes(const unsigned char *in,size_t n)
{
    static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t cap=((n+2)/3)*4+1,i,j;unsigned a,b,c,v;char *o;
    o=(char*)malloc(cap);if(!o)return NULL;
    i=0;j=0;
    while(i<n){
        a=in[i++];b=i<n?in[i++]:0;c=i<n?in[i++]:0;v=(a<<16)|(b<<8)|c;
        o[j++]=t[(v>>18)&63];o[j++]=t[(v>>12)&63];
        if(i-1<=n-1 && (i-1)<n)o[j++]=t[(v>>6)&63];
        if(i<=n)o[j++]=t[v&63];
    }
    switch(n%3){case 1:j-=2;break;case 2:j-=1;break;default:break;}
    o[j]=0;return o;
}

static int unreserved(unsigned char c)
{
    return isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~';
}

static char *url_encode(const char *s)
{
    static const char h[]="0123456789ABCDEF";
    size_t i,n,cap;char *o,*p;unsigned char c;
    if(!s)s="";
    n=strlen(s);cap=n*3+1;o=(char*)malloc(cap);if(!o)return NULL;p=o;
    for(i=0;i<n;i++){
        c=(unsigned char)s[i];
        if(unreserved(c))*p++=(char)c;
        else{*p++='%';*p++=h[(c>>4)&15];*p++=h[c&15];}
    }
    *p=0;return o;
}

static int hexv(int c)
{
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}

static void url_decode_inplace(char *s)
{
    char *p,*q;int a,b;
    if(!s)return;
    p=s;q=s;
    while(*p){
        if(*p=='%'&&p[1]&&p[2]&&(a=hexv((unsigned char)p[1]))>=0&&(b=hexv((unsigned char)p[2]))>=0){*q++=(char)((a<<4)|b);p+=3;}
        else if(*p=='+'){*q++=' ';p++;}
        else *q++=*p++;
    }
    *q=0;
}

static int parse_redirect(const char *uri,RedirectParts *r,char *err,size_t errcap)
{
    const char *p,*colon,*slash;size_t hn,pn;char portbuf[16];long port;
    if(!uri||strncmp(uri,"http://",7)){setmsg(err,errcap,"Redirect URI must use http:// loopback for this native client");return -1;}
    p=uri+7;slash=strchr(p,'/');if(!slash){setmsg(err,errcap,"Redirect URI needs a callback path, for example /callback");return -1;}
    colon=(const char*)memchr(p,':',(size_t)(slash-p));if(!colon){setmsg(err,errcap,"Redirect URI must include an explicit loopback port");return -1;}
    hn=(size_t)(colon-p);if(hn==0||hn>=sizeof(r->host)){setmsg(err,errcap,"Invalid redirect host");return -1;}
    memcpy(r->host,p,hn);r->host[hn]=0;
    if(strcmp(r->host,"127.0.0.1")&&strcmp(r->host,"localhost")){setmsg(err,errcap,"For safety, redirect host must be 127.0.0.1 or localhost");return -1;}
    pn=(size_t)(slash-colon-1);if(pn==0||pn>=sizeof(portbuf)){setmsg(err,errcap,"Invalid redirect port");return -1;}
    memcpy(portbuf,colon+1,pn);portbuf[pn]=0;port=strtol(portbuf,NULL,10);if(port<1||port>65535){setmsg(err,errcap,"Redirect port must be between 1 and 65535");return -1;}
    if(strchr(slash,'?')||strchr(slash,'#')){setmsg(err,errcap,"Redirect URI callback path cannot contain a query or fragment");return -1;}
    copyv(r->path,sizeof(r->path),slash);r->port=(int)port;return 0;
}

static int make_listener(const RedirectParts *r,char *err,size_t errcap)
{
    int fd,yes,flags;struct sockaddr_in a;
    fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0){setmsg(err,errcap,"Could not create OAuth callback socket");return -1;}
    yes=1;(void)setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,(char*)&yes,sizeof(yes));
    memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons((unsigned short)r->port);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(fd,(struct sockaddr*)&a,sizeof(a))<0){close(fd);setmsg(err,errcap,"Could not bind the OAuth callback port; choose another redirect port");return -1;}
    if(listen(fd,4)<0){close(fd);setmsg(err,errcap,"Could not listen on the OAuth callback port");return -1;}
    flags=fcntl(fd,F_GETFL,0);if(flags>=0)(void)fcntl(fd,F_SETFL,flags|O_NONBLOCK);
    return fd;
}

static char *query_value(const char *target,const char *name)
{
    const char *q,*p,*eq,*end;size_t kn,n;char *v;
    q=strchr(target,'?');if(!q)return NULL;q++;kn=strlen(name);p=q;
    while(*p){
        end=strchr(p,'&');if(!end)end=p+strlen(p);eq=(const char*)memchr(p,'=',(size_t)(end-p));
        if(eq&&(size_t)(eq-p)==kn&&!strncmp(p,name,kn)){
            n=(size_t)(end-eq-1);v=(char*)malloc(n+1);if(!v)return NULL;memcpy(v,eq+1,n);v[n]=0;url_decode_inplace(v);return v;
        }
        if(!*end)break;
        p=end+1;
    }
    return NULL;
}

static char *json_string_field(const char *json,const char *field)
{
    char key[128];const char *p,*q;size_t cap,n;char *o;int hv1,hv2;
    if(!json||!field)return NULL;
    snprintf(key,sizeof(key),"\"%s\"",field);p=strstr(json,key);if(!p)return NULL;p+=strlen(key);
    while(*p&&isspace((unsigned char)*p))p++;
    if(*p!=':')return NULL;
    p++;
    while(*p&&isspace((unsigned char)*p))p++;
    if(*p!='\"')return NULL;
    p++;
    cap=strlen(p)+1;o=(char*)malloc(cap);if(!o)return NULL;n=0;
    while(*p&&*p!='\"'){
        if(*p=='\\'){
            p++;if(!*p)break;
            if(*p=='n')o[n++]='\n';else if(*p=='r')o[n++]='\r';else if(*p=='t')o[n++]='\t';else if(*p=='b')o[n++]='\b';else if(*p=='f')o[n++]='\f';
            else if(*p=='u'&&p[1]&&p[2]&&p[3]&&p[4]){
                hv1=hexv((unsigned char)p[3]);hv2=hexv((unsigned char)p[4]);
                if(p[1]=='0'&&p[2]=='0'&&hv1>=0&&hv2>=0){o[n++]=(char)((hv1<<4)|hv2);p+=4;}else{o[n++]='?';p+=4;}
            }else o[n++]=*p;
        }else o[n++]=*p;
        p++;
    }
    o[n]=0;q=o;(void)q;return o;
}

static long json_long_field(const char *json,const char *field)
{
    char key[128];const char *p;
    if(!json||!field)return 0;
    snprintf(key,sizeof(key),"\"%s\"",field);p=strstr(json,key);if(!p)return 0;p+=strlen(key);
    while(*p&&isspace((unsigned char)*p))p++;
    if(*p!=':')return 0;
    p++;
    while(*p&&isspace((unsigned char)*p))p++;
    return strtol(p,NULL,10);
}

static char *form4(const char *k1,const char *v1,const char *k2,const char *v2,const char *k3,const char *v3,const char *k4,const char *v4,const char *k5,const char *v5)
{
    char *a1,*a2,*a3,*a4,*a5,*o;size_t n;
    a1=url_encode(v1);a2=url_encode(v2);a3=url_encode(v3);a4=url_encode(v4);a5=k5?url_encode(v5):NULL;
    if(!a1||!a2||!a3||!a4||(k5&&!a5)){free(a1);free(a2);free(a3);free(a4);free(a5);return NULL;}
    n=strlen(k1)+strlen(a1)+strlen(k2)+strlen(a2)+strlen(k3)+strlen(a3)+strlen(k4)+strlen(a4)+32;
    if(k5)n+=strlen(k5)+strlen(a5)+2;
    o=(char*)malloc(n);if(!o){free(a1);free(a2);free(a3);free(a4);free(a5);return NULL;}
    snprintf(o,n,"%s=%s&%s=%s&%s=%s&%s=%s",k1,a1,k2,a2,k3,a3,k4,a4);
    if(k5){strcat(o,"&");strcat(o,k5);strcat(o,"=");strcat(o,a5);}
    free(a1);free(a2);free(a3);free(a4);free(a5);return o;
}

/* ------------------------------------------------------------------------
 * Provider-scoped OAuth.
 *
 * VS_OAUTH_OPENAI keeps the original behaviour (including the plain-text
 * compatibility profile).  VS_OAUTH_CLAUDE is a separate credential slot with
 * stricter rules, described in OAUTH.md ("Claude OAuth and compliance").
 * ---------------------------------------------------------------------- */

#define OAUTH_CLAUDE_DEFAULT_REDIRECT "http://127.0.0.1:14556/callback"

/* Anthropic's first-party Claude Code client.  Its tokens are issued for
   Claude Pro/Max subscription use in Claude Code and claude.ai, and Anthropic's
   terms do not permit third-party products to use them.  Refusing this ID is a
   guard rail, not the primary control: the primary control is that VibeSolaris
   only uses a client ID that Anthropic issues to VibeSolaris. */
static const char CLAUDE_CONSUMER_CLIENT_ID[]="9d1c250a-e61b-44d9-88ed-5944d1962f5e";

/* Claude OAuth endpoints may only live on Anthropic's API/Console hosts.  The
   claude.ai consumer login host is deliberately absent. */
static const char *CLAUDE_ALLOWED_HOSTS[]={"api.anthropic.com","console.anthropic.com",NULL};

static int ascii_ieq(const char *a,const char *b)
{
    unsigned char x,y;
    if(!a||!b)return 0;
    for(;;){
        x=(unsigned char)*a++;y=(unsigned char)*b++;
        if(tolower(x)!=tolower(y))return 0;
        if(!x)return 1;
    }
}

static const VSOAuthConfig *cfg_of(const VSContext *ctx,VSOAuthProvider which)
{
    return which==VS_OAUTH_CLAUDE?&ctx->claude_oauth:&ctx->oauth;
}

VSOAuthConfig *vs_oauth_config(VSContext *ctx,VSOAuthProvider which)
{
    if(!ctx)return NULL;
    return which==VS_OAUTH_CLAUDE?&ctx->claude_oauth:&ctx->oauth;
}

static int https_url_on_allowed_host(const char *url,const char *what,char *err,size_t errcap)
{
    const char *auth,*auth_end,*host_end;size_t hn;char host[128],b[256];int i;
    if(!url||strncmp(url,"https://",8)!=0){snprintf(b,sizeof(b),"Claude OAuth %s must start with https://",what);setmsg(err,errcap,b);return -1;}
    auth=url+8;auth_end=auth+strcspn(auth,"/?#");
    if(auth_end==auth||memchr(auth,'@',(size_t)(auth_end-auth))){snprintf(b,sizeof(b),"Claude OAuth %s has an invalid host",what);setmsg(err,errcap,b);return -1;}
    host_end=(const char*)memchr(auth,':',(size_t)(auth_end-auth));if(!host_end)host_end=auth_end;
    hn=(size_t)(host_end-auth);if(hn>=sizeof(host)){snprintf(b,sizeof(b),"Claude OAuth %s has an invalid host",what);setmsg(err,errcap,b);return -1;}
    memcpy(host,auth,hn);host[hn]=0;
    for(i=0;CLAUDE_ALLOWED_HOSTS[i];i++)if(ascii_ieq(host,CLAUDE_ALLOWED_HOSTS[i]))return 0;
    snprintf(b,sizeof(b),"Claude OAuth %s host '%.80s' is not an Anthropic API/Console host. Claude.ai consumer login cannot be used by third-party apps.",what,host);
    setmsg(err,errcap,b);return -1;
}

/* Returns 0 when the configuration is allowed to run a Claude OAuth flow. */
int vs_claude_oauth_policy_check(const VSOAuthConfig *cfg,char *err,size_t errcap)
{
    if(!cfg){setmsg(err,errcap,"Claude OAuth configuration is missing");return -1;}
    if(!cfg->client_id[0]){setmsg(err,errcap,"Claude OAuth needs the Client ID that Anthropic issues to VibeSolaris");return -1;}
    if(ascii_ieq(cfg->client_id,CLAUDE_CONSUMER_CLIENT_ID)){
        setmsg(err,errcap,"That Client ID belongs to Anthropic's Claude Code subscription login, which VibeSolaris must not use. Enter the Client ID Anthropic issues to VibeSolaris, or use an Anthropic API key.");
        return -1;
    }
    if(!cfg->scopes[0]){setmsg(err,errcap,"Claude OAuth needs the exact scopes Anthropic issued to VibeSolaris");return -1;}
    if(https_url_on_allowed_host(cfg->authorize_url,"authorisation URL",err,errcap)!=0)return -1;
    if(https_url_on_allowed_host(cfg->token_url,"token URL",err,errcap)!=0)return -1;
    return 0;
}

/* Claude OAuth bearer tokens may only be sent to the official Messages API. */
int vs_claude_oauth_bearer_url_ok(const char *url)
{
    static const char pfx[]="https://api.anthropic.com";
    size_t n=sizeof(pfx)-1;
    if(!url||strncmp(url,pfx,n)!=0)return 0;
    return url[n]==0||url[n]=='/';
}

static int apply_token_response(VSContext *ctx,VSOAuthProvider which,const char *json,int preserve_refresh,char *err,size_t errcap)
{
    VSOAuthConfig *cfg=vs_oauth_config(ctx,which);
    char *access,*refresh,*type,*em;long expires;
    access=json_string_field(json,"access_token");if(!access){em=json_string_field(json,"error_description");if(!em)em=json_string_field(json,"error");if(em){setmsg(err,errcap,em);free(em);}else setmsg(err,errcap,"Token endpoint did not return access_token");return -1;}
    refresh=json_string_field(json,"refresh_token");type=json_string_field(json,"token_type");expires=json_long_field(json,"expires_in");
    copyv(cfg->access_token,sizeof(cfg->access_token),access);
    if(refresh)copyv(cfg->refresh_token,sizeof(cfg->refresh_token),refresh);else if(!preserve_refresh)cfg->refresh_token[0]=0;
    copyv(cfg->token_type,sizeof(cfg->token_type),type?type:"Bearer");
    cfg->expires_at=expires>0?(long)time(NULL)+expires:0;
    free(access);free(refresh);free(type);
    /* Claude tokens are written only to the encrypted configuration store. */
    if(which==VS_OAUTH_OPENAI)(void)vs_oauth_save_profile(ctx);
    (void)vs_persist_settings(ctx);return 0;
}

static int exchange_code(VSContext *ctx,VSOAuthProvider which,const char *code,const char *verifier,char *err,size_t errcap)
{
    VSOAuthConfig *cfg=vs_oauth_config(ctx,which);
    char *form,*resp;const char *h[2];long status;
    if(which==VS_OAUTH_CLAUDE&&vs_claude_oauth_policy_check(cfg,err,errcap)!=0)return -1;
    form=form4("grant_type","authorization_code","client_id",cfg->client_id,"code",code,"redirect_uri",cfg->redirect_uri,"code_verifier",verifier);
    if(!form){setmsg(err,errcap,"Out of memory creating token request");return -1;}
    h[0]="Content-Type: application/x-www-form-urlencoded";h[1]="Accept: application/json";status=0;
    resp=vs_http_post_ctx(ctx,cfg->token_url,h,2,form,&status);free(form);
    if(!resp){setmsg(err,errcap,"Token endpoint request failed");return -1;}
    if(status<200||status>=300){char b[512];snprintf(b,sizeof(b),"Token endpoint returned HTTP %ld: %.360s",status,resp);setmsg(err,errcap,b);free(resp);return -1;}
    if(apply_token_response(ctx,which,resp,0,err,errcap)!=0){free(resp);return -1;}free(resp);return 0;
}

static void browser_reply(int fd,int ok,const char *detail)
{
    char body[1024],head[256];int n;const char *title;
    title=ok?"Authorisation received":"Authorisation failed";
    snprintf(body,sizeof(body),"<html><head><title>VibeSolaris</title></head><body style=\"font-family:sans-serif;max-width:680px;margin:60px auto\"><h2>%s</h2><p>%s</p><p>You may close this tab and return to VibeSolaris.</p></body></html>",title,detail?detail:"");
    snprintf(head,sizeof(head),"HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n",ok?"200 OK":"400 Bad Request",(unsigned long)strlen(body));
    n=(int)write(fd,head,strlen(head));(void)n;n=(int)write(fd,body,strlen(body));(void)n;
}

void vs_oauth_defaults(VSContext *ctx)
{
    if(!ctx)return;
    memset(&ctx->oauth,0,sizeof(ctx->oauth));
    copyv(ctx->oauth.redirect_uri,sizeof(ctx->oauth.redirect_uri),OAUTH_DEFAULT_REDIRECT);
    copyv(ctx->oauth.token_type,sizeof(ctx->oauth.token_type),"Bearer");
    memset(&ctx->claude_oauth,0,sizeof(ctx->claude_oauth));
    copyv(ctx->claude_oauth.redirect_uri,sizeof(ctx->claude_oauth.redirect_uri),OAUTH_CLAUDE_DEFAULT_REDIRECT);
    copyv(ctx->claude_oauth.token_type,sizeof(ctx->claude_oauth.token_type),"Bearer");
}

int vs_oauth_is_configured_for(const VSContext *ctx,VSOAuthProvider which)
{
    const VSOAuthConfig *cfg;
    if(!ctx)return 0;
    cfg=cfg_of(ctx,which);
    return cfg->client_id[0]&&cfg->authorize_url[0]&&cfg->token_url[0]&&cfg->redirect_uri[0];
}

int vs_oauth_is_signed_in_for(const VSContext *ctx,VSOAuthProvider which)
{
    const VSOAuthConfig *cfg;long now;
    if(!ctx)return 0;
    cfg=cfg_of(ctx,which);
    if(!cfg->access_token[0])return 0;
    if(cfg->expires_at==0)return 1;
    now=(long)time(NULL);return cfg->expires_at>now;
}

int vs_oauth_is_configured(const VSContext *ctx)
{
    return vs_oauth_is_configured_for(ctx,VS_OAUTH_OPENAI);
}

int vs_oauth_is_signed_in(const VSContext *ctx)
{
    return vs_oauth_is_signed_in_for(ctx,VS_OAUTH_OPENAI);
}

/* ---- OpenAI compatibility profile (plain text, unchanged behaviour) ---- */

const char *vs_oauth_profile_path(void)
{
    static char p[VS_MAX_PATH];const char *home;
    if(p[0])return p;
    home=getenv("HOME");
    if(home&&*home)snprintf(p,sizeof(p),"%s/.vibesolaris/oauth.conf",home);
    else copyv(p,sizeof(p),".vibesolaris-oauth.conf");
    return p;
}

static int ensure_profile_dir(void)
{
    const char *home;char d[VS_MAX_PATH];
    home=getenv("HOME");if(!home||!*home)return 0;snprintf(d,sizeof(d),"%s/.vibesolaris",home);
    if(mkdir(d,0700)!=0&&errno!=EEXIST)return -1;
    (void)chmod(d,0700);return 0;
}

int vs_oauth_save_profile(const VSContext *ctx)
{
    int fd;FILE *f;const char *p;
    if(!ctx)return -1;
    if(ensure_profile_dir()!=0)return -1;
    p=vs_oauth_profile_path();fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0600);if(fd<0)return -1;
    (void)fchmod(fd,0600);f=fdopen(fd,"w");if(!f){close(fd);return -1;}
    fprintf(f,"client_id=%s\n",ctx->oauth.client_id);fprintf(f,"authorize_url=%s\n",ctx->oauth.authorize_url);fprintf(f,"token_url=%s\n",ctx->oauth.token_url);
    fprintf(f,"scopes=%s\n",ctx->oauth.scopes);fprintf(f,"redirect_uri=%s\n",ctx->oauth.redirect_uri);fprintf(f,"access_token=%s\n",ctx->oauth.access_token);
    fprintf(f,"refresh_token=%s\n",ctx->oauth.refresh_token);fprintf(f,"token_type=%s\n",ctx->oauth.token_type);fprintf(f,"expires_at=%ld\n",ctx->oauth.expires_at);
    if(fclose(f)!=0)return -1;
    return 0;
}

int vs_oauth_load_profile(VSContext *ctx)
{
    FILE *f;char line[8192],*eq,*v;const char *p;
    if(!ctx)return -1;
    p=vs_oauth_profile_path();f=fopen(p,"r");if(!f)return -1;
    while(fgets(line,sizeof(line),f)){
        line[strcspn(line,"\r\n")]=0;eq=strchr(line,'=');if(!eq)continue;*eq=0;v=eq+1;
        if(!strcmp(line,"client_id"))copyv(ctx->oauth.client_id,sizeof(ctx->oauth.client_id),v);
        else if(!strcmp(line,"authorize_url"))copyv(ctx->oauth.authorize_url,sizeof(ctx->oauth.authorize_url),v);
        else if(!strcmp(line,"token_url"))copyv(ctx->oauth.token_url,sizeof(ctx->oauth.token_url),v);
        else if(!strcmp(line,"scopes"))copyv(ctx->oauth.scopes,sizeof(ctx->oauth.scopes),v);
        else if(!strcmp(line,"redirect_uri"))copyv(ctx->oauth.redirect_uri,sizeof(ctx->oauth.redirect_uri),v);
        else if(!strcmp(line,"access_token"))copyv(ctx->oauth.access_token,sizeof(ctx->oauth.access_token),v);
        else if(!strcmp(line,"refresh_token"))copyv(ctx->oauth.refresh_token,sizeof(ctx->oauth.refresh_token),v);
        else if(!strcmp(line,"token_type"))copyv(ctx->oauth.token_type,sizeof(ctx->oauth.token_type),v);
        else if(!strcmp(line,"expires_at"))ctx->oauth.expires_at=strtol(v,NULL,10);
    }
    fclose(f);return 0;
}

/* ---- Authorisation Code + PKCE flow ---- */

int vs_oauth_begin_for(VSContext *ctx,VSOAuthProvider which,VSOAuthFlow *flow,char *url_out,size_t url_cap,char *err,size_t errcap)
{
    VSOAuthConfig *cfg;RedirectParts r;unsigned char rv[48],st[24],digest[32];char *ver,*state,*challenge,*cid,*redir,*scope,*auth;size_t n;int fd;const char *sep;
    if(err&&errcap)err[0]=0;
    if(!ctx||!flow){setmsg(err,errcap,"Invalid OAuth state");return -1;}
    if(!vs_oauth_is_configured_for(ctx,which)){setmsg(err,errcap,"OAuth is not configured: enter Client ID, authorisation URL, token URL, and redirect URI");return -1;}
    cfg=vs_oauth_config(ctx,which);
    if(which==VS_OAUTH_CLAUDE&&vs_claude_oauth_policy_check(cfg,err,errcap)!=0)return -1;
    if(parse_redirect(cfg->redirect_uri,&r,err,errcap)!=0)return -1;
    if(random_bytes(rv,sizeof(rv))!=0||random_bytes(st,sizeof(st))!=0){setmsg(err,errcap,"Secure random source unavailable (/dev/urandom or /dev/random required)");return -1;}
    ver=base64url_bytes(rv,sizeof(rv));state=base64url_bytes(st,sizeof(st));if(!ver||!state){free(ver);free(state);setmsg(err,errcap,"Out of memory creating PKCE values");return -1;}
    vs_sha256((const unsigned char*)ver,strlen(ver),digest);challenge=base64url_bytes(digest,sizeof(digest));if(!challenge){free(ver);free(state);setmsg(err,errcap,"Out of memory creating PKCE challenge");return -1;}
    cid=url_encode(cfg->client_id);redir=url_encode(cfg->redirect_uri);scope=url_encode(cfg->scopes);if(!cid||!redir||!scope){free(ver);free(state);free(challenge);free(cid);free(redir);free(scope);setmsg(err,errcap,"Out of memory creating authorisation URL");return -1;}
    sep=strchr(cfg->authorize_url,'?')?"&":"?";n=strlen(cfg->authorize_url)+strlen(cid)+strlen(redir)+strlen(scope)+strlen(state)+strlen(challenge)+192;auth=(char*)malloc(n);
    if(!auth){free(ver);free(state);free(challenge);free(cid);free(redir);free(scope);setmsg(err,errcap,"Out of memory creating authorisation URL");return -1;}
    snprintf(auth,n,"%s%sresponse_type=code&client_id=%s&redirect_uri=%s&scope=%s&state=%s&code_challenge=%s&code_challenge_method=S256",cfg->authorize_url,sep,cid,redir,scope,state,challenge);
    fd=make_listener(&r,err,errcap);if(fd<0){free(ver);free(state);free(challenge);free(cid);free(redir);free(scope);free(auth);return -1;}
    memset(flow,0,sizeof(*flow));flow->active=1;flow->listener_fd=fd;flow->port=r.port;flow->started_at=(long)time(NULL);flow->provider=which;
    copyv(flow->state,sizeof(flow->state),state);copyv(flow->verifier,sizeof(flow->verifier),ver);copyv(flow->redirect_uri,sizeof(flow->redirect_uri),cfg->redirect_uri);copyv(flow->callback_path,sizeof(flow->callback_path),r.path);
    if(url_out&&url_cap)copyv(url_out,url_cap,auth);
    if(vs_open_url(auth)!=0)setmsg(err,errcap,"OAuth listener started, but no browser launcher was found; open the authorisation URL manually");
    free(ver);free(state);free(challenge);free(cid);free(redir);free(scope);free(auth);return 0;
}

int vs_oauth_begin(VSContext *ctx,VSOAuthFlow *flow,char *url_out,size_t url_cap,char *err,size_t errcap)
{
    return vs_oauth_begin_for(ctx,VS_OAUTH_OPENAI,flow,url_out,url_cap,err,errcap);
}

void vs_oauth_cancel(VSOAuthFlow *flow)
{
    if(!flow)return;
    if(flow->active&&flow->listener_fd>=0)close(flow->listener_fd);
    memset(flow,0,sizeof(*flow));flow->listener_fd=-1;
}

int vs_oauth_poll(VSContext *ctx,VSOAuthFlow *flow,char *msg,size_t msgcap)
{
    int fd,n;char req[16384],method[16],target[8192],version[32];char *code,*state,*oautherr,*desc;long now;VSOAuthProvider which;
    if(msg&&msgcap)msg[0]=0;
    if(!ctx||!flow||!flow->active){setmsg(msg,msgcap,"No OAuth login is active");return -1;}
    now=(long)time(NULL);if(now-flow->started_at>OAUTH_TIMEOUT_SECONDS){vs_oauth_cancel(flow);setmsg(msg,msgcap,"OAuth login timed out after 5 minutes");return -1;}
    fd=accept(flow->listener_fd,NULL,NULL);if(fd<0){if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)return 0;vs_oauth_cancel(flow);setmsg(msg,msgcap,"OAuth callback accept failed");return -1;}
    n=(int)read(fd,req,sizeof(req)-1);if(n<=0){close(fd);return 0;}req[n]=0;method[0]=target[0]=version[0]=0;
    if(sscanf(req,"%15s %8191s %31s",method,target,version)!=3||strcmp(method,"GET")){browser_reply(fd,0,"Invalid callback request.");close(fd);vs_oauth_cancel(flow);setmsg(msg,msgcap,"Invalid OAuth callback request");return -1;}
    if(strncmp(target,flow->callback_path,strlen(flow->callback_path))){browser_reply(fd,0,"Unexpected callback path.");close(fd);return 0;}
    which=flow->provider;
    oautherr=query_value(target,"error");desc=query_value(target,"error_description");if(oautherr){char b[512];snprintf(b,sizeof(b),"OAuth authorisation failed: %s%s%s",oautherr,desc?" - ":"",desc?desc:"");browser_reply(fd,0,b);close(fd);free(oautherr);free(desc);vs_oauth_cancel(flow);setmsg(msg,msgcap,b);return -1;}free(desc);
    code=query_value(target,"code");state=query_value(target,"state");if(!code||!state||strcmp(state,flow->state)){browser_reply(fd,0,"State validation failed.");close(fd);free(code);free(state);vs_oauth_cancel(flow);setmsg(msg,msgcap,"OAuth callback state validation failed");return -1;}
    browser_reply(fd,1,"The authorisation code was received securely.");close(fd);
    if(exchange_code(ctx,which,code,flow->verifier,msg,msgcap)!=0){free(code);free(state);vs_oauth_cancel(flow);return -1;}
    free(code);free(state);vs_oauth_cancel(flow);
    setmsg(msg,msgcap,which==VS_OAUTH_CLAUDE?"Signed in with Claude OAuth; tokens are stored only in the encrypted VibeSolaris configuration":"Signed in with OAuth; access token stored in ~/.vibesolaris/oauth.conf with mode 0600");
    return 1;
}

int vs_oauth_login_blocking_for(VSContext *ctx,VSOAuthProvider which,char *msg,size_t msgcap)
{
    VSOAuthFlow f;char url[4096],warn[512];fd_set r;struct timeval tv;int rc,maxfd;
    memset(&f,0,sizeof(f));f.listener_fd=-1;warn[0]=0;
    if(vs_oauth_begin_for(ctx,which,&f,url,sizeof(url),warn,sizeof(warn))!=0){setmsg(msg,msgcap,warn);return -1;}
    if(warn[0])fprintf(stderr,"%s\nAuthorisation URL:\n%s\n",warn,url);
    while(f.active){FD_ZERO(&r);FD_SET(f.listener_fd,&r);maxfd=f.listener_fd;tv.tv_sec=1;tv.tv_usec=0;rc=select(maxfd+1,&r,NULL,NULL,&tv);if(rc<0&&errno!=EINTR){vs_oauth_cancel(&f);setmsg(msg,msgcap,"OAuth wait failed");return -1;}rc=vs_oauth_poll(ctx,&f,msg,msgcap);if(rc!=0)return rc;}
    setmsg(msg,msgcap,"OAuth login ended");return -1;
}

int vs_oauth_login_blocking(VSContext *ctx,char *msg,size_t msgcap)
{
    return vs_oauth_login_blocking_for(ctx,VS_OAUTH_OPENAI,msg,msgcap);
}

int vs_oauth_refresh_for(VSContext *ctx,VSOAuthProvider which,char *err,size_t errcap)
{
    VSOAuthConfig *cfg;char *form,*resp;const char *h[2];long status;
    if(!ctx){setmsg(err,errcap,"Invalid OAuth state");return -1;}
    cfg=vs_oauth_config(ctx,which);
    if(!cfg->refresh_token[0]){setmsg(err,errcap,"No OAuth refresh token is available");return -1;}
    if(!cfg->token_url[0]||!cfg->client_id[0]){setmsg(err,errcap,"OAuth token endpoint or client ID is missing");return -1;}
    if(which==VS_OAUTH_CLAUDE&&vs_claude_oauth_policy_check(cfg,err,errcap)!=0)return -1;
    form=form4("grant_type","refresh_token","client_id",cfg->client_id,"refresh_token",cfg->refresh_token,"scope",cfg->scopes,NULL,NULL);
    if(!form){setmsg(err,errcap,"Out of memory creating refresh request");return -1;}
    h[0]="Content-Type: application/x-www-form-urlencoded";h[1]="Accept: application/json";status=0;
    resp=vs_http_post_ctx(ctx,cfg->token_url,h,2,form,&status);free(form);
    if(!resp){setmsg(err,errcap,"OAuth refresh request failed");return -1;}
    if(status<200||status>=300){char b[512];snprintf(b,sizeof(b),"OAuth refresh returned HTTP %ld: %.360s",status,resp);setmsg(err,errcap,b);free(resp);return -1;}
    if(apply_token_response(ctx,which,resp,1,err,errcap)!=0){free(resp);return -1;}
    free(resp);setmsg(err,errcap,"");return 0;
}

int vs_oauth_refresh(VSContext *ctx,char *err,size_t errcap)
{
    return vs_oauth_refresh_for(ctx,VS_OAUTH_OPENAI,err,errcap);
}

int vs_oauth_ensure_access_token_for(VSContext *ctx,VSOAuthProvider which,char *err,size_t errcap)
{
    const VSOAuthConfig *cfg;long now;
    if(!ctx){setmsg(err,errcap,"Invalid OAuth state");return -1;}
    cfg=cfg_of(ctx,which);
    if(!cfg->access_token[0]){setmsg(err,errcap,"No OAuth access token is available");return -1;}
    now=(long)time(NULL);
    if(cfg->expires_at==0||cfg->expires_at>now+60)return 0;
    return vs_oauth_refresh_for(ctx,which,err,errcap);
}

int vs_oauth_ensure_access_token(VSContext *ctx,char *err,size_t errcap)
{
    return vs_oauth_ensure_access_token_for(ctx,VS_OAUTH_OPENAI,err,errcap);
}

void vs_oauth_logout_for(VSContext *ctx,VSOAuthProvider which)
{
    VSOAuthConfig *cfg;
    if(!ctx)return;
    cfg=vs_oauth_config(ctx,which);
    cfg->access_token[0]=0;cfg->refresh_token[0]=0;
    copyv(cfg->token_type,sizeof(cfg->token_type),"Bearer");
    cfg->expires_at=0;
    if(which==VS_OAUTH_OPENAI)(void)vs_oauth_save_profile(ctx);
    (void)vs_persist_settings(ctx);
}

void vs_oauth_logout(VSContext *ctx)
{
    vs_oauth_logout_for(ctx,VS_OAUTH_OPENAI);
}
