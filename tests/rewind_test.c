/* SPDX-License-Identifier: Unlicense */
/* Unit tests for conversation-level rewind (src/rewind.c). */
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
    VSRewindStore store;
    VSContext ctx;
    char *result;

    printf("Rewind tests:\n");

    /* --- vs_rewind_init --- */
    printf("  init:\n");
    vs_rewind_init(&store);
    check(store.marker_count == 0, "init clears marker_count");
    check(store.undo_count == 0, "init clears undo_count");
    check(store.undo_log_count == 0, "init clears undo_log_count");
    check(store.last_history_count == 0, "init clears last_history_count");

    /* --- vs_rewind_mark --- */
    printf("  mark:\n");
    store.last_history_count = 10;
    int idx = vs_rewind_mark(&store, "test_marker");
    check(idx == 0, "mark returns index 0");
    check(store.marker_count == 1, "mark increments marker_count");
    check(strcmp(store.markers[0].label, "test_marker") == 0, "mark sets label");
    check(store.markers[0].history_count == 10, "mark records history_count");

    /* Multiple markers */
    store.last_history_count = 15;
    idx = vs_rewind_mark(&store, "second");
    check(idx == 1, "second mark returns index 1");
    check(store.marker_count == 2, "two markers stored");

    /* Default label */
    store.last_history_count = 20;
    idx = vs_rewind_mark(&store, "");
    check(idx == 2, "empty label gets auto-generated name");
    check(store.marker_count == 3, "three markers stored");

    /* NULL label */
    idx = vs_rewind_mark(&store, NULL);
    check(idx == 3, "NULL label gets auto-generated name");
    check(store.marker_count == 4, "four markers stored");

    /* --- vs_rewind_undo --- */
    printf("  undo:\n");
    memset(&ctx, 0, sizeof(ctx));
    ctx.history_count = 5;
    for (int i = 0; i < 5; i++) {
        ctx.history[i].content = (char *)malloc(64);
        snprintf(ctx.history[i].content, 64, "message %d", i);
        ctx.history[i].bytes = strlen(ctx.history[i].content);
        strncpy(ctx.history[i].role, "user", sizeof(ctx.history[i].role));
    }
    ctx.history_bytes = 0;
    for (int i = 0; i < 5; i++) ctx.history_bytes += ctx.history[i].bytes;

    store.last_history_count = ctx.history_count;
    int removed = vs_rewind_undo(&store, &ctx, 2);
    check(removed == 2, "undo removes 2 messages");
    check(ctx.history_count == 3, "history_count reduced to 3");
    check(store.undo_count == 1, "undo_count incremented");
    check(store.undo_log_count == 1, "undo_log entry created");

    /* Undo 1 message */
    removed = vs_rewind_undo(&store, &ctx, 1);
    check(removed == 1, "undo removes 1 message");
    check(ctx.history_count == 2, "history_count reduced to 2");

    /* Undo with n=0 defaults to 1 */
    removed = vs_rewind_undo(&store, &ctx, 0);
    check(removed == 1, "undo with n=0 removes 1 message");
    check(ctx.history_count == 1, "history_count reduced to 1");

    /* Undo more than available */
    removed = vs_rewind_undo(&store, &ctx, 10);
    check(removed == 1, "undo caps at available messages");
    check(ctx.history_count == 0, "history_count is 0");

    /* Undo with empty history */
    removed = vs_rewind_undo(&store, &ctx, 1);
    check(removed == 0, "undo with empty history returns 0");

    /* Undo with NULL ctx */
    removed = vs_rewind_undo(&store, NULL, 1);
    check(removed == -1, "undo with NULL ctx returns -1");

    /* Undo with NULL store */
    removed = vs_rewind_undo(NULL, &ctx, 1);
    check(removed == -1, "undo with NULL store returns -1");

    /* --- vs_rewind_to_marker --- */
    printf("  to_marker:\n");
    vs_rewind_init(&store);
    memset(&ctx, 0, sizeof(ctx));

    /* Build history */
    ctx.history_count = 10;
    for (int i = 0; i < 10; i++) {
        ctx.history[i].content = (char *)malloc(64);
        snprintf(ctx.history[i].content, 64, "msg %d", i);
        ctx.history[i].bytes = strlen(ctx.history[i].content);
        strncpy(ctx.history[i].role, "user", sizeof(ctx.history[i].role));
    }
    ctx.history_bytes = 0;
    for (int i = 0; i < 10; i++) ctx.history_bytes += ctx.history[i].bytes;

    /* Set marker at history_count = 5 */
    store.last_history_count = 5;
    vs_rewind_mark(&store, "checkpoint_5");
    store.last_history_count = ctx.history_count;

    /* Rewind to marker */
    removed = vs_rewind_to_marker(&store, &ctx, "checkpoint_5");
    check(removed == 5, "rewind to marker removes 5 messages");
    check(ctx.history_count == 5, "history_count at marker point");
    check(store.marker_count == 0, "marker consumed after rewind");

    /* Rewind to nonexistent marker */
    removed = vs_rewind_to_marker(&store, &ctx, "nonexistent");
    check(removed == -1, "rewind to nonexistent marker returns -1");

    /* Rewind with NULL label */
    removed = vs_rewind_to_marker(&store, &ctx, NULL);
    check(removed == -1, "rewind with NULL label returns -1");

    /* Rewind with NULL ctx */
    removed = vs_rewind_to_marker(&store, NULL, "test");
    check(removed == -1, "rewind with NULL ctx returns -1");

    /* --- vs_rewind_list --- */
    printf("  list:\n");
    vs_rewind_init(&store);
    store.last_history_count = 8;
    vs_rewind_mark(&store, "alpha");
    vs_rewind_mark(&store, "beta");
    store.undo_count = 3;

    result = vs_rewind_list(&store, 20);
    check(result != NULL, "list returns non-NULL");
    if (result) {
        check(strstr(result, "alpha") != NULL, "list shows marker 'alpha'");
        check(strstr(result, "beta") != NULL, "list shows marker 'beta'");
        check(strstr(result, "markers") != NULL, "list shows marker count");
        free(result);
    }

    /* List with NULL store */
    result = vs_rewind_list(NULL, 10);
    check(result != NULL, "list with NULL store returns non-NULL");
    if (result) free(result);

    /* --- vs_rewind_checkpoint_rollback --- */
    printf("  checkpoint_rollback:\n");
    VSCheckpointStore cp;
    memset(&cp, 0, sizeof(cp));
    int rc = vs_rewind_checkpoint_rollback(&store, &cp);
    check(rc == -1, "checkpoint_rollback with empty store returns -1");

    rc = vs_rewind_checkpoint_rollback(NULL, &cp);
    check(rc == -1, "checkpoint_rollback with NULL store returns -1");

    rc = vs_rewind_checkpoint_rollback(&store, NULL);
    check(rc == -1, "checkpoint_rollback with NULL cp returns -1");

    /* --- vs_rewind_clear --- */
    printf("  clear:\n");
    vs_rewind_clear(&store);
    check(store.marker_count == 0, "clear resets marker_count");
    check(store.undo_count == 0, "clear resets undo_count");
    check(store.undo_log_count == 0, "clear resets undo_log_count");

    vs_rewind_clear(NULL);
    check(1, "clear with NULL store is safe");

    /* --- Capacity limits --- */
    printf("  capacity limits:\n");
    check(VS_REWIND_MARKER_MAX == 64, "max markers is 64");
    check(VS_REWIND_MARKER_LABEL_MAX == 128, "marker label max is 128");

    /* --- Marker eviction --- */
    printf("  marker eviction:\n");
    vs_rewind_init(&store);
    for (int i = 0; i < VS_REWIND_MARKER_MAX + 5; i++) {
        store.last_history_count = i;
        vs_rewind_mark(&store, "eviction_test");
    }
    check(store.marker_count == VS_REWIND_MARKER_MAX, "markers capped at max");

    /* --- Edge cases --- */
    printf("  edge cases:\n");
    vs_rewind_init(&store);
    memset(&ctx, 0, sizeof(ctx));

    /* Undo on empty context */
    ctx.history_count = 0;
    store.last_history_count = 0;
    removed = vs_rewind_undo(&store, &ctx, 1);
    check(removed == 0, "undo on empty context returns 0");

    /* Mark with empty store */
    store.last_history_count = 0;
    idx = vs_rewind_mark(&store, "empty");
    check(idx == 0, "mark on empty store works");

    /* List with 0 max_lines */
    result = vs_rewind_list(&store, 0);
    check(result != NULL, "list with 0 max_lines returns non-NULL");
    if (result) free(result);

    /* Cleanup */
    for (int i = 0; i < ctx.history_count; i++) {
        if (ctx.history[i].content) free(ctx.history[i].content);
    }

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
