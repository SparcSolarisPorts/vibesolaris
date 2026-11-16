/* SPDX-License-Identifier: Unlicense */
/* VibeSolaris Plugins — Versioned bundles of skills, commands, hooks, MCP */
/*
 * Mirrors Claude Code's Plugins: install/list/uninstall/update versioned
 * bundles. A plugin is a directory containing plugin.json + skills/ +
 * commands/ + hooks/ + mcp/.
 */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#define VS_PLUGIN_DIR "~/.vibesolaris/plugins"

static char *pl_dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static int pl_ensure_dir(void)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    char cmd[VS_MAX_PATH + 32];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'" VS_PLUGIN_DIR " 2>/dev/null", home);
    return system(cmd);
}

/* Parse plugin.json metadata */
static int pl_parse_metadata(const char *plugin_path, VSPlugin *p)
{
    char path[VS_MAX_PATH + 32];
    snprintf(path, sizeof(path), "%s/plugin.json", plugin_path);
    
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        const char *s;
        if ((s = strstr(line, "\"name\":\""))) {
            s += 8;
            char *end = strchr(s, '"');
            if (end) {
                size_t len = (size_t)(end - s);
                if (len >= sizeof(p->name)) len = sizeof(p->name) - 1;
                memcpy(p->name, s, len);
                p->name[len] = 0;
            }
        }
        if ((s = strstr(line, "\"version\":\""))) {
            s += 11;
            char *end = strchr(s, '"');
            if (end) {
                size_t len = (size_t)(end - s);
                if (len >= sizeof(p->version)) len = sizeof(p->version) - 1;
                memcpy(p->version, s, len);
                p->version[len] = 0;
            }
        }
        if ((s = strstr(line, "\"description\":\""))) {
            s += 15;
            char *end = strchr(s, '"');
            if (end) {
                size_t len = (size_t)(end - s);
                if (len >= sizeof(p->description)) len = sizeof(p->description) - 1;
                memcpy(p->description, s, len);
                p->description[len] = 0;
            }
        }
    }
    fclose(f);
    return (p->name[0] != 0) ? 0 : -1;
}

/* ---- Public API ---- */

int vs_plugin_install(VSPlugin *plugins, int *count, const char *url_or_path)
{
    if (!plugins || !count || !url_or_path) return -1;
    if (*count >= VS_MAX_PLUGINS) return -1;
    
    pl_ensure_dir();
    
    VSPlugin *p = &plugins[*count];
    memset(p, 0, sizeof(*p));
    
    /* Determine if URL or local path */
    char plugin_path[VS_MAX_PATH];
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    
    if (strncmp(url_or_path, "http://", 7) == 0 || strncmp(url_or_path, "https://", 8) == 0) {
        /* Download via curl */
        char cmd[VS_MAX_PATH + 128];
        char tmp_file[VS_MAX_PATH];
        snprintf(tmp_file, sizeof(tmp_file), "/tmp/vs_plugin_%.32s.tar.gz", url_or_path);
        snprintf(cmd, sizeof(cmd), "curl -sL -o '%s' '%s' 2>/dev/null", tmp_file, url_or_path);
        if (system(cmd) != 0) return -1;
        
        /* Extract */
        snprintf(p->path, sizeof(p->path), "%s" VS_PLUGIN_DIR "/%s", home, url_or_path);
        char *last_slash = strrchr(p->path + strlen(home), '/');
        if (last_slash) *last_slash = 0;
        snprintf(cmd, sizeof(cmd), "mkdir -p '%s' && tar xzf '%s' -C '%s' 2>/dev/null",
                 p->path, tmp_file, p->path);
        if (system(cmd) != 0) {
            unlink(tmp_file);
            return -1;
        }
        unlink(tmp_file);
    } else {
        /* Local path: copy */
        snprintf(p->path, sizeof(p->path), "%s" VS_PLUGIN_DIR "/%s", home, url_or_path);
        char *last_slash = strrchr(p->path + strlen(home), '/');
        if (last_slash) *last_slash = 0;
        char cmd[VS_MAX_PATH + 128];
        snprintf(cmd, sizeof(cmd), "cp -r '%s' '%s' 2>/dev/null", url_or_path, p->path);
        if (system(cmd) != 0) return -1;
    }
    
    /* Parse metadata */
    if (pl_parse_metadata(p->path, p) != 0) {
        /* Use path basename as name */
        const char *name = strrchr(url_or_path, '/');
        name = name ? name + 1 : url_or_path;
        snprintf(p->name, sizeof(p->name), "%s", name);
        snprintf(p->version, sizeof(p->version), "1.0.0");
    }
    
    snprintf(p->path, sizeof(p->path), "%s" VS_PLUGIN_DIR "/%s", home, p->name);
    p->installed_at = (long)time(NULL);
    (*count)++;
    return 0;
}

char *vs_plugin_list(VSPlugin *plugins, int count, int max_lines)
{
    if (!plugins) return pl_dupstr("No plugins loaded");
    
    char *buf = (char *)malloc(4096);
    if (!buf) return NULL;
    int pos = 0;
    int lines = 0;
    
    pos += snprintf(buf + pos, 4096 - pos, "Plugins (%d/%d):\n", count, VS_MAX_PLUGINS);
    
    for (int i = 0; i < count && lines < max_lines; i++) {
        char time_str[64];
        time_t t = (time_t)plugins[i].installed_at;
        struct tm tm_buf;
        if (localtime_r(&t, &tm_buf)) {
            strftime(time_str, sizeof(time_str), "%Y-%m-%d", &tm_buf);
        } else {
            snprintf(time_str, sizeof(time_str), "unknown");
        }
        pos += snprintf(buf + pos, 4096 - pos,
                        "  [%s] v%s — %s (installed %s)\n",
                        plugins[i].name, plugins[i].version,
                        plugins[i].description, time_str);
        if (pos >= 4080) break;
        lines++;
    }
    
    if (count == 0) {
        free(buf);
        return pl_dupstr("No plugins installed. Use /plugin install to add one.");
    }
    return buf;
}

int vs_plugin_uninstall(VSPlugin *plugins, int *count, const char *name)
{
    if (!plugins || !count || !name) return -1;
    
    for (int i = 0; i < *count; i++) {
        if (strcmp(plugins[i].name, name) == 0) {
            /* Remove directory */
            char cmd[VS_MAX_PATH + 32];
            snprintf(cmd, sizeof(cmd), "rm -rf '%s' 2>/dev/null", plugins[i].path);
            system(cmd);
            
            /* Shift remaining plugins */
            memmove(&plugins[i], &plugins[i + 1],
                    (sizeof(VSPlugin) * ((*count) - i - 1)));
            (*count)--;
            return 0;
        }
    }
    return -1; /* not found */
}

int vs_plugin_update(VSPlugin *plugins, int *count, const char *name)
{
    /* For now, update = reinstall from same source */
    /* A full implementation would track the original URL */
    (void)plugins; (void)count; (void)name;
    return -1; /* not implemented yet */
}
