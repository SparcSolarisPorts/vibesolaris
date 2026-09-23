/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>

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
static VSContext *tui_active_ctx = NULL;
static void cprintf(const char *colour,const char *fmt,...);
static int tui_output_mode = 0; /* 0 compact, 1 normal, 2 full */

#define TUI_HISTORY_MAX 32
static char tui_history[TUI_HISTORY_MAX][4096];
static int tui_history_count = 0;

static const char *tui_completions[] = {
    "/help","/quit","/usage","/trace","/trace full","/output compact","/output normal","/output full","/output status",
    "/provider openai","/provider claude","/provider gemini","/provider glm","/provider glm-coding","/provider kimi","/provider qwen","/provider ernie","/provider deepseek","/provider custom",
    "/protocol openai","/protocol anthropic","/model ","/base ","/key ","/proxy status","/proxy on","/proxy off",
    "/oauth status","/oauth login","/oauth logout","/oauth save","/login","/logout",
    "/attach ","/clearattach","/read ","/run ","/web ","/fetch ","/graph files","/graph fields","/graph dot files ","/graph dot fields ",
    "/cache status","/cache on","/cache off","/cache clear","/history status","/history clear",
    "/globalconfig status","/globalconfig load","/globalconfig save","/saveconfig ",
    "/mcp list","/mcp refresh","/mcp tools","/mcp add-stdio ","/mcp add-http ","/mcp remove ",
    "/colour auto","/colour on","/colour off","/colour status",NULL
};

static unsigned long count_lines(const char *s)
{
    unsigned long n=1;if(!s||!*s)return 0;for(;*s;s++)if(*s=='\n')n++;return n;
}

static void one_line_preview(const char *s,char *out,size_t cap,size_t maxchars)
{
    size_t n=0;int space=0;if(!out||cap==0)return;out[0]=0;if(!s)return;
    while(*s&&n+1<cap&&n<maxchars){unsigned char c=(unsigned char)*s++;if(c=='\n'||c=='\r'||c=='\t'||c==' '){space=1;continue;}if(space&&n&&n+1<cap)out[n++]=' ';space=0;if(c<32||c==127)c='?';out[n++]=(char)c;}out[n]=0;
}

static void tui_print_detail(const char *kind,const char *detail)
{
    size_t bytes=detail?strlen(detail):0;unsigned long lines=count_lines(detail);char preview[320];char *q;
    if(!detail)detail="";
    if(tui_output_mode>=2){printf("%s",detail);if(bytes&&detail[bytes-1]!='\n')putchar('\n');return;}
    if(tui_output_mode==1 && bytes<=4096 && lines<=40){printf("%s",detail);if(bytes&&detail[bytes-1]!='\n')putchar('\n');return;}
    if(tui_output_mode==1){q=vs_compact_text_limit(detail,4096,"TUI display compacted; use /trace full for complete output");if(q){printf("%s",q);if(*q&&q[strlen(q)-1]!='\n')putchar('\n');free(q);}return;}
    if(bytes<=260&&lines<=3){printf("%s",detail);if(bytes&&detail[bytes-1]!='\n')putchar('\n');return;}
    one_line_preview(detail,preview,sizeof(preview),220);
    if(kind&&(!strcmp(kind,"model-input")||!strcmp(kind,"tool-output")||!strcmp(kind,"command-output")))
        printf("%s%s[%lu bytes, %lu lines; display compacted]\n",preview,*preview?" ... ":"",(unsigned long)bytes,lines);
    else printf("%s%s[%lu bytes, %lu lines]\n",preview,*preview?" ... ":"",(unsigned long)bytes,lines);
}

static void tui_print_bounded(const char *s,size_t compact_limit,size_t normal_limit)
{
    size_t limit;char *q;if(!s)return;if(tui_output_mode>=2){printf("%s",s);if(*s&&s[strlen(s)-1]!='\n')putchar('\n');return;}
    limit=tui_output_mode==1?normal_limit:compact_limit;q=vs_compact_text_limit(s,limit,"display output shortened; switch /output full to print everything");if(q){printf("%s",q);if(*q&&q[strlen(q)-1]!='\n')putchar('\n');free(q);}
}

static void tui_prompt(void){cprintf(T_BOLD T_CYAN,"vs> ");fflush(stdout);}

static void history_add_line(const char *s)
{
    int i;if(!s||!*s)return;if(tui_history_count&& !strcmp(tui_history[tui_history_count-1],s))return;
    if(tui_history_count>=TUI_HISTORY_MAX){for(i=1;i<TUI_HISTORY_MAX;i++)memcpy(tui_history[i-1],tui_history[i],sizeof(tui_history[0]));tui_history_count=TUI_HISTORY_MAX-1;}
    {size_t n=strlen(s);if(n>=sizeof(tui_history[0]))n=sizeof(tui_history[0])-1;memcpy(tui_history[tui_history_count],s,n);tui_history[tui_history_count][n]=0;}tui_history_count++;
}

static void redraw_input_line(const char *buf)
{
    fputs("\r\033[2K",stdout);tui_prompt();fputs(buf?buf:"",stdout);fflush(stdout);
}

static size_t common_prefix_len(const char **m,int n)
{
    size_t p=0;int i;if(n<=0)return 0;while(m[0][p]){for(i=1;i<n;i++)if(m[i][p]!=m[0][p])return p;p++;}return p;
}

static void complete_slash(char *buf,size_t cap,size_t *len)
{
    const char *m[64];int i,n=0;size_t pre,cur=*len;if(!buf||!len||buf[0]!='/')return;
    for(i=0;tui_completions[i]&&n<(int)(sizeof(m)/sizeof(m[0]));i++)if(!strncmp(tui_completions[i],buf,cur))m[n++]=tui_completions[i];
    if(n==0){fputc('\a',stdout);return;}pre=common_prefix_len(m,n);
    if(pre>cur){size_t add=pre-cur;if(cur+add>=cap)add=cap-cur-1;memcpy(buf+cur,m[0]+cur,add);cur+=add;buf[cur]=0;*len=cur;fwrite(m[0]+(cur-add),1,add,stdout);fflush(stdout);return;}
    if(n==1&&cur+1<cap&&m[0][cur]==0&&cur>0&&buf[cur-1]!=' '){buf[cur++]=' ';buf[cur]=0;*len=cur;fputc(' ',stdout);fflush(stdout);return;}
    printf("\n");for(i=0;i<n&&i<12;i++){cprintf(T_CYAN,"  %s",m[i]);if((i%2)==1||i==n-1||i==11)printf("\n");else printf("    ");}if(n>12)printf("  ... %d more\n",n-12);redraw_input_line(buf);
}

static int tui_read_line(char *buf,size_t cap)
{
    struct termios oldt,raw;size_t len=0;int hist=tui_history_count;unsigned char ch;ssize_t rr;
    if(!isatty(STDIN_FILENO)||tcgetattr(STDIN_FILENO,&oldt)!=0){if(!fgets(buf,(int)cap,stdin))return 0;buf[strcspn(buf,"\r\n")]=0;return 1;}
    raw=oldt;raw.c_lflag&=(tcflag_t)~(ICANON|ECHO);raw.c_iflag&=(tcflag_t)~(IXON|ICRNL);raw.c_cc[VMIN]=1;raw.c_cc[VTIME]=0;if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw)!=0){if(!fgets(buf,(int)cap,stdin))return 0;buf[strcspn(buf,"\r\n")]=0;return 1;}
    buf[0]=0;
    for(;;){rr=read(STDIN_FILENO,&ch,1);if(rr<=0){if(rr<0&&errno==EINTR)continue;tcsetattr(STDIN_FILENO,TCSAFLUSH,&oldt);return 0;}
        if(ch=='\r'||ch=='\n'){putchar('\n');break;}
        if(ch==4){if(len==0){tcsetattr(STDIN_FILENO,TCSAFLUSH,&oldt);return 0;}continue;}
        if(ch==127||ch==8){if(len){len--;buf[len]=0;fputs("\b \b",stdout);fflush(stdout);}continue;}
        if(ch==21){while(len){fputs("\b \b",stdout);len--;}buf[0]=0;fflush(stdout);continue;}
        if(ch==12){fputs("\033[2J\033[H",stdout);redraw_input_line(buf);continue;}
        if(ch=='\t'){complete_slash(buf,cap,&len);continue;}
        if(ch==27){unsigned char a=0,b=0;if(read(STDIN_FILENO,&a,1)==1&&a=='['&&read(STDIN_FILENO,&b,1)==1&&(b=='A'||b=='B')){if(b=='A'&&hist>0)hist--;else if(b=='B'&&hist<tui_history_count)hist++;if(hist>=0&&hist<tui_history_count){strncpy(buf,tui_history[hist],cap-1);buf[cap-1]=0;len=strlen(buf);}else{buf[0]=0;len=0;}redraw_input_line(buf);}continue;}
        if(ch>=32&&ch!=127){if(len+1<cap){buf[len++]=(char)ch;buf[len]=0;(void)write(STDOUT_FILENO,&ch,1);}else fputc('\a',stdout);continue;}
    }
    tcsetattr(STDIN_FILENO,TCSAFLUSH,&oldt);history_add_line(buf);return 1;
}

static void tui_sigint(int sig)
{
    static const char msg[]="\nStop requested (Ctrl+C). Cancelling the active agent operation...\n";
    (void)sig;
    if(tui_active_ctx)tui_active_ctx->cancel_requested=1;
    (void)write(STDOUT_FILENO,msg,sizeof(msg)-1);
}

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
    if(strstr(kind,"reasoning"))return T_MAGENTA;
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
    printf("%2d. [%-12s] ",step,kind?kind:"step");
    tui_print_detail(kind,detail);
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
        printf("%2d. [%-12s] ",i+1,c->trace[i].kind);
        tui_print_detail(c->trace[i].kind,c->trace[i].detail?c->trace[i].detail:"");
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
    cprintf(T_CYAN,"  /proxy VALUE                  ");printf("route HTTP API/MCP/OAuth calls via ip:port or user:pass@ip:port\n");
    cprintf(T_CYAN,"  /proxy on|off|status          ");printf("enable, disable, or inspect the saved proxy\n");
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
    cprintf(T_CYAN,"  /run COMMAND                  ");printf("run a local shell command (display is bounded by default)\n");
    cprintf(T_CYAN,"  /web QUERY                    ");printf("search the web without leaving VibeSolaris\n");
    cprintf(T_CYAN,"  /fetch URL                    ");printf("fetch a web page as compact readable text\n");
    cprintf(T_CYAN,"  /graph files|fields [PATH]    ");printf("inspect project relationships; add dot MODE PATH OUT.dot to export\n");
    cprintf(T_CYAN,"  /cache on|off|status|clear\n");
    cprintf(T_CYAN,"  /history status|clear\n");
    cprintf(T_CYAN,"  /usage                        ");printf("show provider-reported token usage for the current conversation\n");
    cprintf(T_CYAN,"  /trace                        ");printf("show compact activity trace; /trace full shows the bounded stored trace\n");
    cprintf(T_CYAN,"  Ctrl+C during agent work      ");printf("stop the active provider/MCP/command operation without exiting VibeSolaris\n");
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
    cprintf(T_CYAN,"  /output compact|normal|full   ");printf("control how much command/trace output is painted to the terminal\n");
    cprintf(T_DIM,"  Tab completes / commands. Up/Down recalls history. Ctrl+U clears input; Ctrl+L clears the screen.\n");
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

static char *tui_next_word(char **cursor)
{
    char *p,*start;
    if(!cursor||!*cursor)return NULL;
    p=*cursor;while(*p==' '||*p=='\t')p++;
    if(!*p){*cursor=p;return NULL;}
    start=p;while(*p&&*p!=' '&&*p!='\t')p++;
    if(*p)*p++=0;
    *cursor=p;return start;
}

static char *tui_rest(char **cursor)
{
    char *p;if(!cursor||!*cursor)return NULL;p=*cursor;while(*p==' '||*p=='\t')p++;*cursor=p;return *p?p:NULL;
}

static void show_banner(const VSContext *c)
{
    cprintf(T_BOLD T_CYAN,"VibeSolaris %s TUI\n",VS_VERSION);
    cprintf(T_DIM,"  %s %s / %s\n",c->os_name,c->os_release,c->arch);
    cprintf(T_BLUE,"  provider=%s  model=%s  protocol=%s  cache=%s\n",c->provider.name,c->provider.model,vs_protocol_name(c->provider.protocol),c->cache_enabled?"on":"off");
    cprintf(T_DIM,"  Type /help for commands. Tab completes / commands; long output is compacted on screen.\n");
}

int main(int argc, char **argv)
{
    VSContext c;
    /* Agent activity is interactive progress, not batch output.  Disable stdio
       buffering so terminals and pseudo-terminals never hold a run of steps. */
    setvbuf(stdout,NULL,_IONBF,0);
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
        printf("\n");tui_prompt();
        if (!tui_read_line(line, sizeof(line))) break;
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
        } else if (!strcmp(line, "/proxy") || !strcmp(line, "/proxy status")) {
            char pb[1024];vs_proxy_redacted(&c,pb,sizeof(pb));printf("proxy=%s  enabled=%s\n",pb,c.proxy_enabled?"yes":"no");
        } else if (!strncmp(line, "/proxy ", 7)) {
            char pb[1024];if(vs_set_proxy(&c,line+7)==0){vs_proxy_redacted(&c,pb,sizeof(pb));successf("proxy=%s\n",pb);}else errorf("proxy must be ip:port or user:pass@ip:port; use /proxy off to disable\n");
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
            char *x = vs_cached_read_file(&c, line + 6);if(x){cprintf(T_DIM,"--- %s ---\n",line+6);tui_print_bounded(x,12U*1024U,64U*1024U);}else errorf("read failed: %s\n",line+6);free(x);
        } else if (!strncmp(line, "/run ", 5)) {
            int st;char *x=vs_run_command_ctx(&c,line+5,&st);cprintf(T_YELLOW,"$ %s\n",line+5);if(x)tui_print_bounded(x,4U*1024U,64U*1024U);if(st==0)successf("exit %d\n",st);else errorf("exit %d\n",st);free(x);
        } else if (!strcmp(line,"/output") || !strcmp(line,"/output status")) {
            printf("output=%s\n",tui_output_mode==0?"compact":(tui_output_mode==1?"normal":"full"));
        } else if (!strncmp(line,"/output ",8)) {
            const char *v=line+8;if(!strcmp(v,"compact"))tui_output_mode=0;else if(!strcmp(v,"normal"))tui_output_mode=1;else if(!strcmp(v,"full"))tui_output_mode=2;else {errorf("usage: /output compact|normal|full|status\n");continue;}successf("output display=%s\n",v);
        } else if (!strncmp(line,"/web ",5)) {
            char *x=vs_web_search(&c,line+5,8);if(x){cprintf(T_BOLD T_BLUE,"Web search\n");tui_print_bounded(x,20U*1024U,64U*1024U);}else errorf("web search failed\n");free(x);
        } else if (!strncmp(line,"/fetch ",7)) {
            char *x=vs_web_fetch(&c,line+7,128U*1024U);if(x){cprintf(T_BOLD T_BLUE,"Web page\n");tui_print_bounded(x,24U*1024U,96U*1024U);}else errorf("web fetch failed\n");free(x);
        } else if (!strncmp(line,"/graph ",7)) {
            char tmp[4096],*cur,*p0,*p1,*p2,*p3;VSGraph g;VSGraphMode mode;char *sum;
            copyv(tmp,sizeof(tmp),line+7);cur=tmp;p0=tui_next_word(&cur);p1=tui_next_word(&cur);
            if(p0&&!strcmp(p0,"dot")){
                p2=tui_next_word(&cur);p3=tui_rest(&cur);
                mode=(p1&&(!strcmp(p1,"fields")||!strcmp(p1,"field")))?VS_GRAPH_FIELDS:VS_GRAPH_FILES;
                if(!p1||!p2||!p3){errorf("usage: /graph dot files|fields PATH OUTPUT.dot\n");continue;}
                if(vs_graph_build(&c,p2,mode,&g)<0||vs_graph_export_dot(&g,p3)!=0)errorf("graph export failed\n");
                else successf("graph exported: %s (%d nodes, %d edges)\n",p3,g.node_count,g.edge_count);
            }else{
                mode=(p0&&(!strcmp(p0,"fields")||!strcmp(p0,"field")))?VS_GRAPH_FIELDS:VS_GRAPH_FILES;
                if(p0&&strcmp(p0,"files")&&strcmp(p0,"file")&&strcmp(p0,"fields")&&strcmp(p0,"field")){errorf("usage: /graph files|fields [PATH]\n");continue;}
                if(vs_graph_build(&c,p1&&*p1?p1:c.cwd,mode,&g)<0){errorf("graph build failed\n");continue;}
                sum=vs_graph_summary(&g,100);if(sum){cprintf(T_BOLD T_BLUE,"Project graph\n");tui_print_bounded(sum,24U*1024U,64U*1024U);free(sum);}
            }
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
        } else if (!strcmp(line, "/trace") || !strcmp(line,"/trace full")) {
            int old=tui_output_mode;if(!strcmp(line,"/trace full"))tui_output_mode=2;cprintf(T_BOLD T_MAGENTA,"Activity trace\n");print_trace(&c);tui_output_mode=old;
        } else if (line[0] == '/') {
            errorf("Unknown command: %s\n",line);cprintf(T_DIM,"Type /help to see available commands.\n");
        } else if(line[0]) {
            char *a;void (*oldint)(int);
            cprintf(T_BOLD T_MAGENTA,"\nActivity (live)\n");
            vs_cancel_clear(&c);
            tui_active_ctx=&c;oldint=signal(SIGINT,tui_sigint);
            vs_set_trace_callback(&c,live_trace,NULL);
            a=vs_agent_turn(&c,line);
            vs_set_trace_callback(&c,NULL,NULL);
            tui_active_ctx=NULL;if(oldint!=SIG_ERR)(void)signal(SIGINT,oldint);
            cprintf(T_BOLD T_GREEN,"\nVibeSolaris answer\n");
            tui_print_bounded(a?a:"(no response)",32U*1024U,128U*1024U);
            usage_status(&c);
            free(a);vs_clear_attachments(&c);vs_cancel_clear(&c);
        }
    }
    vs_shutdown(&c);
    cprintf(T_DIM,"\nGoodbye.\n");
    return 0;
}