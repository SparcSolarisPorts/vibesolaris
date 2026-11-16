/* SPDX-License-Identifier: Unlicense */
/* Permission rules: tool-level allow/deny system (src/permission.c).
 *
 * Mirrors Claude Code's allowedTools/deny configuration.  Rules are
 * evaluated against a tool name and optional path/args to decide whether
 * a tool call should be blocked before execution.
 *
 * Rules support simple pattern matching:
 *   - Exact tool name: "Bash"
 *   - Tool with path pattern: "Write(production.*)"
 *   - Wildcard: "*" (all tools) */
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

/* Pattern match supporting * (any chars) — plain chars match literally.
 * Unlike simple regex, '.' is literal.  A pattern without '*' must match
 * the entire string; a pattern with '*' can match a prefix. */
static int simple_match(const char *pattern, const char *str)
{
    const char *p = pattern;
    const char *s = str;

    while (*p) {
        if (*p == '*') {
            /* * matches any sequence of characters */
            p++;
            if (*p == 0) return 1; /* trailing * matches everything */
            while (*s) {
                if (simple_match(p, s)) return 1;
                s++;
            }
            return simple_match(p, "");
        } else {
            if (*p != *s) return 0;
            p++; s++;
        }
    }
    /* If pattern has no *, require exact match.
     * If pattern ended with *, already returned above. */
    return *s == 0;
}

/* Parse a rule like "Write(production.*)" into tool="Write" and path_pattern="production.*" */
static void parse_rule(const char *rule, char *tool_out, size_t tool_cap, char *path_out, size_t path_cap)
{
    const char *paren = strchr(rule, '(');
    if (paren) {
        size_t len = (size_t)(paren - rule);
        if (len >= tool_cap) len = tool_cap - 1;
        memcpy(tool_out, rule, len);
        tool_out[len] = 0;
        const char *close = strrchr(paren, ')');
        if (close) {
            len = (size_t)(close - (paren + 1));
            if (len >= path_cap) len = path_cap - 1;
            memcpy(path_out, paren + 1, len);
            path_out[len] = 0;
        } else {
            path_out[0] = 0;
        }
    } else {
        strncpy(tool_out, rule, tool_cap - 1);
        tool_out[tool_cap - 1] = 0;
        path_out[0] = 0;
    }
}

/* ---- Public API ---- */

void vs_permission_init(VSPermissionStore *store)
{
    memset(store, 0, sizeof(*store));
}

int vs_permission_add_rule(VSPermissionStore *store, const char *rule, int allow)
{
    VSPermissionRule *r;
    char tool[64], path_pat[256];

    if (!rule || !rule[0]) return -1;
    if (store->rule_count >= VS_MAX_PERMISSION_RULES) return -1;

    parse_rule(rule, tool, sizeof(tool), path_pat, sizeof(path_pat));

    r = &store->rules[store->rule_count];
    strncpy(r->tool_pattern, tool, sizeof(r->tool_pattern) - 1);
    r->tool_pattern[sizeof(r->tool_pattern) - 1] = 0;
    strncpy(r->path_pattern, path_pat, sizeof(r->path_pattern) - 1);
    r->path_pattern[sizeof(r->path_pattern) - 1] = 0;
    r->allow = allow;
    r->added_at = (long)time(NULL);

    store->rule_count++;
    return 0;
}

int vs_permission_remove_rule(VSPermissionStore *store, int index)
{
    if (index < 0 || index >= store->rule_count) return -1;
    memmove(&store->rules[index], &store->rules[index + 1],
            (size_t)(store->rule_count - index - 1) * sizeof(VSPermissionRule));
    memset(&store->rules[store->rule_count - 1], 0, sizeof(VSPermissionRule));
    store->rule_count--;
    return 0;
}

/* Check whether a tool call is allowed.
 * Returns 1 if allowed, 0 if denied, -1 if no matching rule (default allow). */
int vs_permission_check(VSPermissionStore *store, const char *tool, const char *path)
{
    int i;
    int matched = 0;
    int last_allow = 1; /* default: allow */

    if (!store || !tool) return -1;

    for (i = 0; i < store->rule_count; i++) {
        VSPermissionRule *r = &store->rules[i];
        int tool_match = 0;

        /* Check tool pattern */
        if (r->tool_pattern[0] == '*') {
            tool_match = 1;
        } else if (simple_match(r->tool_pattern, tool)) {
            tool_match = 1;
        }

        if (!tool_match) continue;

        /* If rule has a path pattern, check it */
        if (r->path_pattern[0]) {
            if (!path) continue;
            /* Match with wildcard pattern, or fall back to prefix match */
            if (strchr(r->path_pattern, '*')) {
                if (!simple_match(r->path_pattern, path)) continue;
            } else {
                /* Plain pattern: match as prefix */
                size_t plen = strlen(r->path_pattern);
                if (strncmp(path, r->path_pattern, plen) != 0) continue;
            }
        }

        /* Rule matched */
        matched = 1;
        last_allow = r->allow;
    }

    if (!matched) return -1; /* No rule matched, default allow */
    return last_allow;
}

int vs_permission_clear(VSPermissionStore *store)
{
    vs_permission_init(store);
    return 0;
}

char *vs_permission_list(VSPermissionStore *store, int max_lines)
{
    char *buf;
    size_t cap, len;
    int i;

    if (!store) return dupstr("ERROR: no permission store");

    if (max_lines <= 0) max_lines = 64;
    cap = (size_t)max_lines * 128;
    buf = (char *)malloc(cap);
    if (!buf) return dupstr("ERROR: out of memory");
    buf[0] = 0;
    len = 0;

    if (store->rule_count == 0) {
        snprintf(buf, cap, "No permission rules configured.\n");
        return buf;
    }

    for (i = 0; i < store->rule_count && i < max_lines; i++) {
        VSPermissionRule *r = &store->rules[i];
        char entry[256];
        int n;
        n = snprintf(entry, sizeof(entry), "  %s %s%s\n",
                     r->allow ? "ALLOW" : "DENY",
                     r->tool_pattern,
                     r->path_pattern[0] ? ("(" ) : "");
        if (n > 0 && (size_t)n < cap - len) {
            memcpy(buf + len, entry, (size_t)n);
            len += (size_t)n;
        }
        if (r->path_pattern[0]) {
            n = snprintf(entry, sizeof(entry), "%s)\n", r->path_pattern);
            if (n > 0 && (size_t)n < cap - len) {
                memcpy(buf + len, entry, (size_t)n);
                len += (size_t)n;
            }
        }
    }

    return buf;
}

void vs_permission_load(VSPermissionStore *store, const char *path)
{
    FILE *f;
    char line[512];

    if (!path || !store) return;
    f = fopen(path, "r");
    if (!f) return;

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = 0;
        if (!len || line[0] == '#') continue;

        /* Format: allow|deny tool_pattern(path_pattern) */
        char *space = strchr(line, ' ');
        if (!space) continue;
        *space = 0;
        int allow = (strcmp(line, "allow") == 0);
        const char *rule = space + 1;
        vs_permission_add_rule(store, rule, allow);
    }

    fclose(f);
}

int vs_permission_persist(VSPermissionStore *store, const char *path)
{
    FILE *f;
    int i;

    if (!store || !path) return -1;
    f = fopen(path, "w");
    if (!f) return -1;

    for (i = 0; i < store->rule_count; i++) {
        VSPermissionRule *r = &store->rules[i];
        const char *action = r->allow ? "allow" : "deny";
        if (r->path_pattern[0]) {
            fprintf(f, "%s %s(%s)\n", action, r->tool_pattern, r->path_pattern);
        } else {
            fprintf(f, "%s %s\n", action, r->tool_pattern);
        }
    }

    fclose(f);
    return 0;
}
