/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#ifdef __sun
#include <sys/systeminfo.h>
#endif

static char *dupstr(const char *s) {
    size_t n = s ? strlen(s) : 0;
    char *p = (char*)malloc(n+1);
    if (!p) return NULL;
    if (s) memcpy(p,s,n);
    p[n]=0;
    return p;
}

static unsigned long hash_update(unsigned long h,const char *s) {
    const unsigned char *p=(const unsigned char*)s;
    while(p && *p){h^=(unsigned long)*p++;h*=16777619UL;}
    return h;
}

unsigned long vs_hash_string(const char *s) { return hash_update(2166136261UL,s); }

void vs_refresh_cache_key(VSContext *ctx) {
    unsigned long h=2166136261UL;
    h=hash_update(h,ctx->cwd);h=hash_update(h,"|");h=hash_update(h,ctx->provider.name);
    h=hash_update(h,"|");h=hash_update(h,ctx->provider.model);h=hash_update(h,"|");h=hash_update(h,ctx->agent_md);
    snprintf(ctx->cache_key,sizeof(ctx->cache_key),"vibesolaris-%08lx",h & 0xffffffffUL);
}

void vs_init(VSContext *ctx) {
    memset(ctx,0,sizeof(*ctx));
    if (!getcwd(ctx->cwd,sizeof(ctx->cwd))) strcpy(ctx->cwd,".");
    vs_detect_platform(ctx);
    vs_set_provider(ctx,"openai");
    ctx->cache_enabled=1;
    vs_oauth_defaults(ctx);
    {
        char global_err[256];
        global_err[0]=0;
        if (vs_global_config_exists()) (void)vs_global_config_load(ctx,global_err,sizeof(global_err));
    }
    (void)vs_oauth_load_profile(ctx);
    ctx->config_autosave=1;
    vs_load_agent_md(ctx);
    vs_refresh_cache_key(ctx);
}

void vs_detect_platform(VSContext *ctx) {
    struct utsname u;
    if (uname(&u)>=0) {
        strncpy(ctx->os_name,u.sysname,sizeof(ctx->os_name)-1);
        ctx->os_name[sizeof(ctx->os_name)-1]=0;
        strncpy(ctx->os_release,u.release,sizeof(ctx->os_release)-1);
        ctx->os_release[sizeof(ctx->os_release)-1]=0;
        strncpy(ctx->arch,u.machine,sizeof(ctx->arch)-1);
        ctx->arch[sizeof(ctx->arch)-1]=0;
#ifdef __sun
        {
            char isa[128];
            long r;
#ifdef SI_ARCHITECTURE_NATIVE
            r=sysinfo(SI_ARCHITECTURE_NATIVE,isa,sizeof(isa));
#else
            r=sysinfo(SI_ARCHITECTURE,isa,sizeof(isa));
#endif
            if(r>0){
                strncpy(ctx->arch,isa,sizeof(ctx->arch)-1);
                ctx->arch[sizeof(ctx->arch)-1]=0;
            }
        }
#endif
    } else {
        strcpy(ctx->os_name,"unknown"); strcpy(ctx->os_release,"unknown"); strcpy(ctx->arch,"unknown");
    }
}

int vs_load_agent_md(VSContext *ctx) {
    char p[VS_MAX_PATH];
    char *s;
    if(strlen(ctx->cwd)+10>=sizeof(p)){ctx->agent_md[0]=0;return -1;}
    strcpy(p,ctx->cwd);strcat(p,"/AGENT.MD");
    s=vs_read_file(p);
    if(!s){ctx->agent_md[0]=0;vs_refresh_cache_key(ctx);return -1;}
    strncpy(ctx->agent_md,s,sizeof(ctx->agent_md)-1);
    ctx->agent_md[sizeof(ctx->agent_md)-1]=0;
    free(s); vs_refresh_cache_key(ctx); return 0;
}

char *vs_read_file(const char *path) {
    FILE *f=fopen(path,"rb"); long n; char *b;
    if(!f) return NULL;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return NULL;}
    n=ftell(f); if(n<0 || n>VS_MAX_TEXT*16){fclose(f);return NULL;}
    rewind(f); b=(char*)malloc((size_t)n+1); if(!b){fclose(f);return NULL;}
    if(n && fread(b,1,(size_t)n,f)!=(size_t)n){free(b);fclose(f);return NULL;}
    b[n]=0; fclose(f); return b;
}

static void free_cache_entry(VSFileCacheEntry *e) {
    if(e->text) free(e->text);
    if(e->base64) free(e->base64);
    memset(e,0,sizeof(*e));
}

static VSFileCacheEntry *cache_slot(VSContext *ctx,const char *path,long mtime,long size,int is_image) {
    int i,idx;
    VSFileCacheEntry *e;
    for(i=0;i<ctx->file_cache_count;i++) {
        e=&ctx->file_cache[i];
        if(!strcmp(e->path,path)) {
            if(e->mtime==mtime && e->size==size) return e;
            free_cache_entry(e);
            strncpy(e->path,path,sizeof(e->path)-1); e->mtime=mtime; e->size=size; e->is_image=is_image;
            return e;
        }
    }
    if(ctx->file_cache_count<VS_MAX_FILE_CACHE) idx=ctx->file_cache_count++;
    else { idx=ctx->file_cache_next++ % VS_MAX_FILE_CACHE; free_cache_entry(&ctx->file_cache[idx]); }
    e=&ctx->file_cache[idx];
    strncpy(e->path,path,sizeof(e->path)-1); e->mtime=mtime; e->size=size; e->is_image=is_image;
    return e;
}

char *vs_cached_read_file(VSContext *ctx,const char *path) {
    struct stat st; VSFileCacheEntry *e; char *s;
    if(!ctx || !ctx->cache_enabled) return vs_read_file(path);
    if(stat(path,&st)!=0) return NULL;
    /* Do not keep another full copy of a multi-megabyte source file in RAM. */
    if((unsigned long)st.st_size > (unsigned long)VS_FILE_CACHE_BLOB_MAX) {
        ctx->file_cache_misses++;
        return vs_read_file(path);
    }
    e=cache_slot(ctx,path,(long)st.st_mtime,(long)st.st_size,0);
    if(e->text) { ctx->file_cache_hits++; e->hits++; return dupstr(e->text); }
    s=vs_read_file(path); if(!s) return NULL;
    e->text=dupstr(s); e->hash=vs_hash_string(s); ctx->file_cache_misses++;
    return s;
}

char *vs_cached_base64_file(VSContext *ctx,const char *path,size_t *out_len) {
    struct stat st; VSFileCacheEntry *e; char *s; size_t n=0;
    if(!ctx || !ctx->cache_enabled) return vs_base64_file(path,out_len);
    if(stat(path,&st)!=0) return NULL;
    /* Large images are already expanded ~4/3 by base64.  Caching another copy
       can double peak memory while the request body is being built. */
    if((unsigned long)st.st_size > (unsigned long)VS_FILE_CACHE_BLOB_MAX) {
        ctx->file_cache_misses++;
        return vs_base64_file(path,out_len);
    }
    e=cache_slot(ctx,path,(long)st.st_mtime,(long)st.st_size,1);
    if(e->base64) { ctx->file_cache_hits++; e->hits++; if(out_len)*out_len=e->base64_len; return dupstr(e->base64); }
    s=vs_base64_file(path,&n); if(!s) return NULL;
    e->base64=dupstr(s); e->base64_len=n; e->hash=vs_hash_string(s); ctx->file_cache_misses++;
    if(out_len)*out_len=n;
    return s;
}

void vs_cache_invalidate(VSContext *ctx,const char *path) {
    int i;
    if(!ctx)return;
    for(i=0;i<ctx->file_cache_count;i++) if(!strcmp(ctx->file_cache[i].path,path)) free_cache_entry(&ctx->file_cache[i]);
}

void vs_cache_clear(VSContext *ctx) {
    int i;
    if(!ctx)return;
    for(i=0;i<ctx->file_cache_count;i++) free_cache_entry(&ctx->file_cache[i]);
    ctx->file_cache_count=0;ctx->file_cache_next=0;ctx->file_cache_hits=0;ctx->file_cache_misses=0;
    ctx->provider_cached_tokens=0;ctx->provider_cache_write_tokens=0;
}

static char *compact_copy(const char *s) {
    size_t n,half,cap; char *o;
    const char *mark="\n...[older message compacted locally]...\n";
    if(!s)return dupstr("");
    n=strlen(s);
    if(n<=VS_HISTORY_MESSAGE_MAX)return dupstr(s);
    half=(VS_HISTORY_MESSAGE_MAX-strlen(mark))/2;cap=half*2+strlen(mark)+1;o=(char*)malloc(cap);if(!o)return NULL;
    memcpy(o,s,half);memcpy(o+half,mark,strlen(mark));memcpy(o+half+strlen(mark),s+n-half,half);o[cap-1]=0;return o;
}

static void history_drop_one(VSContext *ctx) {
    int i;
    if(ctx->history_count<=0)return;
    if(ctx->history[0].content)free(ctx->history[0].content);
    if(ctx->history_bytes>=ctx->history[0].bytes)ctx->history_bytes-=ctx->history[0].bytes;else ctx->history_bytes=0;
    for(i=1;i<ctx->history_count;i++)ctx->history[i-1]=ctx->history[i];
    memset(&ctx->history[ctx->history_count-1],0,sizeof(ctx->history[0]));ctx->history_count--;ctx->history_evicted++;
}

static void history_drop_oldest(VSContext *ctx) {
    /* Keep user/assistant ordering valid for providers such as Claude. */
    if(ctx->history_count>=2 && !strcmp(ctx->history[0].role,"user") && !strcmp(ctx->history[1].role,"assistant")){history_drop_one(ctx);history_drop_one(ctx);}
    else history_drop_one(ctx);
}

void vs_history_add(VSContext *ctx,const char *role,const char *content) {
    char *c; size_t n;
    if(!ctx||!content)return;
    c=compact_copy(content);if(!c)return;n=strlen(c);
    while(ctx->history_count>=VS_MAX_HISTORY || (ctx->history_count>0 && ctx->history_bytes+n>VS_HISTORY_BUDGET))history_drop_oldest(ctx);
    strncpy(ctx->history[ctx->history_count].role,role?role:"user",sizeof(ctx->history[ctx->history_count].role)-1);
    ctx->history[ctx->history_count].content=c;ctx->history[ctx->history_count].bytes=n;ctx->history_bytes+=n;ctx->history_count++;
}

void vs_usage_clear(VSContext *ctx) {
    if(!ctx)return;
    ctx->provider_input_tokens=0;ctx->provider_output_tokens=0;ctx->provider_total_tokens=0;
    ctx->conversation_input_tokens=0;ctx->conversation_output_tokens=0;ctx->conversation_total_tokens=0;ctx->conversation_usage_responses=0;
}

void vs_history_clear(VSContext *ctx) {
    int i;if(!ctx)return;for(i=0;i<ctx->history_count;i++)if(ctx->history[i].content)free(ctx->history[i].content);
    memset(ctx->history,0,sizeof(ctx->history));ctx->history_count=0;ctx->history_bytes=0;ctx->history_evicted=0;
    vs_usage_clear(ctx);
}

void vs_shutdown(VSContext *ctx) { if(!ctx)return;(void)vs_persist_settings(ctx);vs_mcp_shutdown(ctx);vs_history_clear(ctx);vs_cache_clear(ctx); }

int vs_write_file(const char *path, const char *text) {
    FILE *f=fopen(path,"wb"); size_t n=strlen(text);
    if(!f) return -1;
    if(fwrite(text,1,n,f)!=n){fclose(f);return -1;}
    fclose(f); return 0;
}

char *vs_run_command(const char *cmd, int *exit_code) {
    int fds[2], flags, status=0, child_done=0, eof_seen=0, timed_out=0, detached_output=0;
    pid_t pid;
    char buf[8192];
    size_t head_cap=VS_MAX_COMMAND_CAPTURE/2, tail_cap=VS_MAX_COMMAND_CAPTURE/2;
    size_t head_n=0, tail_n=0, tail_pos=0, total=0, n, i, out_n, marker_n;
    char *head, *tail, *out; int truncated=0;
    const char *marker="\n...[command output truncated by VibeSolaris]...\n";
    char timeout_marker[160],detached_marker[192];
    long timeout_sec=1800;
    time_t started,child_done_at=0;
    const char *te;

    if(exit_code)*exit_code=-1;
    if(!cmd||!*cmd)return dupstr("ERROR: empty command");
    te=getenv("VIBESOLARIS_COMMAND_TIMEOUT");
    if(te&&*te){char *ep=0;long v=strtol(te,&ep,10);if(ep&&*ep==0&&v>=10&&v<=86400)timeout_sec=v;}

    head=(char*)malloc(head_cap?head_cap:1);tail=(char*)malloc(tail_cap?tail_cap:1);
    if(!head||!tail){free(head);free(tail);return NULL;}
    if(pipe(fds)!=0){free(head);free(tail);return NULL;}

    pid=fork();
    if(pid<0){close(fds[0]);close(fds[1]);free(head);free(tail);return NULL;}
    if(pid==0){
        int devnull;
        (void)setpgid(0,0);
        close(fds[0]);
        if(dup2(fds[1],STDOUT_FILENO)<0)_exit(126);
        if(dup2(fds[1],STDERR_FILENO)<0)_exit(126);
        if(fds[1]>STDERR_FILENO)close(fds[1]);
        devnull=open("/dev/null",O_RDONLY);
        if(devnull>=0){(void)dup2(devnull,STDIN_FILENO);if(devnull>STDERR_FILENO)close(devnull);}
        execl("/bin/sh","sh","-c",cmd,(char*)0);
        _exit(127);
    }

    close(fds[1]);
    (void)setpgid(pid,pid);
    flags=fcntl(fds[0],F_GETFL,0);if(flags>=0)(void)fcntl(fds[0],F_SETFL,flags|O_NONBLOCK);
    started=time(NULL);
    while(!eof_seen || !child_done){
        fd_set rfds;struct timeval tv;int sr;
        FD_ZERO(&rfds);FD_SET(fds[0],&rfds);tv.tv_sec=1;tv.tv_usec=0;
        sr=select(fds[0]+1,&rfds,NULL,NULL,&tv);
        if(sr>0&&FD_ISSET(fds[0],&rfds)){
            for(;;){
                ssize_t rr=read(fds[0],buf,sizeof(buf));
                if(rr>0){
                    n=(size_t)rr;total+=n;if(total>(size_t)VS_MAX_COMMAND_CAPTURE)truncated=1;
                    for(i=0;i<n;i++){
                        unsigned char ch=(unsigned char)buf[i];
                        if(head_n<head_cap)head[head_n++]=(char)ch;
                        else if(tail_cap){tail[tail_pos]=(char)ch;tail_pos=(tail_pos+1)%tail_cap;if(tail_n<tail_cap)tail_n++;}
                    }
                }else if(rr==0){eof_seen=1;break;}
                else if(errno==EINTR)continue;
                else if(errno==EAGAIN||errno==EWOULDBLOCK)break;
                else {eof_seen=1;break;}
            }
        }else if(sr<0&&errno!=EINTR){eof_seen=1;}

        if(!child_done){
            pid_t wr=waitpid(pid,&status,WNOHANG);
            if(wr==pid){child_done=1;child_done_at=time(NULL);}
            else if(wr<0&&errno!=EINTR){child_done=1;status=0;}
        }
        if(child_done && !eof_seen && child_done_at && time(NULL)-child_done_at>=2){
            /* A daemon/background grandchild inherited stdout/stderr after the
               requested shell command itself exited. Do not make the coding
               agent wait forever for that inherited descriptor. */
            detached_output=1;eof_seen=1;break;
        }
        if(time(NULL)-started>=timeout_sec && (!child_done || !eof_seen)){
            timed_out=1;
            if(kill(-pid,SIGTERM)!=0 && !child_done)(void)kill(pid,SIGTERM);
            {struct timeval grace;grace.tv_sec=1;grace.tv_usec=0;(void)select(0,NULL,NULL,NULL,&grace);}
            (void)kill(-pid,SIGKILL);
            if(!child_done){while(waitpid(pid,&status,0)<0&&errno==EINTR){}child_done=1;}
            eof_seen=1;
            break;
        }
    }
    close(fds[0]);

    if(exit_code){
        if(timed_out)*exit_code=124;
        else if(WIFEXITED(status))*exit_code=WEXITSTATUS(status);
        else if(WIFSIGNALED(status))*exit_code=128+WTERMSIG(status);
        else *exit_code=-1;
    }

    timeout_marker[0]=0;detached_marker[0]=0;
    if(timed_out)snprintf(timeout_marker,sizeof(timeout_marker),"\n...[command timed out after %ld seconds; process group terminated]...\n",timeout_sec);
    if(detached_output)snprintf(detached_marker,sizeof(detached_marker),"\n...[command exited; a detached/background child kept the output pipe open, so capture was closed]...\n");
    marker_n=(truncated?strlen(marker):0)+strlen(timeout_marker)+strlen(detached_marker);
    out_n=head_n+tail_n+marker_n;
    out=(char*)malloc(out_n+1);if(!out){free(head);free(tail);return NULL;}
    memcpy(out,head,head_n);i=head_n;
    if(truncated){memcpy(out+i,marker,strlen(marker));i+=strlen(marker);}
    if(timeout_marker[0]){memcpy(out+i,timeout_marker,strlen(timeout_marker));i+=strlen(timeout_marker);}
    if(detached_marker[0]){memcpy(out+i,detached_marker,strlen(detached_marker));i+=strlen(detached_marker);}
    {
        size_t first=(tail_n==tail_cap)?tail_pos:0,j;
        for(j=0;j<tail_n;j++)out[i++]=tail[(first+j)%tail_cap];
    }
    out[i]=0;
    free(head);free(tail);
    return out;
}

char *vs_compact_text_limit(const char *s,size_t limit,const char *reason){
    size_t n,markn,head,tail,cap;char *o,*mark;
    char mbuf[256];
    if(!s)return dupstr("");
    n=strlen(s);if(limit<256)limit=256;if(n<=limit)return dupstr(s);
    snprintf(mbuf,sizeof(mbuf),"\n...[%.120s: %lu bytes omitted]...\n",reason?reason:"content truncated",(unsigned long)(n-limit));
    mark=mbuf;markn=strlen(mark);if(markn>=limit)markn=limit/4;
    head=(limit-markn)*2/3;tail=limit-markn-head;cap=head+markn+tail+1;
    o=(char*)malloc(cap);if(!o)return NULL;
    memcpy(o,s,head);memcpy(o+head,mark,markn);memcpy(o+head+markn,s+n-tail,tail);o[cap-1]=0;return o;
}

char *vs_json_escape(const char *s){
    size_t i,n=2,cap; char *o,*p;
    if(!s)return dupstr("");
    for(i=0;s[i];i++) n += (s[i]=='"'||s[i]=='\\'||s[i]=='\n'||s[i]=='\r'||s[i]=='\t')?2:1;
    cap=n+1;o=(char*)malloc(cap);if(!o)return NULL;p=o;
    for(i=0;s[i];i++){
        switch(s[i]){
            case '"': *p++='\\';*p++='"';break;
            case '\\':*p++='\\';*p++='\\';break;
            case '\n':*p++='\\';*p++='n';break;
            case '\r':*p++='\\';*p++='r';break;
            case '\t':*p++='\\';*p++='t';break;
            default:*p++=s[i];
        }
    }
    *p=0;return o;
}

char *vs_shell_quote(const char *s){
    size_t i,n=3;char *o,*p;
    for(i=0;s&&s[i];i++)n+=(s[i]=='\'')?4:1;
    o=(char*)malloc(n);if(!o)return NULL;p=o;*p++='\'';
    for(i=0;s&&s[i];i++){if(s[i]=='\''){memcpy(p,"'\\''",4);p+=4;}else *p++=s[i];}
    *p++='\'';*p=0;return o;
}

int vs_is_image_path(const char *path){
    const char *e=strrchr(path,'.'); char ext[16]; int i;
    if(!e)return 0;
    e++;
    for(i=0;e[i]&&i<15;i++)ext[i]=(char)tolower((unsigned char)e[i]);
    ext[i]=0;
    return !strcmp(ext,"png")||!strcmp(ext,"jpg")||!strcmp(ext,"jpeg")||!strcmp(ext,"gif")||!strcmp(ext,"webp");
}

int vs_attach(VSContext *ctx,const char *path){
    int i;
    if(!ctx||!path||!*path)return -1;
    if(access(path,R_OK)!=0)return -1;
    for(i=0;i<ctx->attachment_count;i++)if(!strcmp(ctx->attachments[i].path,path))return 0;
    if(ctx->attachment_count>=VS_MAX_ATTACH)return -1;
    strncpy(ctx->attachments[ctx->attachment_count].path,path,VS_MAX_PATH-1);
    ctx->attachments[ctx->attachment_count].path[VS_MAX_PATH-1]=0;
    ctx->attachments[ctx->attachment_count].is_image=vs_is_image_path(path);
    ctx->attachment_count++;return 0;
}
void vs_clear_attachments(VSContext *ctx){ctx->attachment_count=0;}

char *vs_base64_file(const char *path,size_t *out_len){
    static const char T[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    FILE *f=fopen(path,"rb"); long n; size_t olen,j=0,r,i; char *out;
    unsigned char in[49152]; unsigned char carry[3]; size_t carry_n=0;
    if(!f)return NULL;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return NULL;}n=ftell(f);rewind(f);
    if(n<0||n>20*1024*1024){fclose(f);return NULL;}
    olen=4*(((size_t)n+2)/3);out=(char*)malloc(olen+1);if(!out){fclose(f);return NULL;}
    while((r=fread(in,1,sizeof(in),f))>0){
        i=0;
        if(carry_n){while(carry_n<3&&i<r)carry[carry_n++]=in[i++];if(carry_n==3){unsigned t=((unsigned)carry[0]<<16)|((unsigned)carry[1]<<8)|carry[2];out[j++]=T[(t>>18)&63];out[j++]=T[(t>>12)&63];out[j++]=T[(t>>6)&63];out[j++]=T[t&63];carry_n=0;}}
        while(i+3<=r){unsigned t=((unsigned)in[i]<<16)|((unsigned)in[i+1]<<8)|in[i+2];i+=3;out[j++]=T[(t>>18)&63];out[j++]=T[(t>>12)&63];out[j++]=T[(t>>6)&63];out[j++]=T[t&63];}
        while(i<r)carry[carry_n++]=in[i++];
    }
    if(ferror(f)){free(out);fclose(f);return NULL;}fclose(f);
    if(carry_n){unsigned t=(unsigned)carry[0]<<16;if(carry_n>1)t|=(unsigned)carry[1]<<8;out[j++]=T[(t>>18)&63];out[j++]=T[(t>>12)&63];out[j++]=carry_n>1?T[(t>>6)&63]:'=';out[j++]='=';}
    out[j]=0;if(out_len)*out_len=j;return out;
}

static int vs_exec_in_path(const char *name)
{
    const char *path, *p, *q;
    char candidate[VS_MAX_PATH];
    size_t n, dlen;
    if (!name || !*name) return 0;
    if (strchr(name, '/')) return access(name, X_OK) == 0;
    path = getenv("PATH");
    if (!path) path = "/usr/bin:/bin:/usr/local/bin";
    p = path;
    while (*p) {
        q = strchr(p, ':');
        n = q ? (size_t)(q - p) : strlen(p);
        if (n == 0) {
            candidate[0] = '.';
            candidate[1] = 0;
            dlen = 1;
        } else if (n < sizeof(candidate)) {
            memcpy(candidate, p, n);
            candidate[n] = 0;
            dlen = n;
        } else {
            dlen = sizeof(candidate);
        }
        if (dlen < sizeof(candidate) - 2 && dlen != sizeof(candidate)) {
            if (candidate[dlen - 1] != '/') {
                candidate[dlen++] = '/';
                candidate[dlen] = 0;
            }
            if (dlen + strlen(name) < sizeof(candidate)) {
                strcat(candidate, name);
                if (access(candidate, X_OK) == 0) return 1;
            }
        }
        if (!q) break;
        p = q + 1;
    }
    return 0;
}

int vs_open_url(const char *url)
{
    const char *browser;
    const char *launcher;
    const char *candidates[8];
    pid_t first, second;
    int st, i;

    if (!url || !*url) return -1;
    launcher = NULL;
    browser = getenv("BROWSER");
    if (browser && *browser && !strchr(browser, ' ') && !strchr(browser, '\t') &&
        vs_exec_in_path(browser)) {
        launcher = browser;
    }

    candidates[0] = "open"; /* Darwin / macOS */
    candidates[1] = "xdg-open";
    candidates[2] = "gio";
    candidates[3] = "gnome-open";
    candidates[4] = "firefox";
    candidates[5] = "mozilla";
    candidates[6] = "netscape";
    candidates[7] = NULL;

    if (!launcher) {
        for (i = 0; candidates[i]; i++) {
            if (vs_exec_in_path(candidates[i])) {
                launcher = candidates[i];
                break;
            }
        }
    }
    if (!launcher) return -1;

    first = fork();
    if (first < 0) return -1;
    if (first == 0) {
        second = fork();
        if (second < 0) _exit(126);
        if (second > 0) _exit(0);
        (void)setsid();
        if (!strcmp(launcher, "gio"))
            execlp(launcher, launcher, "open", url, (char *)0);
        else
            execlp(launcher, launcher, url, (char *)0);
        _exit(127);
    }
    st = 0;
    (void)waitpid(first, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) return -1;
    return 0;
}
