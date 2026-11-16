/* SPDX-License-Identifier: Unlicense */
/* Notebook tools: Jupyter .ipynb read/edit (src/notebook.c).
 *
 * Parses JSON .ipynb format, extracts markdown/code cells,
 * and supports cell source replacement. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/* ---- Local helpers ---- */
typedef struct {char *p;size_t n,cap;int truncated;} ABuffer;
static int ab_init(ABuffer *b,size_t cap){b->p=(char*)malloc(cap);if(!b->p)return -1;b->p[0]=0;b->n=0;b->cap=cap;b->truncated=0;return 0;}
static void ab_add(ABuffer *b,const char *s){size_t m,room;if(!b||!b->p||!s||b->truncated)return;m=strlen(s);room=b->cap-b->n;if(room<=96){b->truncated=1;return;}if(m>=room-96){m=room-96;b->truncated=1;}memcpy(b->p+b->n,s,m);b->n+=m;b->p[b->n]=0;}
static void ab_addf(ABuffer *b,const char *fmt,...){char t[2048];va_list ap;va_start(ap,fmt);vsnprintf(t,sizeof(t),fmt,ap);va_end(ap);ab_add(b,t);}
static char *ab_finish(ABuffer *b){if(b->truncated&&b->p&&b->cap>b->n+48){const char *m="\n...[native tool output truncated]...\n";size_t z=strlen(m);if(z<b->cap-b->n){memcpy(b->p+b->n,m,z+1);b->n+=z;}}return b->p;}
static char *dupstr(const char *s){char *r=(char*)malloc(strlen(s)+1);if(r)strcpy(r,s);return r;}

/* Minimal file writer for notebook_edit (avoids pulling in util.c) */
static int nb_write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    size_t len = strlen(text);
    size_t written = fwrite(text, 1, len, f);
    int rc = fclose(f);
    return (written == len && rc == 0) ? 0 : -1;
}

/* Minimal JSON string extractor: finds "key" and returns the string value.
 * Handles escaped quotes and backslashes. Returns malloc'd string or NULL. */
static char *json_get_string(const char *json, const char *key)
{
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return NULL;
    p += strlen(search);
    /* Skip whitespace and colon */
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return NULL;
    p++; /* skip opening quote */
    /* Find closing quote, handling escapes */
    size_t cap = 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    size_t pos = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p + 1)) {
            p++;
            if (pos + 2 >= cap) { cap *= 2; char *nb = (char *)realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; }
            buf[pos++] = *p++;
        } else {
            if (pos + 1 >= cap) { cap *= 2; char *nb = (char *)realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; }
            buf[pos++] = *p++;
        }
    }
    buf[pos] = 0;
    if (*p == '"') p++;
    (void)p;
    return buf;
}

/* Extract a JSON array of objects, returning the nth object's string field.
 * This is a simplified parser for .ipynb cells. */
static char *json_get_cell_source(const char *json, int index, const char *field)
{
    /* Find "cells" array */
    const char *cells = strstr(json, "\"cells\"");
    if (!cells) return NULL;
    const char *arr = strchr(cells, '[');
    if (!arr) return NULL;
    arr++; /* skip [ */

    int i = 0;
    while (*arr && i <= index) {
        const char *obj = strchr(arr, '{');
        if (!obj) break;
        if (i == index) {
            return json_get_string(obj, field);
        }
        /* Find closing } */
        int depth = 0;
        const char *p = obj;
        while (*p) {
            if (*p == '{') depth++;
            else if (*p == '}') { depth--; if (depth == 0) { arr = p + 1; break; } }
            p++;
        }
        if (!*p) break;
        i++;
        /* Skip comma */
        while (*arr == ' ' || *arr == ',' || *arr == '\n' || *arr == '\r') arr++;
    }
    return NULL;
}

static int json_count_cells(const char *json)
{
    const char *cells = strstr(json, "\"cells\"");
    if (!cells) return 0;
    const char *arr = strchr(cells, '[');
    if (!arr) return 0;
    arr++;
    int count = 0;
    while (*arr) {
        const char *obj = strchr(arr, '{');
        if (!obj) break;
        int depth = 0;
        const char *p = obj;
        while (*p) {
            if (*p == '{') depth++;
            else if (*p == '}') { depth--; if (depth == 0) { arr = p + 1; break; } }
            p++;
        }
        if (!*p) break;
        count++;
        while (*arr == ' ' || *arr == ',' || *arr == '\n' || *arr == '\r') arr++;
    }
    return count;
}

/* ---- Public API ---- */

char *vs_notebook_read(const char *path)
{
    FILE *f;
    char *raw;
    long size;
    ABuffer b;
    char *output;
    int cell_count;
    int i;

    if (!path) {
        return dupstr("ERROR: notebook_read requires path");
    }

    f = fopen(path, "r");
    if (!f) {
        char *err = (char *)malloc(256);
        if (!err) return NULL;
        snprintf(err, 256, "ERROR: cannot open notebook %s", path);
        return err;
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > VS_MAX_TEXT) {
        fclose(f);
        char *err = (char *)malloc(256);
        if (!err) return NULL;
        snprintf(err, 256, "ERROR: notebook file too large or empty: %s", path);
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

    /* Check if it looks like a notebook */
    if (strstr(raw, "nbformat") == NULL) {
        free(raw);
        char *err = (char *)malloc(256);
        if (!err) return NULL;
        snprintf(err, 256, "ERROR: %s does not appear to be a Jupyter notebook", path);
        return err;
    }

    cell_count = json_count_cells(raw);

    if (ab_init(&b, 65536) != 0) {
        free(raw);
        return dupstr("ERROR: out of memory");
    }

    char *nb_format = json_get_string(raw, "nbformat");
    char *nb_format_minor = json_get_string(raw, "nbformat_minor");
    ab_addf(&b, "[notebook %s nbformat=%s.%s cells=%d]\n",
            path,
            nb_format ? nb_format : "0",
            nb_format_minor ? nb_format_minor : "0",
            cell_count);
    free(nb_format);
    free(nb_format_minor);

    for (i = 0; i < cell_count && i < VS_MAX_NOTEBOOK_CELLS; i++) {
        char *cell_type = json_get_cell_source(raw, i, "cell_type");
        char *source = json_get_cell_source(raw, i, "source");

        if (cell_type) {
            ab_addf(&b, "\n--- cell %d (%s) ---\n", i, cell_type);
            free(cell_type);
        } else {
            ab_addf(&b, "\n--- cell %d ---\n", i);
        }

        if (source) {
            /* Source can be an array of lines or a single string */
            if (source[0] == '[') {
                /* Array of lines - join them */
                const char *p = source + 1;
                while (*p) {
                    const char *str_start = strchr(p, '"');
                    if (!str_start) break;
                    str_start++;
                    const char *str_end = str_start;
                    while (*str_end && *str_end != '"') {
                        if (*str_end == '\\' && *(str_end + 1)) str_end++;
                        str_end++;
                    }
                    if (str_end > str_start) {
                        char line[VS_NOTEBOOK_CELL_MAX];
                        size_t len = (size_t)(str_end - str_start);
                        if (len >= sizeof(line)) len = sizeof(line) - 1;
                        memcpy(line, str_start, len);
                        line[len] = 0;
                        ab_add(&b, line);
                        ab_add(&b, "\n");
                    }
                    p = (*str_end == '"') ? str_end + 1 : str_end;
                    while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == ']') p++;
                }
            } else {
                ab_add(&b, source);
                ab_add(&b, "\n");
            }
            free(source);
        }
    }

    ab_addf(&b, "\n[notebook cells=%d]\n", cell_count);
    output = ab_finish(&b);
    free(raw);
    return output;
}

char *vs_notebook_edit(const char *path, int cell_index, const char *new_source)
{
    FILE *f;
    char *raw;
    long size;

    if (!path || new_source == NULL) {
        return dupstr("ERROR: notebook_edit requires path and source");
    }

    f = fopen(path, "r");
    if (!f) {
        char *err = (char *)malloc(256);
        if (!err) return NULL;
        snprintf(err, 256, "ERROR: cannot open notebook %s", path);
        return err;
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > VS_MAX_TEXT) {
        fclose(f);
        char *err = (char *)malloc(256);
        if (!err) return NULL;
        snprintf(err, 256, "ERROR: notebook file too large or empty: %s", path);
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

    if (strstr(raw, "nbformat") == NULL) {
        free(raw);
        char *err = (char *)malloc(256);
        if (!err) return NULL;
        snprintf(err, 256, "ERROR: %s does not appear to be a Jupyter notebook", path);
        return err;
    }

    int cell_count = json_count_cells(raw);
    if (cell_index < 0 || cell_index >= cell_count) {
        free(raw);
        char *err = (char *)malloc(512);
        if (!err) return NULL;
        snprintf(err, 512, "ERROR: cell index %d out of range (notebook has %d cells)", cell_index, cell_count);
        return err;
    }

    /* Find the source field of the target cell and replace it */
    const char *cells = strstr(raw, "\"cells\"");
    const char *arr = strchr(cells, '[');
    arr++;

    int i = 0;
    while (*arr && i <= cell_index) {
        const char *obj = strchr(arr, '{');
        if (!obj) break;
        if (i == cell_index) {
            /* Find "source" in this cell object */
            const char *src_key = strstr(obj, "\"source\"");
            if (src_key) {
                const char *val_start = strchr(src_key + 8, '"');
                if (val_start) val_start++;
                if (val_start) {
                    /* Find the end of the source value */
                    const char *val_end;
                    if (*val_start == '[') {
                        /* Array value - find matching ] */
                        int depth = 0;
                        val_end = val_start;
                        while (*val_end) {
                            if (*val_end == '[') depth++;
                            else if (*val_end == ']') { depth--; if (depth == 0) break; }
                            val_end++;
                        }
                    } else {
                        /* String value */
                        val_end = val_start;
                        while (*val_end && *val_end != '"') {
                            if (*val_end == '\\') val_end++;
                            val_end++;
                        }
                    }
                    if (*val_end) {
                        /* Build new JSON with replaced source */
                        size_t before = (size_t)(val_start - raw);
                        size_t after = strlen(val_end);

                        /* Escape new_source for JSON */
                        size_t esc_cap = strlen(new_source) * 2 + 64;
                        char *escaped = (char *)malloc(esc_cap);
                        if (!escaped) { free(raw); return dupstr("ERROR: out of memory"); }
                        size_t ep = 0;
                        for (const char *s = new_source; *s && ep < esc_cap - 1; s++) {
                            if (*s == '"') { escaped[ep++] = '\\'; escaped[ep++] = '"'; }
                            else if (*s == '\\') { escaped[ep++] = '\\'; escaped[ep++] = '\\'; }
                            else if (*s == '\n') { escaped[ep++] = '\\'; escaped[ep++] = 'n'; }
                            else if (*s == '\r') { escaped[ep++] = '\\'; escaped[ep++] = 'r'; }
                            else if (*s == '\t') { escaped[ep++] = '\\'; escaped[ep++] = 't'; }
                            else escaped[ep++] = *s;
                        }
                        escaped[ep] = 0;

                        size_t new_size = before + 2 + ep + after;
                        char *new_raw = (char *)malloc(new_size + 1);
                        if (!new_raw) { free(raw); free(escaped); return dupstr("ERROR: out of memory"); }
                        memcpy(new_raw, raw, before);
                        new_raw[before] = '"';
                        memcpy(new_raw + before + 1, escaped, ep);
                        new_raw[before + 1 + ep] = '"';
                        memcpy(new_raw + before + 1 + ep + 1, val_end, after);
                        new_raw[new_size] = 0;

                        /* Write back */
                        int rc = nb_write_file(path, new_raw);
                        free(raw);
                        free(escaped);
                        free(new_raw);

                        if (rc == 0) {
                            char *msg = (char *)malloc(256);
                            if (!msg) return NULL;
                            snprintf(msg, 256, "OK: notebook cell %d updated in %s", cell_index, path);
                            return msg;
                        } else {
                            return dupstr("ERROR: failed to write notebook");
                        }
                    }
                }
            }
            free(raw);
            char *err = (char *)malloc(256);
            if (!err) return NULL;
            snprintf(err, 256, "ERROR: could not find source in cell %d", cell_index);
            return err;
        }
        /* Skip to next object */
        int depth = 0;
        const char *p = obj;
        while (*p) {
            if (*p == '{') depth++;
            else if (*p == '}') { depth--; if (depth == 0) { arr = p + 1; break; } }
            p++;
        }
        if (!*p) break;
        i++;
        while (*arr == ' ' || *arr == ',' || *arr == '\n' || *arr == '\r') arr++;
    }

    free(raw);
    return dupstr("ERROR: cell not found");
}
