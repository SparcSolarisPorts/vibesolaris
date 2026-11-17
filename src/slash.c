/* SPDX-License-Identifier: Unlicense */
/* Slash commands: custom slash command registration and execution (src/slash.c).
 *
 * Mirrors Claude Code's /skill, /fix, etc.  Commands are stored as JSONL
 * at ~/.vibesolaris/slashes.jsonl and can be registered via the tool or TUI. */
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
        } else if (c < 0x20) {
            continue;
        } else {
            dst[di++] = (char)c;
        }
    }
    if (di < dst_cap) dst[di] = 0;
    return (int)di;
}

static int json_extract_string(const char *json, const char *key, char *out, size_t out_cap)
{
    char search[128];
    const char *p, *start, *end;
    size_t key_len = strlen(key);

    snprintf(search, sizeof(search), "\"%s\"", key);
    p = strstr(json, search);
    if (!p) return 0;
    p += key_len + 2;
    while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;
    if (*p != '"') return 0;
    p++;
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

/* ---- Public API ---- */

void vs_slash_init(VSSlashStore *store, const char *path)
{
    memset(store, 0, sizeof(*store));
    if (path) {
        strncpy(store->store_path, path, VS_MAX_SLASH_PATH - 1);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(store->store_path, VS_MAX_SLASH_PATH, "%s/.vibesolaris/slashes.jsonl", home);
        } else {
            strcpy(store->store_path, ".vibesolaris/slashes.jsonl");
        }
    }
}

int vs_slash_register(VSSlashStore *store, const char *name, const char *command, const char *description)
{
    VSSlashCommand *cmd;
    int i;

    if (!name || !name[0] || !command || !command[0]) return -1;
    if (store->command_count >= VS_MAX_SLASH_COMMANDS) return -1;

    /* Check for duplicate */
    for (i = 0; i < store->command_count; i++) {
        if (!strcmp(store->commands[i].name, name)) {
            /* Update existing */
            strncpy(store->commands[i].command, command, sizeof(store->commands[i].command) - 1);
            store->commands[i].command[sizeof(store->commands[i].command) - 1] = 0;
            if (description) {
                strncpy(store->commands[i].description, description, sizeof(store->commands[i].description) - 1);
                store->commands[i].description[sizeof(store->commands[i].description) - 1] = 0;
            }
            return 0;
        }
    }

    cmd = &store->commands[store->command_count];
    strncpy(cmd->name, name, sizeof(cmd->name) - 1);
    cmd->name[sizeof(cmd->name) - 1] = 0;
    strncpy(cmd->command, command, sizeof(cmd->command) - 1);
    cmd->command[sizeof(cmd->command) - 1] = 0;
    if (description) {
        strncpy(cmd->description, description, sizeof(cmd->description) - 1);
        cmd->description[sizeof(cmd->description) - 1] = 0;
    }
    cmd->added_at = (long)time(NULL);

    store->command_count++;
    return 0;
}

int vs_slash_remove(VSSlashStore *store, const char *name)
{
    int i;
    if (!name || !name[0]) return -1;

    for (i = 0; i < store->command_count; i++) {
        if (!strcmp(store->commands[i].name, name)) {
            memmove(&store->commands[i], &store->commands[i + 1],
                    (size_t)(store->command_count - i - 1) * sizeof(VSSlashCommand));
            memset(&store->commands[store->command_count - 1], 0, sizeof(VSSlashCommand));
            store->command_count--;
            return 0;
        }
    }
    return -1;
}

VSSlashCommand *vs_slash_find(VSSlashStore *store, const char *name)
{
    int i;
    if (!name || !name[0] || !store) return NULL;

    for (i = 0; i < store->command_count; i++) {
        if (!strcmp(store->commands[i].name, name)) {
            return &store->commands[i];
        }
    }
    return NULL;
}

char *vs_slash_execute(VSSlashStore *store, const char *name, const char *args)
{
    /* Execute a registered slash command: look the template up in the store,
     * substitute $ARGS (or append the args), and run it via popen. */
    VSSlashCommand *cmd;
    char *full_cmd, *sub = NULL, *result;
    const char *placeholder;
    FILE *f;
    char buf[4096];
    int len;

    if (!store || !name || !name[0]) return dupstr("ERROR: no slash command name");
    cmd = vs_slash_find(store, name);
    if (!cmd) { size_t z = strlen(name) + 64; char *e = (char *)malloc(z); if (e) snprintf(e, z, "ERROR: unknown slash command \"%s\" (use /slash list)", name); return e ? e : dupstr("ERROR: unknown slash command"); }

    /* Build the command line with args substitution.  The template runs
     * through the shell as-is (it is user-authored configuration, same trust
     * model as hooks); only the substituted args are quoted. */
    placeholder = strstr(cmd->command, "$ARGS");
    if (placeholder) {
        char *qargs = vs_shell_quote(args && *args ? args : "");
        size_t head, tail, z;
        if (!qargs) return dupstr("ERROR: out of memory");
        head = (size_t)(placeholder - cmd->command);
        tail = strlen(placeholder + 5);
        z = head + strlen(qargs) + tail + 1;
        sub = (char *)malloc(z);
        if (!sub) { free(qargs); return dupstr("ERROR: out of memory"); }
        memcpy(sub, cmd->command, head);
        memcpy(sub + head, qargs, strlen(qargs));
        memcpy(sub + head + strlen(qargs), placeholder + 5, tail + 1);
        free(qargs);
    } else if (args && *args) {
        char *qargs = vs_shell_quote(args);
        size_t z;
        if (!qargs) return dupstr("ERROR: out of memory");
        z = strlen(cmd->command) + strlen(qargs) + 2;
        sub = (char *)malloc(z);
        if (!sub) { free(qargs); return dupstr("ERROR: out of memory"); }
        snprintf(sub, z, "%s %s", cmd->command, qargs);
        free(qargs);
    } else {
        sub = dupstr(cmd->command);
        if (!sub) return dupstr("ERROR: out of memory");
    }
    full_cmd = sub;

    f = popen(full_cmd, "r");
    free(full_cmd);
    if (!f) return dupstr("ERROR: failed to execute slash command");
    result = (char *)malloc(4096);
    if (!result) {
        pclose(f);
        return dupstr("ERROR: out of memory");
    }
    result[0] = 0;
    len = 0;
    while (fgets(buf, sizeof(buf), f) && len < 4092) {
        int blen = (int)strlen(buf);
        if (len + blen >= 4092) blen = 4092 - len;
        memcpy(result + len, buf, (size_t)blen);
        len += blen;
    }
    result[len] = 0;
    pclose(f);
    return result;
}

char *vs_slash_list(VSSlashStore *store, int max_lines)
{
    char *buf;
    size_t cap, len;
    int i;

    if (!store) return dupstr("ERROR: no slash store");
    if (max_lines <= 0) max_lines = 64;

    cap = (size_t)max_lines * 256;
    buf = (char *)malloc(cap);
    if (!buf) return dupstr("ERROR: out of memory");
    buf[0] = 0;
    len = 0;

    if (store->command_count == 0) {
        snprintf(buf, cap, "No slash commands registered.\n");
        return buf;
    }

    for (i = 0; i < store->command_count && i < max_lines; i++) {
        VSSlashCommand *c = &store->commands[i];
        char entry[256];
        int n;
        n = snprintf(entry, sizeof(entry), "  /%s - %s\n", c->name, c->description[0] ? c->description : c->command);
        if (n > 0 && (size_t)n < cap - len) {
            memcpy(buf + len, entry, (size_t)n);
            len += (size_t)n;
        }
    }

    return buf;
}

void vs_slash_load(VSSlashStore *store)
{
    FILE *f;
    char line[2048];

    if (!store) return;
    f = fopen(store->store_path, "r");
    if (!f) return;

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = 0;
        if (!len || line[0] != '{') continue;

        char name[64], command[512], description[256];
        name[0] = command[0] = description[0] = 0;

        if (json_extract_string(line, "name", name, sizeof(name)) &&
            json_extract_string(line, "command", command, sizeof(command))) {
            json_extract_string(line, "description", description, sizeof(description));
            vs_slash_register(store, name, command, description[0] ? description : NULL);
        }
    }

    fclose(f);
}

int vs_slash_persist(VSSlashStore *store)
{
    FILE *f;
    int i;

    if (!store) return -1;
    f = fopen(store->store_path, "w");
    if (!f) return -1;

    for (i = 0; i < store->command_count; i++) {
        VSSlashCommand *c = &store->commands[i];
        char name_esc[128], cmd_esc[1024], desc_esc[512];
        json_escape_str(c->name, name_esc, sizeof(name_esc));
        json_escape_str(c->command, cmd_esc, sizeof(cmd_esc));
        json_escape_str(c->description, desc_esc, sizeof(desc_esc));
        fprintf(f, "{\"name\":\"%s\",\"command\":\"%s\",\"description\":\"%s\"}\n",
                name_esc, cmd_esc, desc_esc);
    }

    fclose(f);
    return 0;
}

int vs_slash_clear(VSSlashStore *store)
{
    if (!store) return -1;
    store->command_count = 0;
    memset(store->commands, 0, sizeof(store->commands));
    return 0;
}
