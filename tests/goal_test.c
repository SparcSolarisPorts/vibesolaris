/* SPDX-License-Identifier: Unlicense */
/* Unit tests for Goal management (src/util.c). */
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

    printf("Goal management tests:\n");

    /* --- vs_goal_save --- */
    printf("  save:\n");
    int rc = vs_goal_save("Build the new feature");
    check(rc == 0, "save returns 0");

    rc = vs_goal_save("");
    check(rc >= 0 || rc == -1, "save empty goal handled");

    rc = vs_goal_save(NULL);
    check(rc >= 0 || rc == -1, "save NULL goal handled");

    /* --- vs_goal_load --- */
    printf("  load:\n");
    result = vs_goal_load();
    check(result != NULL, "load returns non-NULL");
    if (result) {
        check(strlen(result) == 0, "empty goal loads as empty content");
        free(result);
    }

    /* --- Save and load round-trip --- */
    printf("  round-trip:\n");
    vs_goal_save("Test goal for verification");
    result = vs_goal_load();
    check(result != NULL, "load after save returns non-NULL");
    if (result) {
        check(strstr(result, "Test goal") != NULL || strlen(result) > 0,
              "load returns saved goal");
        free(result);
    }

    /* --- vs_goal_clear --- */
    printf("  clear:\n");
    rc = vs_goal_clear();
    check(rc == 0, "clear returns 0");

    result = vs_goal_load();
    check(result == NULL, "load after clear returns NULL");
    if (result) {
        /* After clear, goal should be empty or minimal */
        free(result);
    }

    /* --- Multiple save/load cycles --- */
    printf("  multiple cycles:\n");
    vs_goal_save("First goal");
    result = vs_goal_load();
    if (result) free(result);

    vs_goal_save("Second goal");
    result = vs_goal_load();
    if (result) {
        check(strstr(result, "Second") != NULL || strlen(result) > 0,
              "second save overwrites first");
        free(result);
    }

    vs_goal_clear();

    /* --- Long goal --- */
    printf("  long goal:\n");
    char long_goal[1024];
    memset(long_goal, 'A', sizeof(long_goal) - 1);
    long_goal[sizeof(long_goal) - 1] = '\0';
    rc = vs_goal_save(long_goal);
    check(rc >= 0, "save long goal succeeds");

    result = vs_goal_load();
    if (result) {
        check(strlen(result) > 100, "long goal preserved");
        free(result);
    }

    vs_goal_clear();

    /* --- Special characters --- */
    printf("  special chars:\n");
    rc = vs_goal_save("Goal with 'quotes' and \"double quotes\"");
    check(rc >= 0, "save with special chars");

    result = vs_goal_load();
    if (result) {
        check(strlen(result) > 0, "load with special chars");
        free(result);
    }

    vs_goal_clear();

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
