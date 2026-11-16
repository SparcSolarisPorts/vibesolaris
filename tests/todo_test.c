/* SPDX-License-Identifier: Unlicense */
/* Unit tests for TodoWrite: visible session task list (src/todo.c). */
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
    VSTodoList todo;
    char *render;
    int rc;

    printf("TodoWrite tests:\n");

    /* --- vs_todo_init --- */
    printf("  init:\n");
    memset(&todo, 0xFF, sizeof(todo));
    vs_todo_init(&todo);
    check(todo.item_count == 0, "init clears item_count");
    check(todo.active == 0, "init clears active");
    check(todo.items[0].text[0] == 0, "init clears item text");
    check(todo.items[0].status == 0, "init clears item status");

    /* --- vs_todo_set_all --- */
    printf("  set_all:\n");
    const char *items[] = {"Task A", "Task B", "Task C"};
    rc = vs_todo_set_all(&todo, items, 3);
    check(rc == 0, "set_all returns 0");
    check(todo.item_count == 3, "set_all sets item_count");
    check(todo.active == 1, "set_all sets active");
    check(strcmp(todo.items[0].text, "Task A") == 0, "set_all item 0 text");
    check(strcmp(todo.items[1].text, "Task B") == 0, "set_all item 1 text");
    check(strcmp(todo.items[2].text, "Task C") == 0, "set_all item 2 text");
    check(todo.items[0].status == VS_TODO_STATUS_PENDING, "set_all item 0 pending");
    check(todo.items[1].status == VS_TODO_STATUS_PENDING, "set_all item 1 pending");

    /* set_all with zero count */
    rc = vs_todo_set_all(&todo, items, 0);
    check(rc == 0, "set_all with 0 items returns 0");
    check(todo.item_count == 0, "set_all with 0 items clears list");

    /* set_all with NULL */
    rc = vs_todo_set_all(NULL, items, 1);
    check(rc == -1, "set_all with NULL todo returns -1");

    rc = vs_todo_set_all(&todo, NULL, 1);
    check(rc == -1, "set_all with NULL items returns -1");

    rc = vs_todo_set_all(&todo, items, -1);
    check(rc == -1, "set_all with negative count returns -1");

    /* set_all overwrites previous */
    const char *items2[] = {"New task"};
    vs_todo_set_all(&todo, items2, 1);
    check(todo.item_count == 1, "set_all overwrites previous items");
    check(strcmp(todo.items[0].text, "New task") == 0, "set_all overwrite text");

    /* --- vs_todo_add --- */
    printf("  add:\n");
    vs_todo_init(&todo);
    rc = vs_todo_add(&todo, "First");
    check(rc == 0, "add returns 0");
    check(todo.item_count == 1, "add increments count");
    check(strcmp(todo.items[0].text, "First") == 0, "add sets text");
    check(todo.items[0].status == VS_TODO_STATUS_PENDING, "add sets pending");

    rc = vs_todo_add(&todo, "Second");
    check(rc == 0, "add second item");
    check(todo.item_count == 2, "add second count");

    /* add with NULL */
    rc = vs_todo_add(NULL, "test");
    check(rc == -1, "add NULL todo returns -1");

    rc = vs_todo_add(&todo, NULL);
    check(rc == -1, "add NULL text returns -1");

    rc = vs_todo_add(&todo, "");
    check(rc == -1, "add empty text returns -1");

    /* add at capacity */
    vs_todo_init(&todo);
    int i;
    for (i = 0; i < VS_MAX_TODO_ITEMS; i++) {
        vs_todo_add(&todo, "item");
    }
    check(todo.item_count == VS_MAX_TODO_ITEMS, "add fills to capacity");
    rc = vs_todo_add(&todo, "overflow");
    check(rc == -1, "add beyond capacity returns -1");
    check(todo.item_count == VS_MAX_TODO_ITEMS, "add beyond capacity no change");

    /* --- vs_todo_update_status --- */
    printf("  update_status:\n");
    vs_todo_set_all(&todo, items, 3);
    rc = vs_todo_update_status(&todo, 0, VS_TODO_STATUS_COMPLETED);
    check(rc == 0, "update_status returns 0");
    check(todo.items[0].status == VS_TODO_STATUS_COMPLETED, "update to completed");

    rc = vs_todo_update_status(&todo, 1, VS_TODO_STATUS_IN_PROGRESS);
    check(todo.items[1].status == VS_TODO_STATUS_IN_PROGRESS, "update to in_progress");

    rc = vs_todo_update_status(&todo, 2, VS_TODO_STATUS_CANCELLED);
    check(todo.items[2].status == VS_TODO_STATUS_CANCELLED, "update to cancelled");

    /* Boundary checks */
    rc = vs_todo_update_status(NULL, 0, VS_TODO_STATUS_COMPLETED);
    check(rc == -1, "update_status NULL todo returns -1");

    rc = vs_todo_update_status(&todo, -1, VS_TODO_STATUS_COMPLETED);
    check(rc == -1, "update_status negative index returns -1");

    rc = vs_todo_update_status(&todo, 99, VS_TODO_STATUS_COMPLETED);
    check(rc == -1, "update_status out of range returns -1");

    rc = vs_todo_update_status(&todo, 0, (VSTodoStatus)99);
    check(rc == -1, "update_status invalid status returns -1");

    /* --- vs_todo_clear --- */
    printf("  clear:\n");
    vs_todo_set_all(&todo, items, 3);
    rc = vs_todo_clear(&todo);
    check(rc == 0, "clear returns 0");
    check(todo.item_count == 0, "clear resets count");
    check(todo.active == 0, "clear resets active");

    rc = vs_todo_clear(NULL);
    check(rc == -1, "clear NULL returns -1");

    /* --- vs_todo_render --- */
    printf("  render:\n");

    /* Render returns NULL when inactive */
    vs_todo_init(&todo);
    render = vs_todo_render(&todo);
    check(render == NULL, "render inactive returns NULL");

    /* Render returns NULL when empty */
    todo.active = 1;
    todo.item_count = 0;
    render = vs_todo_render(&todo);
    check(render == NULL, "render empty returns NULL");

    /* Render NULL input */
    render = vs_todo_render(NULL);
    check(render == NULL, "render NULL returns NULL");

    /* Render with items */
    vs_todo_set_all(&todo, items, 3);
    vs_todo_update_status(&todo, 0, VS_TODO_STATUS_COMPLETED);
    vs_todo_update_status(&todo, 1, VS_TODO_STATUS_IN_PROGRESS);
    /* item 2 stays pending */

    render = vs_todo_render(&todo);
    check(render != NULL, "render returns non-NULL");
    check(strstr(render, "TODO (1/3 done):") != NULL, "render header shows completed count");
    check(strstr(render, "[x] Task A") != NULL, "render completed marker [x]");
    check(strstr(render, "[*] Task B") != NULL, "render in_progress marker [*]");
    check(strstr(render, "[ ] Task C") != NULL, "render pending marker [ ]");
    free(render);

    /* Render cancelled */
    vs_todo_update_status(&todo, 2, VS_TODO_STATUS_CANCELLED);
    render = vs_todo_render(&todo);
    check(strstr(render, "[-] Task C") != NULL, "render cancelled marker [-]");
    free(render);

    /* Render single item */
    const char *single[] = {"Only one"};
    vs_todo_set_all(&todo, single, 1);
    render = vs_todo_render(&todo);
    check(render != NULL, "render single item");
    check(strstr(render, "Only one") != NULL, "render single item text");
    free(render);

    /* --- Text truncation --- */
    printf("  truncation:\n");
    vs_todo_init(&todo);
    {
        char long_text[VS_TODO_ITEM_MAX + 100];
        memset(long_text, 'A', sizeof(long_text) - 1);
        long_text[sizeof(long_text) - 1] = 0;
        rc = vs_todo_add(&todo, long_text);
        check(rc == 0, "add long text");
        check(strlen(todo.items[0].text) < VS_TODO_ITEM_MAX, "add truncates long text");
        check(todo.items[0].text[VS_TODO_ITEM_MAX - 1] == 0, "add null-terminates truncated text");
    }

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
