/* SPDX-License-Identifier: Unlicense */
/* Unit tests for Task management: cross-session tasks (src/task.c). */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

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
    VSTaskStore store;
    char *result;
    VSTaskEntry *entry;
    int id;

    printf("Task management tests:\n");

    /* --- vs_task_init --- */
    printf("  init:\n");
    memset(&store, 0xFF, sizeof(store));
    vs_task_init(&store, "/tmp/vs_task_test.jsonl");
    check(store.task_count == 0, "init clears task_count");
    check(store.next_id == 1, "init sets next_id to 1");
    check(strstr(store.store_path, "vs_task_test") != NULL, "store path set");

    /* --- vs_task_create --- */
    printf("  create:\n");
    id = vs_task_create(&store, "Fix bug", "Fix the login bug", VS_TASK_PRIORITY_HIGH);
    check(id == 1, "first task id is 1");
    check(store.task_count == 1, "task count is 1");
    check(strcmp(store.tasks[0].title, "Fix bug") == 0, "title stored");
    check(store.tasks[0].status == VS_TASK_STATUS_PENDING, "default status is pending");
    check(store.tasks[0].priority == VS_TASK_PRIORITY_HIGH, "priority is high");

    id = vs_task_create(&store, "Add feature", NULL, VS_TASK_PRIORITY_LOW);
    check(id == 2, "second task id is 2");
    check(store.task_count == 2, "task count is 2");

    /* NULL title */
    id = vs_task_create(&store, NULL, "desc", VS_TASK_PRIORITY_MEDIUM);
    check(id == -1, "NULL title returns -1");
    id = vs_task_create(&store, "", "desc", VS_TASK_PRIORITY_MEDIUM);
    check(id == -1, "empty title returns -1");

    /* --- vs_task_get --- */
    printf("  get:\n");
    entry = vs_task_get(&store, 1);
    check(entry != NULL, "get task 1 returns non-NULL");
    check(strcmp(entry->title, "Fix bug") == 0, "title matches");
    entry = vs_task_get(&store, 999);
    check(entry == NULL, "get nonexistent returns NULL");
    entry = vs_task_get(&store, 0);
    check(entry == NULL, "get id 0 returns NULL");

    /* --- vs_task_update --- */
    printf("  update:\n");
    check(vs_task_update(&store, 1, "in_progress", NULL, NULL) == 0, "update status returns 0");
    check(store.tasks[0].status == VS_TASK_STATUS_IN_PROGRESS, "status updated to in_progress");

    check(vs_task_update(&store, 1, "completed", "Fixed bug", "Login bug fixed") == 0, "update all fields");
    check(store.tasks[0].status == VS_TASK_STATUS_COMPLETED, "status is completed");
    check(strcmp(store.tasks[0].title, "Fixed bug") == 0, "title updated");

    check(vs_task_update(&store, 999, "completed", NULL, NULL) == -1, "update nonexistent returns -1");
    check(vs_task_update(&store, 0, "completed", NULL, NULL) == -1, "update id 0 returns -1");

    /* --- vs_task_delete --- */
    printf("  delete:\n");
    check(vs_task_delete(&store, 2) == 0, "delete task 2 returns 0");
    check(store.task_count == 1, "task count is 1");
    check(vs_task_delete(&store, 999) == -1, "delete nonexistent returns -1");
    check(vs_task_delete(&store, 0) == -1, "delete id 0 returns -1");

    /* --- vs_task_list --- */
    printf("  list:\n");
    vs_task_create(&store, "Task A", "desc A", VS_TASK_PRIORITY_MEDIUM);
    vs_task_create(&store, "Task B", "desc B", VS_TASK_PRIORITY_HIGH);
    result = vs_task_list(&store, 10);
    check(result != NULL, "list returns non-NULL");
    check(strstr(result, "Task A") != NULL, "list contains Task A");
    check(strstr(result, "Task B") != NULL, "list contains Task B");
    free(result);

    /* Empty list */
    vs_task_clear(&store);
    result = vs_task_list(&store, 10);
    check(strstr(result, "No tasks") != NULL, "empty list message");
    free(result);

    /* --- vs_task_search --- */
    printf("  search:\n");
    vs_task_create(&store, "Fix login", "login issue", VS_TASK_PRIORITY_HIGH);
    vs_task_create(&store, "Add dashboard", "new UI", VS_TASK_PRIORITY_LOW);
    vs_task_create(&store, "Refactor auth", "cleanup code", VS_TASK_PRIORITY_MEDIUM);

    result = vs_task_search(&store, "login", -1, 10);
    check(result != NULL, "search returns non-NULL");
    check(strstr(result, "Fix login") != NULL, "search finds login");
    free(result);

    /* Case insensitive search */
    result = vs_task_search(&store, "LOGIN", -1, 10);
    check(strstr(result, "Fix login") != NULL, "case insensitive search works");
    free(result);

    /* Status filter */
    result = vs_task_search(&store, "", VS_TASK_STATUS_PENDING, 10);
    check(strstr(result, "Fix login") != NULL, "status filter works");
    free(result);

    /* No matches */
    result = vs_task_search(&store, "NONEXISTENT_XYZ", -1, 10);
    check(strstr(result, "No matching") != NULL, "no matches message");
    free(result);

    /* --- vs_task_clear --- */
    printf("  clear:\n");
    check(vs_task_clear(&store) == 0, "clear returns 0");
    check(store.task_count == 0, "task count is 0");
    check(store.next_id == 1, "next_id reset to 1");

    /* --- vs_task_persist and vs_task_load --- */
    printf("  persist/load:\n");
    vs_task_create(&store, "Persisted task", "should survive", VS_TASK_PRIORITY_HIGH);
    vs_task_create(&store, "Another task", "also survives", VS_TASK_PRIORITY_LOW);
    check(vs_task_persist(&store) == 0, "persist returns 0");

    vs_task_clear(&store);
    vs_task_load(&store);
    check(store.task_count == 2, "loaded 2 tasks");
    check(strcmp(store.tasks[0].title, "Persisted task") == 0, "first task title matches");
    check(store.tasks[0].priority == VS_TASK_PRIORITY_HIGH, "priority preserved");

    /* Cleanup */
    system("rm -f /tmp/vs_task_persist_test.jsonl /tmp/vs_task_test.jsonl");

    /* --- Capacity limit --- */
    printf("  capacity:\n");
    vs_task_clear(&store);
    int i;
    for (i = 0; i < VS_MAX_TASK_ENTRIES; i++) {
        vs_task_create(&store, "Task", "desc", VS_TASK_PRIORITY_MEDIUM);
    }
    check(store.task_count == VS_MAX_TASK_ENTRIES, "filled to capacity");
    id = vs_task_create(&store, "Extra", "desc", VS_TASK_PRIORITY_MEDIUM);
    check(id == -1, "exceeds capacity returns -1");

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
