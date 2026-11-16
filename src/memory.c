/* SPDX-License-Identifier: Unlicense */
/* Persistent memory system: save, load, delete, and search memory entries.
   Mirrors Claude Code's memdir: entries are stored as JSON lines in a file
   under ~/.vibesolaris/memory.jsonl.  Each entry has a key, value, tag,
   type, and timestamps.

   Used for cross-session context: the agent can save important facts,
   preferences, or references that persist across conversations. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static char *dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static int json_escape_str(const char *src, char *dst, size_t dst_cap)
{
    size_t di = 0;
    for (; *src && di + 2 < dst_cap; src++) {
        unsigned char c = (unsigned char)*src;
        if (c == '"' || c == '\\') {
            dst[di++] = '\\';
            dst[di++] = (char)c;
        } else if (c == '\n') {
            dst[di++] = '\\';
            dst[di++] = 'n';
        } else if (c == '\r') {
            dst[di++] = '\\';
            dst[di++] = 'r';
        } else if (c == '\t') {
            dst[di++] = '\\';
            dst[di++] = 't';
        } else if (c < 0x20) {
            continue; /* skip control chars */
        } else {
            dst[di++] = (char)c;
        }
    }
    if (di < dst_cap) dst[di] = 0;
    return (int)di;
}

void vs_memory_init(VSMemoryStore *store, const char *path)
{
    memset(store, 0, sizeof(*store));
    if (path) {
        strncpy(store->store_path, path, VS_MAX_PATH - 1);
    } else {
        /* Default path */
        const char *home = getenv("HOME");
        if (home) {
            snprintf(store->store_path, VS_MAX_PATH, "%s/.vibesolaris/memory.jsonl", home);
        } else {
            strcpy(store->store_path, ".vibesolaris/memory.jsonl");
        }
    }
}

int vs_memory_save(VSMemoryStore *store, const char *key, const char *value, const char *tag, VSMemoryType type)
{
    VSMemoryEntry *entry;
    long now = time(NULL);

    if (!key || !key[0] || !value) return -1;
    if (!tag) tag = "";

    /* Check if key already exists - update in place */
    entry = vs_memory_get(store, key);
    if (entry) {
        strncpy(entry->value, value, VS_MEMORY_VALUE_MAX - 1);
        strncpy(entry->tag, tag, VS_MEMORY_TAG_MAX - 1);
        entry->type = type;
        entry->accessed_at = now;
        entry->access_count++;
        return 0;
    }

    /* New entry */
    if (store->entry_count >= VS_MAX_MEMORY_ENTRIES) {
        /* Evict least recently used */
        int lru_idx = 0;
        long lru_time = store->entries[0].accessed_at;
        int i;
        for (i = 1; i < store->entry_count; i++) {
            if (store->entries[i].accessed_at < lru_time) {
                lru_time = store->entries[i].accessed_at;
                lru_idx = i;
            }
        }
        memset(&store->entries[lru_idx], 0, sizeof(VSMemoryEntry));
        store->entry_count--;
    }

    entry = &store->entries[store->entry_count];
    strncpy(entry->key, key, VS_MEMORY_KEY_MAX - 1);
    strncpy(entry->value, value, VS_MEMORY_VALUE_MAX - 1);
    strncpy(entry->tag, tag, VS_MEMORY_TAG_MAX - 1);
    entry->type = type;
    entry->created_at = now;
    entry->accessed_at = now;
    entry->access_count = 1;
    store->entry_count++;

    return 0;
}

int vs_memory_delete(VSMemoryStore *store, const char *key)
{
    int i;

    for (i = 0; i < store->entry_count; i++) {
        if (!strcmp(store->entries[i].key, key)) {
            /* Shift down */
            if (i < store->entry_count - 1) {
                memmove(&store->entries[i], &store->entries[i + 1],
                        (size_t)(store->entry_count - i - 1) * sizeof(VSMemoryEntry));
            }
            memset(&store->entries[store->entry_count - 1], 0, sizeof(VSMemoryEntry));
            store->entry_count--;
            return 0;
        }
    }
    return -1;
}

VSMemoryEntry *vs_memory_get(VSMemoryStore *store, const char *key)
{
    int i;
    for (i = 0; i < store->entry_count; i++) {
        if (!strcmp(store->entries[i].key, key)) {
            store->entries[i].accessed_at = time(NULL);
            store->entries[i].access_count++;
            return &store->entries[i];
        }
    }
    return NULL;
}

char *vs_memory_search(VSMemoryStore *store, const char *query, int max_results)
{
    int i, count = 0;
    size_t cap = 1024;
    char *out = (char *)malloc(cap);
    size_t pos = 0;

    if (!out) return NULL;
    out[0] = 0;
    if (!query) query = "";

    for (i = 0; i < store->entry_count && count < max_results; i++) {
        VSMemoryEntry *e = &store->entries[i];
        int match = 0;

        if (!query[0]) {
            match = 1; /* No query = return all */
        } else {
            /* Case-insensitive search in key, value, and tag */
            char key_lower[VS_MEMORY_KEY_MAX];
            char val_lower[VS_MEMORY_VALUE_MAX];
            char query_lower[256];
            int j;
            for (j = 0; j < (int)sizeof(key_lower) && e->key[j]; j++) {
                key_lower[j] = (char)((e->key[j] >= 'A' && e->key[j] <= 'Z') ? e->key[j] + 32 : e->key[j]);
            }
            key_lower[j] = 0;
            for (j = 0; j < (int)sizeof(val_lower) && e->value[j]; j++) {
                val_lower[j] = (char)((e->value[j] >= 'A' && e->value[j] <= 'Z') ? e->value[j] + 32 : e->value[j]);
            }
            val_lower[j] = 0;
            for (j = 0; j < (int)sizeof(query_lower) && query[j]; j++) {
                query_lower[j] = (char)((query[j] >= 'A' && query[j] <= 'Z') ? query[j] + 32 : query[j]);
            }
            query_lower[j] = 0;

            if (strstr(key_lower, query_lower) || strstr(val_lower, query_lower) ||
                strstr(e->tag, query_lower)) {
                match = 1;
            }
        }

        if (match) {
            char buf[2048];
            int n = snprintf(buf, sizeof(buf),
                "[%s] %s: %s\n",
                e->tag, e->key, e->value);
            if (n < 0) continue;
            size_t blen = (size_t)n;
            if (pos + blen + 1 > cap) {
                cap = (pos + blen + 1) * 2;
                char *new_out = (char *)realloc(out, cap);
                if (!new_out) { free(out); return NULL; }
                out = new_out;
            }
            memcpy(out + pos, buf, blen);
            pos += blen;
            out[pos] = 0;
            count++;
        }
    }

    return out;
}

char *vs_memory_list(VSMemoryStore *store, int max_lines)
{
    int i, count = 0;
    size_t cap = 1024;
    char *out = (char *)malloc(cap);
    size_t pos = 0;

    if (!out) return NULL;
    out[0] = 0;

    const char *type_names[] = {"note", "pref", "context", "skill_ref"};

    for (i = 0; i < store->entry_count && count < max_lines; i++) {
        VSMemoryEntry *e = &store->entries[i];
        const char *type_name = (e->type >= 0 && e->type < 4) ? type_names[e->type] : "unknown";
        char buf[1024];
        int n = snprintf(buf, sizeof(buf),
            "%-20s [%-8s] %-10s (accessed %d times)\n",
            e->key, type_name, e->tag, e->access_count);
        if (n < 0) continue;
        size_t blen = (size_t)n;
        if (pos + blen + 1 > cap) {
            cap = (pos + blen + 1) * 2;
            char *new_out = (char *)realloc(out, cap);
            if (!new_out) { free(out); return NULL; }
            out = new_out;
        }
        memcpy(out + pos, buf, blen);
        pos += blen;
        out[pos] = 0;
        count++;
    }

    return out;
}

void vs_memory_load(VSMemoryStore *store)
{
    FILE *f;
    char line[4096];

    f = fopen(store->store_path, "r");
    if (!f) return;

    while (fgets(line, sizeof(line), f)) {
        /* Parse JSON line - simple parser */
        char key[VS_MEMORY_KEY_MAX] = {0};
        char value[VS_MEMORY_VALUE_MAX] = {0};
        char tag[VS_MEMORY_TAG_MAX] = {0};
        int type = 0;

        /* Extract "key": "..." */
        {
            const char *p = strstr(line, "\"key\"");
            if (p) {
                p = strchr(p + 5, '"');
                if (p) {
                    p++;
                    const char *e = strchr(p, '"');
                    if (e) {
                        size_t n = (size_t)(e - p);
                        if (n >= VS_MEMORY_KEY_MAX) n = VS_MEMORY_KEY_MAX - 1;
                        memcpy(key, p, n);
                        key[n] = 0;
                    }
                }
            }
        }
        /* Extract "value": "..." */
        {
            const char *p = strstr(line, "\"value\"");
            if (p) {
                p = strchr(p + 7, '"');
                if (p) {
                    p++;
                    const char *e = p;
                    while (*e) {
                        if (*e == '\\' && e[1]) { e += 2; continue; }
                        if (*e == '"') break;
                        e++;
                    }
                    if (*e == '"') {
                        size_t n = (size_t)(e - p);
                        if (n >= VS_MEMORY_VALUE_MAX) n = VS_MEMORY_VALUE_MAX - 1;
                        memcpy(value, p, n);
                        value[n] = 0;
                        /* Simple unescape */
                        {
                            char *w = value;
                            const char *r = value;
                            while (*r && w < value + VS_MEMORY_VALUE_MAX - 1) {
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
                            *w = 0;
                        }
                    }
                }
            }
        }
        /* Extract "tag": "..." */
        {
            const char *p = strstr(line, "\"tag\"");
            if (p) {
                p = strchr(p + 5, '"');
                if (p) {
                    p++;
                    const char *e = strchr(p, '"');
                    if (e) {
                        size_t n = (size_t)(e - p);
                        if (n >= VS_MEMORY_TAG_MAX) n = VS_MEMORY_TAG_MAX - 1;
                        memcpy(tag, p, n);
                        tag[n] = 0;
                    }
                }
            }
        }
        /* Extract "type": N */
        {
            const char *p = strstr(line, "\"type\"");
            if (p) {
                p = strchr(p + 6, ':');
                if (p) {
                    while (*p == ' ' || *p == '\t') p++;
                    type = atoi(p);
                }
            }
        }

        if (key[0] && value[0]) {
            vs_memory_save(store, key, value, tag, (VSMemoryType)type);
        }
    }

    fclose(f);
}

void vs_memory_persist(VSMemoryStore *store)
{
    FILE *f;
    int i;
    char dir[VS_MAX_PATH];
    char *slash;

    /* Create directory if needed */
    strncpy(dir, store->store_path, VS_MAX_PATH - 1);
    slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        /* Simple mkdir -p equivalent */
        {
            char cmd[VS_MAX_PATH + 16];
            snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
            system(cmd);
        }
        *slash = '/';
    }

    f = fopen(store->store_path, "w");
    if (!f) return;

    for (i = 0; i < store->entry_count; i++) {
        VSMemoryEntry *e = &store->entries[i];
        char key_esc[VS_MEMORY_KEY_MAX * 2];
        char val_esc[VS_MEMORY_VALUE_MAX * 2];
        char tag_esc[VS_MEMORY_TAG_MAX * 2];

        json_escape_str(e->key, key_esc, sizeof(key_esc));
        json_escape_str(e->value, val_esc, sizeof(val_esc));
        json_escape_str(e->tag, tag_esc, sizeof(tag_esc));

        fprintf(f, "{\"key\":\"%s\",\"value\":\"%s\",\"tag\":\"%s\",\"type\":%d,\"created\":%ld,\"accessed\":%ld,\"access_count\":%d}\n",
                key_esc, val_esc, tag_esc, e->type, e->created_at, e->accessed_at, e->access_count);
    }

    fclose(f);
}

void vs_memory_clear(VSMemoryStore *store)
{
    memset(store->entries, 0, sizeof(store->entries));
    store->entry_count = 0;
}

/* ---- Memory rules directory: load .md files from ~/.vibesolaris/memory/ ---- */

int vs_memory_load_rules_dir(VSMemoryStore *store, const char *dir)
{
    DIR *d;
    struct dirent *de;
    int count = 0;

    if (!store || !dir) return -1;
    d = opendir(dir);
    if (!d) return -1;

    while ((de = readdir(d)) != NULL) {
        char path[VS_MAX_PATH];
        char buf[VS_MEMORY_VALUE_MAX];
        char key[VS_MEMORY_KEY_MAX];
        FILE *f;
        size_t nlen;

        /* Only .md files */
        nlen = strlen(de->d_name);
        if (nlen < 4 || strcmp(de->d_name + nlen - 3, ".md") != 0) continue;

        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);

        /* Check it's a regular file */
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        /* Build key from filename without .md extension */
        size_t klen = nlen - 3;
        if (klen >= VS_MEMORY_KEY_MAX) klen = VS_MEMORY_KEY_MAX - 1;
        memcpy(key, de->d_name, klen);
        key[klen] = 0;

        /* Read file content */
        f = fopen(path, "r");
        if (!f) continue;
        size_t rlen = fread(buf, 1, VS_MEMORY_VALUE_MAX - 1, f);
        buf[rlen] = 0;
        fclose(f);

        /* Trim trailing whitespace */
        while (rlen > 0 && (buf[rlen - 1] == '\n' || buf[rlen - 1] == '\r' || buf[rlen - 1] == ' '))
            rlen--;
        buf[rlen] = 0;

        if (rlen == 0) continue;

        vs_memory_save(store, key, buf, "rules", VS_MEMORY_TYPE_CONTEXT);
        count++;
    }

    closedir(d);
    return count;
}

/* ---- AutoDream: memory consolidation ---- */

static unsigned long fnv1a(const char *s)
{
    unsigned long h = 2166136261UL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619UL;
    }
    return h;
}

static int key_similarity(const char *a, const char *b)
{
    /* Simple similarity: compare FNV-1a hash of first 16 chars */
    char sa[17] = {0}, sb[17] = {0};
    size_t la = strlen(a), lb = strlen(b);
    if (la > 16) la = 16;
    if (lb > 16) lb = 16;
    if (la != lb) return 0;
    memcpy(sa, a, la);
    memcpy(sb, b, lb);
    unsigned long ha = fnv1a(sa);
    unsigned long hb = fnv1a(sb);
    /* If hashes match, keys are very similar */
    if (ha == hb) return 100;
    /* Check character overlap */
    int match = 0, total = (int)la;
    int i;
    for (i = 0; i < total; i++) {
        if (sa[i] == sb[i]) match++;
    }
    return (int)(match * 100 / (total > 0 ? total : 1));
}

static int normalize_date(char *buf, size_t cap)
{
    /* Convert relative date phrases to absolute ISO 8601 dates */
    time_t now = time(NULL);
    struct tm tm_buf;
    if (!localtime_r(&now, &tm_buf)) return 0;
    int changed = 0;
    char today[32], yesterday[32], lastweek[32];
    /* Today */
    tm_buf.tm_mday;
    strftime(today, sizeof(today), "%Y-%m-%d", &tm_buf);
    /* Yesterday */
    tm_buf.tm_mday--;
    strftime(yesterday, sizeof(yesterday), "%Y-%m-%d", &tm_buf);
    /* Last week */
    tm_buf.tm_mday -= 6;
    strftime(lastweek, sizeof(lastweek), "%Y-%m-%d", &tm_buf);

    char *patterns[] = {"yesterday", "last week", "today"};
    char *replacements[] = {yesterday, lastweek, today};
    int i;
    for (i = 0; i < 3; i++) {
        char *p = buf;
        while ((p = strstr(p, patterns[i])) != NULL) {
            /* Replace in place */
            size_t plen = strlen(patterns[i]);
            size_t rlen = strlen(replacements[i]);
            size_t remaining = strlen(p + plen);
            if (rlen + remaining + 1 < cap) {
                memmove(p + rlen, p + plen, remaining + 1);
                memcpy(p, replacements[i], rlen);
                p += rlen;
                changed = 1;
            } else break;
        }
    }
    return changed;
}

VSDreamStats vs_memory_dream(VSMemoryStore *store)
{
    VSDreamStats stats;
    memset(&stats, 0, sizeof(stats));
    if (!store || store->entry_count == 0) return stats;

    stats.total_before = store->entry_count;
    long now = time(NULL);
    long stale_days = 30; /* configurable via VIBESOLARIS_DREAM_STALE_DAYS */
    {
        const char *env = getenv("VIBESOLARIS_DREAM_STALE_DAYS");
        if (env && *env) {
            long v = atol(env);
            if (v > 0 && v <= 365) stale_days = v;
        }
    }
    long stale_cutoff = now - (stale_days * 86400);

    /* 1. Merge duplicates: entries with similar keys (>80% similarity) */
    int i, j;
    int removed[VS_MAX_MEMORY_ENTRIES];
    memset(removed, 0, sizeof(removed));
    for (i = 0; i < store->entry_count; i++) {
        if (removed[i]) continue;
        for (j = i + 1; j < store->entry_count; j++) {
            if (removed[j]) continue;
            int sim = key_similarity(store->entries[i].key, store->entries[j].key);
            if (sim >= 80) {
                /* Merge: keep the newer one, append old value as note */
                VSMemoryEntry *a = &store->entries[i];
                VSMemoryEntry *b = &store->entries[j];
                if (b->created_at > a->created_at) {
                    /* b is newer, append a's value as note */
                    int space = (int)(sizeof(b->value) - strlen(b->value) - 1);
                    if (space > 20) {
                        strncat(b->value, " [prev: ", space);
                        space -= 8;
                        strncat(b->value, a->value, space > 0 ? space : 0);
                        strncat(b->value, "]", space > 0 ? space : 0);
                    }
                    removed[i] = 1;
                } else {
                    /* a is newer */
                    int space = (int)(sizeof(a->value) - strlen(a->value) - 1);
                    if (space > 20) {
                        strncat(a->value, " [prev: ", space);
                        space -= 8;
                        strncat(a->value, b->value, space > 0 ? space : 0);
                        strncat(a->value, "]", space > 0 ? space : 0);
                    }
                    removed[j] = 1;
                }
                stats.merged++;
            }
        }
    }

    /* 2. Prune stale entries: context entries older than stale_days */
    for (i = 0; i < store->entry_count; i++) {
        if (removed[i]) continue;
        VSMemoryEntry *e = &store->entries[i];
        if (e->type == VS_MEMORY_TYPE_CONTEXT && e->created_at < stale_cutoff) {
            removed[i] = 1;
            stats.pruned++;
        }
    }

    /* 3. Normalize dates in values */
    for (i = 0; i < store->entry_count; i++) {
        if (removed[i]) continue;
        if (normalize_date(store->entries[i].value, VS_MEMORY_VALUE_MAX)) {
            stats.normalized++;
        }
    }

    /* 4. Deduplicate tags: merge entries with overlapping tags */
    for (i = 0; i < store->entry_count; i++) {
        if (removed[i]) continue;
        for (j = i + 1; j < store->entry_count; j++) {
            if (removed[j]) continue;
            if (store->entries[i].tag[0] &&
                strcmp(store->entries[i].tag, store->entries[j].tag) == 0 &&
                strcmp(store->entries[i].tag, "") != 0) {
                /* Same tag — merge if values are similar */
                if (strstr(store->entries[i].value, store->entries[j].value) ||
                    strstr(store->entries[j].value, store->entries[i].value)) {
                    /* Keep the one with more access count */
                    if (store->entries[j].access_count > store->entries[i].access_count) {
                        removed[i] = 1;
                    } else {
                        removed[j] = 1;
                    }
                    stats.deduped_tags++;
                }
            }
        }
    }

    /* Apply removals: compact array */
    int write_idx = 0;
    for (i = 0; i < store->entry_count; i++) {
        if (!removed[i]) {
            if (write_idx != i) {
                store->entries[write_idx] = store->entries[i];
            }
            write_idx++;
        }
    }
    while (write_idx < store->entry_count) {
        memset(&store->entries[write_idx], 0, sizeof(VSMemoryEntry));
        write_idx++;
    }
    store->entry_count = write_idx;
    stats.total_after = store->entry_count;

    /* Persist the consolidated memory */
    vs_memory_persist(store);

    return stats;
}

char *vs_memory_dream_stats(const VSDreamStats *stats)
{
    if (!stats) return dupstr("No dream stats");
    char *buf = (char *)malloc(256);
    if (!buf) return NULL;
    int pos = snprintf(buf, 256,
        "Dream results: %d merged, %d pruned, %d normalized, %d tag-deduped (%d -> %d entries)",
        stats->merged, stats->pruned, stats->normalized, stats->deduped_tags,
        stats->total_before, stats->total_after);
    if (pos < 0 || pos >= 255) {
        free(buf);
        return dupstr("Dream stats error");
    }
    return buf;
}
