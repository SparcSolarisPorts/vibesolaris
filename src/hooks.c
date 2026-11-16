/* SPDX-License-Identifier: Unlicense */
/* Hooks system: PreToolUse/PostToolUse hooks (src/hooks.c).
 *
 * Mirrors Claude Code's hook system.  Hooks are shell commands triggered
 * before/after tool execution.  Config stored at ~/.vibesolaris/hooks.json.
 *
 * Matcher: simple pattern matching on tool name (e.g., "Edit|Write" matches
 * either tool). */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/* Check if a tool name matches a pipe-separated pattern list like "Edit|Write" */
static int hook_matches(const char *matcher, const char *tool_name)
{
    if (!matcher || !tool_name) return 0;

    char match_buf[256];
    strncpy(match_buf, matcher, sizeof(match_buf) - 1);
    match_buf[sizeof(match_buf) - 1] = 0;

    char *tok = match_buf;
    while (*tok) {
        char *sep = strchr(tok, '|');
        if (sep) *sep = 0;
        if (!strcmp(tok, tool_name)) return 1;
        /* Wildcard match */
        if (!strcmp(tok, "*")) return 1;
        tok = sep ? sep + 1 : tok + strlen(tok);
        if (*tok == 0) break;
    }
    return 0;
}

/* ---- Public API ---- */

void vs_hooks_init(VSHooksStore *store, const char *path)
{
    memset(store, 0, sizeof(*store));
    if (path) {
        strncpy(store->config_path, path, VS_MAX_PATH - 1);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(store->config_path, VS_MAX_PATH, "%s/.vibesolaris/hooks.json", home);
        } else {
            strcpy(store->config_path, ".vibesolaris/hooks.json");
        }
    }
}

int vs_hooks_add(VSHooksStore *store, const char *matcher, const char *command, int pre)
{
    VSHook *hook;

    if (!matcher || !matcher[0] || !command || !command[0]) return -1;
    if (store->hook_count >= VS_MAX_HOOKS) return -1;

    hook = &store->hooks[store->hook_count];
    strncpy(hook->matcher, matcher, sizeof(hook->matcher) - 1);
    hook->matcher[sizeof(hook->matcher) - 1] = 0;
    strncpy(hook->command, command, sizeof(hook->command) - 1);
    hook->command[sizeof(hook->command) - 1] = 0;
    hook->is_pre = pre;
    hook->added_at = (long)time(NULL);

    store->hook_count++;
    return 0;
}

int vs_hooks_remove(VSHooksStore *store, int index)
{
    if (index < 0 || index >= store->hook_count) return -1;
    memmove(&store->hooks[index], &store->hooks[index + 1],
            (size_t)(store->hook_count - index - 1) * sizeof(VSHook));
    memset(&store->hooks[store->hook_count - 1], 0, sizeof(VSHook));
    store->hook_count--;
    return 0;
}

/* Run all pre-hooks matching a tool name. Returns 0 on success. */
int vs_hooks_run_pre(VSHooksStore *store, const char *tool_name)
{
    int i;

    if (!store || !tool_name) return 0;

    for (i = 0; i < store->hook_count; i++) {
        VSHook *h = &store->hooks[i];
        if (!h->is_pre) continue;
        if (!hook_matches(h->matcher, tool_name)) continue;

        /* Run the hook command */
        char full_cmd[1024];
        snprintf(full_cmd, sizeof(full_cmd), "%s \"%s\"", h->command, tool_name);
        int rc = system(full_cmd);
        (void)rc; /* Ignore exit code for hooks */
    }
    return 0;
}

/* Run all post-hooks matching a tool name. Returns 0 on success. */
int vs_hooks_run_post(VSHooksStore *store, const char *tool_name, const char *result)
{
    int i;

    if (!store || !tool_name) return 0;

    for (i = 0; i < store->hook_count; i++) {
        VSHook *h = &store->hooks[i];
        if (h->is_pre) continue;
        if (!hook_matches(h->matcher, tool_name)) continue;

        /* Run the hook command with result as argument */
        char full_cmd[1024];
        const char *result_preview = result ? result : "";
        size_t rlen = strlen(result_preview);
        if (rlen > 100) rlen = 100;
        snprintf(full_cmd, sizeof(full_cmd), "%s \"%s\" \"%.*s\"", h->command, tool_name, (int)rlen, result_preview);
        int rc = system(full_cmd);
        (void)rc;
    }
    return 0;
}

char *vs_hooks_list(VSHooksStore *store, int max_lines)
{
    char *buf;
    size_t cap, len;
    int i;

    if (!store) return dupstr("ERROR: no hooks store");
    if (max_lines <= 0) max_lines = 64;

    cap = (size_t)max_lines * 256;
    buf = (char *)malloc(cap);
    if (!buf) return dupstr("ERROR: out of memory");
    buf[0] = 0;
    len = 0;

    if (store->hook_count == 0) {
        snprintf(buf, cap, "No hooks configured.\n");
        return buf;
    }

    for (i = 0; i < store->hook_count && i < max_lines; i++) {
        VSHook *h = &store->hooks[i];
        char entry[256];
        int n;
        n = snprintf(entry, sizeof(entry), "  %s %s -> %s\n",
                     h->is_pre ? "PreToolUse" : "PostToolUse",
                     h->matcher, h->command);
        if (n > 0 && (size_t)n < cap - len) {
            memcpy(buf + len, entry, (size_t)n);
            len += (size_t)n;
        }
    }

    return buf;
}

void vs_hooks_load(VSHooksStore *store)
{
    FILE *f;
    char line[2048];

    if (!store) return;
    f = fopen(store->config_path, "r");
    if (!f) return;

    /* Simple JSON array parsing: look for "matcher", "command", "pre" fields */
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = 0;
        if (!len) continue;

        char matcher[256], command[512];
        int pre = -1;
        matcher[0] = command[0] = 0;

        if (json_extract_string(line, "matcher", matcher, sizeof(matcher)) &&
            json_extract_string(line, "command", command, sizeof(command))) {
            /* Check for "pre" field */
            char pre_str[16];
            if (json_extract_string(line, "type", pre_str, sizeof(pre_str))) {
                pre = (!strcmp(pre_str, "pre") || !strcmp(pre_str, "PreToolUse"));
            } else if (json_extract_string(line, "pre", pre_str, sizeof(pre_str))) {
                pre = (!strcmp(pre_str, "true"));
            } else {
                pre = 0; /* Default to post */
            }
            vs_hooks_add(store, matcher, command, pre);
        }
    }

    fclose(f);
}

int vs_hooks_persist(VSHooksStore *store)
{
    FILE *f;
    int i;

    if (!store) return -1;
    f = fopen(store->config_path, "w");
    if (!f) return -1;

    fprintf(f, "[\n");
    for (i = 0; i < store->hook_count; i++) {
        VSHook *h = &store->hooks[i];
        char match_esc[512], cmd_esc[1024];
        json_escape_str(h->matcher, match_esc, sizeof(match_esc));
        json_escape_str(h->command, cmd_esc, sizeof(cmd_esc));
        fprintf(f, "  {\"matcher\":\"%s\",\"command\":\"%s\",\"type\":\"%s\"}%s\n",
                match_esc, cmd_esc,
                h->is_pre ? "PreToolUse" : "PostToolUse",
                i < store->hook_count - 1 ? "," : "");
    }
    fprintf(f, "]\n");

    fclose(f);
    return 0;
}

int vs_hooks_clear(VSHooksStore *store)
{
    if (!store) return -1;
    store->hook_count = 0;
    memset(store->hooks, 0, sizeof(store->hooks));
    return 0;
}

/* Run MessageDisplay hooks — triggered as streaming tokens arrive */
int vs_hooks_run_message_display(VSHooksStore *store, const char *text)
{
    int i;

    if (!store || !text) return 0;

    for (i = 0; i < store->hook_count; i++) {
        VSHook *h = &store->hooks[i];
        if (h->hook_type != VS_HOOK_MESSAGE_DISPLAY) continue;

        char full_cmd[1024];
        size_t tlen = strlen(text);
        if (tlen > 200) tlen = 200;
        snprintf(full_cmd, sizeof(full_cmd), "%s \"%.*s\"", h->command, (int)tlen, text);
        int rc = system(full_cmd);
        (void)rc;
    }
    return 0;
}

/* Run prompt hooks — modify prompt text before sending to provider */
int vs_hooks_run_prompt(VSHooksStore *store, const char *prompt, char *modified, size_t cap)
{
    int i;

    if (!store || !prompt) return 0;

    /* Start with original prompt */
    if (modified && cap > 0) {
        strncpy(modified, prompt, cap - 1);
        modified[cap - 1] = 0;
    }

    for (i = 0; i < store->hook_count; i++) {
        VSHook *h = &store->hooks[i];
        if (h->hook_type != VS_HOOK_PROMPT) continue;

        /* Run hook, capture output to modify prompt */
        char full_cmd[1024];
        snprintf(full_cmd, sizeof(full_cmd), "%s \"%s\"", h->command, prompt);

        FILE *fp = popen(full_cmd, "r");
        if (fp && modified && cap > 0) {
            char hook_output[VS_MAX_TEXT];
            size_t olen = 0;
            char line[512];
            while (fgets(line, sizeof(line), fp) && olen < cap - 1) {
                size_t ll = strlen(line);
                if (olen + ll >= cap - 1) ll = cap - 1 - olen;
                memcpy(modified + olen, line, ll);
                olen += ll;
            }
            pclose(fp);
            modified[olen] = 0;
        } else if (fp) {
            pclose(fp);
        }
    }
    return 0;
}

/* Send HTTP webhook notification */
int vs_hooks_send_webhook(VSHooksStore *store, const char *event, const char *payload)
{
    int i;

    if (!store || !event) return -1;

    for (i = 0; i < store->hook_count; i++) {
        VSHook *h = &store->hooks[i];
        if (h->hook_type != VS_HOOK_HTTP) continue;
        if (!h->webhook_url[0]) continue;

        /* Build JSON payload */
        char json[2048];
        char esc_event[256], esc_payload[1536];
        json_escape_str(event, esc_event, sizeof(esc_event));
        json_escape_str(payload ? payload : "", esc_payload, sizeof(esc_payload));
        snprintf(json, sizeof(json),
                 "{\"event\":\"%s\",\"payload\":\"%s\",\"timestamp\":%ld}",
                 esc_event, esc_payload, (long)time(NULL));

        /* Use curl to POST */
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
                 "curl -s -X POST -H 'Content-Type: application/json' -d '%s' '%s' 2>/dev/null",
                 json, h->webhook_url);
        int rc = system(cmd);
        (void)rc;
    }
    return 0;
}
