/* SPDX-License-Identifier: Unlicense */
/* TodoWrite: visible session task list.

   Mirrors Claude Code's TodoWrite tool: the agent maintains a visible
   checklist of session tasks.  The TUI renders the list after each turn
   so the user can track progress.  Items support four statuses:
   pending, in_progress, completed, cancelled. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void vs_todo_init(VSTodoList *todo)
{
    if (!todo) return;
    memset(todo, 0, sizeof(*todo));
}

int vs_todo_set_all(VSTodoList *todo, const char **items, int count)
{
    int i;
    if (!todo || !items || count < 0) return -1;
    if (count > VS_MAX_TODO_ITEMS) count = VS_MAX_TODO_ITEMS;
    memset(todo, 0, sizeof(*todo));
    for (i = 0; i < count; i++) {
        strncpy(todo->items[i].text, items[i], VS_TODO_ITEM_MAX - 1);
        todo->items[i].text[VS_TODO_ITEM_MAX - 1] = 0;
        todo->items[i].status = VS_TODO_STATUS_PENDING;
        todo->item_count++;
    }
    todo->active = 1;
    return 0;
}

int vs_todo_add(VSTodoList *todo, const char *text)
{
    if (!todo || !text || !*text) return -1;
    if (todo->item_count >= VS_MAX_TODO_ITEMS) return -1;
    VSTodoItem *item = &todo->items[todo->item_count];
    strncpy(item->text, text, VS_TODO_ITEM_MAX - 1);
    item->text[VS_TODO_ITEM_MAX - 1] = 0;
    item->status = VS_TODO_STATUS_PENDING;
    todo->item_count++;
    todo->active = 1;
    return 0;
}

int vs_todo_update_status(VSTodoList *todo, int index, VSTodoStatus status)
{
    if (!todo || index < 0 || index >= todo->item_count) return -1;
    if (status < VS_TODO_STATUS_PENDING || status > VS_TODO_STATUS_CANCELLED) return -1;
    todo->items[index].status = status;
    return 0;
}

int vs_todo_clear(VSTodoList *todo)
{
    if (!todo) return -1;
    memset(todo, 0, sizeof(*todo));
    return 0;
}

char *vs_todo_render(const VSTodoList *todo)
{
    char *out;
    size_t cap = 1024;
    size_t n = 0;
    int i;
    int completed = 0;
    int in_progress = 0;
    int pending = 0;

    if (!todo || !todo->active || todo->item_count <= 0) {
        return NULL;
    }

    for (i = 0; i < todo->item_count; i++) {
        switch (todo->items[i].status) {
            case VS_TODO_STATUS_COMPLETED: completed++; break;
            case VS_TODO_STATUS_IN_PROGRESS: in_progress++; break;
            case VS_TODO_STATUS_CANCELLED: break;
            default: pending++; break;
        }
    }

    out = (char *)malloc(cap);
    if (!out) return NULL;

    n = (size_t)snprintf(out, cap,
        "TODO (%d/%d done):\n", completed, todo->item_count);

    for (i = 0; i < todo->item_count && n < cap - 128; i++) {
        const VSTodoItem *item = &todo->items[i];
        const char *marker;
        switch (item->status) {
            case VS_TODO_STATUS_COMPLETED: marker = "[x]"; break;
            case VS_TODO_STATUS_IN_PROGRESS: marker = "[*]"; break;
            case VS_TODO_STATUS_CANCELLED: marker = "[-]"; break;
            default: marker = "[ ]"; break;
        }
        n += (size_t)snprintf(out + n, cap - n, "  %s %s\n", marker, item->text);
    }

    if (n >= cap - 1) n = cap - 1;
    out[n] = 0;
    return out;
}
