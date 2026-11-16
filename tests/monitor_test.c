/* SPDX-License-Identifier: Unlicense */
/* Unit tests for Monitor tool: background process management (src/monitor.c). */
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
    int id;

    printf("Monitor tool tests:\n");

    /* --- Error cases --- */
    printf("  error cases:\n");
    id = vs_monitor_start(NULL);
    check(id < 0, "NULL cmd returns -1");

    id = vs_monitor_start("");
    check(id < 0, "empty cmd returns -1");

    result = vs_monitor_output(-1);
    check(result != NULL && strstr(result, "ERROR") != NULL, "invalid id returns error");
    free(result);

    result = vs_monitor_output(999);
    check(result != NULL && strstr(result, "ERROR") != NULL, "out of range id returns error");
    free(result);

    /* --- Start a quick command --- */
    printf("  start quick command:\n");
    id = vs_monitor_start("echo hello_world_test");
    check(id >= 0, "monitor_start returns valid id");
    check(id < VS_MAX_MONITORS, "monitor id within range");

    /* Kill it immediately to avoid waiting */
    vs_monitor_kill(id);

    /* --- Kill a monitor --- */
    printf("  kill monitor:\n");
    id = vs_monitor_start("cat /dev/null");
    check(id >= 0, "monitor started");

    int rc = vs_monitor_kill(id);
    check(rc == 0, "monitor_kill returns 0");

    result = vs_monitor_output(id);
    check(result != NULL && strstr(result, "ERROR") != NULL,
          "output after kill returns error");
    free(result);

    /* --- Kill error cases --- */
    printf("  kill error cases:\n");
    rc = vs_monitor_kill(-1);
    check(rc < 0, "kill invalid id returns -1");

    rc = vs_monitor_kill(999);
    check(rc < 0, "kill out of range returns -1");

    /* --- Kill all --- */
    printf("  kill all:\n");
    rc = vs_monitor_kill_all();
    check(rc == 0, "kill_all returns 0");

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
