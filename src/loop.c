/* SPDX-License-Identifier: Unlicense */
/* VibeSolaris Loop — Scheduled recurring prompt execution */
/*
 * Mirrors Claude Code's /loop command: run a prompt on a recurring schedule.
 * A background thread sleeps for the specified interval, then executes the
 * prompt via vs_chat and appends the result to the conversation history.
 *
 * Minimum interval is 60 seconds to prevent excessive API usage.
 * Maximum 16 concurrent loops.
 */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#define VS_LOOP_MIN_INTERVAL 60   /* minimum interval in seconds */
#define VS_LOOP_MAX_INTERVAL (3600*24) /* maximum interval: 24 hours */

/* ---- Internal helpers ---- */

static char *lp_dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* Background thread that monitors active loops */
static void *vs_loop_thread_func(void *arg)
{
    VSContext *ctx = (VSContext *)arg;
    if (!ctx) return NULL;

    while (!ctx->cancel_requested) {
        int any_active = 0;
        time_t now = time(NULL);

        for (int i = 0; i < ctx->loop_count; i++) {
            VSLoopEntry *loop = &ctx->loops[i];
            if (!loop->active) continue;
            any_active = 1;

            time_t elapsed = (time_t)(now - loop->last_run);
            if (loop->last_run > 0 && elapsed >= (time_t)loop->interval_seconds) {
                /* Execute the loop prompt */
                char *result = vs_chat(ctx, loop->prompt);
                if (result) {
                    /* Append result to history */
                    vs_history_add(ctx, "loop", result);
                    free(result);
                    loop->run_count++;
                    loop->last_run = (long)time(NULL);
                    loop->last_result = (loop->run_count % 4 == 0) ?
                        loop->last_result : loop->run_count - 1;
                }
            }
        }

        /* Sleep for 10 seconds between checks */
        for (int s = 0; s < 10 && !ctx->cancel_requested; s++) {
            sleep(1);
        }

        /* If no active loops, keep running but sleep longer */
        if (!any_active) {
            for (int s = 0; s < 30 && !ctx->cancel_requested; s++) {
                sleep(1);
            }
        }
    }
    return NULL;
}

/* ---- Public API ---- */

void vs_loop_init(VSContext *ctx)
{
    if (!ctx) return;
    memset(ctx->loops, 0, sizeof(ctx->loops));
    ctx->loop_count = 0;
    ctx->loop_thread_active = 0;
}

int vs_loop_add(VSContext *ctx, const char *prompt, int interval_seconds)
{
    if (!ctx || !prompt) return -1;

    /* Enforce minimum interval */
    if (interval_seconds < VS_LOOP_MIN_INTERVAL)
        interval_seconds = VS_LOOP_MIN_INTERVAL;
    if (interval_seconds > VS_LOOP_MAX_INTERVAL)
        interval_seconds = VS_LOOP_MAX_INTERVAL;

    /* Find a free slot */
    int slot = -1;
    for (int i = 0; i < VS_MAX_LOOPS; i++) {
        if (!ctx->loops[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;  /* No free slots */

    VSLoopEntry *loop = &ctx->loops[slot];
    memset(loop, 0, sizeof(*loop));
    loop->id = ctx->loop_next_id++;
    loop->active = 1;
    loop->interval_seconds = interval_seconds;
    snprintf(loop->prompt, sizeof(loop->prompt), "%s", prompt);
    loop->last_run = (long)time(NULL);
    loop->run_count = 0;
    ctx->loop_count++;

    /* Start the background thread if not already running */
    if (!ctx->loop_thread_active) {
        ctx->loop_thread_active = 1;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&ctx->loop_thread, &attr, vs_loop_thread_func, ctx);
        pthread_attr_destroy(&attr);
    }

    return loop->id;
}

int vs_loop_remove(VSContext *ctx, int id)
{
    if (!ctx) return -1;

    for (int i = 0; i < VS_MAX_LOOPS; i++) {
        if (ctx->loops[i].active && ctx->loops[i].id == id) {
            ctx->loops[i].active = 0;
            ctx->loop_count--;
            return 0;
        }
    }
    return -1;  /* Not found */
}

int vs_loop_stop_all(VSContext *ctx)
{
    if (!ctx) return -1;
    for (int i = 0; i < VS_MAX_LOOPS; i++) {
        if (ctx->loops[i].active) {
            ctx->loops[i].active = 0;
        }
    }
    ctx->loop_count = 0;
    return 0;
}

char *vs_loop_list(VSContext *ctx, int max_lines)
{
    if (!ctx) return lp_dupstr("No loop context initialized");

    char *buf = (char *)malloc(4096);
    if (!buf) return NULL;

    int pos = 0;
    int lines = 0;
    int active_count = 0;

    for (int i = 0; i < VS_MAX_LOOPS; i++) {
        if (ctx->loops[i].active) active_count++;
    }

    pos += snprintf(buf + pos, 4096 - pos,
                    "Loops: %d active, %d total runs\n",
                    active_count, ctx->loop_count);
    lines++;

    if (active_count == 0) {
        pos += snprintf(buf + pos, 4096 - pos, "No active loops.\n");
        return buf;
    }

    for (int i = 0; i < VS_MAX_LOOPS && lines < max_lines; i++) {
        VSLoopEntry *loop = &ctx->loops[i];
        if (!loop->active) continue;

        pos += snprintf(buf + pos, 4096 - pos,
                        "  [ID:%d] every %ds: \"%s\" (runs: %d)\n",
                        loop->id, loop->interval_seconds,
                        loop->prompt, loop->run_count);
        if (pos >= 4080) break;
        lines++;
    }

    return buf;
}

void vs_loop_shutdown(VSContext *ctx)
{
    if (!ctx) return;
    vs_loop_stop_all(ctx);
    ctx->loop_thread_active = 0;
}
