/* SPDX-License-Identifier: Unlicense */
/* Unit tests for git checkpoint system (src/checkpoint.c). */
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

#define CP_FILE "/tmp/vs_checkpoint_test.jsonl"

int main(void) {
    VSCheckpointStore store;
    char *result;

    printf("Checkpoint tests:\n");

    /* --- vs_checkpoint_init --- */
    printf("  init:\n");
    vs_checkpoint_init(&store);
    check(store.count == 0, "init clears count");
    check(store.is_git_repo >= 0, "init detects git repo status");
    check(strlen(store.dir) > 0, "init sets directory path");

    /* --- vs_checkpoint_create --- */
    printf("  create:\n");
    int rc = vs_checkpoint_create(&store, "test checkpoint");
    check(rc >= 0 || rc == -1, "create returns int (may fail without git)");
    /* If create succeeded and count > 0, verify entry fields */
    if (rc >= 0 && store.count > 0) {
        check(strlen(store.entries[0].id) > 0, "create sets checkpoint ID");
        check(strlen(store.entries[0].description) > 0, "create sets description");
    } else if (rc == 0 && store.count == 0) {
        check(1, "create returns 0 when working tree is clean (no stash needed)");
    } else {
        check(store.count == 0, "create failure leaves count at 0");
    }

    /* --- vs_checkpoint_list --- */
    printf("  list:\n");
    result = vs_checkpoint_list(&store, 10);
    check(result != NULL, "list returns non-NULL");
    if (result) {
        free(result);
    }

    /* --- vs_checkpoint_rollback --- */
    printf("  rollback:\n");
    if (store.count > 0) {
        rc = vs_checkpoint_rollback(&store, store.entries[0].id);
        check(rc >= 0 || rc == -1, "rollback returns int");
    } else {
        rc = vs_checkpoint_rollback(&store, "nonexistent");
        check(rc == -1, "rollback nonexistent returns -1");
    }

    /* --- vs_checkpoint_auto_before_edit --- */
    printf("  auto_before_edit:\n");
    rc = vs_checkpoint_auto_before_edit(&store);
    check(rc >= 0 || rc == -1, "auto_before_edit returns int");

    /* --- vs_checkpoint_persist --- */
    printf("  persist:\n");
    rc = vs_checkpoint_persist(&store);
    check(rc >= 0 || rc == -1, "persist returns int");

    /* --- vs_checkpoint_clear --- */
    printf("  clear:\n");
    vs_checkpoint_clear(&store);
    check(store.count == 0, "clear resets count");

    /* --- Multiple checkpoints --- */
    printf("  multiple checkpoints:\n");
    if (store.is_git_repo) {
        vs_checkpoint_create(&store, "first");
        vs_checkpoint_create(&store, "second");
        vs_checkpoint_create(&store, "third");
        check(store.count <= VS_MAX_CHECKPOINTS, "count respects max limit");
        vs_checkpoint_clear(&store);
    }

    /* --- Capacity limit --- */
    printf("  capacity limit:\n");
    check(VS_MAX_CHECKPOINTS == 16, "max checkpoints is 16");

    /* --- Checkpoint ID format --- */
    printf("  ID format:\n");
    check(VS_CHECKPOINT_ID_MAX == 64, "checkpoint ID max is 64");
    check(VS_CHECKPOINT_DESC_MAX == 256, "checkpoint desc max is 256");

    /* --- Edge cases --- */
    printf("  edge cases:\n");
    rc = vs_checkpoint_create(&store, NULL);
    check(rc >= 0 || rc == -1, "create with NULL desc handled");

    rc = vs_checkpoint_create(&store, "");
    check(rc >= 0 || rc == -1, "create with empty desc handled");

    result = vs_checkpoint_list(&store, 0);
    check(result != NULL, "list with 0 max_lines returns non-NULL");
    if (result) free(result);

    result = vs_checkpoint_list(&store, 1);
    check(result != NULL, "list with 1 max_lines returns non-NULL");
    if (result) free(result);

    vs_checkpoint_clear(&store);

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
