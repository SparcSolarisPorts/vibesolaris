/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>

#define T_RESET   "\033[0m"
#define T_BOLD    "\033[1m"
#define T_DIM     "\033[2m"
#define T_RED     "\033[31m"
#define T_GREEN   "\033[32m"
#define T_YELLOW  "\033[33m"
#define T_BLUE    "\033[34m"
#define T_MAGENTA "\033[35m"
#define T_CYAN    "\033[36m"
#define T_WHITE   "\033[37m"

static int tui_colour = 0;
static int tui_colour_mode = -1; /* -1 auto, 0 never, 1 always */

static void copyv(char *dst,size_t cap,const char *src)
{
    size_t n;
    if(!src)src="";
    n=strlen(src);if(n>=cap)n=cap-1;memcpy(dst,src,n);dst[n]=0;
}

static int colour_auto_enabled(void)
{
    const char *term=getenv("TERM");
    if(getenv("NO_COLOR"))return 0;
    if(!isatty(STDOUT_FILENO))return 0;
    if(!term || !term[0] || !strcmp(term,"dumb"))return 0;
    return 1;
}

static void colour_refresh(void)
{
    const char *e=getenv("VIBESOLARIS_COLOUR");
    if(!e)e=getenv("VIBESOLARIS_COLOR"); /* backwards-compatible alias */
    if(tui_colour_mode==1){tui_colour=1;return;}
    if(tui_colour_mode==0){tui_colour=0;return;}
    if(e && (!strcmp(e,"always")||!strcmp(e,"on")||!strcmp(e,"1"))){tui_colour=1;return;}
    if(e && (!strcmp(e,"never")||!strcmp(e,"off")||!strcmp(e,"0"))){tui_colour=0;return;}
    tui_colour=colour_auto_enabled();
}

static void cprintf(const char *colour,const char *fmt,...)
{
    va_list ap;
    if(tui_colour && colour)fputs(colour,stdout);
    va_start(ap,fmt);vprintf(fmt,ap);va_end(ap);
    if(tui_colour && colour)fputs(T_RESET,stdout);
}

static void successf(const char *fmt,...)
{
    va_list ap;if(tui_colour)fputs(T_GREEN,stdout);va_start(ap,fmt);vprintf(fmt,ap);va_end(ap);if(tui_colour)fputs(T_RESET,stdout);
}

static void warnf(const char *fmt,...)
{
    va_list ap;if(tui_colour)fputs(T_YELLOW,stdout);va_start(ap,fmt);vprintf(fmt,ap);va_end(ap);if(tui_colour)fputs(T_RESET,stdout);
}

static void errorf(const char *fmt,...)
{
    va_list ap;if(tui_colour)fputs(T_RED,stdout);va_start(ap,fmt);vprintf(fmt,ap);va_end(ap);if(tui_colour)fputs(T_RESET,stdout);
}

static const char *trace_colour(const char *kind)
{
    if(!kind)return T_DIM;
    if(strstr(kind,"mcp"))return T_MAGENTA;
    if(strstr(kind,"model"))return T_CYAN;
    if(strstr(kind,"tool")||strstr(kind,"command"))return T_YELLOW;
    if(strstr(kind,"result")||strstr(kind,"output"))return T_GREEN;
    if(strstr(kind,"error")||strstr(kind,"fail"))return T_RED;
    if(strstr(kind,"agent"))return T_BLUE;
    return T_DIM;
}

static void live_trace(void *userdata,int step,const char *kind,const char *detail)
{
    const char *col=trace_colour(kind);
    (void)userdata;
    if(tui_colour)fputs(col,stdout);
    printf("%2d. [%-12s] %s\n",step,kind?kind:"step",detail?detail:"");
    if(tui_colour)fputs(T_RESET,stdout);
    fflush(stdout);
}

static void usage_status(const VSContext *c)
{
    if(!c)return;
    cprintf(T_BOLD T_BLUE,"Conversation tokens: ");
    if(c->conversation_usage_responses==0){
        printf("not reported yet\n");
        return;
    }
    printf("%lu total  (%lu input / %lu output)  [%lu model response%s]\n",
           c->conversation_total_tokens,c->conversation_input_tokens,c->conversation_output_tokens,
           c->conversation_usage_responses,c->conversation_usage_responses==1?"":"s");
}

static void print_trace(const VSContext *c)
{
    int i;
    if(!c || c->trace_count<=0){warnf("(no activity trace yet)\n");return;}
    for(i=0;i<c->trace_count;i++){
        const char *col=trace_colour(c->trace[i].kind);
        if(tui_colour)fputs(col,stdout);
        printf("%2d. [%-12s] %s\n",i+1,c->trace[i].kind,c->trace[i].detail);
        if(tui_colour)fputs(T_RESET,stdout);
    }
    if(c->trace_dropped)warnf("... %lu older trace events dropped\n",c->trace_dropped);
}

static void help(void)
{
    cprintf(T_BOLD T_CYAN,"\nVibeSolaris TUI commands\n");
    cprintf(T_DIM,"  Commands begin with /. Ordinary text is sent to the selected model.\n\n");
    cprintf(T_BOLD T_BLUE,"Connection and model\n");
    cprintf(T_CYAN,"  /provider NAME                 ");printf("openai claude gemini glm glm-coding kimi qwen ernie deepseek custom\n");
    cprintf(T_CYAN,"  /protocol openai|anthropic    ");printf("select protocol where supported\n");
    cprintf(T_CYAN,"  /model MODEL                  ");printf("set model for the active provider\n");
    cprintf(T_CYAN,"  /base URL                     ");printf("set API endpoint for active provider/protocol\n");
    cprintf(T_CYAN,"  /key KEY                      ");printf("set the active provider API key (not echoed)\n");
    cprintf(T_BOLD T_BLUE,"\nOAuth / OpenAI application login\n");
    cprintf(T_CYAN,"  /oauth status                 ");printf("show OAuth configuration/session status\n");
    cprintf(T_CYAN,"  /oauth client CLIENT_ID\n");
    cprintf(T_CYAN,"  /oauth authorize URL\n");
    cprintf(T_CYAN,"  /oauth token URL\n");
    cprintf(T_CYAN,"  /oauth scopes SCOPE_LIST\n");
    cprintf(T_CYAN,"  /oauth redirect URL\n");
    cprintf(T_CYAN,"  /oauth login | /oauth logout | /oauth save\n");
    cprintf(T_DIM,"  /login is an alias for /oauth login. Use only OAuth values officially issued for this app.\n");
    cprintf(T_BOLD T_BLUE,"\nFiles, tools, cache and history\n");
    cprintf(T_CYAN,"  /attach PATH                  ");printf("attach a file or image to the next request\n");
    cprintf(T_CYAN,"  /clearattach                  ");printf("clear pending attachments\n");
    cprintf(T_CYAN,"  /read PATH                    ");printf("read a local file directly\n");
    cprintf(T_CYAN,"  /run COMMAND                  ");printf("run a local shell command\n");
    cprintf(T_CYAN,"  /cache on|off|status|clear\n");
    cprintf(T_CYAN,"  /history status|clear\n");
    cprintf(T_CYAN,"  /usage                        ");printf("show provider-reported token usage for the current conversation\n");
    cprintf(T_CYAN,"  /trace                        ");printf("show every model/local-tool/MCP step from the last turn\n");
    cprintf(T_BOLD T_BLUE,"\nEncrypted configuration\n");
    cprintf(T_CYAN,"  /globalconfig status|load|save\n");
    cprintf(T_DIM,"  Uses /etc/vibesolaris when writable, otherwise ~/.vibesolaris.\n");
    cprintf(T_CYAN,"  /saveconfig PATH              ");printf("save an explicit legacy/plain config file\n");
    cprintf(T_BOLD T_BLUE,"\nMCP\n");
    cprintf(T_CYAN,"  /mcp list | /mcp refresh | /mcp tools\n");
    cprintf(T_CYAN,"  /mcp add-stdio NAME COMMAND   ");printf("local MCP server (/mcp add-stdin is an alias)\n");
    cprintf(T_CYAN,"  /mcp add-http NAME URL [TOKEN]\n");
    cprintf(T_CYAN,"  /mcp remove NAME\n");
    cprintf(T_BOLD T_BLUE,"\nDisplay\n");
    cprintf(T_CYAN,"  /colour auto|on|off|status    ");printf("ANSI colour control; /color remains a compatibility alias; NO_COLOR is respected in auto mode\n");
    cprintf(T_CYAN,"  /help                         ");printf("show this help\n");
    cprintf(T_CYAN,"  /quit                         ");printf("exit\n");
}

static void cache_status(const VSContext *c)
{
    cprintf(T_BOLD T_BLUE,"Cache status\n");
    printf("  cache=%s\n",c->cache_enabled ? "on" : "off");
    printf("  key=%s\n",c->cache_key);
    printf("  local_hits=%lu  local_misses=%lu\n",c->file_cache_hits,c->file_cache_misses);
    printf("  provider_cached_tokens=%ld  provider_cache_write_tokens=%ld\n",c->provider_cached_tokens,c->provider_cache_write_tokens);
    printf("  history_messages=%d  history_bytes=%lu  history_evicted=%lu\n",c->history_count,(unsigned long)c->history_bytes,c->history_evicted);
}

static void auth_status(const VSContext *c)
{
    long remain;
    cprintf(T_BOLD T_BLUE,"Authentication status\n");
    printf("  OpenAI API key: %s\n", c->provider_api_keys[VS_PROVIDER_OPENAI][0] ? "configured" : "not set");
    printf("  OAuth profile: %s\n", vs_oauth_profile_path());
    printf("  OAuth configuration: %s\n", vs_oauth_is_configured(c) ? "ready" : "incomplete");
    printf("  OAuth session: %s\n", vs_oauth_is_signed_in(c) ? "signed in" : (c->oauth.access_token[0] ? "expired / refresh required" : "not signed in"));
    printf("  Client ID: %s\n", c->oauth.client_id[0] ? c->oauth.client_id : "(not set)");
    printf("  Authorisation URL: %s\n", c->oauth.authorize_url[0] ? c->oauth.authorize_url : "(not set)");
    printf("  Token URL: %s\n", c->oauth.token_url[0] ? c->oauth.token_url : "(not set)");
    printf("  Scopes: %s\n", c->oauth.scopes[0] ? c->oauth.scopes : "(not set)");
    printf("  Redirect URI: %s\n", c->oauth.redirect_uri[0] ? c->oauth.redirect_uri : "(not set)");
    if(c->oauth.expires_at>0){remain=c->oauth.expires_at-(long)time(NULL);if(remain<0)remain=0;printf("  Access-token lifetime remaining: %ld seconds\n",remain);}
    cprintf(T_DIM,"  VibeSolaris never asks for or stores your ChatGPT password.\n");
}

static void oauth_set(VSContext *c,const char *which,const char *value)
{
    if(!strcmp(which,"client"))copyv(c->oauth.client_id,sizeof(c->oauth.client_id),value);
    else if(!strcmp(which,"authorize"))copyv(c->oauth.authorize_url,sizeof(c->oauth.authorize_url),value);
    else if(!strcmp(which,"token"))copyv(c->oauth.token_url,sizeof(c->oauth.token_url),value);
    else if(!strcmp(which,"scopes"))copyv(c->oauth.scopes,sizeof(c->oauth.scopes),value);
    else if(!strcmp(which,"redirect"))copyv(c->oauth.redirect_uri,sizeof(c->oauth.redirect_uri),value);
    else {errorf("Unknown OAuth setting: %s\n",which);return;}
    if(vs_oauth_save_profile(c)==0){(void)vs_persist_settings(c);successf("OAuth %s updated and saved.\n",which);}else warnf("OAuth %s updated, but profile save failed.\n",which);
}

static void show_banner(const VSContext *c)
{
    cprintf(T_BOLD T_CYAN,"VibeSolaris %s TUI\n",VS_VERSION);
    cprintf(T_DIM,"  %s %s / %s\n",c->os_name,c->os_release,c->arch);
    cprintf(T_BLUE,"  provider=%s  model=%s  protocol=%s  cache=%s\n",c->provider.name,c->provider.model,vs_protocol_name(c->provider.protocol),c->cache_enabled?"on":"off");
    cprintf(T_DIM,"  Type /help for commands. Type a message to start chatting.\n");
}

int main(int argc, char **argv)
{
    VSContext c;
    char line[4096];
    int i;
    const char *config_path=NULL;
    for(i=1;i<argc;i++){
        if(!strcmp(argv[i],"--no-colour")||!strcmp(argv[i],"--no-color"))tui_colour_mode=0;
        else if(!strcmp(argv[i],"--colour")||!strcmp(argv[i],"--color"))tui_colour_mode=1;
        else if(!strcmp(argv[i],"--help")){colour_refresh();help();return 0;}
        else config_path=argv[i];
    }
    colour_refresh();
    vs_init(&c);
    if (config_path) vs_load_config(&c, config_path);
    show_banner(&c);
    while (1) {
        printf("\n");cprintf(T_BOLD T_CYAN,"vs> ");fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(line, "/quit")) break;
        else if (!strcmp(line,"/help"))help();
        else if (!strcmp(line,"/colour")||!strcmp(line,"/colour status")||!strcmp(line,"/color")||!strcmp(line,"/color status")){
            printf("colour=%s mode=%s TERM=%s NO_COLOR=%s\n",tui_colour?"on":"off",tui_colour_mode<0?"auto":(tui_colour_mode?"on":"off"),getenv("TERM")?getenv("TERM"):"(unset)",getenv("NO_COLOR")?"set":"not set");
        } else if (!strncmp(line,"/colour ",8) || !strncmp(line,"/color ",7)){
            const char *v=!strncmp(line,"/colour ",8)?line+8:line+7;
            if(!strcmp(v,"auto"))tui_colour_mode=-1;
            else if(!strcmp(v,"on")||!strcmp(v,"always"))tui_colour_mode=1;
            else if(!strcmp(v,"off")||!strcmp(v,"never"))tui_colour_mode=0;
            else {errorf("usage: /colour auto|on|off|status\n");continue;}
            colour_refresh();printf("colour=%s (mode=%s)\n",tui_colour?"on":"off",tui_colour_mode<0?"auto":(tui_colour_mode?"on":"off"));
        } else if (!strncmp(line, "/provider ", 10)) {
            if(vs_set_provider(&c, line + 10)==0){if(vs_persist_settings(&c)==0)successf("provider=%s model=%s (saved)\n", c.provider.name,c.provider.model);else errorf("provider changed, but encrypted config could not be saved\n");}else errorf("unknown provider\n");
        } else if (!strncmp(line, "/protocol ", 10)) {
            if(vs_set_protocol(&c,line+10)==0)successf("protocol=%s base=%s\n",vs_protocol_name(c.provider.protocol),c.provider.base_url);
            else errorf("protocol not supported by provider %s\n",c.provider.name);
        } else if (!strncmp(line, "/key ", 5)) {
            vs_set_api_key(&c, line + 5);successf("API key saved for %s\n", c.provider.name);
        } else if (!strncmp(line, "/model ", 7)) {
            vs_set_model(&c,line+7);if(vs_persist_settings(&c)==0)successf("model=%s (saved for %s)\n", c.provider.model,c.provider.name);else errorf("model=%s, but encrypted config could not be saved\n",c.provider.model);
        } else if (!strncmp(line, "/base ", 6)) {
            vs_set_base_url(&c,line+6);successf("base=%s\n", c.provider.base_url);
        } else if (!strcmp(line, "/oauth status") || !strcmp(line, "/auth") || !strcmp(line, "/auth status")) {
            auth_status(&c);
        } else if (!strncmp(line, "/oauth client ", 14)) {
            oauth_set(&c,"client",line+14);
        } else if (!strncmp(line, "/oauth authorize ", 17)) {
            oauth_set(&c,"authorize",line+17);
        } else if (!strncmp(line, "/oauth token ", 13)) {
            oauth_set(&c,"token",line+13);
        } else if (!strncmp(line, "/oauth scopes ", 14)) {
            oauth_set(&c,"scopes",line+14);
        } else if (!strncmp(line, "/oauth redirect ", 16)) {
            oauth_set(&c,"redirect",line+16);
        } else if (!strcmp(line, "/oauth save")) {
            if(vs_oauth_save_profile(&c)==0){(void)vs_persist_settings(&c);successf("OAuth profile and encrypted config saved.\n");}else errorf("OAuth profile save failed.\n");
        } else if (!strcmp(line, "/oauth login") || !strcmp(line, "/login")) {
            char msg[1024];
            if(!vs_oauth_is_configured(&c))errorf("OAuth is not configured. Set Client ID, authorisation URL, token URL, scopes, and redirect URI first.\n");
            else {warnf("Starting OAuth 2.0 Authorisation Code + PKCE login; your browser should open.\n");msg[0]=0;if(vs_oauth_login_blocking(&c,msg,sizeof(msg))>0)successf("%s\n",msg);else errorf("OAuth login failed: %s\n",msg[0]?msg:"unknown error");}
        } else if (!strcmp(line, "/oauth logout") || !strcmp(line, "/logout")) {
            vs_oauth_logout(&c);successf("OAuth tokens cleared.\n");
        } else if (!strncmp(line, "/attach ", 8)) {
            if(vs_attach(&c,line+8)==0)successf("attached: %s\n",line+8);else errorf("attach failed: %s\n",line+8);
        } else if (!strcmp(line, "/clearattach")) {
            vs_clear_attachments(&c);successf("attachments cleared\n");
        } else if (!strncmp(line, "/read ", 6)) {
            char *x = vs_cached_read_file(&c, line + 6);if(x){cprintf(T_DIM,"--- %s ---\n",line+6);printf("%s\n",x);}else errorf("read failed: %s\n",line+6);free(x);
        } else if (!strncmp(line, "/run ", 5)) {
            int st;char *x=vs_run_command(line+5,&st);cprintf(T_YELLOW,"$ %s\n",line+5);if(x)printf("%s\n",x);if(st==0)successf("exit %d\n",st);else errorf("exit %d\n",st);free(x);
        } else if (!strcmp(line, "/cache on")) {
            c.cache_enabled=1;(void)vs_persist_settings(&c);successf("cache enabled\n");
        } else if (!strcmp(line, "/cache off")) {
            c.cache_enabled=0;(void)vs_persist_settings(&c);warnf("cache disabled\n");
        } else if (!strcmp(line, "/cache clear")) {
            vs_cache_clear(&c);successf("local cache and cache statistics cleared\n");
        } else if (!strcmp(line, "/cache") || !strcmp(line, "/cache status")) {
            cache_status(&c);
        } else if (!strcmp(line, "/history clear")) {
            vs_history_clear(&c);successf("conversation history cleared\n");
        } else if (!strcmp(line, "/history") || !strcmp(line, "/history status")) {
            printf("history_messages=%d history_bytes=%lu history_evicted=%lu\n",c.history_count,(unsigned long)c.history_bytes,c.history_evicted);
            usage_status(&c);
        } else if (!strcmp(line, "/usage")) {
            usage_status(&c);
        } else if (!strncmp(line, "/saveconfig ", 12)) {
            if(vs_save_config(&c,line+12)==0)successf("saved: %s\n",line+12);else errorf("save failed: %s\n",line+12);
        } else if (!strcmp(line, "/globalconfig") || !strcmp(line, "/globalconfig status")) {
            char pth[VS_MAX_PATH];if(vs_secure_config_path(pth,sizeof(pth))!=0)strcpy(pth,"(path unavailable)");cprintf(T_BOLD T_BLUE,"Encrypted config\n");printf("  status=%s\n  scope=%s\n  path=%s\n",vs_global_config_exists()?"present":"not found",vs_secure_config_is_per_user()?"per-user":"system",pth);
        } else if (!strcmp(line, "/globalconfig load")) {
            char msg[512];msg[0]=0;if(vs_global_config_load(&c,msg,sizeof(msg))==0)successf("%s\n",msg);else errorf("secure config load failed: %s\n",msg[0]?msg:"unknown error");
        } else if (!strcmp(line, "/globalconfig save")) {
            char msg[512];msg[0]=0;if(vs_global_config_save(&c,msg,sizeof(msg))==0)successf("%s\n",msg);else errorf("secure config save failed: %s\n",msg[0]?msg:"unknown error");
        } else if (!strcmp(line, "/mcp list")) {
            if(!c.mcp_server_count)warnf("No MCP servers configured.\n");
            for(i=0;i<c.mcp_server_count;i++){cprintf(T_MAGENTA,"%d: %s",i,c.mcp_servers[i].name);printf(" [%s] %s%s\n",c.mcp_servers[i].transport==VS_MCP_HTTP?"http":"stdio",c.mcp_servers[i].target,c.mcp_servers[i].enabled?"":" (disabled)");}
        } else if (!strcmp(line, "/mcp refresh")) {
            int n=vs_mcp_refresh_all(&c,1);if(n>=0)successf("MCP catalogue refreshed: %d tool(s)\n",n);else errorf("MCP refresh failed\n");
        } else if (!strcmp(line, "/mcp tools")) {
            if(!c.mcp_tool_count)(void)vs_mcp_refresh_all(&c,0);
            if(!c.mcp_tool_count)warnf("No MCP tools discovered.\n");
            for(i=0;i<c.mcp_tool_count;i++){
                cprintf(T_MAGENTA,"%s.%s",c.mcp_tools[i].server,c.mcp_tools[i].name);
                printf(" - %s\n",c.mcp_tools[i].description);
            }
        } else if (!strncmp(line, "/mcp add-stdio ", 15) || !strncmp(line, "/mcp add-stdin ", 15)) {
            char *p=line+15,*sp=strchr(p,' ');if(!sp)errorf("usage: /mcp add-stdio NAME COMMAND\n");else {*sp=0;if(vs_mcp_add_stdio(&c,p,sp+1)==0)successf("stdio MCP saved: %s\n",p);else errorf("MCP add failed\n");}
        } else if (!strncmp(line, "/mcp add-http ", 14)) {
            char *p=line+14,*sp=strchr(p,' '),*url,*tok=NULL,*sp2;if(!sp)errorf("usage: /mcp add-http NAME URL [TOKEN]\n");else {*sp=0;url=sp+1;sp2=strchr(url,' ');if(sp2){*sp2=0;tok=sp2+1;}if(vs_mcp_add_http(&c,p,url,tok)==0)successf("remote MCP saved: %s\n",p);else errorf("MCP add failed\n");}
        } else if (!strncmp(line, "/mcp remove ", 12)) {
            if(vs_mcp_remove(&c,line+12)==0)successf("MCP server removed: %s\n",line+12);else errorf("MCP server not found: %s\n",line+12);
        } else if (!strcmp(line, "/trace")) {
            cprintf(T_BOLD T_MAGENTA,"Activity trace\n");print_trace(&c);
        } else if (line[0] == '/') {
            errorf("Unknown command: %s\n",line);cprintf(T_DIM,"Type /help to see available commands.\n");
        } else if(line[0]) {
            char *a;
            cprintf(T_BOLD T_MAGENTA,"\nActivity (live)\n");
            vs_set_trace_callback(&c,live_trace,NULL);
            a=vs_agent_turn(&c,line);
            vs_set_trace_callback(&c,NULL,NULL);
            cprintf(T_BOLD T_GREEN,"\nVibeSolaris answer\n");
            printf("%s\n",a?a:"(no response)");
            usage_status(&c);
            free(a);vs_clear_attachments(&c);
        }
    }
    vs_shutdown(&c);
    cprintf(T_DIM,"\nGoodbye.\n");
    return 0;
}
