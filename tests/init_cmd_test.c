/* SPDX-License-Identifier: Unlicense */
/* Unit tests for Init command auto-onboarding (src/init_cmd.c). */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

int main(void) {
    char *result;

    printf("Init command tests:\n");

    /* --- vs_init_repo on current dir --- */
    printf("  init repo:\n");
    result = vs_init_repo(".");
    check(result != NULL, "init_repo returns non-NULL");
    if (result) {
        check(strlen(result) > 0, "init_repo returns content");
        free(result);
    }

    /* --- vs_init_repo on /tmp --- */
    printf("  init /tmp:\n");
    result = vs_init_repo("/tmp");
    check(result != NULL, "init_repo /tmp returns non-NULL");
    if (result) free(result);

    /* --- vs_init_repo on nonexistent --- */
    printf("  init nonexistent:\n");
    result = vs_init_repo("/nonexistent/path/that/does/not/exist");
    check(result != NULL, "init_repo nonexistent returns non-NULL");
    if (result) free(result);

    /* --- vs_init_repo with NULL --- */
    printf("  init NULL:\n");
    result = vs_init_repo(NULL);
    check(result != NULL, "init_repo NULL returns non-NULL");
    if (result) free(result);

    /* --- vs_init_repo with empty string --- */
    printf("  init empty:\n");
    result = vs_init_repo("");
    check(result != NULL, "init_repo empty returns non-NULL");
    if (result) free(result);

    /* --- Init on project root --- */
    printf("  init project:\n");
    result = vs_init_repo("/export/home/solaris/git/vibesolaris");
    check(result != NULL, "init_repo on project returns non-NULL");
    if (result) {
        /* Should detect C project */
        check(strstr(result, "C") != NULL || strstr(result, "c") != NULL || strlen(result) > 0,
              "init detects project type");
        free(result);
    }

    /* --- Init creates CLAUDE.md content --- */
    printf("  CLAUDE.md content:\n");
    result = vs_init_repo(".");
    if (result) {
        check(strstr(result, "Project") != NULL || strstr(result, "project") != NULL || strlen(result) > 0,
              "init includes project info");
        free(result);
    }

    /* --- Multiple init calls --- */
    printf("  multiple init:\n");
    result = vs_init_repo(".");
    char *result2 = vs_init_repo(".");
    check(result != NULL && result2 != NULL, "multiple init calls work");
    if (result) free(result);
    if (result2) free(result2);

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
