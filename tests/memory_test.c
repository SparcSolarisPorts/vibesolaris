/* SPDX-License-Identifier: Unlicense */
/* Unit tests for persistent memory system (src/memory.c). */
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

#define MEM_FILE "/tmp/vs_memory_test.jsonl"

int main(void) {
    VSMemoryStore store;
    VSMemoryEntry *entry;
    char *result;

    printf("Memory tests:\n");

    /* --- vs_memory_init --- */
    printf("  init:\n");
    vs_memory_init(&store, MEM_FILE);
    check(strcmp(store.store_path, MEM_FILE) == 0, "init sets custom path");
    check(store.entry_count == 0, "init clears entry_count");

    /* --- vs_memory_save --- */
    printf("  save:\n");
    vs_memory_clear(&store);

    int rc = vs_memory_save(&store, "key1", "value1", "tag1", VS_MEMORY_TYPE_NOTE);
    check(rc == 0, "save returns 0");
    check(store.entry_count == 1, "save increments count");

    rc = vs_memory_save(&store, "key2", "value2", "tag2", VS_MEMORY_TYPE_PREF);
    check(store.entry_count == 2, "save second entry");

    /* Save with NULL key */
    rc = vs_memory_save(&store, NULL, "val", "tag", VS_MEMORY_TYPE_NOTE);
    check(rc == -1, "save NULL key returns -1");

    /* Save with empty key */
    rc = vs_memory_save(&store, "", "val", "tag", VS_MEMORY_TYPE_NOTE);
    check(rc == -1, "save empty key returns -1");

    /* Save with NULL value */
    rc = vs_memory_save(&store, "key", NULL, "tag", VS_MEMORY_TYPE_NOTE);
    check(rc == -1, "save NULL value returns -1");

    /* Save with NULL tag (should work) */
    rc = vs_memory_save(&store, "key3", "value3", NULL, VS_MEMORY_TYPE_CONTEXT);
    check(rc == 0, "save NULL tag returns 0");
    check(store.entry_count == 3, "save with NULL tag increments count");

    /* --- vs_memory_get --- */
    printf("  get:\n");

    entry = vs_memory_get(&store, "key1");
    check(entry != NULL, "get existing key");
    check(strcmp(entry->key, "key1") == 0, "get returns correct key");
    check(strcmp(entry->value, "value1") == 0, "get returns correct value");
    check(strcmp(entry->tag, "tag1") == 0, "get returns correct tag");
    check(entry->type == VS_MEMORY_TYPE_NOTE, "get returns correct type");

    entry = vs_memory_get(&store, "nonexistent");
    check(entry == NULL, "get nonexistent returns NULL");

    /* --- vs_memory_save update --- */
    printf("  save update:\n");

    rc = vs_memory_save(&store, "key1", "updated_value", "updated_tag", VS_MEMORY_TYPE_PREF);
    check(rc == 0, "save update returns 0");
    check(store.entry_count == 3, "save update does not increase count");

    entry = vs_memory_get(&store, "key1");
    check(entry != NULL, "get after update");
    check(strcmp(entry->value, "updated_value") == 0, "update changes value");
    check(strcmp(entry->tag, "updated_tag") == 0, "update changes tag");
    check(entry->type == VS_MEMORY_TYPE_PREF, "update changes type");

    /* --- vs_memory_delete --- */
    printf("  delete:\n");

    rc = vs_memory_delete(&store, "key2");
    check(rc == 0, "delete existing returns 0");
    check(store.entry_count == 2, "delete decrements count");

    entry = vs_memory_get(&store, "key2");
    check(entry == NULL, "deleted key not found");

    rc = vs_memory_delete(&store, "nonexistent");
    check(rc == -1, "delete nonexistent returns -1");

    /* --- vs_memory_search --- */
    printf("  search:\n");

    vs_memory_clear(&store);
    vs_memory_save(&store, "username", "alice", "user", VS_MEMORY_TYPE_PREF);
    vs_memory_save(&store, "project", "vibesolaris", "context", VS_MEMORY_TYPE_CONTEXT);
    vs_memory_save(&store, "note", "Use C99", "tip", VS_MEMORY_TYPE_NOTE);

    /* Search with no query returns all */
    result = vs_memory_search(&store, "", 10);
    check(result != NULL, "search empty returns non-NULL");
    check(strstr(result, "username") != NULL, "search empty includes username");
    check(strstr(result, "project") != NULL, "search empty includes project");
    free(result);

    /* Search by key */
    result = vs_memory_search(&store, "username", 10);
    check(result != NULL && strstr(result, "alice") != NULL, "search by key");
    free(result);

    /* Search by value */
    result = vs_memory_search(&store, "vibesolaris", 10);
    check(result != NULL && strstr(result, "project") != NULL, "search by value");
    free(result);

    /* Case-insensitive search */
    result = vs_memory_search(&store, "ALICE", 10);
    check(result != NULL && strstr(result, "username") != NULL, "search case-insensitive");
    free(result);

    /* Search no match */
    result = vs_memory_search(&store, "zzz_no_match", 10);
    check(result != NULL && result[0] == 0, "search no match returns empty");
    free(result);

    /* Search with max_results limit */
    result = vs_memory_search(&store, "", 1);
    check(result != NULL, "search with limit returns non-NULL");
    /* Should only have one entry */
    {
        int newlines = 0;
        char *p = result;
        while ((p = strchr(p, '\n'))) { newlines++; p++; }
        check(newlines == 1, "search limit restricts results");
    }
    free(result);

    /* --- vs_memory_list --- */
    printf("  list:\n");

    result = vs_memory_list(&store, 10);
    check(result != NULL, "list returns non-NULL");
    check(strstr(result, "username") != NULL, "list includes username");
    check(strstr(result, "note") != NULL, "list includes note");
    check(strstr(result, "pref") != NULL, "list shows type name");
    free(result);

    /* List with limit */
    result = vs_memory_list(&store, 1);
    check(result != NULL, "list with limit returns non-NULL");
    free(result);

    /* List empty store */
    vs_memory_clear(&store);
    result = vs_memory_list(&store, 10);
    check(result != NULL && result[0] == 0, "list empty returns empty string");
    free(result);

    /* --- vs_memory_persist and vs_memory_load --- */
    printf("  persist/load:\n");

    vs_memory_clear(&store);
    vs_memory_save(&store, "persist_key", "persist_value", "persist_tag", VS_MEMORY_TYPE_NOTE);
    vs_memory_save(&store, "another", "value", "tag", VS_MEMORY_TYPE_PREF);

    vs_memory_persist(&store);

    /* Verify file exists */
    {
        FILE *f = fopen(MEM_FILE, "r");
        check(f != NULL, "persist creates file");
        if (f) fclose(f);
    }

    /* Load into a new store */
    VSMemoryStore store2;
    vs_memory_init(&store2, MEM_FILE);
    check(store2.entry_count == 0, "new store is empty");

    vs_memory_load(&store2);
    check(store2.entry_count == 2, "load restores entry count");

    entry = vs_memory_get(&store2, "persist_key");
    check(entry != NULL, "load restores persist_key");
    check(strcmp(entry->value, "persist_value") == 0, "load restores value");

    entry = vs_memory_get(&store2, "another");
    check(entry != NULL, "load restores another");

    /* Cleanup */
    unlink(MEM_FILE);

    /* --- vs_memory_clear --- */
    printf("  clear:\n");

    vs_memory_clear(&store);
    check(store.entry_count == 0, "clear resets count");
    check(store.entries[0].key[0] == 0, "clear zeros entries");

    /* --- LRU eviction --- */
    printf("  lru eviction:\n");

    vs_memory_clear(&store);

    /* Fill to capacity */
    int i;
    for (i = 0; i < VS_MAX_MEMORY_ENTRIES; i++) {
        char key[64];
        char val[64];
        snprintf(key, sizeof(key), "lru_%d", i);
        snprintf(val, sizeof(val), "val_%d", i);
        vs_memory_save(&store, key, val, "lru", VS_MEMORY_TYPE_NOTE);
    }
    check(store.entry_count == VS_MAX_MEMORY_ENTRIES, "fill to capacity");

    /* Add one more — should trigger eviction, keeping count at max */
    vs_memory_save(&store, "new_entry", "new_value", "new", VS_MEMORY_TYPE_NOTE);
    check(store.entry_count == VS_MAX_MEMORY_ENTRIES, "eviction keeps count at max");

    entry = vs_memory_get(&store, "new_entry");
    check(entry != NULL, "new entry exists after eviction");

    /* At least one old entry should have been evicted */
    int evicted = 0;
    for (i = 0; i < VS_MAX_MEMORY_ENTRIES; i++) {
        char key[64];
        snprintf(key, sizeof(key), "lru_%d", i);
        if (vs_memory_get(&store, key) == NULL) evicted++;
    }
    check(evicted >= 1, "at least one old entry was evicted");

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
