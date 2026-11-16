/* SPDX-License-Identifier: Unlicense */
/* Glob tool: pattern-based file discovery (src/glob.c).
 *
 * Supports ** (recursive), * (any chars except /), and ? (single char)
 * patterns via a recursive directory walk.  No libglob dependency. */
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

/* ---- Pattern matching helpers ---- */

/* Match a single path segment against a glob fragment.
 * fragment may contain * and ? but never /. */
static int match_segment(const char *frag, const char *seg)
{
    while (*frag && *seg) {
        if (*frag == '?') {
            frag++; seg++;
        } else if (*frag == '*') {
            /* Skip consecutive stars */
            while (*frag == '*') frag++;
            if (*frag == 0) return 1; /* trailing * matches everything */
            if (*(frag - 1) == '/' && *frag == '/') {
                /* ** matches zero or more segments — handled by caller */
                return 1;
            }
            /* Try matching the rest of frag against every suffix of seg */
            while (*seg) {
                if (match_segment(frag, seg)) return 1;
                seg++;
            }
            return match_segment(frag, "");
        } else {
            if (*frag != *seg) return 0;
            frag++; seg++;
        }
    }
    while (*frag == '*') frag++;
    return (*frag == 0) && (*seg == 0);
}

/* Check if a filename matches a single-segment pattern */
static int name_matches(const char *name, const char *pattern)
{
    return match_segment(pattern, name);
}

/* ---- Recursive directory walk with pattern ---- */

static void glob_rec(const char *base_path, const char *pattern, int *count, char results[][VS_MAX_PATH], int max_results)
{
    DIR *dp;
    struct dirent *de;
    char full[VS_MAX_PATH];
    struct stat st;

    if (*count >= max_results) return;

    dp = opendir(base_path);
    if (!dp) return;

    while ((de = readdir(dp)) != NULL && *count < max_results) {
        if (de->d_name[0] == '.' && (de->d_name[1] == 0 || (de->d_name[1] == '.' && de->d_name[2] == 0)))
            continue;

        if (snprintf(full, sizeof(full), "%s/%s", base_path, de->d_name) >= (int)sizeof(full))
            continue;

        if (lstat(full, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            /* Check if pattern has ** for recursive matching */
            if (strstr(pattern, "**") != NULL) {
                /* Try matching the current name against the first segment of pattern */
                const char *slash = strchr(pattern, '/');
                if (slash) {
                    char seg[VS_MAX_PATH];
                    size_t len = (size_t)(slash - pattern);
                    if (len >= sizeof(seg)) len = sizeof(seg) - 1;
                    memcpy(seg, pattern, len);
                    seg[len] = 0;

                    if (name_matches(de->d_name, seg)) {
                        /* Match with remaining pattern */
                        const char *rest = slash + 1;
                        /* Skip leading slashes after ** */
                        while (*rest == '/') rest++;

                        /* Try ** matching zero more directories */
                        if (rest[0] == '*' && rest[1] == '/') {
                            /* Recurse with the same pattern */
                            glob_rec(full, pattern, count, results, max_results);
                            /* Also try matching the rest after ** */
                            const char *after_double = rest + 2;
                            if (after_double[0]) {
                                glob_rec(full, after_double, count, results, max_results);
                            } else {
                                /* ** at end matches the directory itself */
                                if (*count < max_results) {
                                    strncpy(results[*count], full, VS_MAX_PATH - 1);
                                    results[*count][VS_MAX_PATH - 1] = 0;
                                    (*count)++;
                                }
                            }
                        }
                    }
                } else {
                    /* No slash in pattern — pattern applies to directory name */
                    if (name_matches(de->d_name, pattern)) {
                        if (*count < max_results) {
                            strncpy(results[*count], full, VS_MAX_PATH - 1);
                            results[*count][VS_MAX_PATH - 1] = 0;
                            (*count)++;
                        }
                    }
                }
            } else {
                /* No ** — just recurse into subdirectories */
                glob_rec(full, pattern, count, results, max_results);
            }
        } else if (S_ISREG(st.st_mode)) {
            /* Regular file — try to match against the pattern */
            const char *slash = strrchr(pattern, '/');
            const char *file_pat = slash ? slash + 1 : pattern;

            if (name_matches(de->d_name, file_pat)) {
                /* Also check the directory part if pattern has path components */
                int dir_ok = 1;
                if (slash) {
                    /* Check directory part of pattern against base */
                    /* Simplified: if pattern has **, we already handled it above */
                    if (strstr(pattern, "**") == NULL) {
                        /* Exact directory match needed */
                        char dir_name[VS_MAX_PATH];
                        size_t dlen = (size_t)(slash - pattern);
                        if (dlen >= sizeof(dir_name)) dlen = sizeof(dir_name) - 1;
                        memcpy(dir_name, pattern, dlen);
                        dir_name[dlen] = 0;
                        /* Remove leading slash */
                        if (dir_name[0] == '/') dir_name[0] = 0;
                        if (dir_name[0] && strcmp(base_path, ".") != 0) {
                            /* Check if the base path ends with the dir pattern */
                            const char *base_name = strrchr(base_path, '/');
                            base_name = base_name ? base_name + 1 : base_path;
                            if (!name_matches(base_name, dir_name))
                                dir_ok = 0;
                        }
                    }
                }
                if (dir_ok && *count < max_results) {
                    strncpy(results[*count], full, VS_MAX_PATH - 1);
                    results[*count][VS_MAX_PATH - 1] = 0;
                    (*count)++;
                }
            }
        }
    }
    closedir(dp);
}

/* Simplified glob for recursive file pattern matching */
static void glob_simple(const char *base, const char *pattern, int *count, char results[][VS_MAX_PATH], int max_results)
{
    DIR *dp;
    struct dirent *de;
    char full[VS_MAX_PATH];
    struct stat st;

    if (*count >= max_results) return;

    dp = opendir(base);
    if (!dp) return;

    while ((de = readdir(dp)) != NULL && *count < max_results) {
        if (de->d_name[0] == '.' && (de->d_name[1] == 0 || (de->d_name[1] == '.' && de->d_name[2] == 0)))
            continue;

        if (snprintf(full, sizeof(full), "%s/%s", base, de->d_name) >= (int)sizeof(full))
            continue;

        if (lstat(full, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            if (strstr(pattern, "**") != NULL) {
                /* Recursive pattern — recurse into subdirectory */
                glob_simple(full, pattern, count, results, max_results);
            } else {
                /* Non-recursive — still recurse to find files in subdirs */
                glob_simple(full, pattern, count, results, max_results);
            }
        } else if (S_ISREG(st.st_mode)) {
            /* Check if filename matches the pattern */
            const char *pat = pattern;
            const char *slash = strrchr(pattern, '/');

            if (slash && strstr(pattern, "**") != NULL) {
                /* Double-star pattern - match just the filename */
                pat = slash + 1;
            } else if (slash) {
                pat = slash + 1;
            }

            if (name_matches(de->d_name, pat)) {
                if (*count < max_results) {
                    strncpy(results[*count], full, VS_MAX_PATH - 1);
                    results[*count][VS_MAX_PATH - 1] = 0;
                    (*count)++;
                }
            }
        }
    }
    closedir(dp);
}

/* Public API */
char *vs_glob(const char *path, const char *pattern, int *out_count, char results[][VS_MAX_PATH])
{
    char base[VS_MAX_PATH];
    int count = 0;
    ABuffer b;
    char *output;

    if (!path || !pattern || !out_count || !results) {
        if (out_count) *out_count = 0;
        return dupstr("ERROR: glob requires path and pattern");
    }

    /* Determine base path */
    if (path[0] == 0 || strcmp(path, ".") == 0) {
        strcpy(base, ".");
    } else {
        strncpy(base, path, sizeof(base) - 1);
        base[sizeof(base) - 1] = 0;
    }

    struct stat st;
    if (stat(base, &st) != 0 || !S_ISDIR(st.st_mode)) {
        *out_count = 0;
        return dupstr("ERROR: glob path is not a directory");
    }

    memset(results, 0, sizeof(char[VS_MAX_PATH]) * VS_MAX_GLOB_RESULTS);
    glob_simple(base, pattern, &count, results, VS_MAX_GLOB_RESULTS);

    /* Build output string */
    if (ab_init(&b, 65536) != 0) {
        *out_count = 0;
        return dupstr("ERROR: out of memory");
    }
    ab_addf(&b, "[glob %s/%s]\n", base, pattern);
    if (count == 0) {
        ab_add(&b, "(no matches)\n");
    } else {
        int i;
        for (i = 0; i < count; i++) {
            ab_addf(&b, "%s\n", results[i]);
        }
    }
    ab_addf(&b, "[matches=%d]\n", count);
    output = ab_finish(&b);
    *out_count = count;
    return output;
}
