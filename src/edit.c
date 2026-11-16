/* SPDX-License-Identifier: Unlicense */
/* Edit tool: targeted file modifications.

   Mirrors Claude Code's Edit tool: instead of rewriting an entire file,
   the agent provides a search string (exact match) and a replacement string.
   This is safer than full writes because it preserves surrounding context
   and makes the diff explicit.  Supports replace_all for repeated patterns.

   Portable C: uses standard file I/O, no external dependencies. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* Unescape \\n, \\t, \\r, \\\\ sequences in place */
static void unesc(char *s)
{
    char *r = s, *w = s;
    while (r && *r) {
        if (*r == '\\' && r[1]) {
            r++;
            if (*r == 'n') *w++ = '\n';
            else if (*r == 't') *w++ = '\t';
            else if (*r == 'r') *w++ = '\r';
            else *w++ = *r;
            r++;
        } else {
            *w++ = *r++;
        }
    }
    if (s) *w = 0;
}

char *vs_edit_file(const char *path, const char *search, const char *replace, int *replacements)
{
    FILE *f;
    char *content;
    unsigned long size;
    char *result;
    size_t cap;
    size_t n = 0;
    int count = 0;
    char *pos;
    size_t search_len;
    size_t replace_len;

    if (!path || !*path) {
        return dupstr("ERROR: edit requires a non-empty path");
    }
    if (!search || !*search) {
        return dupstr("ERROR: edit requires a non-empty search string");
    }
    if (!replace) replace = "";

    /* Read the file */
    f = fopen(path, "r");
    if (!f) {
        size_t sz = strlen(path) + 128;
        result = (char *)malloc(sz);
        if (!result) return NULL;
        snprintf(result, sz, "ERROR: cannot open file for editing: %s", path);
        return result;
    }

    /* Read entire file into memory */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 0 || fsize > (long)VS_EDIT_DIFF_MAX) {
        fclose(f);
        size_t sz = strlen(path) + 128;
        result = (char *)malloc(sz);
        if (!result) return NULL;
        snprintf(result, sz, "ERROR: file too large for editing: %s (%ld bytes)", path, fsize);
        return result;
    }

    content = (char *)malloc((size_t)fsize + 1);
    if (!content) {
        fclose(f);
        return dupstr("ERROR: out of memory reading file");
    }
    size = fread(content, 1, (size_t)fsize, f);
    content[size] = 0;
    fclose(f);

    search_len = strlen(search);
    replace_len = strlen(replace);

    /* Find and replace all occurrences */
    pos = content;
    while ((pos = strstr(pos, search)) != NULL) {
        count++;
        pos += search_len;
    }

    if (count == 0) {
        free(content);
        size_t sz = strlen(path) + strlen(search) + 128;
        result = (char *)malloc(sz);
        if (!result) return NULL;
        snprintf(result, sz, "ERROR: search text not found in %s", path);
        return result;
    }

    /* Allocate result buffer with extra space for replacements */
    if (replace_len > ((size_t)-1 - (size_t)size - 1) / (size_t)count) {
        free(content);
        return dupstr("ERROR: edit result too large");
    }
    cap = (size_t)size + replace_len * (size_t)count + 1;
    result = (char *)malloc(cap);
    if (!result) {
        free(content);
        return dupstr("ERROR: out of memory for edit result");
    }

    /* Perform the replacement */
    n = 0;
    pos = content;
    char *next = content;
    while ((next = strstr(pos, search)) != NULL) {
        /* Copy text before the match */
        size_t before = (size_t)(next - pos);
        memcpy(result + n, pos, before);
        n += before;
        /* Copy the replacement */
        memcpy(result + n, replace, replace_len);
        n += replace_len;
        pos = next + search_len;
    }
    /* Copy remaining text */
    {
        size_t remaining = strlen(pos);
        memcpy(result + n, pos, remaining);
        n += remaining;
    }
    result[n] = 0;
    free(content);

    /* Write the modified content back */
    f = fopen(path, "w");
    if (!f) {
        size_t sz = strlen(path) + 128;
        char *err = (char *)malloc(sz);
        if (!err) { free(result); return NULL; }
        snprintf(err, sz, "ERROR: cannot write to file: %s", path);
        free(result);
        return err;
    }
    fprintf(f, "%s", result);
    fclose(f);

    /* Build success message */
    cap = strlen(path) + 256;
    char *msg = (char *)malloc(cap);
    if (!msg) { free(result); return NULL; }
    snprintf(msg, cap, "OK: edited %s (%d replacement%s)", path, count, count == 1 ? "" : "s");
    free(result);

    if (replacements) *replacements = count;
    return msg;
}
