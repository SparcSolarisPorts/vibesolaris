/* SPDX-License-Identifier: Unlicense */
/* Unit tests for Auto Mode action classifier (src/auto_mode.c). */
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
    VSAutoModeStore store;
    char *result;

    printf("Auto Mode tests:\n");

    /* --- vs_auto_mode_init --- */
    printf("  init:\n");
    vs_auto_mode_init(&store);
    check(store.rule_count > 0, "init populates default rules");
    check(store.enabled == 0, "init disables auto mode by default");

    /* --- Default rules --- */
    printf("  default rules:\n");
    vs_auto_mode_load(&store);
    check(store.rule_count > 0, "load does not clear rules");

    /* --- vs_auto_mode_classify --- */
    printf("  classify safe:\n");
    int cls = vs_auto_mode_classify(&store, "Read", NULL);
    check(cls == VS_AUTO_MODE_SAFE, "Read is safe");

    cls = vs_auto_mode_classify(&store, "Glob", NULL);
    check(cls == VS_AUTO_MODE_SAFE, "Glob is safe");

    cls = vs_auto_mode_classify(&store, "Grep", NULL);
    check(cls == VS_AUTO_MODE_SAFE, "Grep is safe");

    cls = vs_auto_mode_classify(&store, "WebSearch", NULL);
    check(cls == VS_AUTO_MODE_SAFE, "WebSearch is safe");

    printf("  classify risky:\n");
    cls = vs_auto_mode_classify(&store, "write", NULL);
    check(cls == VS_AUTO_MODE_RISKY, "write is risky");

    cls = vs_auto_mode_classify(&store, "edit", NULL);
    check(cls == VS_AUTO_MODE_RISKY, "edit is risky");

    cls = vs_auto_mode_classify(&store, "run", "rm file.txt");
    check(cls == VS_AUTO_MODE_RISKY, "run rm is risky");

    printf("  classify blocked:\n");
    cls = vs_auto_mode_classify(&store, "run", "sudo rm -rf /");
    check(cls == VS_AUTO_MODE_BLOCKED, "run sudo is blocked");

    /* --- vs_auto_mode_should_block --- */
    printf("  should_block:\n");
    store.enabled = 1;
    check(vs_auto_mode_should_block(&store, VS_AUTO_MODE_SAFE) == 0, "safe not blocked");
    check(vs_auto_mode_should_block(&store, VS_AUTO_MODE_RISKY) == 0, "risky not blocked");
    check(vs_auto_mode_should_block(&store, VS_AUTO_MODE_BLOCKED) == 1, "blocked is blocked");

    store.enabled = 0;
    check(vs_auto_mode_should_block(&store, VS_AUTO_MODE_BLOCKED) == 0, "disabled: nothing blocked");

    /* --- vs_auto_mode_list_rules --- */
    printf("  list_rules:\n");
    result = vs_auto_mode_list_rules(&store, 20);
    check(result != NULL, "list_rules returns non-NULL");
    if (result) {
        check(strlen(result) > 0, "list_rules returns content");
        free(result);
    }

    /* --- vs_auto_mode_persist --- */
    printf("  persist:\n");
    int rc = vs_auto_mode_persist(&store);
    check(rc >= 0 || rc == -1, "persist returns int");

    /* --- Constants --- */
    printf("  constants:\n");
    check(VS_AUTO_MODE_SAFE == 0, "SAFE is 0");
    check(VS_AUTO_MODE_RISKY == 1, "RISKY is 1");
    check(VS_AUTO_MODE_BLOCKED == 2, "BLOCKED is 2");
    check(VS_MAX_AUTO_MODE_RULES == 64, "max rules is 64");

    /* --- Edge cases --- */
    printf("  edge cases:\n");
    cls = vs_auto_mode_classify(&store, NULL, NULL);
    check(cls >= 0, "classify NULL tool returns valid class");

    cls = vs_auto_mode_classify(&store, "", NULL);
    check(cls >= 0, "classify empty tool returns valid class");

    cls = vs_auto_mode_classify(&store, "UnknownTool", NULL);
    check(cls >= 0 && cls <= VS_AUTO_MODE_BLOCKED, "classify unknown tool returns valid class");

    result = vs_auto_mode_list_rules(&store, 0);
    check(result != NULL, "list_rules with 0 max returns non-NULL");
    if (result) free(result);

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
