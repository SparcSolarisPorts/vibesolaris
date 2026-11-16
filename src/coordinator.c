/* SPDX-License-Identifier: Unlicense */
/* Agent coordinator: parallel sub-agent scheduling with worktree isolation.

   Mirrors Claude Code's coordinator pattern: each spawned agent runs in its own
   thread with an optional isolated worktree directory.  Results are collected
   when all threads complete.  Cancellation propagates to all children.

   Portable C: uses pthreads, POSIX fork/exec for worktree git commands,
   and the existing vs_subagent_run for the agent loop. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

static char *dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* ---- Worktree helpers ---- */

int vs_coordinator_worktree_create(VSCoordinator *coord, int agent_id, const char *project_root)
{
    char cmd[2048];
    char path[VS_MAX_PATH];
    int ok = 0;

    (void)coord;
    vs_coordinator_worktree_path(coord, agent_id, path, sizeof(path));

    /* Try git worktree first; fall back to plain copy if git is unavailable. */
    snprintf(cmd, sizeof(cmd),
        "git -C '%s' worktree add '%s' --detach 2>/dev/null",
        project_root ? project_root : ".", path);
    if (system(cmd) == 0) {
        ok = 1;
    } else {
        /* Fallback: mkdir + copy tracked files */
        snprintf(cmd, sizeof(cmd),
            "mkdir -p '%s' && git -C '%s' ls-files -z 2>/dev/null | "
            "xargs -0 -I{} cp -- '{}' '%s/{}' 2>/dev/null",
            path, project_root ? project_root : ".", path);
        if (system(cmd) == 0) {
            ok = 1;
        }
    }
    return ok ? 0 : -1;
}

int vs_coordinator_worktree_remove(const char *worktree)
{
    if (!worktree || !*worktree) return -1;
    /* Try git worktree remove first */
    {
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "git worktree remove '%s' --force 2>/dev/null", worktree);
        if (system(cmd) == 0) return 0;
    }
    /* Fallback: rmdir */
    {
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", worktree);
        return (system(cmd) == 0) ? 0 : -1;
    }
}

char *vs_coordinator_worktree_path(VSCoordinator *coord, int agent_id, char *out, size_t cap)
{
    (void)coord;
    snprintf(out, cap, "%s/%s_agent_%d",
        VS_COORDINATOR_WORKTREE_PREFIX,
        vs_command_shell_name() ? "" : "", agent_id);
    return out;
}

/* ---- Standalone worktree functions (for non-coordinator use) ---- */

int vs_worktree_enter(const char *project_root, char *path_out, size_t cap)
{
    char cmd[2048];
    char path[VS_MAX_PATH];
    int ok = 0;

    const char *root = project_root ? project_root : ".";
    snprintf(path, sizeof(path), "%s/%s_main", root, VS_COORDINATOR_WORKTREE_PREFIX);

    /* Try git worktree first */
    snprintf(cmd, sizeof(cmd),
        "git -C '%s' worktree add '%s' --detach 2>/dev/null",
        root, path);
    if (system(cmd) == 0) {
        ok = 1;
    } else {
        /* Fallback: mkdir + copy tracked files */
        snprintf(cmd, sizeof(cmd),
            "mkdir -p '%s' && git -C '%s' ls-files -z 2>/dev/null | "
            "xargs -0 -I{} cp -- '{}' '%s/{}' 2>/dev/null",
            path, root, path);
        if (system(cmd) == 0) {
            ok = 1;
        }
    }

    if (ok && path_out && cap > 0) {
        strncpy(path_out, path, cap - 1);
        path_out[cap - 1] = 0;
    }
    return ok ? 0 : -1;
}

int vs_worktree_exit(const char *worktree)
{
    if (!worktree || !*worktree) return -1;
    /* Try git worktree remove first */
    {
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "git worktree remove '%s' --force 2>/dev/null", worktree);
        if (system(cmd) == 0) return 0;
    }
    /* Fallback: rmdir */
    {
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", worktree);
        return (system(cmd) == 0) ? 0 : -1;
    }
}

/* ---- Thread entry for sub-agent execution ---- */

typedef struct {
    VSCoordinator *coord;
    VSContext *parent;
    int agent_id;
    int depth;
    char *task;
} CoordinatorThreadArg;

static void *coordinator_thread_entry(void *arg)
{
    CoordinatorThreadArg *ta = (CoordinatorThreadArg *)arg;
    VSCoordinator *coord = ta->coord;
    VSContext *parent = ta->parent;
    int agent_id = ta->agent_id;
    char *task = ta->task;
    char *result;
    VSCoordinatorAgent *agent;

    /* Run the sub-agent using the existing serial runner.
       The coordinator wraps it in a thread for parallelism. */
    result = vs_subagent_run(parent, task);

    pthread_mutex_lock(&coord->lock);
    agent = &coord->agents[agent_id];
    agent->status = coord->cancel_all ? VS_AGENT_STATUS_CANCELLED : VS_AGENT_STATUS_COMPLETED;
    agent->finished_at = time(NULL);
    agent->thread_active = 0;

    if (result) {
        size_t len = strlen(result);
        if (len >= sizeof(agent->result)) len = sizeof(agent->result) - 1;
        memcpy(agent->result, result, len);
        agent->result[len] = 0;
        free(result);
    } else {
        agent->status = VS_AGENT_STATUS_FAILED;
        strcpy(agent->result, "(no result)");
    }
    pthread_mutex_unlock(&coord->lock);

    free(task);
    free(ta);
    return NULL;
}

/* ---- Public API ---- */

void vs_coordinator_init(VSCoordinator *coord)
{
    memset(coord, 0, sizeof(*coord));
    coord->next_id = 1;
    pthread_mutex_init(&coord->lock, NULL);
}

void vs_coordinator_shutdown(VSCoordinator *coord)
{
    vs_coordinator_cancel_all(coord);
    vs_coordinator_wait_all(coord);
    int i;
    for (i = 0; i < coord->agent_count; i++) {
        if (coord->agents[i].worktree[0]) {
            vs_coordinator_worktree_remove(coord->agents[i].worktree);
        }
    }
    pthread_mutex_destroy(&coord->lock);
    memset(coord, 0, sizeof(*coord));
}

int vs_coordinator_spawn(VSCoordinator *coord, VSContext *parent, const char *task, int depth)
{
    int id;
    VSCoordinatorAgent *agent;
    CoordinatorThreadArg *ta;
    pthread_attr_t attr;

    if (!task || !*task) return -1;
    if (depth > VS_MAX_SUBAGENT_DEPTH) return -2;

    pthread_mutex_lock(&coord->lock);
    if (coord->agent_count >= VS_MAX_COORDINATOR_SLOTS) {
        pthread_mutex_unlock(&coord->lock);
        return -3;
    }
    id = coord->next_id++;
    int slot = coord->agent_count++;
    agent = &coord->agents[slot];
    pthread_mutex_unlock(&coord->lock);

    memset(agent, 0, sizeof(*agent));
    agent->id = id;
    agent->parent_id = parent ? 0 : -1;
    agent->depth = depth;
    agent->status = VS_AGENT_STATUS_RUNNING;
    agent->started_at = time(NULL);
    agent->thread_active = 1;
    strncpy(agent->task, task, sizeof(agent->task) - 1);

    ta = (CoordinatorThreadArg *)malloc(sizeof(*ta));
    if (!ta) {
        agent->status = VS_AGENT_STATUS_FAILED;
        agent->thread_active = 0;
        strcpy(agent->result, "out of memory");
        return -1;
    }
    ta->coord = coord;
    ta->parent = parent;
    ta->agent_id = slot;
    ta->depth = depth;
    ta->task = dupstr(task);

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    if (pthread_create(&agent->thread, &attr, coordinator_thread_entry, ta) != 0) {
        agent->status = VS_AGENT_STATUS_FAILED;
        agent->thread_active = 0;
        strcpy(agent->result, "thread creation failed");
        free(ta->task);
        free(ta);
        pthread_attr_destroy(&attr);
        return -1;
    }
    pthread_attr_destroy(&attr);
    return id;
}

void vs_coordinator_cancel_all(VSCoordinator *coord)
{
    pthread_mutex_lock(&coord->lock);
    coord->cancel_all = 1;
    pthread_mutex_unlock(&coord->lock);
}

void vs_coordinator_wait_all(VSCoordinator *coord)
{
    int i;
    pthread_mutex_lock(&coord->lock);
    for (i = 0; i < coord->agent_count; i++) {
        if (coord->agents[i].thread_active) {
            pthread_mutex_unlock(&coord->lock);
            pthread_join(coord->agents[i].thread, NULL);
            pthread_mutex_lock(&coord->lock);
        }
    }
    pthread_mutex_unlock(&coord->lock);
}

int vs_coordinator_active_count(const VSCoordinator *coord)
{
    int i, count = 0;
    for (i = 0; i < coord->agent_count; i++) {
        if (coord->agents[i].status == VS_AGENT_STATUS_RUNNING) count++;
    }
    return count;
}

VSCoordinatorAgent *vs_coordinator_agent_by_id(VSCoordinator *coord, int id)
{
    int i;
    for (i = 0; i < coord->agent_count; i++) {
        if (coord->agents[i].id == id) return &coord->agents[i];
    }
    return NULL;
}
