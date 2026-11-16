/* SPDX-License-Identifier: Unlicense */
/* Unit tests for Grep tool: content search with context (src/grep.c). */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int tests = 0;
static int passed = 0;

static void check(int cond, const char *msg) {
    tests++;
    if (cond) {
        passed++;
        printf("  ok: %s\n", msg);
    } else {
        printf("  FAIL: %s\n", msg);
    }
}

static void setup_test_dir(void) {
    mkdir("/tmp/vs_grep_test", 0755);
    mkdir("/tmp/vs_grep_test/src", 0755);
    
    FILE *f;
    f = fopen("/tmp/vs_grep_test/main.c", "w");
    if (f) {
        fprintf(f, "#include <stdio.h>\n");
        fprintf(f, "int main(void) {\n");
        fprintf(f, "    printf(\"Hello World\");\n");
        fprintf(f, "    return 0;\n");
        fprintf(f, "}\n");
        fclose(f);
    }
    f = fopen("/tmp/vs_grep_test/util.c", "w");
    if (f) {
        fprintf(f, "void helper(void) {\n");
        fprintf(f, "    /* utility function */\n");
        fprintf(f, "    printf(\"Helper\");\n");
        fprintf(f, "}\n");
        fclose(f);
    }
    f = fopen("/tmp/vs_grep_test/src/core.c", "w");
    if (f) {
        fprintf(f, "int core_init(void) {\n");
        fprintf(f, "    return 0;\n");
        fprintf(f, "}\n");
        fclose(f);
    }
}

static void cleanup_test_dir(void) {
    system("rm -rf /tmp/vs_grep_test");
}

int main(void) {
    char *result;

    printf("Grep tool tests:\n");

    setup_test_dir();

    /* --- Basic search --- */
    printf("  basic search:\n");
    result = vs_grep("/tmp/vs_grep_test", "printf", 0, 0, 100);
    check(result != NULL, "grep returns non-NULL");
    check(strstr(result, "main.c") != NULL, "finds printf in main.c");
    check(strstr(result, "util.c") != NULL, "finds printf in util.c");
    check(strstr(result, "[matches=") != NULL, "result contains match count");
    free(result);

    /* --- Case insensitive search --- */
    printf("  case insensitive search:\n");
    result = vs_grep("/tmp/vs_grep_test", "PRINTF", 1, 0, 100);
    check(result != NULL, "case insensitive grep returns non-NULL");
    check(strstr(result, "main.c") != NULL, "case insensitive finds printf");
    free(result);

    /* --- Case sensitive search (should not match) --- */
    printf("  case sensitive no match:\n");
    result = vs_grep("/tmp/vs_grep_test", "PRINTF", 0, 0, 100);
    check(result != NULL, "case sensitive grep returns non-NULL");
    /* PRINTF in uppercase should not match printf */
    check(strstr(result, "PRINTF") == NULL || strstr(result, "[matches=0]") != NULL,
          "case sensitive does not match uppercase");
    free(result);

    /* --- Search in single file --- */
    printf("  single file search:\n");
    result = vs_grep("/tmp/vs_grep_test/main.c", "Hello", 0, 0, 100);
    check(result != NULL, "single file grep returns non-NULL");
    check(strstr(result, "Hello") != NULL, "finds Hello in main.c");
    free(result);

    /* --- No matches --- */
    printf("  no matches:\n");
    result = vs_grep("/tmp/vs_grep_test", "NONEXISTENT_PATTERN_XYZ", 0, 0, 100);
    check(result != NULL, "no match grep returns non-NULL");
    check(strstr(result, "[matches=0]") != NULL, "no match count is 0");
    free(result);

    /* --- Context lines --- */
    printf("  context lines:\n");
    result = vs_grep("/tmp/vs_grep_test/main.c", "printf", 0, 1, 100);
    check(result != NULL, "context grep returns non-NULL");
    check(strstr(result, "with-context") != NULL, "context mode indicated");
    free(result);

    /* --- Error cases --- */
    printf("  error cases:\n");
    result = vs_grep(NULL, "test", 0, 0, 100);
    check(result != NULL && strstr(result, "ERROR") != NULL, "NULL path returns error");
    free(result);

    result = vs_grep("/tmp/vs_grep_test", NULL, 0, 0, 100);
    check(result != NULL && strstr(result, "ERROR") != NULL, "NULL pattern returns error");
    free(result);

    result = vs_grep("/nonexistent/path", "test", 0, 0, 100);
    check(result != NULL && strstr(result, "ERROR") != NULL, "nonexistent path returns error");
    free(result);

    /* --- Limit results --- */
    printf("  limit results:\n");
    result = vs_grep("/tmp/vs_grep_test", "printf", 0, 0, 1);
    check(result != NULL, "limited grep returns non-NULL");
    /* Should have at most 1 match */
    check(strstr(result, "[matches=1]") != NULL || strstr(result, "[matches=0]") != NULL,
          "limit respected");
    free(result);

    /* --- Recursive search --- */
    printf("  recursive search:\n");
    result = vs_grep("/tmp/vs_grep_test", "core_init", 0, 0, 100);
    check(result != NULL, "recursive grep returns non-NULL");
    check(strstr(result, "core.c") != NULL, "finds core_init in src/core.c");
    free(result);

    cleanup_test_dir();

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
