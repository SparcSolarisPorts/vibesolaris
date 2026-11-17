/* SPDX-License-Identifier: Unlicense */
/* Project index: symbol search and dependency tracking (src/project_index.c).
 *
 * Builds a lightweight index of function/class/method declarations and
 * import/include relationships for common languages.  Stored as JSON
 * in ~/.vibesolaris/index/<project_hash>/. */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

/* ---- Constants ---- */
#define VS_INDEX_MAX_SYMBOLS 1024
#define VS_INDEX_MAX_FILES 256
#define VS_INDEX_MAX_DEPS 64

typedef struct {
    char name[128];
    char file[VS_MAX_PATH];
    int line;
    char kind[16];  /* "function", "class", "method", "type" */
} VSIndexSymbol;

typedef struct {
    char path[VS_MAX_PATH];
    char imports[VS_INDEX_MAX_DEPS][VS_MAX_PATH];
    int import_count;
} VSIndexFile;

typedef struct {
    VSIndexSymbol symbols[VS_INDEX_MAX_SYMBOLS];
    int symbol_count;
    VSIndexFile files[VS_INDEX_MAX_FILES];
    int file_count;
    char root[VS_MAX_PATH];
    char index_dir[VS_MAX_PATH];
    long mtime;
} VSProjectIndex;

/* ---- Static instance ---- */
static VSProjectIndex g_index;
static int g_index_valid = 0;

/* ---- Helpers ---- */
static char *dupstr(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = (char*)malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

static void compute_project_hash(const char *root, char *hash, size_t cap)
{
    /* Simple hash: just use the path itself truncated */
    if (!root || !hash || cap < 2) return;
    size_t len = strlen(root);
    size_t copy = len < cap - 1 ? len : cap - 1;
    memcpy(hash, root, copy);
    hash[copy] = 0;
    /* Replace / with _ */
    size_t i;
    for (i = 0; hash[i]; i++) {
        if (hash[i] == '/') hash[i] = '_';
    }
}

/* Parse a source file for symbols */
static void parse_file_symbols(const char *path, VSIndexSymbol *symbols, int *count, int max)
{
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[1024];
    int line_num = 0;

    while (fgets(line, sizeof(line), f) && *count < max) {
        line_num++;
        const char *p = line;

        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p || *p == '/' || *p == '#') continue;

        /* Look for function declarations: type name( */
        const char *name_start = NULL;
        const char *paren = NULL;

        /* C/C++/Java style: type name(...) */
        const char *word = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (p == word) continue;
        const char *type_end = p;

        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;

        /* Check for identifier */
        if (!isalpha((unsigned char)*p) && *p != '_') continue;
        name_start = p;
        while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;

        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;

        if (*p == '(') {
            /* Found a function */
            size_t nlen = (size_t)(p - name_start);
            if (nlen > 0 && nlen < 128) {
                VSIndexSymbol *s = &symbols[*count];
                memcpy(s->name, name_start, nlen);
                s->name[nlen] = 0;
                strncpy(s->file, path, VS_MAX_PATH - 1);
                s->line = line_num;
                strcpy(s->kind, "function");
                (*count)++;
            }
        }
    }

    fclose(f);
}

/* Parse imports/includes */
static void parse_file_imports(const char *path, char imports[][VS_MAX_PATH], int *count, int max)
{
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[1024];
    while (fgets(line, sizeof(line), f) && *count < max) {
        const char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;

        /* #include "file" or #include <file> */
        if (strncmp(p, "#include", 8) == 0) {
            p += 8;
            while (*p && (*p == ' ' || *p == '\t')) p++;
            if (*p == '"' || *p == '<') {
                p++;
                const char *end = strchr(p, *p == '"' ? '"' : '>');
                if (end) {
                    size_t len = (size_t)(end - p);
                    if (len < VS_MAX_PATH - 1) {
                        memcpy(imports[*count], p, len);
                        imports[*count][len] = 0;
                        (*count)++;
                    }
                }
            }
        }
        /* import "module" (JS/TS) */
        else if (strncmp(p, "import ", 7) == 0 || strncmp(p, "from ", 5) == 0) {
            const char *q = strchr(p, '"');
            if (q) {
                q++;
                const char *end = strchr(q, '"');
                if (end) {
                    size_t len = (size_t)(end - q);
                    if (len < VS_MAX_PATH - 1) {
                        memcpy(imports[*count], q, len);
                        imports[*count][len] = 0;
                        (*count)++;
                    }
                }
            }
        }
        /* import module (Python) */
        else if (strncmp(p, "import ", 7) == 0) {
            p += 7;
            const char *end = p;
            while (*end && !isspace((unsigned char)*end) && *end != '#') end++;
            size_t len = (size_t)(end - p);
            if (len < VS_MAX_PATH - 1) {
                memcpy(imports[*count], p, len);
                imports[*count][len] = 0;
                (*count)++;
            }
        }
    }

    fclose(f);
}

/* Walk directory recursively */
static void walk_directory(const char *dir, char files[][VS_MAX_PATH], int *count, int max)
{
    char cmd[VS_MAX_PATH + 64];
    snprintf(cmd, sizeof(cmd), "find '%s' -type f -name '*.[ch]' -o -name '*.cpp' -o -name '*.java' -o -name '*.py' -o -name '*.rs' -o -name '*.go' -o -name '*.ts' -o -name '*.js' -o -name '*.kt' -o -name '*.rb' 2>/dev/null | head -%d", dir, max);

    FILE *fp = popen(cmd, "r");
    if (!fp) return;

    char path[VS_MAX_PATH];
    while (fgets(path, sizeof(path), fp) && *count < max) {
        char *nl = strchr(path, '\n');
        if (nl) *nl = 0;
        if (path[0]) {
            strncpy(files[*count], path, VS_MAX_PATH - 1);
            (*count)++;
        }
    }
    pclose(fp);
}

/* ---- Public API ---- */

void vs_project_index_init(const char *root)
{
    memset(&g_index, 0, sizeof(g_index));
    g_index_valid = 0;
    if (!root) return;
    strncpy(g_index.root, root, VS_MAX_PATH - 1);

    /* Set up index directory */
    const char *home = getenv("HOME");
    if (home) {
        char hash[256];
        compute_project_hash(root, hash, sizeof(hash));
        snprintf(g_index.index_dir, sizeof(g_index.index_dir),
                 "%s/.vibesolaris/index/%s", home, hash);
    }
}

int vs_project_index_build(const char *root)
{
    if (!root) return -1;
    vs_project_index_init(root);

    char files[VS_INDEX_MAX_FILES][VS_MAX_PATH];
    int file_count = 0;

    walk_directory(root, files, &file_count, VS_INDEX_MAX_FILES);

    g_index.symbol_count = 0;
    g_index.file_count = 0;

    int i;
    for (i = 0; i < file_count; i++) {
        /* Parse symbols */
        parse_file_symbols(files[i], g_index.symbols, &g_index.symbol_count,
                          VS_INDEX_MAX_SYMBOLS - g_index.symbol_count);

        /* Parse imports */
        VSIndexFile *f = &g_index.files[g_index.file_count];
        strncpy(f->path, files[i], VS_MAX_PATH - 1);
        f->import_count = 0;
        parse_file_imports(files[i], f->imports, &f->import_count, VS_INDEX_MAX_DEPS);
        g_index.file_count++;
    }

    g_index_valid = 1;
    return g_index.symbol_count;
}

char *vs_project_index_search(const char *query)
{
    if (!query || !g_index_valid) return dupstr("Index not built");

    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char*)malloc(cap);
    if (!buf) return NULL;
    buf[0] = 0;

    int found = 0;
    int i;
    for (i = 0; i < g_index.symbol_count; i++) {
        if (strstr(g_index.symbols[i].name, query)) {
            size_t need = strlen(buf) + 256;
            if (len + need + 1 > cap) {
                size_t nc = cap * 2;
                while (len + need + 1 > nc) nc *= 2;
                char *nb = (char*)realloc(buf, nc);
                if (!nb) { free(buf); return NULL; }
                buf = nb;
                cap = nc;
            }
            len += snprintf(buf + len, cap - len,
                           "%s (%s) %s:%d\n",
                           g_index.symbols[i].name, g_index.symbols[i].kind,
                           g_index.symbols[i].file, g_index.symbols[i].line);
            found++;
            if (found >= 50) break;
        }
    }

    if (!found) {
        free(buf);
        return dupstr("No symbols found");
    }

    buf[len] = 0;
    return buf;
}

char *vs_project_index_references(const char *symbol)
{
    if (!symbol || !g_index_valid) return dupstr("Index not built");

    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char*)malloc(cap);
    if (!buf) return NULL;
    buf[0] = 0;

    int found = 0;
    int i;
    for (i = 0; i < g_index.file_count; i++) {
        int j;
        for (j = 0; j < g_index.files[i].import_count; j++) {
            if (strstr(g_index.files[i].imports[j], symbol)) {
                size_t need = strlen(buf) + VS_MAX_PATH + 32;
                if (len + need + 1 > cap) {
                    size_t nc = cap * 2;
                    while (len + need + 1 > nc) nc *= 2;
                    char *nb = (char*)realloc(buf, nc);
                    if (!nb) { free(buf); return NULL; }
                    buf = nb;
                    cap = nc;
                }
                len += snprintf(buf + len, cap - len, "%s\n", g_index.files[i].path);
                found++;
                break;
            }
        }
    }

    if (!found) {
        free(buf);
        return dupstr("No references found");
    }

    buf[len] = 0;
    return buf;
}

char *vs_project_index_dependencies(const char *file_path)
{
    if (!file_path || !g_index_valid) return dupstr("Index not built");

    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char*)malloc(cap);
    if (!buf) return NULL;
    buf[0] = 0;

    int i;
    for (i = 0; i < g_index.file_count; i++) {
        if (strcmp(g_index.files[i].path, file_path) == 0) {
            int j;
            for (j = 0; j < g_index.files[i].import_count; j++) {
                size_t need = strlen(buf) + VS_MAX_PATH + 8;
                if (len + need + 1 > cap) {
                    size_t nc = cap * 2;
                    while (len + need + 1 > nc) nc *= 2;
                    char *nb = (char*)realloc(buf, nc);
                    if (!nb) { free(buf); return NULL; }
                    buf = nb;
                    cap = nc;
                }
                len += snprintf(buf + len, cap - len, "  %s\n", g_index.files[i].imports[j]);
            }
            buf[len] = 0;
            if (g_index.files[i].import_count == 0) {
                free(buf);
                return dupstr("No dependencies");
            }
            return buf;
        }
    }

    free(buf);
    return dupstr("File not in index");
}

int vs_project_index_is_valid(void)
{
    return g_index_valid;
}

void vs_project_index_clear(void)
{
    memset(&g_index, 0, sizeof(g_index));
    g_index_valid = 0;
}
