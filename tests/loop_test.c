/* SPDX-License-Identifier: Unlicense */
/* Unit tests for scheduled recurring prompts (src/loop.c). */
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
    VSContext ctx;
    char *result;

    printf("Loop tests:\n");

    /* --- vs_loop_init --- */
    printf("  init:\n");
    memset(&ctx, 0, sizeof(ctx));
    vs_loop_init(&ctx);
    check(ctx.loop_count == 0, "init clears loop_count");
    check(ctx.loop_thread_active == 0, "init clears thread_active");
    check(ctx.loop_next_id == 0, "init clears next_id");

    /* --- vs_loop_add --- */
    printf("  add:\n");
    int id = vs_loop_add(&ctx, "Check for new issues", 300);
    check(id >= 0, "add returns valid ID");
    check(ctx.loop_count > 0, "add increments loop_count");

    /* Find the added loop */
    int found = 0;
    for (int i = 0; i < VS_MAX_LOOPS; i++) {
        if (ctx.loops[i].active && ctx.loops[i].id == id) {
            found = 1;
            check(strcmp(ctx.loops[i].prompt, "Check for new issues") == 0,
                  "loop stores prompt");
            check(ctx.loops[i].interval_seconds == 300, "loop stores interval");
            check(ctx.loops[i].run_count == 0, "loop starts with 0 runs");
            break;
        }
    }
    check(found == 1, "added loop is findable");

    /* Minimum interval enforcement */
    id = vs_loop_add(&ctx, "Fast loop", 10);
    check(id >= 0, "add with short interval returns valid ID");
    for (int i = 0; i < VS_MAX_LOOPS; i++) {
        if (ctx.loops[i].active && ctx.loops[i].id == id) {
            check(ctx.loops[i].interval_seconds >= 60,
                  "interval enforced to minimum 60s");
            break;
        }
    }

    /* Multiple loops */
    id = vs_loop_add(&ctx, "Third loop", 600);
    check(id >= 0, "add third loop succeeds");

    /* --- vs_loop_remove --- */
    printf("  remove:\n");
    int rc = vs_loop_remove(&ctx, id);
    check(rc == 0, "remove returns 0 for valid ID");

    rc = vs_loop_remove(&ctx, 9999);
    check(rc == -1, "remove returns -1 for invalid ID");

    /* --- vs_loop_stop_all --- */
    printf("  stop_all:\n");
    vs_loop_stop_all(&ctx);
    check(ctx.loop_count == 0, "stop_all clears loop_count");
    for (int i = 0; i < VS_MAX_LOOPS; i++) {
        check(ctx.loops[i].active == 0, "stop_all deactivates all loops");
    }

    /* --- vs_loop_list --- */
    printf("  list:\n");
    result = vs_loop_list(&ctx, 20);
    check(result != NULL, "list returns non-NULL");
    if (result) {
        check(strstr(result, "No active loops") != NULL,
              "list shows no active loops message");
        free(result);
    }

    /* List with NULL ctx */
    result = vs_loop_list(NULL, 10);
    check(result != NULL, "list with NULL ctx returns non-NULL");
    if (result) free(result);

    /* --- vs_loop_shutdown --- */
    printf("  shutdown:\n");
    vs_loop_shutdown(&ctx);
    check(ctx.loop_thread_active == 0, "shutdown clears thread_active");

    vs_loop_shutdown(NULL);
    check(1, "shutdown with NULL ctx is safe");

    /* --- Capacity limits --- */
    printf("  capacity limits:\n");
    check(VS_MAX_LOOPS == 16, "max loops is 16");
    check(VS_LOOP_PROMPT_MAX == 1024, "loop prompt max is 1024");

    /* --- NULL safety --- */
    printf("  null safety:\n");
    vs_loop_init(NULL);
    check(1, "init with NULL ctx is safe");

    id = vs_loop_add(NULL, "test", 60);
    check(id == -1, "add with NULL ctx returns -1");

    id = vs_loop_add(&ctx, NULL, 60);
    check(id == -1, "add with NULL prompt returns -1");

    rc = vs_loop_remove(NULL, 1);
    check(rc == -1, "remove with NULL ctx returns -1");

    rc = vs_loop_stop_all(NULL);
    check(rc == -1, "stop_all with NULL ctx returns -1");

    /* --- Edge cases --- */
    printf("  edge cases:\n");
    vs_loop_init(&ctx);

    /* Add loop with very long interval */
    id = vs_loop_add(&ctx, "Daily check", 86400 * 2);
    if (id >= 0) {
        for (int i = 0; i < VS_MAX_LOOPS; i++) {
            if (ctx.loops[i].active && ctx.loops[i].id == id) {
                check(ctx.loops[i].interval_seconds <= 86400,
                      "interval capped at 24 hours");
                break;
            }
        }
    }

    /* List with 0 max_lines */
    result = vs_loop_list(&ctx, 0);
    check(result != NULL, "list with 0 max_lines returns non-NULL");
    if (result) free(result);

    vs_loop_shutdown(&ctx);

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
