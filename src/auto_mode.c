/* SPDX-License-Identifier: Unlicense */
/* VibeSolaris Auto Mode — Action classifier for safe/risky/blocked operations */
/*
 * Mirrors Claude Code's Auto Mode: classifies each tool call as safe, risky,
 * or blocked. Safe actions proceed silently, risky actions are logged,
 * blocked actions are denied.
 *
 * Rules are loaded from ~/.vibesolaris/auto_mode.json with defaults.
 */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>


/* ---- Internal helpers ---- */

static char *am_dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static int am_home_dir(char *out, size_t cap)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(out, cap, "%s/.vibesolaris", home);
    return 0;
}

/* Check if a string matches a pattern (supports * wildcard) */
static int am_pattern_match(const char *pattern, const char *str)
{
    if (!pattern || !str) return 0;
    if (strcmp(pattern, "*") == 0) return 1;
    if (strcmp(pattern, str) == 0) return 1;
    
    /* Prefix match: pattern "rm " matches "rm -rf /" */
    size_t plen = strlen(pattern);
    if (plen == 0) return str[0] == 0;
    if (pattern[plen - 1] == ' ' || pattern[plen - 1] == '*') {
        /* Remove trailing wildcard/space for prefix comparison */
        size_t prefix_len = plen;
        while (prefix_len > 0 && (pattern[prefix_len - 1] == '*' || pattern[prefix_len - 1] == ' '))
            prefix_len--;
        if (prefix_len > 0 && strncmp(pattern, str, prefix_len) == 0)
            return 1;
    }
    return 0;
}

/* Default rules built into the system */
static const char *default_safe_tools[] = {
    "read", "list", "search", "graph", "web_search", "web_fetch",
    "todo", "glob", "grep", "notebook_read", "tool_search",
    "task_list", "task_search", "task_get", "skill", "memory",
    "checkpoint", "lsp", NULL
};

static const char *default_risky_tools[] = {
    "write", "edit", "multi_edit", "notebook_edit", "monitor",
    "subagent", "kill_shell", NULL
};

static const char *default_blocked_patterns[] = {
    "sudo ", "apt-get ", "yum ", "pip install ", "npm install -g ",
    "rm -rf /", "mkfs.", "dd if=", NULL
};

/* ---- Public API ---- */

void vs_auto_mode_init(VSAutoModeStore *store)
{
    if (!store) return;
    memset(store, 0, sizeof(*store));
    store->enabled = 0;
    
    char dir[VS_MAX_PATH];
    am_home_dir(dir, sizeof(dir));
    snprintf(store->config_path, sizeof(store->config_path), "%s/auto_mode.json", dir);
    
    /* Add default safe tool rules */
    for (int i = 0; default_safe_tools[i] && store->rule_count < VS_MAX_AUTO_MODE_RULES; i++) {
        VSAutoModeRule *r = &store->rules[store->rule_count];
        snprintf(r->tool, sizeof(r->tool), "%s", default_safe_tools[i]);
        snprintf(r->pattern, sizeof(r->pattern), "*");
        r->classification = VS_AUTO_MODE_SAFE;
        store->rule_count++;
    }
    
    /* Add default risky tool rules */
    for (int i = 0; default_risky_tools[i] && store->rule_count < VS_MAX_AUTO_MODE_RULES; i++) {
        VSAutoModeRule *r = &store->rules[store->rule_count];
        snprintf(r->tool, sizeof(r->tool), "%s", default_risky_tools[i]);
        snprintf(r->pattern, sizeof(r->pattern), "*");
        r->classification = VS_AUTO_MODE_RISKY;
        store->rule_count++;
    }
    
    /* Add default blocked patterns for run (Bash) tool */
    for (int i = 0; default_blocked_patterns[i] && store->rule_count < VS_MAX_AUTO_MODE_RULES; i++) {
        VSAutoModeRule *r = &store->rules[store->rule_count];
        snprintf(r->tool, sizeof(r->tool), "run");
        snprintf(r->pattern, sizeof(r->pattern), "%s", default_blocked_patterns[i]);
        r->classification = VS_AUTO_MODE_BLOCKED;
        store->rule_count++;
    }
}

int vs_auto_mode_classify(VSAutoModeStore *store, const char *tool, const char *args)
{
    if (!store || !tool) return VS_AUTO_MODE_SAFE;
    
    /* Check custom rules first (they override defaults) */
    for (int i = store->rule_count - 1; i >= 0; i--) {
        VSAutoModeRule *r = &store->rules[i];
        if (strcmp(r->tool, tool) == 0 || strcmp(r->tool, "*") == 0) {
            if (am_pattern_match(r->pattern, args ? args : "")) {
                return r->classification;
            }
        }
    }
    
    /* Default: "run" (Bash) is risky, everything else is safe */
    if (strcmp(tool, "run") == 0) return VS_AUTO_MODE_RISKY;
    return VS_AUTO_MODE_SAFE;
}

int vs_auto_mode_should_block(VSAutoModeStore *store, int classification)
{
    if (!store) return 0;
    return (store->enabled && classification == VS_AUTO_MODE_BLOCKED);
}

char *vs_auto_mode_list_rules(VSAutoModeStore *store, int max_lines)
{
    if (!store) return am_dupstr("Auto Mode not initialized");
    
    char *buf = (char *)malloc(4096);
    if (!buf) return NULL;
    int pos = 0;
    int lines = 0;
    
    pos += snprintf(buf + pos, 4096 - pos,
                    "Auto Mode rules (%d rules, enabled: %s):\n",
                    store->rule_count, store->enabled ? "yes" : "no");
    
    for (int i = 0; i < store->rule_count && lines < max_lines; i++) {
        VSAutoModeRule *r = &store->rules[i];
        const char *cls = "safe";
        if (r->classification == VS_AUTO_MODE_RISKY) cls = "risky";
        if (r->classification == VS_AUTO_MODE_BLOCKED) cls = "blocked";
        pos += snprintf(buf + pos, 4096 - pos, "  [%s] %s %s\n", cls, r->tool, r->pattern);
        if (pos >= 4080) break;
        lines++;
    }
    return buf;
}

void vs_auto_mode_load(VSAutoModeStore *store)
{
    if (!store) return;
    
    FILE *f = fopen(store->config_path, "r");
    if (!f) return;
    
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* Parse simple JSON: {"tool":"...","pattern":"...","classification":"blocked"} */
        const char *s;
        char tool[64] = "", pattern[256] = "", cls_str[16] = "";
        
        if ((s = strstr(line, "\"tool\":\""))) {
            s += 8;
            char *end = strchr(s, '"');
            if (end) {
                size_t len = (size_t)(end - s);
                if (len >= sizeof(tool)) len = sizeof(tool) - 1;
                memcpy(tool, s, len);
                tool[len] = 0;
            }
        }
        if ((s = strstr(line, "\"pattern\":\""))) {
            s += 11;
            char *end = strchr(s, '"');
            if (end) {
                size_t len = (size_t)(end - s);
                if (len >= sizeof(pattern)) len = sizeof(pattern) - 1;
                memcpy(pattern, s, len);
                pattern[len] = 0;
            }
        }
        if ((s = strstr(line, "\"classification\":\""))) {
            s += 18;
            char *end = strchr(s, '"');
            if (end) {
                size_t len = (size_t)(end - s);
                if (len >= sizeof(cls_str)) len = sizeof(cls_str) - 1;
                memcpy(cls_str, s, len);
                cls_str[len] = 0;
            }
        }
        
        if (tool[0] && store->rule_count < VS_MAX_AUTO_MODE_RULES) {
            int cls = VS_AUTO_MODE_SAFE;
            if (strcmp(cls_str, "risky") == 0) cls = VS_AUTO_MODE_RISKY;
            if (strcmp(cls_str, "blocked") == 0) cls = VS_AUTO_MODE_BLOCKED;
            
            VSAutoModeRule *r = &store->rules[store->rule_count];
            snprintf(r->tool, sizeof(r->tool), "%s", tool);
            snprintf(r->pattern, sizeof(r->pattern), "%s", pattern);
            r->classification = cls;
            store->rule_count++;
        }
    }
    fclose(f);
}

int vs_auto_mode_persist(VSAutoModeStore *store)
{
    if (!store) return -1;
    
    char dir[VS_MAX_PATH];
    am_home_dir(dir, sizeof(dir));
    system("mkdir -p ~/.vibesolaris 2>/dev/null");
    
    FILE *f = fopen(store->config_path, "w");
    if (!f) return -1;
    
    for (int i = 0; i < store->rule_count; i++) {
        VSAutoModeRule *r = &store->rules[i];
        const char *cls = "safe";
        if (r->classification == VS_AUTO_MODE_RISKY) cls = "risky";
        if (r->classification == VS_AUTO_MODE_BLOCKED) cls = "blocked";
        fprintf(f, "{\"tool\":\"%s\",\"pattern\":\"%s\",\"classification\":\"%s\"}\n",
                r->tool, r->pattern, cls);
    }
    fclose(f);
    return 0;
}
