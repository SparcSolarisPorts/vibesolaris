/* SPDX-License-Identifier: Unlicense */
/* TUI Suggestions engine tests */
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

int main(void)
{
    VSTUISuggestion suggestions[VS_TUI_SUGGESTION_MAX];
    VSTUISuggestion results[VS_TUI_SUGGESTION_MAX];
    int count = 0;

    /* Test 1: Init */
    vs_tui_suggestions_init(suggestions, &count);
    check(count == 0, "init clears count");

    /* Test 2: Add suggestions */
    count = 0;
    check(vs_tui_suggestions_add(suggestions, &count, "/help", 0) == 0, "add /help");
    check(count == 1, "count is 1");
    check(vs_tui_suggestions_add(suggestions, &count, "/quit", 0) == 0, "add /quit");
    check(vs_tui_suggestions_add(suggestions, &count, "/provider", 0) == 0, "add /provider");
    check(vs_tui_suggestions_add(suggestions, &count, "/model", 0) == 0, "add /model");
    check(vs_tui_suggestions_add(suggestions, &count, "main.c", 1) == 0, "add file");
    check(vs_tui_suggestions_add(suggestions, &count, "src/util.c", 2) == 0, "add path");
    check(count == 6, "count is 6");

    /* Test 3: Exact prefix match */
    int n = vs_tui_suggestions_match(suggestions, count, "/he", results, 10);
    check(n >= 1, "prefix /he matches");
    check(strcmp(results[0].text, "/help") == 0, "/he matches /help");
    check(results[0].score > 0, "match has positive score");

    /* Test 4: Fuzzy match */
    n = vs_tui_suggestions_match(suggestions, count, "/pr", results, 10);
    check(n >= 1, "prefix /pr matches");
    check(strcmp(results[0].text, "/provider") == 0, "/pr matches /provider");

    /* Test 5: No match */
    n = vs_tui_suggestions_match(suggestions, count, "/zzz", results, 10);
    check(n == 0, "no match for /zzz");

    /* Test 6: Case insensitive */
    n = vs_tui_suggestions_match(suggestions, count, "/HELP", results, 10);
    check(n >= 1, "case insensitive match");

    /* Test 7: Multiple matches sorted by score */
    n = vs_tui_suggestions_match(suggestions, count, "/", results, 10);
    check(n >= 4, "slash matches multiple commands");
    /* First result should have highest score */
    check(results[0].score >= results[1].score, "results sorted by score");

    /* Test 8: Max results limit */
    n = vs_tui_suggestions_match(suggestions, count, "/", results, 2);
    check(n <= 2, "max results limit enforced");

    /* Test 9: Fuzzy match scoring */
    int score = vs_tui_suggestions_fuzzy_match("help", "/help");
    check(score > 0, "fuzzy match help in /help");

    score = vs_tui_suggestions_fuzzy_match("xyz", "/help");
    check(score == 0, "no fuzzy match for xyz");

    /* Test 10: Start match bonus */
    int s1 = vs_tui_suggestions_fuzzy_match("hel", "hello");
    int s2 = vs_tui_suggestions_fuzzy_match("hel", "say hello");
    check(s1 > s2, "start match has higher score");

    /* Test 11: NULL handling */
    check(vs_tui_suggestions_fuzzy_match(NULL, "test") == 0, "NULL input");
    check(vs_tui_suggestions_fuzzy_match("test", NULL) == 0, "NULL candidate");
    check(vs_tui_suggestions_fuzzy_match("", "test") == 0, "empty input");
    check(vs_tui_suggestions_fuzzy_match("test", "") == 0, "empty candidate");

    /* Test 12: Add to full list */
    count = VS_TUI_SUGGESTION_MAX;
    check(vs_tui_suggestions_add(suggestions, &count, "overflow", 0) == -1, "add to full list");

    /* Test 13: Match with NULL params */
    check(vs_tui_suggestions_match(NULL, 5, "test", results, 10) == 0, "NULL suggestions");
    check(vs_tui_suggestions_match(suggestions, 5, NULL, results, 10) == 0, "NULL input");
    check(vs_tui_suggestions_match(suggestions, 5, "test", NULL, 10) == 0, "NULL results");
    check(vs_tui_suggestions_match(suggestions, 5, "test", results, 0) == 0, "zero max_results");

    /* Test 14: File type suggestions */
    n = vs_tui_suggestions_match(suggestions, count, "main", results, 10);
    check(n >= 1, "file match");
    check(results[0].type == 1, "file type preserved");

    /* Test 15: Path type suggestions */
    n = vs_tui_suggestions_match(suggestions, count, "src", results, 10);
    check(n >= 1, "path match");
    check(results[0].type == 2, "path type preserved");

    /* Test 16: Init with NULL */
    vs_tui_suggestions_init(NULL, NULL); /* should not crash */
    check(1, "init with NULL does not crash");

    /* Test 17: Add with NULL */
    check(vs_tui_suggestions_add(NULL, &count, "test", 0) == -1, "add with NULL suggestions");
    check(vs_tui_suggestions_add(suggestions, NULL, "test", 0) == -1, "add with NULL count");
    check(vs_tui_suggestions_add(suggestions, &count, NULL, 0) == -1, "add with NULL text");

    /* Test 18: Render with NULL */
    vs_tui_suggestions_render(NULL, 5, 80); /* should not crash */
    check(1, "render with NULL does not crash");

    /* Test 19: Render with small width */
    vs_tui_suggestions_render(results, 1, 3); /* should not crash */
    check(1, "render with small width does not crash");

    /* Test 20: Word boundary bonus */
    int wb1 = vs_tui_suggestions_fuzzy_match("fd", "file discovery");
    int wb2 = vs_tui_suggestions_fuzzy_match("fd", "afd bcd");
    check(wb1 > wb2, "word boundary gives higher score");

    /* Test 21: Consecutive match bonus */
    int c1 = vs_tui_suggestions_fuzzy_match("abc", "abc");
    int c2 = vs_tui_suggestions_fuzzy_match("abc", "a_b_c");
    check(c1 > c2, "consecutive match gives higher score");

    /* Test 22: Longer candidate penalty */
    int l1 = vs_tui_suggestions_fuzzy_match("test", "test");
    int l2 = vs_tui_suggestions_fuzzy_match("test", "test command with long name");
    check(l1 > l2, "shorter candidate preferred");

    /* Test 23: Single char match */
    n = vs_tui_suggestions_match(suggestions, count, "m", results, 10);
    check(n >= 1, "single char matches");

    /* Test 24: Special chars in input */
    n = vs_tui_suggestions_match(suggestions, count, "/pro", results, 10);
    check(n >= 1, "special chars in input");
    check(strcmp(results[0].text, "/provider") == 0, "/pro matches /provider");

    /* Test 25: Empty suggestions list */
    count = 0;
    n = vs_tui_suggestions_match(suggestions, 0, "test", results, 10);
    check(n == 0, "empty suggestions returns 0");

    printf("Result: %d/%d tests passed\n", passed, tests);
    return passed == tests ? 0 : 1;
}
