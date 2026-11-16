/* SPDX-License-Identifier: Unlicense */
/* Unit tests for Glob tool: pattern-based file discovery (src/glob.c). */
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
    mkdir("/tmp/vs_glob_test", 0755);
    mkdir("/tmp/vs_glob_test/src", 0755);
    mkdir("/tmp/vs_glob_test/src/sub", 0755);
    
    /* Create test files */
    FILE *f;
    f = fopen("/tmp/vs_glob_test/main.c", "w"); if(f) fclose(f);
    f = fopen("/tmp/vs_glob_test/util.c", "w"); if(f) fclose(f);
    f = fopen("/tmp/vs_glob_test/main.h", "w"); if(f) fclose(f);
    f = fopen("/tmp/vs_glob_test/src/core.c", "w"); if(f) fclose(f);
    f = fopen("/tmp/vs_glob_test/src/helper.c", "w"); if(f) fclose(f);
    f = fopen("/tmp/vs_glob_test/src/sub/deep.c", "w"); if(f) fclose(f);
    f = fopen("/tmp/vs_glob_test/README.md", "w"); if(f) fclose(f);
}

static void cleanup_test_dir(void) {
    system("rm -rf /tmp/vs_glob_test");
}

int main(void) {
    char *result;
    int count;
    char results[VS_MAX_GLOB_RESULTS][VS_MAX_PATH];

    printf("Glob tool tests:\n");

    setup_test_dir();

    /* --- Basic *.c pattern --- */
    printf("  basic *.c pattern:\n");
    result = vs_glob("/tmp/vs_glob_test", "*.c", &count, results);
    check(result != NULL, "glob returns non-NULL");
    check(count >= 2, "glob finds at least 2 .c files in root");
    check(strstr(result, "main.c") != NULL, "result contains main.c");
    check(strstr(result, "util.c") != NULL, "result contains util.c");
    check(strstr(result, "[matches=") != NULL, "result contains match count");
    free(result);

    /* --- recursive pattern --- */
    printf("  **/*.c recursive pattern:\n");
    result = vs_glob("/tmp/vs_glob_test", "**/*.c", &count, results);
    check(result != NULL, "recursive glob returns non-NULL");
    check(count >= 5, "recursive glob finds at least 5 .c files");
    check(strstr(result, "main.c") != NULL, "recursive finds main.c");
    check(strstr(result, "core.c") != NULL, "recursive finds core.c");
    check(strstr(result, "deep.c") != NULL, "recursive finds deep.c");
    free(result);

    /* --- *.h pattern --- */
    printf("  *.h pattern:\n");
    result = vs_glob("/tmp/vs_glob_test", "*.h", &count, results);
    check(result != NULL, "*.h glob returns non-NULL");
    check(count >= 1, "*.h glob finds at least 1 .h file");
    check(strstr(result, "main.h") != NULL, "result contains main.h");
    free(result);

    /* --- recursive md pattern --- */
    printf("  **/*.md pattern:\n");
    result = vs_glob("/tmp/vs_glob_test", "**/*.md", &count, results);
    check(result != NULL, "**/*.md glob returns non-NULL");
    check(count >= 1, "**/*.md finds at least 1 .md file");
    check(strstr(result, "README.md") != NULL, "result contains README.md");
    free(result);

    /* --- No matches --- */
    printf("  no matches:\n");
    result = vs_glob("/tmp/vs_glob_test", "*.xyz", &count, results);
    check(result != NULL, "no match glob returns non-NULL");
    check(count == 0, "no match count is 0");
    check(strstr(result, "(no matches)") != NULL, "no match message");
    free(result);

    /* --- Error cases --- */
    printf("  error cases:\n");
    result = vs_glob(NULL, "*.c", &count, results);
    check(result != NULL && strstr(result, "ERROR") != NULL, "NULL path returns error");
    free(result);

    result = vs_glob("/tmp/vs_glob_test", NULL, &count, results);
    check(result != NULL && strstr(result, "ERROR") != NULL, "NULL pattern returns error");
    free(result);

    result = vs_glob("/nonexistent/path", "*.c", &count, results);
    check(result != NULL && strstr(result, "ERROR") != NULL, "nonexistent path returns error");
    free(result);

    /* --- ? pattern --- */
    printf("  ? pattern:\n");
    result = vs_glob("/tmp/vs_glob_test", "?.c", &count, results);
    check(result != NULL, "? pattern glob returns non-NULL");
    /* No single-char .c files, so count should be 0 */
    check(count == 0, "? pattern finds no single-char .c files");
    free(result);

    /* --- Results array populated --- */
    printf("  results array:\n");
    result = vs_glob("/tmp/vs_glob_test", "*.c", &count, results);
    check(count > 0, "results array has entries");
    int found = 0;
    int i;
    for (i = 0; i < count; i++) {
        if (strstr(results[i], "main.c") != NULL) found++;
    }
    check(found > 0, "results array contains main.c");
    free(result);

    cleanup_test_dir();

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
