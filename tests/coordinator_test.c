/* SPDX-License-Identifier: Unlicense */
/* Unit tests for agent coordinator (src/coordinator.c).
 *
 * These tests exercise the coordinator API without requiring a full
 * subagent runtime.  We stub vs_subagent_run so threads complete quickly. */
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

/* Stub: vs_subagent_run returns a quick result so threads finish fast */
char *vs_subagent_run(VSContext *ctx, const char *task) {
    (void)ctx;
    char *result = (char *)malloc(256);
    if (!result) return NULL;
    snprintf(result, 256, "subagent_result_for:%s", task ? task : "(null)");
    return result;
}

const char *vs_command_shell_name(void) {
    return "/bin/sh";
}

int main(void) {
    VSCoordinator coord;
    VSContext ctx;
    char path_buf[VS_MAX_PATH];

    printf("Coordinator tests:\n");

    memset(&ctx, 0, sizeof(ctx));

    /* --- vs_coordinator_init --- */
    printf("  init:\n");
    vs_coordinator_init(&coord);
    check(coord.next_id == 1, "init sets next_id to 1");
    check(coord.agent_count == 0, "init clears agent_count");
    check(coord.cancel_all == 0, "init clears cancel_all");

    /* --- vs_coordinator_spawn --- */
    printf("  spawn:\n");

    int id = vs_coordinator_spawn(&coord, &ctx, "Test task 1", 0);
    check(id > 0, "spawn returns positive id");
    check(coord.agent_count == 1, "spawn increments agent_count");

    id = vs_coordinator_spawn(&coord, &ctx, "Test task 2", 0);
    check(id > 0, "spawn second agent");
    check(coord.agent_count == 2, "spawn second increments count");

    /* Spawn with NULL task */
    id = vs_coordinator_spawn(&coord, &ctx, NULL, 0);
    check(id < 0, "spawn NULL task returns error");

    /* Spawn with empty task */
    id = vs_coordinator_spawn(&coord, &ctx, "", 0);
    check(id < 0, "spawn empty task returns error");

    /* Spawn with too deep nesting */
    id = vs_coordinator_spawn(&coord, &ctx, "deep", VS_MAX_SUBAGENT_DEPTH + 1);
    check(id < 0, "spawn too deep returns error");

    /* --- vs_coordinator_active_count --- */
    printf("  active_count:\n");

    /* Give threads a moment to start */
    usleep(10000); /* 10ms */
    int active = vs_coordinator_active_count(&coord);
    check(active >= 0, "active_count returns non-negative");

    /* --- vs_coordinator_wait_all --- */
    printf("  wait_all:\n");

    vs_coordinator_wait_all(&coord);

    /* After wait, all should be completed */
    check(coord.agents[0].status == VS_AGENT_STATUS_COMPLETED, "agent 0 completed");
    check(coord.agents[1].status == VS_AGENT_STATUS_COMPLETED, "agent 1 completed");
    check(coord.agents[0].thread_active == 0, "agent 0 thread inactive");
    check(coord.agents[0].result[0] != 0, "agent 0 has result");
    check(strstr(coord.agents[0].result, "subagent_result") != NULL, "agent 0 result content");

    active = vs_coordinator_active_count(&coord);
    check(active == 0, "active_count is 0 after wait");

    /* --- vs_coordinator_agent_by_id --- */
    printf("  agent_by_id:\n");

    VSCoordinatorAgent *agent = vs_coordinator_agent_by_id(&coord, coord.agents[0].id);
    check(agent != NULL, "find agent by id");
    check(agent->status == VS_AGENT_STATUS_COMPLETED, "found agent status");

    agent = vs_coordinator_agent_by_id(&coord, 9999);
    check(agent == NULL, "find nonexistent id returns NULL");

    /* --- vs_coordinator_cancel_all --- */
    printf("  cancel_all:\n");

    vs_coordinator_cancel_all(&coord);
    check(coord.cancel_all == 1, "cancel_all sets flag");

    /* --- vs_coordinator_worktree_path --- */
    printf("  worktree_path:\n");

    char *path = vs_coordinator_worktree_path(&coord, 42, path_buf, sizeof(path_buf));
    check(path != NULL, "worktree_path returns non-NULL");
    check(strstr(path, "agent_42") != NULL, "worktree_path includes agent id");

    /* --- vs_coordinator_shutdown --- */
    printf("  shutdown:\n");

    vs_coordinator_shutdown(&coord);
    check(coord.agent_count == 0, "shutdown clears agent_count");
    check(coord.cancel_all == 0, "shutdown clears cancel_all");

    /* --- Capacity test --- */
    printf("  capacity:\n");

    vs_coordinator_init(&coord);
    int i;
    int spawned = 0;
    for (i = 0; i < VS_MAX_COORDINATOR_SLOTS + 5; i++) {
        id = vs_coordinator_spawn(&coord, &ctx, "capacity test", 0);
        if (id > 0) spawned++;
    }
    check(spawned == VS_MAX_COORDINATOR_SLOTS, "spawn respects max slots");

    vs_coordinator_wait_all(&coord);
    vs_coordinator_shutdown(&coord);

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
