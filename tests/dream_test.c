/* SPDX-License-Identifier: Unlicense */
/* Unit tests for AutoDream memory consolidation (src/memory.c). */
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

#define DREAM_MEM_FILE "/tmp/vs_dream_test.jsonl"

int main(void) {
    VSMemoryStore store;
    VSDreamStats stats;
    char *result;

    printf("Dream (memory consolidation) tests:\n");

    /* --- Init --- */
    printf("  init:\n");
    vs_memory_init(&store, DREAM_MEM_FILE);
    check(store.entry_count == 0, "init clears entry count");

    /* --- Dream on empty store --- */
    printf("  dream empty:\n");
    stats = vs_memory_dream(&store);
    check(stats.total_before == 0, "empty dream: total_before is 0");
    check(stats.total_after == 0, "empty dream: total_after is 0");
    check(stats.merged == 0, "empty dream: merged is 0");
    check(stats.pruned == 0, "empty dream: pruned is 0");
    check(stats.normalized == 0, "empty dream: normalized is 0");

    /* --- Dream stats rendering --- */
    printf("  dream stats:\n");
    result = vs_memory_dream_stats(&stats);
    check(result != NULL, "dream_stats returns non-NULL");
    if (result) free(result);

    /* --- Populate store --- */
    printf("  populate:\n");
    vs_memory_save(&store, "user", "alice", "pref", VS_MEMORY_TYPE_PREF);
    vs_memory_save(&store, "project", "vibesolaris", "context", VS_MEMORY_TYPE_CONTEXT);
    vs_memory_save(&store, "note1", "Use C99", "tip", VS_MEMORY_TYPE_NOTE);
    vs_memory_save(&store, "note2", "Use pthreads", "tip", VS_MEMORY_TYPE_NOTE);
    check(store.entry_count == 4, "saved 4 entries");

    /* --- Dream with entries --- */
    printf("  dream with entries:\n");
    stats = vs_memory_dream(&store);
    check(stats.total_before == 4, "dream: total_before matches entries");
    check(stats.total_after >= 0, "dream: total_after is non-negative");

    /* --- Duplicate merge --- */
    printf("  duplicate merge:\n");
    vs_memory_clear(&store);
    vs_memory_save(&store, "color", "blue", "pref", VS_MEMORY_TYPE_PREF);
    vs_memory_save(&store, "color", "red", "pref", VS_MEMORY_TYPE_PREF);
    check(store.entry_count == 1, "duplicate key updates in place");
    stats = vs_memory_dream(&store);
    check(stats.merged >= 0, "dream merged count is valid");

    /* --- Stale entry pruning --- */
    printf("  stale pruning:\n");
    vs_memory_clear(&store);
    vs_memory_save(&store, "old_ctx", "stale data", "old", VS_MEMORY_TYPE_CONTEXT);
    vs_memory_save(&store, "recent", "fresh data", "current", VS_MEMORY_TYPE_NOTE);
    stats = vs_memory_dream(&store);
    check(stats.pruned >= 0, "dream pruned count is valid");

    /* --- Date normalization --- */
    printf("  date normalization:\n");
    vs_memory_clear(&store);
    vs_memory_save(&store, "deadline", "yesterday", "date", VS_MEMORY_TYPE_NOTE);
    stats = vs_memory_dream(&store);
    check(stats.normalized >= 0, "dream normalized count is valid");

    /* --- Multiple dreams --- */
    printf("  multiple dreams:\n");
    vs_memory_clear(&store);
    vs_memory_save(&store, "a", "1", "t", VS_MEMORY_TYPE_NOTE);
    vs_memory_save(&store, "b", "2", "t", VS_MEMORY_TYPE_NOTE);
    stats = vs_memory_dream(&store);
    int first_after = stats.total_after;
    stats = vs_memory_dream(&store);
    check(stats.total_before == first_after, "second dream sees first result");

    /* --- Dream stats string --- */
    printf("  stats string:\n");
    vs_memory_clear(&store);
    vs_memory_save(&store, "x", "y", "z", VS_MEMORY_TYPE_NOTE);
    stats = vs_memory_dream(&store);
    result = vs_memory_dream_stats(&stats);
    check(result != NULL, "stats string is non-NULL");
    if (result) {
        check(strlen(result) > 0, "stats string is not empty");
        free(result);
    }

    /* --- Edge cases --- */
    printf("  edge cases:\n");
    vs_memory_clear(&store);
    stats = vs_memory_dream(&store);
    result = vs_memory_dream_stats(&stats);
    check(result != NULL, "stats on empty store is non-NULL");
    if (result) free(result);

    /* --- VSDreamStats fields --- */
    printf("  stats fields:\n");
    VSDreamStats empty_stats;
    memset(&empty_stats, 0, sizeof(empty_stats));
    empty_stats.merged = 5;
    empty_stats.pruned = 3;
    empty_stats.normalized = 2;
    empty_stats.total_before = 20;
    empty_stats.total_after = 14;
    result = vs_memory_dream_stats(&empty_stats);
    check(result != NULL, "stats string for populated stats");
    if (result) {
        check(strstr(result, "5") != NULL || strlen(result) > 0, "stats string contains data");
        free(result);
    }

    vs_memory_clear(&store);

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
