/* SPDX-License-Identifier: Unlicense */
/* MultiEdit tool: multi-hunk file editing (src/multi_edit.c).
 *
 * Applies multiple search/replace pairs atomically in a single pass. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/* ---- Local helpers ---- */
#define VS_MULTI_EDIT_MAX (64U*1024U)
static char *dupstr(const char *s){char *r=(char*)malloc(strlen(s)+1);if(r)strcpy(r,s);return r;}

/* Minimal file writer for multi_edit (avoids pulling in util.c) */
static int me_write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    size_t len = strlen(text);
    size_t written = fwrite(text, 1, len, f);
    int rc = fclose(f);
    return (written == len && rc == 0) ? 0 : -1;
}

char *vs_multi_edit(const char *path, const char **searches, const char **replaces, int count, int *total_replacements)
{
    FILE *f;
    char *raw;
    long size;
    char *result;
    int i;
    int total = 0;

    if (!path || !searches || !replaces || count <= 0) {
        if (total_replacements) *total_replacements = 0;
        return dupstr("ERROR: multi_edit requires path, searches, replaces, and count");
    }

    if (count > VS_MAX_MULTI_EDIT_PAIRS) {
        if (total_replacements) *total_replacements = 0;
        return dupstr("ERROR: multi_edit exceeds maximum edit pairs");
    }

    f = fopen(path, "r");
    if (!f) {
        char *err = (char *)malloc(256);
        if (!err) return NULL;
        snprintf(err, 256, "ERROR: cannot open %s", path);
        return err;
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0 || size > (long)VS_MULTI_EDIT_MAX) {
        fclose(f);
        char *err = (char *)malloc(256);
        if (!err) return NULL;
        snprintf(err, 256, "ERROR: file too large: %s", path);
        return err;
    }

    raw = (char *)malloc((size_t)size + 1);
    if (!raw) {
        fclose(f);
        return dupstr("ERROR: out of memory");
    }
    fread(raw, 1, (size_t)size, f);
    raw[size] = 0;
    fclose(f);

    /* Apply each search/replace pair */
    for (i = 0; i < count; i++) {
        const char *search = searches[i];
        const char *replace = replaces[i] ? replaces[i] : "";

        if (!search || !*search) continue;

        /* Find all occurrences and replace */
        char *buf = raw;
        char *new_buf = NULL;
        size_t new_cap = 0;
        size_t new_len = 0;
        size_t search_len = strlen(search);
        size_t replace_len = strlen(replace);
        int found = 0;

        new_cap = strlen(raw) + 1;
        new_buf = (char *)malloc(new_cap);
        if (!new_buf) {
            free(raw);
            if (total_replacements) *total_replacements = total;
            return dupstr("ERROR: out of memory");
        }

        const char *p = buf;
        while (*p) {
            if (strncmp(p, search, search_len) == 0) {
                /* Found a match */
                if (replace_len > (size_t)-1 - new_len - 1) {
                    free(raw); free(new_buf);
                    if (total_replacements) *total_replacements = 0;
                    return dupstr("ERROR: multi_edit result too large");
                }
                if (replace_len >= new_cap - new_len) {
                    new_cap = new_len + replace_len + 1;
                    char *nb = (char *)realloc(new_buf, new_cap);
                    if (!nb) { free(raw); free(new_buf); if (total_replacements) *total_replacements = total; return dupstr("ERROR: out of memory"); }
                    new_buf = nb;
                }
                memcpy(new_buf + new_len, replace, replace_len);
                new_len += replace_len;
                p += search_len;
                found++;
                total++;
            } else {
                if (new_len >= (size_t)-1 - 1) {
                    free(raw); free(new_buf);
                    if (total_replacements) *total_replacements = 0;
                    return dupstr("ERROR: multi_edit result too large");
                }
                if (new_len + 1 >= new_cap) {
                    new_cap = new_len + 2;
                    char *nb = (char *)realloc(new_buf, new_cap);
                    if (!nb) { free(raw); free(new_buf); if (total_replacements) *total_replacements = total; return dupstr("ERROR: out of memory"); }
                    new_buf = nb;
                }
                new_buf[new_len++] = *p++;
            }
        }
        new_buf[new_len] = 0;
        free(raw);
        raw = new_buf;

        if (!found && search[0]) {
            /* Search string not found - continue to next pair */
        }
    }

    /* Write the result back */
    int rc = me_write_file(path, raw);
    free(raw);

    if (total_replacements) *total_replacements = total;

    if (rc != 0) {
        return dupstr("ERROR: failed to write file");
    }

    result = (char *)malloc(256);
    if (!result) return NULL;
    snprintf(result, 256, "OK: %d replacement(s) across %d edit pair(s) in %s", total, count, path);
    return result;
}
