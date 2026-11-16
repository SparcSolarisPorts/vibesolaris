/* SPDX-License-Identifier: Unlicense */
/* Task management: cross-session task tracking (src/task.c).
 *
 * Distinct from TodoWrite (per-session), this provides persistent tasks
 * that survive across sessions.  Stored as JSONL at ~/.vibesolaris/tasks.jsonl.
 *
 * Each task has: id, title, status, priority, created_at, updated_at, description. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- Local helpers ---- */
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
            continue;
        } else {
            dst[di++] = (char)c;
        }
    }
    if (di < dst_cap) dst[di] = 0;
    return (int)di;
}

static const char *status_name(int status)
{
    switch (status) {
        case VS_TASK_STATUS_PENDING:   return "pending";
        case VS_TASK_STATUS_IN_PROGRESS: return "in_progress";
        case VS_TASK_STATUS_COMPLETED: return "completed";
        case VS_TASK_STATUS_CANCELLED: return "cancelled";
        default:                       return "unknown";
    }
}

static int status_from_name(const char *name)
{
    if (!name) return VS_TASK_STATUS_PENDING;
    if (!strcmp(name, "pending")) return VS_TASK_STATUS_PENDING;
    if (!strcmp(name, "in_progress")) return VS_TASK_STATUS_IN_PROGRESS;
    if (!strcmp(name, "completed")) return VS_TASK_STATUS_COMPLETED;
    if (!strcmp(name, "cancelled")) return VS_TASK_STATUS_CANCELLED;
    return VS_TASK_STATUS_PENDING;
}

static const char *priority_name(int priority)
{
    switch (priority) {
        case VS_TASK_PRIORITY_LOW:    return "low";
        case VS_TASK_PRIORITY_MEDIUM: return "medium";
        case VS_TASK_PRIORITY_HIGH:   return "high";
        default:                      return "medium";
    }
}

static int priority_from_name(const char *name)
{
    if (!name) return VS_TASK_PRIORITY_MEDIUM;
    if (!strcmp(name, "low")) return VS_TASK_PRIORITY_LOW;
    if (!strcmp(name, "high")) return VS_TASK_PRIORITY_HIGH;
    return VS_TASK_PRIORITY_MEDIUM;
}

/* ---- Public API ---- */

void vs_task_init(VSTaskStore *store, const char *path)
{
    memset(store, 0, sizeof(*store));
    if (path) {
        strncpy(store->store_path, path, VS_MAX_PATH - 1);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(store->store_path, VS_MAX_PATH, "%s/.vibesolaris/tasks.jsonl", home);
        } else {
            strcpy(store->store_path, ".vibesolaris/tasks.jsonl");
        }
    }
    store->next_id = 1;
}

int vs_task_create(VSTaskStore *store, const char *title, const char *description, int priority)
{
    VSTaskEntry *entry;
    long now = (long)time(NULL);

    if (!title || !title[0]) return -1;
    if (!description) description = "";
    if (store->task_count >= VS_MAX_TASK_ENTRIES) return -1;

    entry = &store->tasks[store->task_count];
    entry->id = store->next_id++;
    strncpy(entry->title, title, VS_TASK_TITLE_MAX - 1);
    entry->title[VS_TASK_TITLE_MAX - 1] = 0;
    strncpy(entry->description, description, VS_TASK_DESC_MAX - 1);
    entry->description[VS_TASK_DESC_MAX - 1] = 0;
    entry->status = VS_TASK_STATUS_PENDING;
    entry->priority = priority;
    entry->created_at = now;
    entry->updated_at = now;

    store->task_count++;
    return entry->id;
}

int vs_task_update(VSTaskStore *store, int id, const char *status_str, const char *title, const char *description)
{
    int i;
    VSTaskEntry *entry;
    long now = (long)time(NULL);

    if (id <= 0) return -1;

    for (i = 0; i < store->task_count; i++) {
        if (store->tasks[i].id == id) {
            entry = &store->tasks[i];
            if (status_str) {
                entry->status = status_from_name(status_str);
            }
            if (title && title[0]) {
                strncpy(entry->title, title, VS_TASK_TITLE_MAX - 1);
                entry->title[VS_TASK_TITLE_MAX - 1] = 0;
            }
            if (description) {
                strncpy(entry->description, description, VS_TASK_DESC_MAX - 1);
                entry->description[VS_TASK_DESC_MAX - 1] = 0;
            }
            entry->updated_at = now;
            return 0;
        }
    }
    return -1; /* Not found */
}

int vs_task_delete(VSTaskStore *store, int id)
{
    int i;
    if (id <= 0) return -1;

    for (i = 0; i < store->task_count; i++) {
        if (store->tasks[i].id == id) {
            memmove(&store->tasks[i], &store->tasks[i + 1],
                    (size_t)(store->task_count - i - 1) * sizeof(VSTaskEntry));
            memset(&store->tasks[store->task_count - 1], 0, sizeof(VSTaskEntry));
            store->task_count--;
            return 0;
        }
    }
    return -1;
}

VSTaskEntry *vs_task_get(VSTaskStore *store, int id)
{
    int i;
    if (id <= 0 || !store) return NULL;

    for (i = 0; i < store->task_count; i++) {
        if (store->tasks[i].id == id) {
            return &store->tasks[i];
        }
    }
    return NULL;
}

char *vs_task_list(VSTaskStore *store, int max_lines)
{
    char *buf;
    size_t cap, len;
    int i;

    if (!store) return dupstr("ERROR: no task store");

    if (max_lines <= 0) max_lines = 128;
    cap = (size_t)max_lines * 256;
    buf = (char *)malloc(cap);
    if (!buf) return dupstr("ERROR: out of memory");
    buf[0] = 0;
    len = 0;

    if (store->task_count == 0) {
        snprintf(buf, cap, "No tasks.\n");
        return buf;
    }

    for (i = 0; i < store->task_count && i < max_lines; i++) {
        VSTaskEntry *t = &store->tasks[i];
        char entry[512];
        int n;
        const char *status_sym;

        switch (t->status) {
            case VS_TASK_STATUS_PENDING:     status_sym = "[ ]"; break;
            case VS_TASK_STATUS_IN_PROGRESS: status_sym = "[*]"; break;
            case VS_TASK_STATUS_COMPLETED:   status_sym = "[x]"; break;
            case VS_TASK_STATUS_CANCELLED:   status_sym = "[-]"; break;
            default:                         status_sym = "[?]"; break;
        }

        n = snprintf(entry, sizeof(entry),
                     "  %s #%d %-6s %s\n",
                     status_sym, t->id,
                     priority_name(t->priority),
                     t->title);
        if (n > 0 && (size_t)n < cap - len) {
            memcpy(buf + len, entry, (size_t)n);
            len += (size_t)n;
        }
    }

    return buf;
}

char *vs_task_search(VSTaskStore *store, const char *query, int status_filter, int max_results)
{
    char *buf;
    size_t cap, len;
    int i, count = 0;

    if (!store) return dupstr("ERROR: no task store");
    if (!query) query = "";
    if (max_results <= 0) max_results = 64;

    cap = (size_t)max_results * 256;
    buf = (char *)malloc(cap);
    if (!buf) return dupstr("ERROR: out of memory");
    buf[0] = 0;
    len = 0;

    for (i = 0; i < store->task_count && count < max_results; i++) {
        VSTaskEntry *t = &store->tasks[i];
        int match = 0;

        if (status_filter >= 0 && t->status != status_filter) continue;

        if (query[0]) {
            /* Case-insensitive search in title and description */
            char title_lower[VS_TASK_TITLE_MAX], query_lower[256];
            char desc_lower[VS_TASK_DESC_MAX];
            size_t j;
            size_t qlen = strlen(query);
            if (qlen >= sizeof(query_lower)) qlen = sizeof(query_lower) - 1;
            for (j = 0; j <= qlen; j++) {
                query_lower[j] = (char)((query[j] >= 'A' && query[j] <= 'Z')
                                         ? query[j] - 'A' + 'a' : query[j]);
            }
            size_t tlen = strlen(t->title);
            if (tlen >= sizeof(title_lower)) tlen = sizeof(title_lower) - 1;
            for (j = 0; j <= tlen; j++) {
                title_lower[j] = (char)((t->title[j] >= 'A' && t->title[j] <= 'Z')
                                         ? t->title[j] - 'A' + 'a' : t->title[j]);
            }
            size_t dlen = strlen(t->description);
            if (dlen >= sizeof(desc_lower)) dlen = sizeof(desc_lower) - 1;
            for (j = 0; j <= dlen; j++) {
                desc_lower[j] = (char)((t->description[j] >= 'A' && t->description[j] <= 'Z')
                                        ? t->description[j] - 'A' + 'a' : t->description[j]);
            }
            match = (strstr(title_lower, query_lower) != NULL) ||
                    (strstr(desc_lower, query_lower) != NULL);
        } else {
            match = 1;
        }

        if (!match) continue;

        char entry[512];
        int n;
        const char *status_sym;

        switch (t->status) {
            case VS_TASK_STATUS_PENDING:     status_sym = "[ ]"; break;
            case VS_TASK_STATUS_IN_PROGRESS: status_sym = "[*]"; break;
            case VS_TASK_STATUS_COMPLETED:   status_sym = "[x]"; break;
            case VS_TASK_STATUS_CANCELLED:   status_sym = "[-]"; break;
            default:                         status_sym = "[?]"; break;
        }

        n = snprintf(entry, sizeof(entry),
                     "  %s #%d %-6s %s\n",
                     status_sym, t->id,
                     priority_name(t->priority),
                     t->title);
        if (n > 0 && (size_t)n < cap - len) {
            memcpy(buf + len, entry, (size_t)n);
            len += (size_t)n;
        }
        count++;
    }

    if (count == 0) {
        snprintf(buf, cap, "No matching tasks.\n");
    }

    return buf;
}

static int json_extract_string(const char *json, const char *key, char *out, size_t out_cap)
{
    char search[128];
    const char *p, *start, *end;
    size_t key_len = strlen(key);

    snprintf(search, sizeof(search), "\"%s\"", key);
    p = strstr(json, search);
    if (!p) return 0;
    p += key_len + 2; /* skip "key" */
    while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;
    if (*p != '"') return 0;
    p++; /* skip opening quote */
    start = p;
    end = p;
    while (*end && *end != '"') {
        if (*end == '\\') end++;
        end++;
    }
    size_t len = (size_t)(end - start);
    if (len >= out_cap) len = out_cap - 1;
    memcpy(out, start, len);
    out[len] = 0;
    return 1;
}

void vs_task_load(VSTaskStore *store)
{
    FILE *f;
    char line[2048];
    char key[64], val[VS_TASK_DESC_MAX];

    if (!store) return;
    f = fopen(store->store_path, "r");
    if (!f) return;

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = 0;
        if (!len || line[0] != '{') continue;

        /* Parse JSON fields from the line */
        VSTaskEntry *entry;
        if (store->task_count >= VS_MAX_TASK_ENTRIES) break;
        entry = &store->tasks[store->task_count];
        memset(entry, 0, sizeof(*entry));

        /* Extract fields using simple JSON parsing */
        if (json_extract_string(line, "id", key, sizeof(key))) {
            entry->id = atoi(key);
            if (entry->id >= store->next_id) store->next_id = entry->id + 1;
        }
        if (json_extract_string(line, "title", val, sizeof(val))) {
            strncpy(entry->title, val, sizeof(entry->title) - 1);
        }
        if (json_extract_string(line, "description", val, sizeof(val))) {
            strncpy(entry->description, val, sizeof(entry->description) - 1);
        }
        if (json_extract_string(line, "status", key, sizeof(key))) {
            entry->status = status_from_name(key);
        }
        if (json_extract_string(line, "priority", key, sizeof(key))) {
            entry->priority = priority_from_name(key);
        }
        if (json_extract_string(line, "created_at", key, sizeof(key))) {
            entry->created_at = atol(key);
        }
        if (json_extract_string(line, "updated_at", key, sizeof(key))) {
            entry->updated_at = atol(key);
        }

        store->task_count++;
    }

    fclose(f);
}

int vs_task_persist(VSTaskStore *store)
{
    FILE *f;
    int i;

    if (!store) return -1;
    f = fopen(store->store_path, "w");
    if (!f) return -1;

    for (i = 0; i < store->task_count; i++) {
        VSTaskEntry *t = &store->tasks[i];
        char title_esc[VS_TASK_TITLE_MAX * 2];
        char desc_esc[VS_TASK_DESC_MAX * 2];

        json_escape_str(t->title, title_esc, sizeof(title_esc));
        json_escape_str(t->description, desc_esc, sizeof(desc_esc));

        fprintf(f, "{\"id\":%d,\"title\":\"%s\",\"description\":\"%s\","
                     "\"status\":\"%s\",\"priority\":\"%s\","
                     "\"created_at\":%ld,\"updated_at\":%ld}\n",
                t->id, title_esc, desc_esc,
                status_name(t->status), priority_name(t->priority),
                t->created_at, t->updated_at);
    }

    fclose(f);
    return 0;
}

int vs_task_clear(VSTaskStore *store)
{
    if (!store) return -1;
    store->task_count = 0;
    store->next_id = 1;
    memset(store->tasks, 0, sizeof(store->tasks));
    return 0;
}
