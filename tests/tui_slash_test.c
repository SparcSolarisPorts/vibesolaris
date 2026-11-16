/* SPDX-License-Identifier: Unlicense */
/* Slash-command dropdown model: prefix matching and description lookup that
 * back the TUI composer popup.  Plain C, no terminal I/O — the rendering
 * itself lives in src/tui.c and is exercised manually over SSH. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests = 0;
static int passed = 0;

static void check(int cond, const char *msg)
{
    tests++;
    if (cond) passed++;
    else printf("FAIL: %s\n", msg);
}

static int all_start_with(const char **names, const char **descs, int n, const char *prefix)
{
    int i;
    size_t plen = strlen(prefix);
    for (i = 0; i < n; i++) {
        if (strncmp(names[i], prefix, plen) != 0) return 0;
        if (!descs[i] || !*descs[i]) return 0;
    }
    return 1;
}

int main(void)
{
    const char *names[256], *descs[256];
    int i, n, total = vs_tui_slash_command_count();

    /* Table sanity: non-trivial size, every entry a / command. */
    check(total > 100, "command table has more than 100 entries");
    check(total < 512, "command table is bounded");
    for (i = 0; i < total; i++) {
        const char *nm = vs_tui_slash_command_name(i);
        if (!nm || nm[0] != '/') { check(0, "every entry starts with /"); break; }
    }
    check(vs_tui_slash_command_name(-1) == NULL, "negative index returns NULL");
    check(vs_tui_slash_command_name(total) == NULL, "out-of-range index returns NULL");

    /* Bare "/" lists every command, each with a description. */
    n = vs_tui_slash_match("/", names, descs, 256);
    check(n == total, "bare / matches the whole table");
    check(all_start_with(names, descs, n, "/"), "all matches carry descriptions");

    /* Prefix narrowing. */
    n = vs_tui_slash_match("/pro", names, descs, 256);
    check(n >= 10, "/pro narrows to the provider/protocol family");
    check(all_start_with(names, descs, n, "/pro"), "/pro matches all start with /pro");

    /* Single-candidate filter for space-free prefixes. */
    n = vs_tui_slash_match("/usage", names, descs, 256);
    check(n == 1 && strcmp(names[0], "/usage") == 0, "exact entry matches exactly itself");
    n = vs_tui_slash_match("/custom-commands", names, descs, 256);
    check(n == 1 && strcmp(names[0], "/custom-commands list") == 0,
          "unique family prefix narrows to one entry");

    /* No match. */
    check(vs_tui_slash_match("/zzz-not-a-command", names, descs, 256) == 0,
          "unknown prefix matches nothing");

    /* Argument-taking entries (trailing space) are terminal nodes: once the
     * buffer contains whitespace the dropdown must not appear. */
    check(vs_tui_slash_match("/model ", names, descs, 128) == 0,
          "buffer with space yields no dropdown");
    check(vs_tui_slash_match("/model gpt", names, descs, 128) == 0,
          "argument text yields no dropdown");
    check(vs_tui_slash_match("/cache\tx", names, descs, 128) == 0,
          "tab in buffer yields no dropdown");
    check(vs_tui_slash_match("hello", names, descs, 128) == 0,
          "non-slash buffer yields no dropdown");
    check(vs_tui_slash_match("", names, descs, 128) == 0,
          "empty buffer yields no dropdown");
    check(vs_tui_slash_match(NULL, names, descs, 128) == 0,
          "NULL buffer yields no dropdown");

    /* Cap is respected. */
    n = vs_tui_slash_match("/", names, descs, 5);
    check(n == 5, "cap limits the number of returned matches");

    /* names may be NULL (count-only query used by the editor). */
    check(vs_tui_slash_match("/", NULL, NULL, 256) == total,
          "count-only query works with NULL arrays");

    /* Description lookup: longest-prefix wins. */
    check(strstr(vs_tui_slash_desc("/effort high"), "effort") != NULL,
          "/effort high describes effort");
    check(strcmp(vs_tui_slash_desc("/plan accept"), vs_tui_slash_desc("/plan")) == 0,
          "subcommand inherits family description");
    check(strstr(vs_tui_slash_desc("/cache clear"), "cache") != NULL,
          "/cache clear describes cache");
    check(strstr(vs_tui_slash_desc("/rewind mark x"), "rewind") != NULL,
          "rewind subcommand described");
    check(vs_tui_slash_desc("/totally-unknown") && vs_tui_slash_desc("/totally-unknown")[0] == 0,
          "unknown command has empty description");
    check(vs_tui_slash_desc(NULL)[0] == 0, "NULL command has empty description");

    /* Every table entry must resolve to a non-empty description. */
    for (i = 0; i < total; i++) {
        const char *d = vs_tui_slash_desc(vs_tui_slash_command_name(i));
        if (!d || !*d) { check(0, "every table entry has a description"); break; }
    }
    check(1, "description sweep completed");

    printf("Result: %d/%d tests passed\n", passed, tests);
    return passed == tests ? 0 : 1;
}
