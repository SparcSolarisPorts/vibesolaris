/* SPDX-License-Identifier: Unlicense */
/* Grep tool: content search with context lines (src/grep.c).
 *
 * Line-by-line search with binary file skip, case sensitivity,
 * context lines, and file/line number reporting. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* ---- Local helpers ---- */
typedef struct {char *p;size_t n,cap;int truncated;} ABuffer;
static int ab_init(ABuffer *b,size_t cap){b->p=(char*)malloc(cap);if(!b->p)return -1;b->p[0]=0;b->n=0;b->cap=cap;b->truncated=0;return 0;}
static void ab_add(ABuffer *b,const char *s){size_t m,room;if(!b||!b->p||!s||b->truncated)return;m=strlen(s);room=b->cap-b->n;if(room<=96){b->truncated=1;return;}if(m>=room-96){m=room-96;b->truncated=1;}memcpy(b->p+b->n,s,m);b->n+=m;b->p[b->n]=0;}
static void ab_addf(ABuffer *b,const char *fmt,...){char t[2048];va_list ap;va_start(ap,fmt);vsnprintf(t,sizeof(t),fmt,ap);va_end(ap);ab_add(b,t);}
static char *ab_finish(ABuffer *b){if(b->truncated&&b->p&&b->cap>b->n+48){const char *m="\n...[native tool output truncated]...\n";size_t z=strlen(m);if(z<b->cap-b->n){memcpy(b->p+b->n,m,z+1);b->n+=z;}}return b->p;}
static char *dupstr(const char *s){char *r=(char*)malloc(strlen(s)+1);if(r)strcpy(r,s);return r;}

/* ---- Case-insensitive string search ---- */
static int str_contains_ci(const char *haystack, const char *needle)
{
    size_t hn, nn;
    if (!needle || !*needle) return 1;
    nn = strlen(needle);
    for (; *haystack; haystack++) {
        int match = 1;
        size_t i;
        for (i = 0; i < nn; i++) {
            unsigned char a = (unsigned char)haystack[i];
            unsigned char b = (unsigned char)needle[i];
            if (!a) { match = 0; break; }
            if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
            if (a != b) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

/* ---- Binary file detection ---- */
static int is_binary_file(FILE *f)
{
    unsigned char buf[512];
    size_t n = fread(buf, 1, sizeof(buf), f);
    size_t i;
    for (i = 0; i < n; i++) {
        if (buf[i] == 0) return 1; /* null byte = binary */
    }
    rewind(f);
    return 0;
}

/* ---- Grep a single file ---- */
static void grep_file(const char *path, const char *pattern, int ci, int ctx, int max_results, long *total_hits, ABuffer *b)
{
    FILE *f;
    char line[VS_GREP_LINE_MAX];
    long line_num = 0;
    long match_count = 0;
    /* Buffer for context lines */
    char ctx_buf[16][VS_GREP_LINE_MAX];
    long ctx_line_nums[16];
    int ctx_count = 0;

    if (*total_hits >= max_results || b->truncated) return;

    f = fopen(path, "rb");
    if (!f) return;

    if (is_binary_file(f)) {
        fclose(f);
        return;
    }

    while (fgets(line, sizeof(line), f) && *total_hits < max_results && !b->truncated) {
        line_num++;
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = 0;

        int matched = ci ? str_contains_ci(line, pattern) : (strstr(line, pattern) != 0);

        if (matched) {
            /* Flush pending context before this match */
            if (ctx_count > 0 && ctx > 0) {
                int i;
                for (i = 0; i < ctx_count; i++) {
                    ab_addf(b, "%s:%ld: %s\n", path, ctx_line_nums[i], ctx_buf[i]);
                }
            }
            ab_addf(b, "%s:%ld: %s\n", path, line_num, line);
            (*total_hits)++;
            match_count++;
            ctx_count = 0; /* Reset context after match */
        } else if (ctx > 0 && (match_count > 0 || ctx_count > 0)) {
            /* Store as potential context line */
            if (ctx_count < 16) {
                strncpy(ctx_buf[ctx_count], line, VS_GREP_LINE_MAX - 1);
                ctx_buf[ctx_count][VS_GREP_LINE_MAX - 1] = 0;
                ctx_line_nums[ctx_count] = line_num;
                ctx_count++;
            }
        } else {
            ctx_count = 0;
        }
    }

    /* Flush remaining context lines after last match */
    if (ctx_count > 0 && match_count > 0) {
        int i;
        for (i = 0; i < ctx_count; i++) {
            ab_addf(b, "%s:%ld: %s\n", path, ctx_line_nums[i], ctx_buf[i]);
        }
    }

    fclose(f);
}

/* ---- Recursive directory walk ---- */
static void grep_rec(const char *path, const char *pattern, int ci, int ctx, int max_results, long *total_hits, ABuffer *b)
{
    DIR *dp;
    struct dirent *de;
    char full[VS_MAX_PATH];
    struct stat st;

    if (*total_hits >= max_results || b->truncated) return;

    dp = opendir(path);
    if (!dp) return;

    while ((de = readdir(dp)) != NULL && *total_hits < max_results && !b->truncated) {
        if (de->d_name[0] == '.' && (de->d_name[1] == 0 || (de->d_name[1] == '.' && de->d_name[2] == 0)))
            continue;
        if (!strcmp(de->d_name, ".git") || !strcmp(de->d_name, ".hg") || !strcmp(de->d_name, ".svn"))
            continue;

        if (snprintf(full, sizeof(full), "%s/%s", path, de->d_name) >= (int)sizeof(full))
            continue;

        if (lstat(full, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            grep_rec(full, pattern, ci, ctx, max_results, total_hits, b);
        } else if (S_ISREG(st.st_mode)) {
            grep_file(full, pattern, ci, ctx, max_results, total_hits, b);
        }
    }
    closedir(dp);
}

/* ---- Public API ---- */
char *vs_grep(const char *path, const char *pattern, int case_insensitive, int context_lines, int max_results)
{
    char base[VS_MAX_PATH];
    long total_hits = 0;
    ABuffer b;
    char *output;
    struct stat st;

    if (!path || !pattern) {
        return dupstr("ERROR: grep requires path and pattern");
    }

    if (max_results <= 0) max_results = VS_MAX_GREP_RESULTS;
    if (context_lines < 0) context_lines = 0;
    if (context_lines > 16) context_lines = 16;

    if (path[0] == 0 || strcmp(path, ".") == 0) {
        strcpy(base, ".");
    } else {
        strncpy(base, path, sizeof(base) - 1);
        base[sizeof(base) - 1] = 0;
    }

    if (ab_init(&b, 65536) != 0) {
        return dupstr("ERROR: out of memory");
    }

    ab_addf(&b, "[grep path=%s pattern=%s%s%s]\n",
            base, pattern,
            case_insensitive ? " case-insensitive" : "",
            context_lines > 0 ? " with-context" : "");

    if (stat(base, &st) != 0) {
        ab_addf(&b, "ERROR: cannot stat %s\n", base);
    } else if (S_ISDIR(st.st_mode)) {
        grep_rec(base, pattern, case_insensitive, context_lines, max_results, &total_hits, &b);
    } else if (S_ISREG(st.st_mode)) {
        grep_file(base, pattern, case_insensitive, context_lines, max_results, &total_hits, &b);
    }

    ab_addf(&b, "[matches=%ld]\n", total_hits);
    output = ab_finish(&b);
    return output;
}
