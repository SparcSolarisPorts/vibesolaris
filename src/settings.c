/* SPDX-License-Identifier: Unlicense */
/* Multi-layer settings system (src/settings.c).
 *
 * Three-layer settings matching Claude Code's .claude/ hierarchy:
 *   User layer:    ~/.claude/settings.json
 *   Project layer: .claude/settings.json (project root)
 *   Local layer:   .claude/settings.local.json
 *
 * Merge priority: local > project > user.
 * Settings are simple key=value pairs stored as JSON objects. */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- JSON helpers ---- */

/* Minimal JSON string parser: extract "key": "value" pairs from a JSON object.
 * Returns number of entries parsed. */
static int parse_json_object(const char *text, VSSettingsEntry *entries, int max_entries, int layer)
{
    int count = 0;
    const char *p = text;

    if (!text || !entries || max_entries <= 0) return 0;

    while (*p && count < max_entries) {
        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '}') break;

        /* Find key */
        if (*p != '"') { p++; continue; }
        p++; /* skip opening quote */
        const char *key_start = p;
        const char *key_end = NULL;
        while (*p && *p != '"' && *p != '\\') p++;
        if (*p == '"') {
            key_end = p;
            p++; /* skip closing quote */
        } else {
            /* Escaped string - find end */
            while (*p && (*p != '"' || *(p-1) == '\\')) p++;
            if (*p == '"') {
                key_end = p;
                p++;
            } else break;
        }
        if (!key_end || (size_t)(key_end - key_start) >= VS_SETTINGS_KEY_MAX - 1) continue;

        /* Skip colon and whitespace */
        while (*p && (*p == ':' || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;

        /* Find value */
        const char *val_start = NULL;
        const char *val_end = NULL;
        if (*p == '"') {
            p++; /* skip opening quote */
            val_start = p;
            while (*p && *p != '"') {
                if (*p == '\\') p++; /* skip escaped char */
                if (*p) p++;
            }
            if (*p == '"') {
                val_end = p;
                p++;
            } else {
                val_start = NULL;
            }
        } else if (*p == 't' || *p == 'f') {
            /* true/false */
            val_start = p;
            while (*p && *p != ',' && *p != '}' && !isspace((unsigned char)*p)) p++;
            val_end = p;
        } else if (*p == '-' || isdigit((unsigned char)*p)) {
            /* number */
            val_start = p;
            while (*p && *p != ',' && *p != '}' && !isspace((unsigned char)*p)) p++;
            val_end = p;
        } else if (*p == 'n') {
            /* null */
            val_start = p;
            p += 4;
            val_end = p;
        }

        if (key_start && key_end && val_start && val_end) {
            size_t klen = (size_t)(key_end - key_start);
            size_t vlen = (size_t)(val_end - val_start);
            if (klen >= VS_SETTINGS_KEY_MAX) klen = VS_SETTINGS_KEY_MAX - 1;
            if (vlen >= VS_SETTINGS_VALUE_MAX) vlen = VS_SETTINGS_VALUE_MAX - 1;
            memcpy(entries[count].key, key_start, klen);
            entries[count].key[klen] = 0;
            memcpy(entries[count].value, val_start, vlen);
            entries[count].value[vlen] = 0;
            entries[count].layer = layer;
            count++;
        }

        /* Skip comma */
        while (*p && (*p == ',' || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }
    return count;
}

/* Build a simple JSON object from settings entries */
static char *build_json_object(const VSSettingsEntry *entries, int count)
{
    size_t cap = 1024;
    size_t len = 0;
    char *buf = (char*)malloc(cap);
    if (!buf) return NULL;
    buf[0] = 0;

    /* Calculate needed size */
    size_t needed = 2; /* {} */
    int i;
    for (i = 0; i < count; i++) {
        needed += 2 + strlen(entries[i].key) + 2 + 2 + strlen(entries[i].value) + 2;
    }
    if (needed > cap) {
        free(buf);
        buf = (char*)malloc(needed + 1);
        if (!buf) return NULL;
        cap = needed + 1;
        len = 0;
        buf[0] = 0;
    }

    buf[len++] = '{';
    for (i = 0; i < count; i++) {
        if (i > 0) buf[len++] = ',';
        buf[len++] = '"';
        const char *k = entries[i].key;
        while (*k && len < cap - 1) {
            if (*k == '"') { buf[len++] = '\\'; buf[len++] = '"'; }
            else if (*k == '\\') { buf[len++] = '\\'; buf[len++] = '\\'; }
            else buf[len++] = *k;
            k++;
        }
        buf[len++] = '"';
        buf[len++] = ':';
        buf[len++] = '"';
        const char *v = entries[i].value;
        while (*v && len < cap - 1) {
            if (*v == '"') { buf[len++] = '\\'; buf[len++] = '"'; }
            else if (*v == '\\') { buf[len++] = '\\'; buf[len++] = '\\'; }
            else buf[len++] = *v;
            v++;
        }
        buf[len++] = '"';
    }
    buf[len++] = '}';
    buf[len] = 0;
    return buf;
}

/* ---- Project root detection ---- */

int vs_settings_detect_project_root(const char *start_path, char *out, size_t cap)
{
    if (!start_path || !out || cap < 2) return -1;

    char cwd[VS_MAX_PATH];
    if (!getcwd(cwd, sizeof(cwd))) return -1;

    /* Try git root first */
    char cmd[VS_MAX_PATH + 64];
    snprintf(cmd, sizeof(cmd), "cd '%s' && git rev-parse --show-toplevel 2>/dev/null", start_path);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char root[VS_MAX_PATH];
        if (fgets(root, sizeof(root), fp)) {
            /* Trim newline */
            char *nl = strchr(root, '\n');
            if (nl) *nl = 0;
            char *cr = strchr(root, '\r');
            if (cr) *cr = 0;
            if (root[0]) {
                strncpy(out, root, cap - 1);
                out[cap - 1] = 0;
                pclose(fp);
                return 0;
            }
        }
        pclose(fp);
    }

    /* Fallback: look for .claude/ directory */
    snprintf(cmd, sizeof(cmd), "test -d '%s/.claude' && echo '%s'", start_path, start_path);
    fp = popen(cmd, "r");
    if (fp) {
        char found[VS_MAX_PATH];
        if (fgets(found, sizeof(found), fp)) {
            char *nl = strchr(found, '\n');
            if (nl) *nl = 0;
            if (found[0]) {
                strncpy(out, found, cap - 1);
                out[cap - 1] = 0;
                pclose(fp);
                return 0;
            }
        }
        pclose(fp);
    }

    /* No project root found */
    out[0] = 0;
    return -1;
}

/* ---- Settings store operations ---- */

void vs_settings_init(VSSettingsStore *store, const char *project_root)
{
    if (!store) return;
    memset(store, 0, sizeof(*store));

    /* Set paths */
    const char *home = getenv("HOME");
    if (home) {
        snprintf(store->user_path, sizeof(store->user_path),
                 "%s/.claude/settings.json", home);
    } else {
        strncpy(store->user_path, ".claude/settings.json", sizeof(store->user_path) - 1);
    }

    if (project_root && project_root[0]) {
        strncpy(store->project_root, project_root, sizeof(store->project_root) - 1);
        snprintf(store->project_path, sizeof(store->project_path),
                 "%s/.claude/settings.json", project_root);
        snprintf(store->local_path, sizeof(store->local_path),
                 "%s/.claude/settings.local.json", project_root);
    } else {
        store->project_root[0] = 0;
        strncpy(store->project_path, ".claude/settings.json", sizeof(store->project_path) - 1);
        strncpy(store->local_path, ".claude/settings.local.json", sizeof(store->local_path) - 1);
    }
}

static int load_layer(VSSettingsStore *store, const char *path, int layer, int *loaded_flag)
{
    if (!store || !path) return -1;

    char *content = vs_read_file(path);
    if (!content) {
        if (loaded_flag) *loaded_flag = 0;
        return -1;
    }

    int start = store->entry_count;
    int parsed = parse_json_object(content, store->entries + start,
                                   VS_SETTINGS_MAX_KEYS - start, layer);
    if (parsed > 0) {
        store->entry_count += parsed;
        if (loaded_flag) *loaded_flag = 1;
    }
    free(content);
    return parsed;
}

int vs_settings_load_user(VSSettingsStore *store)
{
    return load_layer(store, store->user_path, VS_SETTINGS_LAYER_USER, &store->user_loaded);
}

int vs_settings_load_project(VSSettingsStore *store)
{
    return load_layer(store, store->project_path, VS_SETTINGS_LAYER_PROJECT, &store->project_loaded);
}

int vs_settings_load_local(VSSettingsStore *store)
{
    return load_layer(store, store->local_path, VS_SETTINGS_LAYER_LOCAL, &store->local_loaded);
}

int vs_settings_load_all(VSSettingsStore *store)
{
    if (!store) return -1;
    /* Clear any existing entries */
    store->entry_count = 0;
    store->user_loaded = 0;
    store->project_loaded = 0;
    store->local_loaded = 0;

    /* Load in priority order: user first (lowest), then project, then local (highest) */
    int total = 0;
    int rc;

    rc = vs_settings_load_user(store);
    if (rc > 0) total += rc;

    rc = vs_settings_load_project(store);
    if (rc > 0) total += rc;

    rc = vs_settings_load_local(store);
    if (rc > 0) total += rc;

    return total;
}

/* Get a setting value, respecting merge priority (local > project > user) */
const char *vs_settings_get(VSSettingsStore *store, const char *key)
{
    if (!store || !key) return NULL;

    /* Search in reverse order: local (highest priority) first */
    int i;
    for (i = store->entry_count - 1; i >= 0; i--) {
        if (!strcmp(store->entries[i].key, key)) {
            return store->entries[i].value;
        }
    }
    return NULL;
}

/* Set a setting value at a specific layer */
int vs_settings_set(VSSettingsStore *store, const char *key, const char *value, VSSettingsLayer layer)
{
    if (!store || !key || !value) return -1;

    /* Check if key exists at this layer and update */
    int i;
    for (i = 0; i < store->entry_count; i++) {
        if (!strcmp(store->entries[i].key, key) && store->entries[i].layer == layer) {
            strncpy(store->entries[i].value, value, VS_SETTINGS_VALUE_MAX - 1);
            store->entries[i].value[VS_SETTINGS_VALUE_MAX - 1] = 0;
            return 0;
        }
    }

    /* Add new entry */
    if (store->entry_count >= VS_SETTINGS_MAX_KEYS) return -1;

    VSSettingsEntry *entry = &store->entries[store->entry_count];
    strncpy(entry->key, key, VS_SETTINGS_KEY_MAX - 1);
    entry->key[VS_SETTINGS_KEY_MAX - 1] = 0;
    strncpy(entry->value, value, VS_SETTINGS_VALUE_MAX - 1);
    entry->value[VS_SETTINGS_VALUE_MAX - 1] = 0;
    entry->layer = layer;
    store->entry_count++;
    return 0;
}

/* Save local settings to disk */
int vs_settings_save_local(VSSettingsStore *store)
{
    if (!store) return -1;

    /* Collect local-layer entries */
    VSSettingsEntry local_entries[VS_SETTINGS_MAX_KEYS];
    int local_count = 0;
    int i;
    for (i = 0; i < store->entry_count; i++) {
        if (store->entries[i].layer == VS_SETTINGS_LAYER_LOCAL) {
            local_entries[local_count++] = store->entries[i];
        }
    }

    char *json = build_json_object(local_entries, local_count);
    if (!json) return -1;

    /* Ensure directory exists */
    char dir_cmd[VS_MAX_PATH + 64];
    char *dir = strrchr(store->local_path, '/');
    if (dir) {
        char dir_path[VS_MAX_PATH];
        size_t dir_len = (size_t)(dir - store->local_path);
        if (dir_len >= sizeof(dir_path)) dir_len = sizeof(dir_path) - 1;
        memcpy(dir_path, store->local_path, dir_len);
        dir_path[dir_len] = 0;
        snprintf(dir_cmd, sizeof(dir_cmd), "mkdir -p '%s'", dir_path);
        system(dir_cmd);
    }

    FILE *f = fopen(store->local_path, "w");
    if (!f) {
        free(json);
        return -1;
    }
    fprintf(f, "%s\n", json);
    fclose(f);
    free(json);
    return 0;
}

/* Dump all settings as a human-readable string */
char *vs_settings_dump(VSSettingsStore *store)
{
    if (!store) return NULL;

    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char*)malloc(cap);
    if (!buf) return NULL;
    buf[0] = 0;

    const char *layer_names[] = {"user", "project", "local"};

    int i;
    for (i = 0; i < store->entry_count; i++) {
        const char *ln = (store->entries[i].layer >= 0 && store->entries[i].layer < 3) ?
                         layer_names[store->entries[i].layer] : "unknown";
        size_t need = strlen(buf) + strlen(store->entries[i].key) + strlen(store->entries[i].value)
                      + strlen(ln) + 32;
        if (len + need + 1 > cap) {
            size_t nc = cap * 2;
            while (len + need + 1 > nc) nc *= 2;
            char *nb = (char*)realloc(buf, nc);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
            cap = nc;
        }
        len += snprintf(buf + len, cap - len, "%-20s = %-40s [%s]\n",
                        store->entries[i].key, store->entries[i].value, ln);
    }

    if (len == 0) {
        len += snprintf(buf + len, cap - len, "(no settings)\n");
    }

    /* Add layer status */
    len += snprintf(buf + len, cap - len, "\nLayers:\n");
    len += snprintf(buf + len, cap - len, "  User:    %s %s\n", store->user_path,
                    store->user_loaded ? "(loaded)" : "(not found)");
    len += snprintf(buf + len, cap - len, "  Project: %s %s\n", store->project_path,
                    store->project_loaded ? "(loaded)" : "(not found)");
    len += snprintf(buf + len, cap - len, "  Local:   %s %s\n", store->local_path,
                    store->local_loaded ? "(loaded)" : "(not found)");

    return buf;
}
