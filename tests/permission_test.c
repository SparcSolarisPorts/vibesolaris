/* SPDX-License-Identifier: Unlicense */
/* Unit tests for Permission rules: allow/deny system (src/permission.c). */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    VSPermissionStore store;
    char *result;

    printf("Permission rules tests:\n");

    /* --- vs_permission_init --- */
    printf("  init:\n");
    memset(&store, 0xFF, sizeof(store));
    vs_permission_init(&store);
    check(store.rule_count == 0, "init clears rule_count");

    /* --- vs_permission_add_rule --- */
    printf("  add rule:\n");
    check(vs_permission_add_rule(&store, "Bash", 0) == 0, "add deny rule returns 0");
    check(store.rule_count == 1, "rule count is 1");
    check(store.rules[0].allow == 0, "rule is deny");
    check(strcmp(store.rules[0].tool_pattern, "Bash") == 0, "tool pattern is Bash");

    check(vs_permission_add_rule(&store, "Write", 1) == 0, "add allow rule returns 0");
    check(store.rule_count == 2, "rule count is 2");

    /* Rule with path pattern */
    check(vs_permission_add_rule(&store, "Write(production.*)", 0) == 0, "add path rule returns 0");
    check(store.rule_count == 3, "rule count is 3");
    check(strcmp(store.rules[2].tool_pattern, "Write") == 0, "tool pattern parsed");
    check(strcmp(store.rules[2].path_pattern, "production.*") == 0, "path pattern parsed");

    /* NULL rule */
    check(vs_permission_add_rule(&store, NULL, 1) == -1, "NULL rule returns -1");
    check(vs_permission_add_rule(&store, "", 1) == -1, "empty rule returns -1");

    /* --- vs_permission_check --- */
    printf("  check:\n");
    check(vs_permission_check(&store, "Bash", NULL) == 0, "Bash is denied");
    check(vs_permission_check(&store, "Write", "test.txt") == 1, "Write test.txt is allowed");
    check(vs_permission_check(&store, "Read", "any.txt") == -1, "Read has no rule (default allow)");
    check(vs_permission_check(&store, "Write", "production.db") == 0, "Write production.db is denied");
    check(vs_permission_check(NULL, "Bash", NULL) == -1, "NULL store returns -1");
    check(vs_permission_check(&store, NULL, NULL) == -1, "NULL tool returns -1");

    /* --- vs_permission_remove_rule --- */
    printf("  remove rule:\n");
    check(vs_permission_remove_rule(&store, 0) == 0, "remove first rule returns 0");
    check(store.rule_count == 2, "rule count is 2");
    check(strcmp(store.rules[0].tool_pattern, "Write") == 0, "first rule is now Write");
    check(vs_permission_remove_rule(&store, -1) == -1, "remove invalid index returns -1");
    check(vs_permission_remove_rule(&store, 100) == -1, "remove out of range returns -1");

    /* --- vs_permission_clear --- */
    printf("  clear:\n");
    check(vs_permission_clear(&store) == 0, "clear returns 0");
    check(store.rule_count == 0, "rule count is 0 after clear");

    /* --- vs_permission_list --- */
    printf("  list:\n");
    vs_permission_add_rule(&store, "Bash", 0);
    vs_permission_add_rule(&store, "Write", 1);
    result = vs_permission_list(&store, 10);
    check(result != NULL, "list returns non-NULL");
    check(strstr(result, "DENY") != NULL, "list contains DENY");
    check(strstr(result, "ALLOW") != NULL, "list contains ALLOW");
    free(result);

    /* Empty list */
    vs_permission_clear(&store);
    result = vs_permission_list(&store, 10);
    check(strstr(result, "No permission rules") != NULL, "empty list message");
    free(result);

    /* --- Wildcard pattern --- */
    printf("  wildcard:\n");
    vs_permission_add_rule(&store, "*", 0);
    check(vs_permission_check(&store, "Anything", NULL) == 0, "wildcard denies all");

    /* --- Capacity limit --- */
    printf("  capacity:\n");
    vs_permission_clear(&store);
    int i;
    for (i = 0; i < VS_MAX_PERMISSION_RULES; i++) {
        vs_permission_add_rule(&store, "Tool", 1);
    }
    check(store.rule_count == VS_MAX_PERMISSION_RULES, "filled to capacity");
    check(vs_permission_add_rule(&store, "Extra", 1) == -1, "exceeds capacity returns -1");

    /* --- Pattern matching --- */
    printf("  pattern matching:\n");
    vs_permission_clear(&store);
    vs_permission_add_rule(&store, "Bash(rm )", 0);
    check(vs_permission_check(&store, "Bash", "rm -rf /") == 0, "Bash rm -rf / is denied");
    check(vs_permission_check(&store, "Bash", "ls") == -1, "Bash ls has no match");

    /* --- Load/Persist --- */
    printf("  load/persist:\n");
    vs_permission_clear(&store);
    vs_permission_add_rule(&store, "Bash", 0);
    vs_permission_add_rule(&store, "Write", 1);
    check(vs_permission_persist(&store, "/tmp/vs_perm_test.txt") == 0, "persist returns 0");

    vs_permission_clear(&store);
    vs_permission_load(&store, "/tmp/vs_perm_test.txt");
    check(store.rule_count == 2, "loaded 2 rules");
    check(vs_permission_check(&store, "Bash", NULL) == 0, "loaded Bash deny works");

    /* Cleanup */
    system("rm -f /tmp/vs_perm_test.txt");

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
