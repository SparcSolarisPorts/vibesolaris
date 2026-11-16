/* SPDX-License-Identifier: Unlicense */
/* TUI Theme system tests */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int tests = 0;
static int passed = 0;

static void check(int cond, const char *msg)
{
    tests++;
    if (cond) passed++;
    else printf("FAIL: %s\n", msg);
}

static void setup_temp_theme(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

int main(void)
{
    VSTUITheme theme;

    /* Test 1: Init with name */
    vs_tui_theme_init(&theme, "test");
    check(strcmp(theme.name, "test") == 0, "init with name");
    check(theme.is_dark == 1, "init defaults to dark");

    /* Test 2: Init with NULL name */
    vs_tui_theme_init(&theme, NULL);
    check(strcmp(theme.name, "default") == 0, "init with NULL name");

    /* Test 3: Load dark default */
    vs_tui_theme_load_default(&theme, 1);
    check(strcmp(theme.name, "dark") == 0, "dark theme name");
    check(theme.is_dark == 1, "dark theme is_dark");
    check(strcmp(vs_tui_theme_color(&theme, 4), "36m") == 0, "dark prompt color");

    /* Test 4: Load light default */
    vs_tui_theme_load_default(&theme, 0);
    check(strcmp(theme.name, "light") == 0, "light theme name");
    check(theme.is_dark == 0, "light theme is_dark");
    check(strcmp(vs_tui_theme_color(&theme, 1), "30m") == 0, "light fg color");

    /* Test 5: Color access bounds */
    vs_tui_theme_load_default(&theme, 1);
    check(strcmp(vs_tui_theme_color(&theme, -1), "0m") == 0, "negative color index");
    check(strcmp(vs_tui_theme_color(&theme, 16), "0m") == 0, "out of bounds color index");
    check(strcmp(vs_tui_theme_color(NULL, 0), "0m") == 0, "NULL theme color");

    /* Test 6: Empty color returns default */
    vs_tui_theme_init(&theme, "empty");
    check(strcmp(vs_tui_theme_color(&theme, 0), "0m") == 0, "empty color returns 0m");

    /* Test 7: Save and load theme file */
    const char *tmpfile = "/tmp/vs_test_theme.txt";
    vs_tui_theme_load_default(&theme, 1);
    check(vs_tui_theme_save_file(&theme, tmpfile) == 0, "save theme file");

    struct stat st;
    check(stat(tmpfile, &st) == 0, "theme file exists");
    check(st.st_size > 0, "theme file has content");

    /* Test 8: Load saved theme */
    VSTUITheme loaded;
    check(vs_tui_theme_load_file(&loaded, tmpfile) == 0, "load theme file");
    check(strcmp(loaded.name, "dark") == 0, "loaded theme name");
    check(loaded.is_dark == 1, "loaded theme is_dark");

    /* Test 9: Load non-existent file */
    check(vs_tui_theme_load_file(&theme, "/nonexistent/theme.txt") == -1, "load non-existent file");

    /* Test 10: Load custom theme file */
    setup_temp_theme(tmpfile,
        "# Custom theme\n"
        "name = custom\n"
        "dark = 0\n"
        "prompt = 34m\n"
        "error = 31m\n"
        "success = 32m\n"
    );
    check(vs_tui_theme_load_file(&theme, tmpfile) == 0, "load custom theme");
    check(strcmp(theme.name, "custom") == 0, "custom theme name");
    check(theme.is_dark == 0, "custom theme is light");
    check(strcmp(vs_tui_theme_color(&theme, 4), "34m") == 0, "custom prompt color");
    check(strcmp(vs_tui_theme_color(&theme, 8), "31m") == 0, "custom error color");
    check(strcmp(vs_tui_theme_color(&theme, 10), "32m") == 0, "custom success color");

    /* Test 11: Load theme with color_N syntax */
    setup_temp_theme(tmpfile,
        "name = indexed\n"
        "dark = 1\n"
        "color_0 = 40m\n"
        "color_1 = 37m\n"
        "color_7 = 33m\n"
    );
    check(vs_tui_theme_load_file(&theme, tmpfile) == 0, "load indexed theme");
    check(strcmp(theme.name, "indexed") == 0, "indexed theme name");
    check(strcmp(vs_tui_theme_color(&theme, 0), "40m") == 0, "indexed bg color");
    check(strcmp(vs_tui_theme_color(&theme, 1), "37m") == 0, "indexed fg color");
    check(strcmp(vs_tui_theme_color(&theme, 7), "33m") == 0, "indexed highlight color");

    /* Test 12: Theme apply (no-op) */
    vs_tui_theme_load_default(&theme, 1);
    vs_tui_theme_apply(&theme); /* should not crash */
    check(1, "apply does not crash");

    /* Test 13: Save to invalid path */
    check(vs_tui_theme_save_file(&theme, "/nonexistent/dir/theme.txt") == -1, "save to invalid path");

    /* Test 14: All color names */
    setup_temp_theme(tmpfile,
        "name = all_colors\n"
        "dark = 1\n"
        "bg = 40m\n"
        "fg = 37m\n"
        "status_bg = 44m\n"
        "status_fg = 37m\n"
        "prompt = 36m\n"
        "sidebar_bg = 40m\n"
        "sidebar_fg = 36m\n"
        "highlight = 33m\n"
        "error = 31m\n"
        "warning = 33m\n"
        "success = 32m\n"
        "dim = 2m\n"
        "link = 34m\n"
        "comment = 2m\n"
        "keyword = 35m\n"
        "string = 32m\n"
    );
    check(vs_tui_theme_load_file(&theme, tmpfile) == 0, "load all color names");
    check(strcmp(vs_tui_theme_color(&theme, 0), "40m") == 0, "bg color");
    check(strcmp(vs_tui_theme_color(&theme, 2), "44m") == 0, "status_bg color");
    check(strcmp(vs_tui_theme_color(&theme, 5), "40m") == 0, "sidebar_bg color");
    check(strcmp(vs_tui_theme_color(&theme, 12), "34m") == 0, "link color");
    check(strcmp(vs_tui_theme_color(&theme, 14), "35m") == 0, "keyword color");
    check(strcmp(vs_tui_theme_color(&theme, 15), "32m") == 0, "string color");

    /* Test 15: Dark=true parsing */
    setup_temp_theme(tmpfile,
        "name = bool_test\n"
        "dark = true\n"
        "prompt = 36m\n"
    );
    check(vs_tui_theme_load_file(&theme, tmpfile) == 0, "load bool dark theme");
    check(theme.is_dark == 1, "dark=true parsed");

    /* Test 16: Dark=0 parsing */
    setup_temp_theme(tmpfile,
        "name = bool_test2\n"
        "dark = 0\n"
        "prompt = 34m\n"
    );
    check(vs_tui_theme_load_file(&theme, tmpfile) == 0, "load bool light theme");
    check(theme.is_dark == 0, "dark=0 parsed");

    /* Cleanup */
    unlink(tmpfile);

    printf("Result: %d/%d tests passed\n", passed, tests);
    return passed == tests ? 0 : 1;
}
