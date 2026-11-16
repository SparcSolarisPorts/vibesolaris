/* SPDX-License-Identifier: Unlicense */
/* VibeSolaris Rewind — Conversation-level undo with checkpoint rollback */
/*
 * Mirrors Claude Code's /rewind command: removes the last N messages from
 * the conversation history and optionally restores file state via checkpoint
 * rollback. Supports named markers for rewinding to specific points.
 *
 * Works with the existing checkpoint system (src/checkpoint.c) for file
 * state restoration. History truncation is immediate and irreversible.
 */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VS_REWIND_MARKER_MAX 64
#define VS_REWIND_MARKER_LABEL_MAX 128
#define VS_REWIND_HISTORY_SNAPSHOT_MAX 48

/* ---- Internal helpers ---- */

static char *rw_dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* ---- Public API ---- */

void vs_rewind_init(VSRewindStore *store)
{
    if (!store) return;
    memset(store, 0, sizeof(*store));
}

int vs_rewind_mark(VSRewindStore *store, const char *label)
{
    if (!store) return -1;

    /* Evict oldest marker if at capacity */
    if (store->marker_count >= VS_REWIND_MARKER_MAX) {
        memmove(&store->markers[0], &store->markers[1],
                (sizeof(VSRewindMarker) * (VS_REWIND_MARKER_MAX - 1)));
        store->marker_count--;
    }

    VSRewindMarker *m = &store->markers[store->marker_count];
    if (label && strlen(label) > 0) {
        snprintf(m->label, sizeof(m->label), "%s", label);
    } else {
        snprintf(m->label, sizeof(m->label), "mark_%d", store->marker_count);
    }
    m->history_count = store->last_history_count;
    m->timestamp = (long)time(NULL);
    store->marker_count++;
    return store->marker_count - 1;
}

int vs_rewind_undo(VSRewindStore *store, VSContext *ctx, int n)
{
    if (!store || !ctx) return -1;
    if (n <= 0) n = 1;

    int original_count = ctx->history_count;
    int to_remove = n;
    if (to_remove > ctx->history_count) to_remove = ctx->history_count;
    if (to_remove <= 0) return 0;

    /* Free and remove the last N history messages */
    for (int i = 0; i < to_remove; i++) {
        int idx = ctx->history_count - 1;
        if (idx < 0) break;
        if (ctx->history[idx].content) {
            free(ctx->history[idx].content);
            ctx->history[idx].content = NULL;
        }
        if (ctx->history_bytes >= ctx->history[idx].bytes)
            ctx->history_bytes -= ctx->history[idx].bytes;
        else
            ctx->history_bytes = 0;
        memset(&ctx->history[idx], 0, sizeof(ctx->history[idx]));
        ctx->history_count--;
    }

    store->last_history_count = ctx->history_count;
    store->undo_count++;

    /* Track what was removed */
    if (store->undo_log_count < VS_REWIND_HISTORY_SNAPSHOT_MAX) {
        VSRewindUndoLog *log = &store->undo_log[store->undo_log_count];
        log->messages_removed = to_remove;
        log->timestamp = (long)time(NULL);
        snprintf(log->description, sizeof(log->description),
                 "rewind %d messages from %d to %d",
                 to_remove, original_count, ctx->history_count);
        store->undo_log_count++;
    }

    return to_remove;
}

int vs_rewind_to_marker(VSRewindStore *store, VSContext *ctx, const char *label)
{
    if (!store || !ctx || !label) return -1;

    /* Find the marker */
    int marker_idx = -1;
    for (int i = 0; i < store->marker_count; i++) {
        if (strcmp(store->markers[i].label, label) == 0) {
            marker_idx = i;
            break;
        }
    }
    if (marker_idx < 0) return -1;

    VSRewindMarker *m = &store->markers[marker_idx];
    int target_count = m->history_count;

    /* Remove messages from the end until we reach the target count */
    int to_remove = ctx->history_count - target_count;
    if (to_remove <= 0) return 0;

    int original_count = ctx->history_count;
    for (int i = 0; i < to_remove; i++) {
        int idx = ctx->history_count - 1;
        if (idx < 0) break;
        if (ctx->history[idx].content) {
            free(ctx->history[idx].content);
            ctx->history[idx].content = NULL;
        }
        if (ctx->history_bytes >= ctx->history[idx].bytes)
            ctx->history_bytes -= ctx->history[idx].bytes;
        else
            ctx->history_bytes = 0;
        memset(&ctx->history[idx], 0, sizeof(ctx->history[idx]));
        ctx->history_count--;
    }

    store->last_history_count = ctx->history_count;
    store->undo_count++;

    /* Remove the marker we rewound to */
    if (marker_idx < store->marker_count - 1) {
        memmove(&store->markers[marker_idx], &store->markers[marker_idx + 1],
                (sizeof(VSRewindMarker) * (store->marker_count - marker_idx - 1)));
    }
    store->marker_count--;

    /* Log the rewind */
    if (store->undo_log_count < VS_REWIND_HISTORY_SNAPSHOT_MAX) {
        VSRewindUndoLog *log = &store->undo_log[store->undo_log_count];
        log->messages_removed = to_remove;
        log->timestamp = (long)time(NULL);
        snprintf(log->description, sizeof(log->description),
                 "rewind to marker '%s' (%d -> %d messages)",
                 label, original_count, ctx->history_count);
        store->undo_log_count++;
    }

    return to_remove;
}

int vs_rewind_checkpoint_rollback(VSRewindStore *store, VSCheckpointStore *cp_store)
{
    if (!store || !cp_store) return -1;
    /* Delegate to checkpoint system for file state restoration */
    return vs_checkpoint_rollback(cp_store, NULL);
}

char *vs_rewind_list(VSRewindStore *store, int max_lines)
{
    if (!store) return rw_dupstr("No rewind store initialized");

    char *buf = (char *)malloc(4096);
    if (!buf) return NULL;

    int pos = 0;
    int lines = 0;

    pos += snprintf(buf + pos, 4096 - pos,
                    "Rewind state: %d markers, %d undos, history at %d messages\n",
                    store->marker_count, store->undo_count, store->last_history_count);
    lines++;

    if (store->marker_count > 0) {
        pos += snprintf(buf + pos, 4096 - pos, "Markers:\n");
        lines++;
        for (int i = 0; i < store->marker_count && lines < max_lines; i++) {
            VSRewindMarker *m = &store->markers[i];
            char time_str[64];
            time_t t = (time_t)m->timestamp;
            struct tm tm_buf;
            if (localtime_r(&t, &tm_buf)) {
                strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);
            } else {
                snprintf(time_str, sizeof(time_str), "%ld", (long)t);
            }
            pos += snprintf(buf + pos, 4096 - pos,
                            "  [%s] %s (history: %d messages)\n",
                            m->label, time_str, m->history_count);
            if (pos >= 4080) break;
            lines++;
        }
    }

    if (store->undo_log_count > 0 && lines < max_lines) {
        pos += snprintf(buf + pos, 4096 - pos, "\nUndo log:\n");
        lines++;
        int start = store->undo_log_count > 5 ? store->undo_log_count - 5 : 0;
        for (int i = start; i < store->undo_log_count && lines < max_lines; i++) {
            VSRewindUndoLog *log = &store->undo_log[i];
            pos += snprintf(buf + pos, 4096 - pos,
                            "  - %s (%d messages removed)\n",
                            log->description, log->messages_removed);
            if (pos >= 4080) break;
            lines++;
        }
    }

    return buf;
}

void vs_rewind_clear(VSRewindStore *store)
{
    if (!store) return;
    store->marker_count = 0;
    store->undo_count = 0;
    store->undo_log_count = 0;
    store->last_history_count = 0;
}
