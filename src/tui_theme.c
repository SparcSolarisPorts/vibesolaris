/* SPDX-License-Identifier: Unlicense */
/* TUI Theme system: configurable color themes for full-screen mode */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Default dark theme (terminal-friendly) */
static const char *dark_theme_colors[VS_TUI_THEME_MAX_COLORS] = {
    "0m",        /* 0=bg: default */
    "37m",       /* 1=fg: white */
    "40m",       /* 2=status_bg: black bg */
    "37m",       /* 3=status_fg: white */
    "36m",       /* 4=prompt: cyan */
    "40m",       /* 5=sidebar_bg: black bg */
    "36m",       /* 6=sidebar_fg: cyan */
    "33m",       /* 7=highlight: yellow */
    "31m",       /* 8=error: red */
    "33m",       /* 9=warning: yellow */
    "32m",       /* 10=success: green */
    "2m",        /* 11=dim: dim */
    "34m",       /* 12=link: blue */
    "2m",        /* 13=comment: dim */
    "35m",       /* 14=keyword: magenta */
    "32m",       /* 15=string: green */
};

/* Default light theme */
static const char *light_theme_colors[VS_TUI_THEME_MAX_COLORS] = {
    "0m",        /* 0=bg: default */
    "30m",       /* 1=fg: black */
    "47m",       /* 2=status_bg: white bg */
    "30m",       /* 3=status_fg: black */
    "34m",       /* 4=prompt: blue */
    "47m",       /* 5=sidebar_bg: white bg */
    "34m",       /* 6=sidebar_fg: blue */
    "33m",       /* 7=highlight: yellow */
    "31m",       /* 8=error: red */
    "33m",       /* 9=warning: yellow */
    "32m",       /* 10=success: green */
    "2m",        /* 11=dim: dim */
    "34m",       /* 12=link: blue */
    "2m",        /* 13=comment: dim */
    "35m",       /* 14=keyword: magenta */
    "32m",       /* 15=string: green */
};

void vs_tui_theme_init(VSTUITheme *theme, const char *name)
{
    memset(theme, 0, sizeof(*theme));
    if (name) {
        strncpy(theme->name, name, VS_TUI_THEME_NAME_MAX - 1);
        theme->name[VS_TUI_THEME_NAME_MAX - 1] = 0;
    } else {
        strcpy(theme->name, "default");
    }
    theme->is_dark = 1;
}

void vs_tui_theme_load_default(VSTUITheme *theme, int dark)
{
    const char **colors = dark ? dark_theme_colors : light_theme_colors;
    vs_tui_theme_init(theme, dark ? "dark" : "light");
    theme->is_dark = dark;
    for (int i = 0; i < VS_TUI_THEME_MAX_COLORS; i++) {
        strncpy(theme->colors[i], colors[i], 7);
        theme->colors[i][7] = 0;
    }
}

int vs_tui_theme_load_file(VSTUITheme *theme, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    int i = 0;
    vs_tui_theme_init(theme, "custom");

    while (fgets(line, sizeof(line), f) && i < VS_TUI_THEME_MAX_COLORS) {
        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        /* Parse: name = color_code */
        char key[64] = {0};
        char val[64] = {0};
        char *eq = strchr(line, '=');
        if (!eq) continue;

        size_t klen = eq - line;
        if (klen >= sizeof(key)) klen = sizeof(key) - 1;
        strncpy(key, line, klen);
        key[klen] = 0;
        /* Trim whitespace */
        char *kp = key;
        while (*kp == ' ') kp++;
        char *end = kp + strlen(kp) - 1;
        while (end > kp && (*end == ' ' || *end == '\n' || *end == '\r')) *end-- = 0;
        /* Shift trimmed key to start */
        size_t shift = kp - key;
        memmove(key, kp, strlen(kp) + 1);

        char *v = eq + 1;
        while (*v == ' ') v++;
        size_t vlen = strlen(v);
        if (vlen >= sizeof(val)) vlen = sizeof(val) - 1;
        strncpy(val, v, vlen);
        val[vlen] = 0;
        end = val + strlen(val) - 1;
        while (end > val && (*end == ' ' || *end == '\n' || *end == '\r')) *end-- = 0;

        /* Check if this is a metadata key */
        if (strcmp(key, "name") == 0) {
            strncpy(theme->name, val, VS_TUI_THEME_NAME_MAX - 1);
            continue;
        } else if (strcmp(key, "dark") == 0) {
            theme->is_dark = (strcmp(val, "1") == 0 || strcmp(val, "true") == 0);
            continue;
        }

        /* Parse color index from key (e.g., "color_0", "bg", "fg") */
        int color_idx = -1;
        if (sscanf(key, "color_%d", &color_idx) == 1) {
            /* Direct index */
        } else if (strcmp(key, "bg") == 0) color_idx = 0;
        else if (strcmp(key, "fg") == 0) color_idx = 1;
        else if (strcmp(key, "status_bg") == 0) color_idx = 2;
        else if (strcmp(key, "status_fg") == 0) color_idx = 3;
        else if (strcmp(key, "prompt") == 0) color_idx = 4;
        else if (strcmp(key, "sidebar_bg") == 0) color_idx = 5;
        else if (strcmp(key, "sidebar_fg") == 0) color_idx = 6;
        else if (strcmp(key, "highlight") == 0) color_idx = 7;
        else if (strcmp(key, "error") == 0) color_idx = 8;
        else if (strcmp(key, "warning") == 0) color_idx = 9;
        else if (strcmp(key, "success") == 0) color_idx = 10;
        else if (strcmp(key, "dim") == 0) color_idx = 11;
        else if (strcmp(key, "link") == 0) color_idx = 12;
        else if (strcmp(key, "comment") == 0) color_idx = 13;
        else if (strcmp(key, "keyword") == 0) color_idx = 14;
        else if (strcmp(key, "string") == 0) color_idx = 15;

        if (color_idx >= 0 && color_idx < VS_TUI_THEME_MAX_COLORS) {
            strncpy(theme->colors[color_idx], val, 7);
            theme->colors[color_idx][7] = 0;
            i++;
        }
    }
    fclose(f);
    return i > 0 ? 0 : -1;
}

int vs_tui_theme_save_file(const VSTUITheme *theme, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "# VibeSolaris TUI Theme\n");
    fprintf(f, "name = %s\n", theme->name);
    fprintf(f, "dark = %d\n", theme->is_dark);
    fprintf(f, "\n");

    const char *names[VS_TUI_THEME_MAX_COLORS] = {
        "bg", "fg", "status_bg", "status_fg", "prompt", "sidebar_bg",
        "sidebar_fg", "highlight", "error", "warning", "success", "dim",
        "link", "comment", "keyword", "string"
    };

    for (int i = 0; i < VS_TUI_THEME_MAX_COLORS; i++) {
        if (theme->colors[i][0]) {
            fprintf(f, "%s = %s\n", names[i], theme->colors[i]);
        }
    }
    fclose(f);
    return 0;
}

const char *vs_tui_theme_color(const VSTUITheme *theme, int color_index)
{
    if (!theme || color_index < 0 || color_index >= VS_TUI_THEME_MAX_COLORS) return "0m";
    if (theme->colors[color_index][0] == 0) return "0m";
    return theme->colors[color_index];
}

void vs_tui_theme_apply(const VSTUITheme *theme)
{
    /* Apply theme by setting terminal colors via ANSI sequences
       This is a no-op for now — the theme is used by the compositor
       when rendering. In the future, could set OSC 4 for terminal
       palette changes. */
    (void)theme;
}
