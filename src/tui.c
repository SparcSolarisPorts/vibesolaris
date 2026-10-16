/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include "tui_stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <stdarg.h>
#include <signal.h>
#include <termios.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>

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
static pthread_mutex_t spin_mutex=PTHREAD_MUTEX_INITIALIZER;
static void cprintf(const char *colour,const char *fmt,...);
static int tui_effort = 0; /* 0 default, 1 low, 2 medium, 3 high */
static int tui_output_mode = 0; /* 0 compact, 1 normal, 2 full */

#define TUI_HISTORY_MAX 32
static char tui_history[TUI_HISTORY_MAX][4096];
static int tui_history_count = 0;

static const char *tui_completions[] = {
    "/help","/quit","/effort","/effort default","/effort low","/effort medium","/effort high","/usage","/trace","/trace full","/output compact","/output normal","/output full","/output status",
    "/provider openai","/provider claude","/provider gemini","/provider glm","/provider glm-coding","/provider kimi","/provider qwen","/provider ernie","/provider deepseek","/provider custom",
    "/protocol openai","/protocol anthropic","/anthropic-auth bearer","/anthropic-auth x-api-key","/model ","/base ","/key ","/proxy status","/proxy on","/proxy off",
    "/oauth status","/oauth login","/oauth logout","/oauth save","/login","/logout","/claude-oauth status","/claude-oauth login","/claude-oauth logout","/subagent ","/subagents",
    "/attach ","/paste-image","/attachments","/attach-remove ","/clearattach","/read ","/run ","/web ","/fetch ","/graph files","/graph fields","/graph dot files ","/graph dot fields ",
    "/cache status","/cache on","/cache off","/cache clear","/history status","/history clear",
    "/globalconfig status","/globalconfig load","/globalconfig save","/saveconfig ",
    "/mcp list","/mcp refresh","/mcp tools","/mcp add-stdio ","/mcp add-http ","/mcp remove ",
    "/colour auto","/colour on","/colour off","/colour status","/mode auto","/mode plan","/mode manual","/mode status","/plan","/plan accept","/plan approve-action","/plan reject","/plan revise",NULL
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

static void tui_prompt(void){cprintf(T_BOLD T_CYAN,"❯ ");fflush(stdout);}
static void tui_status(const VSContext *c)
{
    char path[128];size_t n;
    if(!c||!isatty(STDOUT_FILENO))return;
    n=strlen(c->cwd);if(n>=sizeof(path)){snprintf(path,sizeof(path),"...%s",c->cwd+n-(sizeof(path)-4));}else snprintf(path,sizeof(path),"%s",c->cwd);
    cprintf(T_DIM,"%s   %s / %s   [%s]   effort: %s   %d attachment%s",path,c->provider.name,c->provider.model,vs_workflow_mode_name(c->workflow.mode),tui_effort==0?"default":tui_effort==1?"low":tui_effort==2?"medium":"high",c->attachment_count,c->attachment_count==1?"":"s");
    putchar('\n');
}

static int history_is_sensitive(const char *s)
{
    static const char *prefixes[]={"/key ","/oauth set ","/oauth token ","/claude-oauth ","/login ","/saveconfig ",NULL};int i;
    if(!s)return 0;for(i=0;prefixes[i];i++)if(!strncmp(s,prefixes[i],strlen(prefixes[i])))return 1;return 0;
}
static void history_add_line(const char *s)
{
    int i;if(!s||!*s||history_is_sensitive(s))return;if(tui_history_count&& !strcmp(tui_history[tui_history_count-1],s))return;
    if(tui_history_count>=TUI_HISTORY_MAX){for(i=1;i<TUI_HISTORY_MAX;i++)memcpy(tui_history[i-1],tui_history[i],sizeof(tui_history[0]));tui_history_count=TUI_HISTORY_MAX-1;}
    {size_t n=strlen(s);if(n>=sizeof(tui_history[0]))n=sizeof(tui_history[0])-1;memcpy(tui_history[tui_history_count],s,n);tui_history[tui_history_count][n]=0;}tui_history_count++;
}

static size_t common_prefix_len(const char **m,int n)
{
    size_t p=0;int i;if(n<=0)return 0;while(m[0][p]){for(i=1;i<n;i++)if(m[i][p]!=m[0][p])return p;p++;}return p;
}

static int tui_read_escape(unsigned char *key);
/* Repaint multiline input with terminal-width wrapping and cursor restoration.
   Erasing starts from the editor origin, which is preserved by the redraw
   algorithm rather than assuming a one-row prompt. */
static int tui_editor_cursor_row=0;
static int tui_editor_width(void)
{
    struct winsize w;
    if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&w)==0&&w.ws_col>=12)return w.ws_col;
    return 80;
}
static void tui_editor_position(const char *buf,size_t end,size_t *row,size_t *col)
{
    size_t i;int width=tui_editor_width();*row=0;*col=2;
    for(i=0;buf&&i<end&&buf[i];i++){
        unsigned char ch=(unsigned char)buf[i];
        if(ch=='\n'){(*row)++;*col=0;continue;}
        if((ch&0xc0)==0x80)continue;
        if(ch=='\t')*col+=4;else *col+=1;
        if(*col>=(size_t)width){*row+=*col/(size_t)width;*col%=(size_t)width;}
    }
}
static void tui_redraw_cursor(const char *buf,size_t cursor);
static void complete_slash(char *buf,size_t cap,size_t *len)
{
    const char *m[64];int i,n=0;size_t pre,cur=*len;
    if(!buf||!len||buf[0]!='/'||strchr(buf,'\n')||strchr(buf,' ')||strchr(buf,'\t'))return;
    for(i=0;tui_completions[i]&&n<(int)(sizeof(m)/sizeof(m[0]));i++)
        if(!strncmp(tui_completions[i],buf,cur))m[n++]=tui_completions[i];
    if(n==0){fputc('\a',stdout);return;}
    pre=common_prefix_len(m,n);
    if(pre<=cur)return;
    if(pre>=cap)pre=cap-1;
    memcpy(buf+cur,m[0]+cur,pre-cur);buf[pre]=0;*len=pre;
}

static int tui_slash_matches(const char *buf,const char **out,int cap)
{
    int i,n=0;size_t len=buf?strlen(buf):0;
    if(!buf||buf[0]!='/'||strchr(buf,'\n')||strchr(buf,' ')||strchr(buf,'\t'))return 0;
    for(i=0;tui_completions[i]&&n<cap;i++)if(!strncmp(tui_completions[i],buf,len)){
        if(out)out[n]=tui_completions[i];
        n++;
    }
    return n;
}

static void tui_clear_popup(int rows)
{
    int i;
    for(i=0;i<rows;i++)fputs("\033[1B\r\033[2K",stdout);
    if(rows)printf("\033[%dA",rows);
}

/* Bounded input composer.  The terminal owns the screen; never let a large
   paste cause terminal scrolling on every repaint.  Keep a small reserved
   viewport and paint only the visible rows using ordinary ANSI CSI sequences.
   No alternate-screen, terminal pixel API, or local clipboard dependency. */
#define TUI_COMPOSER_MAX_ROWS 6
static int tui_composer_rows=0;
static size_t tui_composer_top=0;
static int tui_composer_cursor_row=0;
static size_t tui_visual_rows(const char *buf,size_t end,size_t *cursor_row,size_t *cursor_col)
{
    size_t i,row=0,col=2;
    size_t width=(size_t)tui_editor_width();
    if(width>1)width--; /* don't write into the auto-wrap last column */
    for(i=0;buf&&i<end&&buf[i];i++){
        unsigned char ch=(unsigned char)buf[i];
        if(ch=='\n'){row++;col=0;continue;}
        if((ch&0xc0)==0x80)continue;
        if(col>=width){row++;col=0;}
        col+=(ch=='\t')?1:1;
    }
    if(col>=width){row++;col=0;}
    if(cursor_row)*cursor_row=row;
    if(cursor_col)*cursor_col=col;
    return row+1;
}
static void tui_redraw_cursor(const char *buf,size_t cursor)
{
    size_t len=buf?strlen(buf):0,cr=0,cc=0,total,top,row=0,col=2,i,painted=0;
    size_t width=(size_t)tui_editor_width();
    int j;
    if(!tui_composer_rows)return;
    if(width>1)width--;
    if(cursor>len)cursor=len;
    total=tui_visual_rows(buf,len,NULL,NULL);
    tui_visual_rows(buf,cursor,&cr,&cc);
    top=tui_composer_top;
    if(cr<top)top=cr;
    if(cr>=top+(size_t)tui_composer_rows)top=cr-(size_t)tui_composer_rows+1;
    if(total<= (size_t)tui_composer_rows)top=0;
    tui_composer_top=top;
    /* Return to the anchored first row of the editor. */
    if(tui_composer_cursor_row)printf("\033[%dA",tui_composer_cursor_row);
    fputc('\r',stdout);
    /* Clear the entire viewport before writing. */
    for(j=0;j<tui_composer_rows;j++){
        fputs("\033[2K",stdout);
        if(j+1<tui_composer_rows)fputs("\033[1B\r",stdout);
    }
    if(tui_composer_rows>1)printf("\033[%dA",tui_composer_rows-1);
    fputc('\r',stdout);
    /* Paint each visible row; no bytes ever go into the last column. */
    for(i=0;i<=len;i++){
        unsigned char ch=i<len?(unsigned char)buf[i]:0;
        if(i==0 && top==0){tui_prompt();}
        if(i==len)break;
        if(ch=='\n'){
            row++;col=0;
            if(row>top&&row<top+(size_t)tui_composer_rows){fputs("\033[1B\r",stdout);painted=row-top;}
            continue;
        }
        if((ch&0xc0)!=0x80&&col>=width){row++;col=0;if(row>top&&row<top+(size_t)tui_composer_rows){fputs("\033[1B\r",stdout);painted=row-top;}}
        if(row>=top&&row<top+(size_t)tui_composer_rows){
            if(ch=='\t')fputc(' ',stdout);
            else if(ch>=32|| (ch&0xc0)==0x80)fputc(ch,stdout);
        }
        if((ch&0xc0)!=0x80)col++;
    }
    /* Even if the full buffer extends past the viewport, the terminal
       cursor is on the last *painted* row, not on a logical offscreen row. */
    if(painted>cr-top)printf("\033[%luA",(unsigned long)(painted-(cr-top)));
    else if(painted<cr-top)printf("\033[%luB",(unsigned long)((cr-top)-painted));
    fputc('\r',stdout);
    if(cc)printf("\033[%luC",(unsigned long)cc);
    tui_composer_cursor_row=(int)(cr-top);
    fflush(stdout);
}
static void tui_redraw_editor(const char *buf,size_t cursor,int selected,int dismissed,int *old_rows)
{
    /* Suggestions are available through Tab; keep the bounded composer stable
       instead of appending a popup beyond the reserved SSH viewport. */
    (void)selected;(void)dismissed;
    if(old_rows)*old_rows=0;
    tui_redraw_cursor(buf,cursor);
}
static int tui_read_escape(unsigned char *key)
{
    fd_set set;struct timeval tv;unsigned char a,b;ssize_t n;FD_ZERO(&set);FD_SET(STDIN_FILENO,&set);tv.tv_sec=0;tv.tv_usec=80000;if(select(STDIN_FILENO+1,&set,NULL,NULL,&tv)<=0)return 0;
    n=read(STDIN_FILENO,&a,1);if(n!=1||a!='[')return 0;FD_ZERO(&set);FD_SET(STDIN_FILENO,&set);tv.tv_sec=0;tv.tv_usec=80000;if(select(STDIN_FILENO+1,&set,NULL,NULL,&tv)<=0)return 0;
    n=read(STDIN_FILENO,&b,1);if(n!=1)return 0;*key=b;return 1;
}
static size_t tui_prev_char(const char *s,size_t pos){if(!pos)return 0;pos--;while(pos>0&&(((unsigned char)s[pos]&0xc0)==0x80))pos--;return pos;}
static size_t tui_next_char(const char *s,size_t len,size_t pos){if(pos>=len)return len;pos++;while(pos<len&&(((unsigned char)s[pos]&0xc0)==0x80))pos++;return pos;}
static void tui_complete_path(char *buf,size_t cap,size_t *len,size_t *cursor)
{
    size_t begin=*cursor,plen,j;char path[2048],prefix[1024],best[1024];
    DIR *dp;struct dirent *e;int matches=0;
    begin=*cursor;
    while(begin&&buf[begin-1]!=' '&&buf[begin-1]!='\n'&&buf[begin-1]!='\t')begin--;
    plen=*cursor-begin;if(!plen||plen>=sizeof(path))return;
    memcpy(path,buf+begin,plen);path[plen]=0;
    {char *slash=strrchr(path,'/');if(slash){size_t n=(size_t)(slash-path)+1;memcpy(prefix,slash+1,strlen(slash+1)+1);slash[1]=0;}else {strcpy(prefix,path);strcpy(path,"./");}}
    if(strlen(prefix)>=sizeof(prefix))return;
    dp=opendir(path);if(!dp)return;best[0]=0;
    while((e=readdir(dp))){if(strncmp(e->d_name,prefix,strlen(prefix)))continue;
        if(!matches)snprintf(best,sizeof(best),"%s",e->d_name);
        else {for(j=0;best[j]&&e->d_name[j]&&best[j]==e->d_name[j];j++);best[j]=0;}
        matches++;
    }
    closedir(dp);
    if(matches&&strlen(best)>strlen(prefix)){
        size_t n=strlen(best)-strlen(prefix);
        if(*len+n>=cap)return;
        memmove(buf+*cursor+n,buf+*cursor,*len-*cursor+1);
        memcpy(buf+*cursor,best+strlen(prefix),n);*cursor+=n;*len+=n;
    }else fputc('\a',stdout);
}

/* Terminal bracketed paste: ESC[200~ ... ESC[201~.  Paste is one atomic
   editor operation: embedded CR/LF must never submit the prompt. */
static size_t tui_receive_paste(char *out,size_t cap,int *truncated)
{
    static const unsigned char endmark[]={27,'[','2','0','1','~'};
    unsigned char pending[sizeof(endmark)],ch;
    size_t n=0,pn=0,k;
    *truncated=0;
    for(;;){
        ssize_t got=read(STDIN_FILENO,&ch,1);
        if(got<0&&errno==EINTR)continue;
        if(got!=1)break;
        if(ch==endmark[pn]){pending[pn++]=ch;if(pn==sizeof(endmark))break;continue;}
        for(k=0;k<pn;k++){
            if(n<cap)out[n++]=(char)pending[k];else *truncated=1;
        }
        pn=0;
        if(ch==endmark[0]){pending[pn++]=ch;continue;}
        if(ch=='\r')ch='\n';
        /* Never replay clipboard control sequences as terminal commands. */
        if(ch!= '\n' && ch!='\t' && (ch<32 || ch==127))continue;
        if(n<cap)out[n++]=(char)ch;else *truncated=1;
    }
    return n;
}
#ifdef __APPLE__
/* macOS Terminal does not transmit clipboard images through stdin.  Save a
   clipboard PNG to a private temp folder and use the existing attachment API. */
static int tui_paste_clipboard_image(VSContext *c)
{
    static const char script[] =
        "on run argv\n"
        "set targetPath to POSIX file (item 1 of argv)\n"
        "set imageData to the clipboard as «class PNGf»\n"
        "set outFile to open for access targetPath with write permission\n"
        "try\n"
        "set eof outFile to 0\n"
        "write imageData to outFile\n"
        "close access outFile\n"
        "on error errMsg\n"
        "try\nclose access outFile\nend try\n"
        "error errMsg\n"
        "end try\nend run";
    char dir[]="/tmp/vibesolaris-paste-XXXXXX",path[512];
    pid_t pid;int status=0;
    if(!c||!mkdtemp(dir))return -1;
    snprintf(path,sizeof(path),"%s/image.png",dir);
    pid=fork();
    if(pid==0){execlp("osascript","osascript","-e",script,path,(char*)NULL);_exit(127);}
    if(pid<0){rmdir(dir);return -1;}
    while(waitpid(pid,&status,0)<0){if(errno!=EINTR){rmdir(dir);return -1;}}
    if(!WIFEXITED(status)||WEXITSTATUS(status)!=0||vs_attach(c,path)!=0){unlink(path);rmdir(dir);return -1;}
    return 0;
}
#else
static int tui_paste_clipboard_image(VSContext *c){(void)c;return -1;}
#endif

static int tui_read_line(char *buf,size_t cap)
{
    struct termios oldt,raw;size_t len=0,cursor=0;int hist=tui_history_count;unsigned char ch,key;ssize_t rr;char draft[4096];int browsing=0,slash_selected=0,slash_dismissed=0,popup_rows=0;
    if(!isatty(STDIN_FILENO)||tcgetattr(STDIN_FILENO,&oldt)!=0){if(!fgets(buf,(int)cap,stdin))return 0;buf[strcspn(buf,"\r\n")]=0;return 1;}
    raw=oldt;raw.c_lflag&=(tcflag_t)~(ICANON|ECHO);raw.c_iflag&=(tcflag_t)~(IXON|ICRNL);raw.c_cc[VMIN]=1;raw.c_cc[VTIME]=0;if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw)!=0){if(!fgets(buf,(int)cap,stdin))return 0;buf[strcspn(buf,"\r\n")]=0;return 1;}
    fputs("\033[?2004h",stdout);fflush(stdout);
    buf[0]=draft[0]=0;tui_editor_cursor_row=0;
    {struct winsize ws;int h=24;if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws)==0&&ws.ws_row>0)h=ws.ws_row;
     tui_composer_rows=h>10?TUI_COMPOSER_MAX_ROWS:(h>4?h-3:1);
     tui_composer_top=0;tui_composer_cursor_row=0;
     for(int k=1;k<tui_composer_rows;k++)fputc('\n',stdout);
     if(tui_composer_rows>1)printf("\033[%dA",tui_composer_rows-1);fputc('\r',stdout);}
    tui_redraw_editor(buf,cursor,slash_selected,slash_dismissed,&popup_rows);
    for(;;){
        rr=read(STDIN_FILENO,&ch,1);if(rr<=0){if(rr<0&&errno==EINTR)continue;tui_clear_popup(popup_rows);fputs("\033[?2004l",stdout);tcsetattr(STDIN_FILENO,TCSAFLUSH,&oldt);return 0;}
        if(ch=='\r'){if(!slash_dismissed&&cursor==len&&tui_slash_matches(buf,NULL,64)>0){const char *m[64];int n=tui_slash_matches(buf,m,64);if(slash_selected>=n)slash_selected=n-1;size_t z=strlen(m[slash_selected]);if(z>=cap)z=cap-1;memcpy(buf,m[slash_selected],z);buf[z]=0;len=cursor=z;slash_dismissed=1;tui_redraw_editor(buf,cursor,slash_selected,slash_dismissed,&popup_rows);continue;}tui_clear_popup(popup_rows);popup_rows=0;tui_redraw_cursor(buf,len);if(tui_composer_rows-1>tui_composer_cursor_row)printf("\033[%dB",tui_composer_rows-1-tui_composer_cursor_row);fputs("\r\n",stdout);break;}
        if(ch==4){if(len==0){tui_clear_popup(popup_rows);fputs("\033[?2004l",stdout);tcsetattr(STDIN_FILENO,TCSAFLUSH,&oldt);return 0;}continue;}
        if(ch=='\n'){if(len+1<cap){memmove(buf+cursor+1,buf+cursor,len-cursor+1);buf[cursor++]='\n';len++;slash_dismissed=1;tui_redraw_cursor(buf,cursor);}else fputc('\a',stdout);continue;}
        if(ch==127||ch==8){if(cursor){size_t prev=tui_prev_char(buf,cursor);memmove(buf+prev,buf+cursor,len-cursor+1);len-=cursor-prev;cursor=prev;slash_selected=0;slash_dismissed=0;tui_redraw_editor(buf,cursor,slash_selected,slash_dismissed,&popup_rows);}continue;}
        if(ch==21){buf[0]=0;len=cursor=0;browsing=0;slash_selected=0;slash_dismissed=0;tui_redraw_editor(buf,cursor,slash_selected,slash_dismissed,&popup_rows);continue;}
        if(ch==23){size_t start=cursor;while(start>0&&buf[start-1]==' ')start--;while(start>0&&buf[start-1]!=' ')start=tui_prev_char(buf,start);memmove(buf+start,buf+cursor,len-cursor+1);len-=cursor-start;cursor=start;slash_selected=0;slash_dismissed=0;tui_redraw_editor(buf,cursor,slash_selected,slash_dismissed,&popup_rows);continue;}
        if(ch==1){cursor=0;slash_dismissed=1;tui_redraw_cursor(buf,cursor);continue;}if(ch==5){cursor=len;slash_dismissed=0;tui_redraw_editor(buf,cursor,slash_selected,slash_dismissed,&popup_rows);continue;}
        if(ch==12){fputs("\033[2J\033[H",stdout);popup_rows=0;tui_redraw_editor(buf,cursor,slash_selected,slash_dismissed,&popup_rows);continue;}
        if(ch=='\t'){if(cursor==len&&!strchr(buf,'\n')){if(buf[0]=='/'&&strchr(buf,' ')==NULL){complete_slash(buf,cap,&len);cursor=len;}else tui_complete_path(buf,cap,&len,&cursor);slash_selected=0;slash_dismissed=0;tui_redraw_editor(buf,cursor,slash_selected,slash_dismissed,&popup_rows);}continue;}
        if(ch==27){
            if(!tui_read_escape(&key)){if(tui_slash_matches(buf,NULL,64)>0){slash_dismissed=1;tui_redraw_editor(buf,cursor,slash_selected,slash_dismissed,&popup_rows);}continue;}
            if(key=='2'){
                unsigned char seq[3];size_t j;
                for(j=0;j<3;j++)if(read(STDIN_FILENO,&seq[j],1)!=1)break;
                if(j==3&&seq[0]=='0'&&seq[1]=='0'&&seq[2]=='~'){
                    size_t available=cap-len-1,added;char *paste=(char*)malloc(available?available:1);int trunc=0;
                    if(paste){added=tui_receive_paste(paste,available,&trunc);
                        if(added){memmove(buf+cursor+added,buf+cursor,len-cursor+1);memcpy(buf+cursor,paste,added);len+=added;cursor+=added;}
                        free(paste);
                    }else {char discard[1];tui_receive_paste(discard,0,&trunc);trunc=1;}
                    browsing=0;slash_dismissed=1;slash_selected=0;
                    tui_redraw_editor(buf,cursor,slash_selected,slash_dismissed,&popup_rows);
                    if(trunc){fputc('\a',stdout);fputs("\r\n[Paste truncated: input exceeds editor capacity]\r\n",stdout);tui_composer_cursor_row=0;}
                    continue;
                }
                continue;
            }
            if(key=='A'||key=='B'){
                if(!slash_dismissed&&cursor==len){const char *m[64];int n=tui_slash_matches(buf,m,64);if(n>0){if(key=='A'&&slash_selected>0)slash_selected--;else if(key=='B'&&slash_selected<n-1)slash_selected++;tui_redraw_editor(buf,cursor,slash_selected,slash_dismissed,&popup_rows);continue;}}
                if(!browsing){snprintf(draft,sizeof(draft),"%s",buf);browsing=1;}
                if(key=='A'&&hist>0)hist--;else if(key=='B'&&hist<tui_history_count)hist++;
                if(hist>=0&&hist<tui_history_count)snprintf(buf,cap,"%s",tui_history[hist]);else snprintf(buf,cap,"%s",draft);
                len=strlen(buf);cursor=len;slash_selected=0;slash_dismissed=0;
            }else if(key=='Z'){if(tui_active_ctx){vs_workflow_cycle_mode(&tui_active_ctx->workflow);tui_redraw_editor(buf,cursor,slash_selected,slash_dismissed,&popup_rows);}else fputc('\a',stdout);continue;
            }else if(key=='C'&&cursor<len){cursor=tui_next_char(buf,len,cursor);slash_dismissed=1;}else if(key=='D'&&cursor>0){cursor=tui_prev_char(buf,cursor);slash_dismissed=1;}else if(key=='H'){cursor=0;slash_dismissed=1;}else if(key=='F'){cursor=len;slash_dismissed=0;}else if(key=='3'){fd_set set;struct timeval tv;FD_ZERO(&set);FD_SET(STDIN_FILENO,&set);tv.tv_sec=0;tv.tv_usec=80000;if(select(STDIN_FILENO+1,&set,NULL,NULL,&tv)>0&&read(STDIN_FILENO,&key,1)==1&&key=='~'&&cursor<len){size_t next=tui_next_char(buf,len,cursor);memmove(buf+cursor,buf+next,len-next+1);len-=next-cursor;slash_selected=0;slash_dismissed=0;}}
            tui_redraw_editor(buf,cursor,slash_selected,slash_dismissed,&popup_rows);continue;
        }
        if(ch>=32&&ch!=127){if(len+1<cap){memmove(buf+cursor+1,buf+cursor,len-cursor+1);buf[cursor++]=(char)ch;len++;browsing=0;slash_selected=0;slash_dismissed=0;tui_redraw_editor(buf,cursor,slash_selected,slash_dismissed,&popup_rows);}else fputc('\a',stdout);continue;}
    }
    tui_clear_popup(popup_rows);fputs("\033[?2004l",stdout);tcsetattr(STDIN_FILENO,TCSAFLUSH,&oldt);history_add_line(buf);return 1;
}

static int tui_choose(const char *prompt,const char *const *options,int count,char *answer,size_t cap)
{
    int i,selected=0;unsigned char key;struct termios oldt,raw;int tty=isatty(STDIN_FILENO)&&isatty(STDOUT_FILENO)&&tcgetattr(STDIN_FILENO,&oldt)==0;
    if(count<=0||!options||!answer||cap==0)return -1;
    if(!tty){printf("%s\n",prompt?prompt:"");for(i=0;i<count;i++)printf("  %d) %s\n",i+1,options[i]);printf("Choose 1-%d%s (or Enter to skip): ",count,count==2&&strstr(options[0],"Yes")&&strstr(options[1],"No")?", y=yes, n=no":"");fflush(stdout);if(!fgets(answer,(int)cap,stdin))return -1;answer[strcspn(answer,"\r\n")]=0;if(!answer[0])return -1;if(count==2&&strstr(options[0],"Yes")&&strstr(options[1],"No")&&(answer[0]=='y'||answer[0]=='Y'))i=1;else if(count==2&&strstr(options[0],"Yes")&&strstr(options[1],"No")&&(answer[0]=='n'||answer[0]=='N'))i=2;else i=atoi(answer);if(i<1||i>count)return -1;snprintf(answer,cap,"%s",options[i-1]);return i-1;}
    raw=oldt;raw.c_lflag&=(tcflag_t)~(ICANON|ECHO);raw.c_iflag&=(tcflag_t)~(IXON|ICRNL);raw.c_cc[VMIN]=1;raw.c_cc[VTIME]=0;if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw)!=0)return -1;
    for(;;){printf("\r\033[2K%s\n",prompt?prompt:"");for(i=0;i<count;i++){if(i==selected)cprintf(T_BOLD T_CYAN,"  > %s\n",options[i]);else printf("    %s\n",options[i]);}cprintf(T_DIM,"Use Up/Down, Enter to choose, Esc to skip.\n");fflush(stdout);if(read(STDIN_FILENO,&key,1)!=1)break;if(key==27){if(tui_read_escape(&key)){if(key=='A'&&selected>0)selected--;else if(key=='B'&&selected<count-1)selected++;else if(key=='\r'||key=='\n')break;else continue;}else break;}else if(key=='y'||key=='Y'){selected=0;break;}else if(key=='n'||key=='N'){selected=count>1?1:0;break;}else if(key=='\r'||key=='\n')break;}
    (void)tcsetattr(STDIN_FILENO,TCSAFLUSH,&oldt);putchar('\n');snprintf(answer,cap,"%s",options[selected]);return selected;
}

static void tui_plan_tree(const VSWorkflow *w)
{
    int i,j,depth;if(!w)return;for(i=0;i<w->task_count;i++){depth=0;for(j=0;j<w->task_count;j++)if(w->tasks[j].id==w->tasks[i].parent_id&&w->tasks[i].parent_id)depth=1;while(depth-->0)printf("  ");printf("• %s\n",w->tasks[i].title);}
}

static void tui_sigint(int sig)
{
    static const char msg[]="\nStop requested (Ctrl+C). Cancelling the active agent operation...\n";
    (void)sig;
    /* SIGINT may interrupt a thread that already owns spin_mutex; never
       acquire a pthread mutex from an asynchronous signal handler. */
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

/* ---- live activity indicator ----
   A status line (spinner, phase label, elapsed seconds) is redrawn by a helper
   thread while a turn runs.  Trace lines erase and redraw it under the same
   mutex so the two never interleave.  Disabled when stdout is not a terminal. */
static pthread_t spin_thread;
static int spin_thread_ok=0,spin_running=0,spin_drawn=0,spin_enabled=0,spin_frame=0;
static int spin_streaming=0;
static char spin_label[128]="Thinking";
static time_t spin_started=0;

static void spin_erase_locked(void)
{
    if(spin_drawn){fputs("\r\033[2K",stdout);spin_drawn=0;fflush(stdout);}
}

static void spin_draw_locked(void)
{
    static const char frames[]="|/-\\";
    long elapsed;
    if(!spin_enabled||!spin_running||spin_streaming)return;
    elapsed=(long)(time(NULL)-spin_started);
    printf("\r\033[2K%c %s... %lds  (Ctrl-C to stop)",frames[spin_frame&3],spin_label,elapsed);
    spin_frame++;spin_drawn=1;fflush(stdout);
}

static void *spin_main(void *arg)
{
    (void)arg;
    for(;;){
        pthread_mutex_lock(&spin_mutex);
        if(!spin_running){pthread_mutex_unlock(&spin_mutex);break;}
        spin_draw_locked();
        pthread_mutex_unlock(&spin_mutex);
        usleep(120000);
    }
    return NULL;
}

static void spin_start(void)
{
    pthread_mutex_lock(&spin_mutex);
    spin_enabled=isatty(STDOUT_FILENO)?1:0;
    spin_running=1;spin_streaming=0;spin_started=time(NULL);spin_frame=0;
    strncpy(spin_label,"Thinking",sizeof(spin_label)-1);
    spin_label[sizeof(spin_label)-1]=0;
    pthread_mutex_unlock(&spin_mutex);
    spin_thread_ok=0;
    if(spin_enabled&&pthread_create(&spin_thread,NULL,spin_main,NULL)==0)spin_thread_ok=1;
}

static void spin_stop(void)
{
    pthread_mutex_lock(&spin_mutex);
    spin_running=0;spin_streaming=0;spin_erase_locked();
    pthread_mutex_unlock(&spin_mutex);
    if(spin_thread_ok){pthread_join(spin_thread,NULL);spin_thread_ok=0;}
}

/* Provider callbacks carry one model round, not necessarily the final answer:
   the agent may consume directives and ask for more rounds.  Capture them for
   diagnostics, but render only the complete answer returned by the agent loop. */
static VSTUIStream tui_stream;
static int tui_stream_line_start=1,tui_stream_active=0;
static int tui_stream_heading_shown=0,tui_stream_answer_rendered=0;
static const char *tui_stream_heading="VibeSolaris answer";

static void tui_stream_reset(const char *heading)
{
    pthread_mutex_lock(&spin_mutex);
    vs_tui_stream_reset(&tui_stream);
    tui_stream_line_start=1;tui_stream_active=0;tui_stream_heading_shown=0;tui_stream_answer_rendered=0;
    spin_streaming=0;
    tui_stream_heading=heading?heading:"VibeSolaris answer";
    pthread_mutex_unlock(&spin_mutex);
}

static int tui_stream_append_locked(const char *data,size_t len)
{
    if(!data||!len)return 0;
    return vs_tui_stream_append(&tui_stream,data,len);
}

static int live_stream(void *userdata,const char *data,size_t len)
{
    (void)userdata;
    if(!data||!len)return 0;
    pthread_mutex_lock(&spin_mutex);
    spin_erase_locked();
    tui_stream_active=1;spin_streaming=1;
    if(tui_stream_append_locked(data,len)!=0){
        pthread_mutex_unlock(&spin_mutex);
        return 1;
    }
    pthread_mutex_unlock(&spin_mutex);
    return 0;
}

static void tui_stream_render_final_locked(const char *answer,int cancelled)
{
    answer=vs_tui_stream_select_answer(&tui_stream,answer,cancelled);
    if(tui_stream_answer_rendered)return;
    if(!tui_stream_heading_shown){
        cprintf(T_BOLD T_GREEN,"\n%s\n",tui_stream_heading);
        tui_stream_heading_shown=1;
    }
    tui_print_bounded(answer,32U*1024U,128U*1024U);
    tui_stream_line_start=1;
    tui_stream_answer_rendered=1;
}

static void live_trace(void *userdata,int step,const char *kind,const char *detail)
{
    const char *col=trace_colour(kind);
    char label[160];
    int model_wait,model_output;
    (void)userdata;
    model_wait=kind&&!strcmp(kind,"model-wait");
    model_output=kind&&!strcmp(kind,"model-output");
    pthread_mutex_lock(&spin_mutex);
    spin_erase_locked();
    if(model_wait){
        vs_tui_stream_reset(&tui_stream);
        tui_stream_line_start=1;tui_stream_active=0;spin_streaming=0;
    }
    if(model_output){
        /* A stream is already painted.  Close its line before the next trace
           event; the complete answer is reconciled after the agent turn. */
        tui_stream_active=0;spin_streaming=0;
        if(vs_trace_phase(kind,detail,label,sizeof(label))){
            strncpy(spin_label,label,sizeof(spin_label)-1);
            spin_label[sizeof(spin_label)-1]=0;
        }
        pthread_mutex_unlock(&spin_mutex);
        return;
    }
    if(tui_output_mode==0&&kind&&(!strcmp(kind,"model-input")||!strcmp(kind,"model-wait")||!strcmp(kind,"model-result"))){
        if(vs_trace_phase(kind,detail,label,sizeof(label))){
            strncpy(spin_label,label,sizeof(spin_label)-1);
            spin_label[sizeof(spin_label)-1]=0;
        }
        spin_draw_locked();pthread_mutex_unlock(&spin_mutex);return;
    }
    if(tui_colour)fputs(col,stdout);
    printf("%2d. [%-12s] ",step,kind?kind:"step");
    tui_print_detail(kind,detail);
    if(tui_colour)fputs(T_RESET,stdout);
    fflush(stdout);
    if(vs_trace_phase(kind,detail,label,sizeof(label))){
        strncpy(spin_label,label,sizeof(spin_label)-1);
        spin_label[sizeof(spin_label)-1]=0;
    }
    if(!tui_stream_active)spin_draw_locked();
    pthread_mutex_unlock(&spin_mutex);
}

static char *tui_run_agent(VSContext *c,const char *prompt,const char *heading,int suppress_history)
{
    char *answer;
    void (*oldint)(int);
    int old_suppress,cancelled;
    if(!c)return NULL;
    vs_cancel_clear(c);
    tui_active_ctx=c;
    oldint=signal(SIGINT,tui_sigint);
    old_suppress=c->suppress_history_once;
    if(suppress_history)c->suppress_history_once=1;
    vs_set_trace_callback(c,live_trace,NULL);
    vs_set_stream_callback(c,live_stream,NULL);
    tui_stream_reset(heading);
    spin_start();
    answer=vs_agent_turn(c,prompt?prompt:"");
    cancelled=vs_cancel_requested(c)!=0;
    spin_stop();
    vs_set_trace_callback(c,NULL,NULL);
    vs_set_stream_callback(c,NULL,NULL);
    tui_active_ctx=NULL;
    if(oldint!=SIG_ERR)(void)signal(SIGINT,oldint);
    if(suppress_history)c->suppress_history_once=old_suppress;
    pthread_mutex_lock(&spin_mutex);
    spin_erase_locked();
    tui_stream_render_final_locked(answer,cancelled);
    pthread_mutex_unlock(&spin_mutex);
    return answer;
}

static void print_subagents(const VSContext *c)
{
    int i;if(!c||c->subagent_record_count<=0){warnf("(no subagents in this turn)\n");return;}
    cprintf(T_BOLD T_MAGENTA,"Subagent tasks\n");
    for(i=0;i<c->subagent_record_count;i++){
        const VSSubagentRecord *r=&c->subagents[i];const char *status=r->status==0?"running":r->status==1?"completed":r->status==2?"stopped":"failed/limited";
        {int j;for(j=1;j<r->depth;j++)fputs("  │ ",stdout);printf("  ├─ %s #%d  %s\n",status,r->id,r->task);}
        if(r->summary[0]&&strcmp(r->summary,"running"))printf("      %s\n",r->summary);
    }
}

static int remove_attachment(VSContext *c,int index)
{
    int i;if(!c||index<0||index>=c->attachment_count)return -1;
    for(i=index+1;i<c->attachment_count;i++)c->attachments[i-1]=c->attachments[i];
    c->attachment_count--;if(c->attachment_send_from>index)c->attachment_send_from--;
    if(c->attachment_send_from>c->attachment_count)c->attachment_send_from=c->attachment_count;
    return 0;
}

static void print_attachments(const VSContext *c)
{
    int i;if(!c||c->attachment_count<=0){warnf("(no pending attachments)\n");return;}
    cprintf(T_BOLD T_BLUE,"Pending attachments (%d/%d)\n",c->attachment_count,VS_MAX_ATTACH);
    for(i=0;i<c->attachment_count;i++){
        struct stat st;long size=-1;if(stat(c->attachments[i].path,&st)==0)size=(long)st.st_size;
        printf("  %d. %-4s %s",i+1,c->attachments[i].is_image?"IMAGE":"FILE",c->attachments[i].path);
        if(size>=0)printf(" (%ld bytes)",size);putchar('\n');
    }
    cprintf(T_DIM,"Use /attach-remove INDEX to remove one; /clearattach removes all.\n");
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
    cprintf(T_CYAN,"  /anthropic-auth bearer|x-api-key ");printf("set custom Anthropic-compatible API-key header\n");
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
    cprintf(T_BOLD T_BLUE,"\nClaude OAuth (Anthropic-issued client only)\n");
    cprintf(T_CYAN,"  /claude-oauth status          ");printf("show Claude OAuth configuration and compliance status\n");
    cprintf(T_CYAN,"  /claude-oauth client ID | authorize URL | token URL | scopes LIST | redirect URL\n");
    cprintf(T_CYAN,"  /claude-oauth login | /claude-oauth logout\n");
    cprintf(T_BOLD T_BLUE,"\nPlan and execution\n");
    cprintf(T_CYAN,"  /mode auto|manual             ");printf("select autonomous or per-action confirmation mode\n");
    cprintf(T_CYAN,"  /plan                          ");printf("show the accepted-plan task tree and approval state\n");
    cprintf(T_CYAN,"  Plan prompts                  ");printf("choose Yes/No with arrow keys + Enter (or y/n); slash commands remain available\n");
    cprintf(T_CYAN,"  /plan approve-action          ");printf("approve the exact pending Manual action, then resubmit the task\n");
    cprintf(T_DIM,"  Both modes require explicit acceptance; Manual approval is one-shot; execution remains serial.\n");
    cprintf(T_BOLD T_BLUE,"\nSubagents\n");
    cprintf(T_CYAN,"  /subagent TASK               ");printf("delegate a focused task to an isolated subagent\n");
    cprintf(T_CYAN,"  /subagents                    ");printf("show this turn's task list and outcomes\n");
    cprintf(T_DIM,"  The agent can also delegate on its own with [[VS_TOOL subagent task=\"...\"]].\n");
    cprintf(T_DIM,"  Use only the client ID Anthropic issues to VibeSolaris. Claude.ai and Claude Code subscription logins are refused.\n");
    cprintf(T_BOLD T_BLUE,"\nFiles, tools, cache and history\n");
    cprintf(T_CYAN,"  /paste-image                  ");printf("attach clipboard image on macOS\n");
    cprintf(T_CYAN,"  /attach PATH                  ");printf("attach a file or image to the next request\n");
    cprintf(T_CYAN,"  /attachments                  ");printf("list pending files/images and sizes\n");
    cprintf(T_CYAN,"  /attach-remove INDEX          ");printf("remove one pending attachment\n");
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
    cprintf(T_DIM,"  Tab completes slash commands and relative paths; Up/Down recalls history (draft restored). Ctrl+J inserts a newline; Enter submits.\n");
    cprintf(T_DIM,"  Ctrl+A/E moves to start/end; Ctrl+U clears input; Ctrl+L clears the screen.\n");
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

static void claude_oauth_status(const VSContext *c)
{
    char err[512];long remain;
    printf("Claude OAuth (separate from OpenAI OAuth)\n");
    err[0]=0;
    printf("  Policy check: %s\n", vs_claude_oauth_policy_check(&c->claude_oauth,err,sizeof(err))==0 ? "ready" : "not ready");
    if(err[0])printf("  Note: %s\n",err);
    printf("  Client ID: %s\n", c->claude_oauth.client_id[0] ? c->claude_oauth.client_id : "(not set)");
    printf("  Authorisation URL: %s\n", c->claude_oauth.authorize_url[0] ? c->claude_oauth.authorize_url : "(not set)");
    printf("  Token URL: %s\n", c->claude_oauth.token_url[0] ? c->claude_oauth.token_url : "(not set)");
    printf("  Scopes: %s\n", c->claude_oauth.scopes[0] ? c->claude_oauth.scopes : "(not set)");
    printf("  Redirect URI: %s\n", c->claude_oauth.redirect_uri[0] ? c->claude_oauth.redirect_uri : "(not set)");
    printf("  Session: %s\n", vs_oauth_is_signed_in_for(c,VS_OAUTH_CLAUDE) ? "signed in" : (c->claude_oauth.access_token[0] ? "expired / refresh required" : "not signed in"));
    if(c->claude_oauth.expires_at>0){remain=c->claude_oauth.expires_at-(long)time(NULL);if(remain<0)remain=0;printf("  Access-token lifetime remaining: %ld seconds\n",remain);}
    printf("  Bearer sent only to https://api.anthropic.com; storage: encrypted config only\n");
}

static void claude_oauth_set(VSContext *c,const char *which,const char *value)
{
    VSOAuthConfig *o=&c->claude_oauth;
    if(!strcmp(which,"client"))copyv(o->client_id,sizeof(o->client_id),value);
    else if(!strcmp(which,"authorize"))copyv(o->authorize_url,sizeof(o->authorize_url),value);
    else if(!strcmp(which,"token"))copyv(o->token_url,sizeof(o->token_url),value);
    else if(!strcmp(which,"scopes"))copyv(o->scopes,sizeof(o->scopes),value);
    else if(!strcmp(which,"redirect"))copyv(o->redirect_uri,sizeof(o->redirect_uri),value);
    else {errorf("Unknown Claude OAuth setting: %s\n",which);return;}
    if(vs_persist_settings(c)==0)successf("Claude OAuth %s updated and saved to the encrypted config.\n",which);
    else warnf("Claude OAuth %s updated, but the encrypted config could not be saved.\n",which);
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
    cprintf(T_DIM,"  Enter sends · Ctrl+J newline · paste multiline text · /paste-image (macOS clipboard).\n");
}

int main(int argc, char **argv)
{
    VSContext c;
    /* Agent activity is interactive progress, not batch output.  Disable stdio
       buffering so terminals and pseudo-terminals never hold a run of steps. */
    setvbuf(stdout,NULL,_IONBF,0);
    char line[262144];
    int i,has_prefill=0;
    const char *config_path=NULL;
    for(i=1;i<argc;i++){
        if(!strcmp(argv[i],"--no-colour")||!strcmp(argv[i],"--no-color"))tui_colour_mode=0;
        else if(!strcmp(argv[i],"--colour")||!strcmp(argv[i],"--color"))tui_colour_mode=1;
        else if(!strcmp(argv[i],"--help")){colour_refresh();help();return 0;}
        else config_path=argv[i];
    }
    colour_refresh();
    vs_init(&c);
    vs_tui_stream_init(&tui_stream);
    if (config_path) vs_load_config(&c, config_path);
    show_banner(&c);
    while (1) {
        if(!has_prefill){printf("\n");tui_status(&c);tui_active_ctx=&c;if (!tui_read_line(line, sizeof(line))){tui_active_ctx=NULL;break;}tui_active_ctx=NULL;}
        has_prefill=0;
        if (!strcmp(line, "/quit")) break;
        else if (!strcmp(line,"/help"))help();
        else if (!strcmp(line,"/effort") || !strcmp(line,"/effort status")){printf("effort=%s\n",tui_effort==0?"default":tui_effort==1?"low":tui_effort==2?"medium":"high");}
        else if (!strncmp(line,"/effort ",8)){const char *v=line+8;if(!strcmp(v,"default"))tui_effort=0;else if(!strcmp(v,"low"))tui_effort=1;else if(!strcmp(v,"medium"))tui_effort=2;else if(!strcmp(v,"high"))tui_effort=3;else {errorf("usage: /effort default|low|medium|high\n");continue;}c.effort=tui_effort;successf("effort=%s (sent when provider protocol supports it)\n",v);}
        else if (!strcmp(line,"/colour")||!strcmp(line,"/colour status")||!strcmp(line,"/color")||!strcmp(line,"/color status")){
            printf("colour=%s mode=%s TERM=%s NO_COLOR=%s\n",tui_colour?"on":"off",tui_colour_mode<0?"auto":(tui_colour_mode?"on":"off"),getenv("TERM")?getenv("TERM"):"(unset)",getenv("NO_COLOR")?"set":"not set");
        } else if (!strncmp(line,"/colour ",8) || !strncmp(line,"/color ",7)){
            const char *v=!strncmp(line,"/colour ",8)?line+8:line+7;
            if(!strcmp(v,"auto"))tui_colour_mode=-1;
            else if(!strcmp(v,"on")||!strcmp(v,"always"))tui_colour_mode=1;
            else if(!strcmp(v,"off")||!strcmp(v,"never"))tui_colour_mode=0;
            else {errorf("usage: /colour auto|on|off|status\n");continue;}
            colour_refresh();printf("colour=%s (mode=%s)\n",tui_colour?"on":"off",tui_colour_mode<0?"auto":(tui_colour_mode?"on":"off"));
        } else if (!strcmp(line,"/paste-image")) {
            if(tui_paste_clipboard_image(&c)==0)successf("Clipboard image attached to next request.\n");
            else errorf("Unable to attach clipboard PNG (on macOS, grant clipboard automation permission and copy an image; elsewhere use /attach PATH).\n");
        } else if (!strncmp(line, "/provider ", 10)) {
            if(vs_set_provider(&c, line + 10)==0){if(vs_persist_settings(&c)==0)successf("provider=%s model=%s (saved)\n", c.provider.name,c.provider.model);else errorf("provider changed, but encrypted config could not be saved\n");}else errorf("unknown provider\n");
        } else if (!strncmp(line, "/protocol ", 10)) {
            if(vs_set_protocol(&c,line+10)==0)successf("protocol=%s base=%s\n",vs_protocol_name(c.provider.protocol),c.provider.base_url);
            else errorf("protocol not supported by provider %s\n",c.provider.name);
        } else if (!strncmp(line, "/anthropic-auth ", 16)) {
            const char *mode=line+16;
            if(!strcmp(mode,"bearer"))c.provider_anthropic_auth[(int)c.provider.kind]=0;
            else if(!strcmp(mode,"x-api-key"))c.provider_anthropic_auth[(int)c.provider.kind]=1;
            else {errorf("usage: /anthropic-auth bearer|x-api-key\n");continue;}
            if(vs_persist_settings(&c)==0)successf("Anthropic-compatible auth=%s (saved for %s)\n",mode,c.provider.name);
            else errorf("Anthropic auth changed, but encrypted config could not be saved\n");
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
        } else if (!strcmp(line, "/claude-oauth status")) {
            claude_oauth_status(&c);
        } else if (!strncmp(line, "/claude-oauth client ", 21)) {
            claude_oauth_set(&c,"client",line+21);
        } else if (!strncmp(line, "/claude-oauth authorize ", 24)) {
            claude_oauth_set(&c,"authorize",line+24);
        } else if (!strncmp(line, "/claude-oauth token ", 20)) {
            claude_oauth_set(&c,"token",line+20);
        } else if (!strncmp(line, "/claude-oauth scopes ", 21)) {
            claude_oauth_set(&c,"scopes",line+21);
        } else if (!strncmp(line, "/claude-oauth redirect ", 23)) {
            claude_oauth_set(&c,"redirect",line+23);
        } else if (!strcmp(line, "/claude-oauth login")) {
            char msg[1024];
            if(!vs_oauth_is_configured_for(&c,VS_OAUTH_CLAUDE))errorf("Claude OAuth is not configured. Set client, authorize, token, scopes and redirect first.\n");
            else {warnf("Starting Claude OAuth (Authorisation Code + PKCE); your browser should open.\n");msg[0]=0;if(vs_oauth_login_blocking_for(&c,VS_OAUTH_CLAUDE,msg,sizeof(msg))>0)successf("%s\n",msg);else errorf("Claude OAuth login failed: %s\n",msg[0]?msg:"unknown error");}
        } else if (!strcmp(line, "/claude-oauth logout")) {
            vs_oauth_logout_for(&c,VS_OAUTH_CLAUDE);successf("Claude OAuth tokens cleared.\n");
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
            if(vs_attach(&c,line+8)==0)successf("attached: %s\n",line+8);else errorf("attach failed (unreadable, path too long, or attachment limit reached): %s\n",line+8);
        } else if (!strcmp(line,"/attachments") || !strcmp(line,"/attachments list")) {
            print_attachments(&c);
        } else if (!strncmp(line,"/attach-remove ",15)) {
            char *ep;long index=strtol(line+15,&ep,10);if(*ep||index<1||index>c.attachment_count||remove_attachment(&c,(int)index-1)!=0)errorf("usage: /attach-remove INDEX (see /attachments)\n");else successf("removed attachment %ld\n",index);
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
        } else if (!strcmp(line,"/mode") || !strcmp(line,"/mode status")) {
            printf("mode=%s; phase=%s\n",vs_workflow_mode_name(c.workflow.mode),c.workflow.phase==VS_WORKFLOW_AWAITING_ACCEPT?"awaiting plan acceptance":(vs_workflow_authorized(&c.workflow)?"executing":"idle"));
        } else if (!strncmp(line,"/mode ",6)) {
            const char *mode=line+6;
            if(!strcmp(mode,"auto"))c.workflow.mode=VS_MODE_AUTO;
            else if(!strcmp(mode,"plan"))c.workflow.mode=VS_MODE_PLAN;
            else if(!strcmp(mode,"manual"))c.workflow.mode=VS_MODE_MANUAL;
            else {errorf("usage: /mode auto|plan|manual|status\n");continue;}
            c.workflow.action_approved=0;c.workflow.pending_request[0]=0;
            successf("execution mode=%s; pending plan acceptance is unchanged\n",vs_workflow_mode_name(c.workflow.mode));
        } else if (!strcmp(line,"/plan")) {
            int j,k;
            if(!c.workflow.task_count)warnf("No plan is currently proposed. Execution authorization is not inferred from conversation text.\n");
            else {
                cprintf(T_BOLD T_BLUE,"%s  [%s, revision %lu]\n",c.workflow.title,c.workflow.phase==VS_WORKFLOW_AWAITING_ACCEPT?"awaiting acceptance":(vs_workflow_authorized(&c.workflow)?"accepted":"not accepted"),c.workflow.revision);
                for(j=0;j<c.workflow.task_count;j++){
                    VSTaskRecord *t=&c.workflow.tasks[j];int depth=0,parent=t->parent_id;
                    while(parent&&depth<VS_MAX_PLAN_TASKS){int pidx=-1;for(k=0;k<c.workflow.task_count;k++)if(c.workflow.tasks[k].id==parent)pidx=k;if(pidx<0)break;depth++;parent=c.workflow.tasks[pidx].parent_id;}
                    while(depth-->0)printf("  ");
                    printf("• %s  [%s]",t->title,t->status==VS_TASK_COMPLETED?"done":(t->status==VS_TASK_RUNNING?"running":(t->status==VS_TASK_BLOCKED?"blocked":(t->status==VS_TASK_FAILED?"failed":"planned"))));
                    if(t->dependency_count){printf(" (depends on");for(k=0;k<t->dependency_count;k++)printf(" %s%d",k?",":"",t->dependencies[k]);printf(")");}
                    putchar('\n');
                }
                puts("Execution is serial; sibling tasks are not running in parallel.");
            }
        } else if (!strcmp(line,"/plan accept")) {
            if(vs_workflow_accept(&c.workflow)==0)successf("Plan accepted for revision %lu. Submit the same task to execute it.\n",c.workflow.revision);
            else errorf("No proposed plan is awaiting acceptance.\n");
        } else if (!strcmp(line,"/plan approve-action")) {
            if(vs_workflow_approve_action(&c.workflow)==0)successf("Next manual action approved. Submit the same task to continue.\n");
            else errorf("No manual action is awaiting approval.\n");
        } else if (!strcmp(line,"/plan reject") || !strcmp(line,"/plan revise")) {
            if(c.workflow.task_count){vs_workflow_reject(&c.workflow);successf("Plan approval cleared; request or propose a revised plan before execution.\n");}
            else warnf("No proposed plan to reject or revise.\n");
        } else if (!strcmp(line,"/subagents")) {
            print_subagents(&c);
        } else if (!strcmp(line, "/trace") || !strcmp(line,"/trace full")) {
            int old=tui_output_mode;if(!strcmp(line,"/trace full"))tui_output_mode=2;cprintf(T_BOLD T_MAGENTA,"Activity trace\n");print_trace(&c);tui_output_mode=old;
        } else if (line[0] == '/' && strncmp(line, "/subagent ", 10)) {
            errorf("Unknown command: %s\n",line);cprintf(T_DIM,"Type /help to see available commands.\n");
        } else if(line[0]) {
            char *a;
            cprintf(T_BOLD T_MAGENTA,"\nActivity (live)\n");
            {
                char *delegated=NULL;const char *prompt=line;
                if(!strncmp(line,"/subagent ",10)){delegated=vs_subagent_request_prompt(line+10);if(delegated)prompt=delegated;}
                a=tui_run_agent(&c,prompt,"VibeSolaris answer",0);
                free(delegated);
            }
            if(c.workflow.phase==VS_WORKFLOW_AWAITING_ACCEPT){const char *choices[]={"Yes — accept plan","No — reject plan"};char selected[VS_NEED_USER_OPTION_MAX];int pick;cprintf(T_BOLD T_BLUE,"Plan: %s\n",c.workflow.title);tui_plan_tree(&c.workflow);pick=tui_choose("Accept this plan?",choices,2,selected,sizeof(selected));if(pick==0){char continuation[VS_ACTIVE_TASK_MAX];if(vs_workflow_accept(&c.workflow)==0){successf("Plan accepted; continuing...\n");if(vs_workflow_take_pending_task(&c,continuation,sizeof(continuation))==0){char *continued;tui_active_ctx=NULL;continued=tui_run_agent(&c,continuation,"VibeSolaris continuation",1);free(continued);}}}else if(pick==1){vs_workflow_reject(&c.workflow);vs_workflow_clear_pending_task(&c);warnf("Plan rejected.\n");}}
            else if(c.pending_question&&c.question_option_count>0){const char *choices[VS_NEED_USER_MAX_OPTIONS];char selected[VS_NEED_USER_OPTION_MAX];int qi,pick;for(qi=0;qi<c.question_option_count;qi++)choices[qi]=c.question_options[qi];pick=tui_choose(c.question,choices,c.question_option_count,selected,sizeof(selected));if(pick>=0){snprintf(line,sizeof(line),"%s",selected);has_prefill=1;c.pending_question=0;c.question_option_count=0;}else{c.pending_question=0;c.question_option_count=0;}}
            usage_status(&c);
            free(a);vs_clear_attachments(&c);vs_cancel_clear(&c);
        }
    }
    vs_shutdown(&c);
    vs_tui_stream_free(&tui_stream);
    cprintf(T_DIM,"\nGoodbye.\n");
    return 0;
}