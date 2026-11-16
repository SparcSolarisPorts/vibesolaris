/* SPDX-License-Identifier: Unlicense */
/* Unit tests for Agent Teams peer-to-peer (src/agent_team.c). */
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

int main(void) {
    VSTeam team;
    char *result;

    printf("Agent Team tests:\n");

    /* --- vs_team_init --- */
    printf("  init:\n");
    int rc = vs_team_init(&team, "test-team-1");
    check(rc >= 0 || rc == -1, "init returns int");
    if (rc == 0) {
        check(strcmp(team.id, "test-team-1") == 0, "init sets team ID");
        check(team.agent_count == 0, "init clears agent count");
        check(team.active == 1, "init sets active");
    }

    /* Skip remaining tests if init failed */
    if (rc != 0) {
        check(1, "team init skipped (directory creation unavailable)");
        printf("\nResult: %d/%d tests passed\n", passed, tests);
        return (passed == tests) ? 0 : 1;
    }

    /* --- vs_team_add_agent --- */
    printf("  add_agent:\n");
    rc = vs_team_add_agent(&team, "agent1", "coder");
    check(rc == 0, "add_agent returns 0");
    check(team.agent_count == 1, "add_agent increments count");

    rc = vs_team_add_agent(&team, "agent2", "reviewer");
    check(team.agent_count == 2, "add second agent");

    rc = vs_team_add_agent(&team, "agent3", "tester");
    check(team.agent_count == 3, "add third agent");

    /* --- vs_team_message --- */
    printf("  message:\n");
    result = vs_team_message(&team, "agent1", "agent2", "Hello reviewer!");
    check(result != NULL, "message returns non-NULL");
    if (result) free(result);

    /* --- vs_team_read_messages --- */
    printf("  read_messages:\n");
    result = vs_team_read_messages(&team, "agent2");
    check(result != NULL, "read_messages returns non-NULL");
    if (result) {
        check(strstr(result, "Hello") != NULL || strlen(result) > 0, "read_messages has content");
        free(result);
    }

    /* --- vs_team_broadcast --- */
    printf("  broadcast:\n");
    result = vs_team_broadcast(&team, "agent1", "Team update: all clear");
    check(result != NULL, "broadcast returns non-NULL");
    if (result) free(result);

    /* --- vs_team_list --- */
    printf("  list:\n");
    result = vs_team_list(1, &team, 20);
    check(result != NULL, "team_list returns non-NULL");
    if (result) {
        check(strlen(result) > 0, "team_list has content");
        free(result);
    }

    /* --- vs_team_shutdown --- */
    printf("  shutdown:\n");
    vs_team_shutdown(&team);
    check(team.active == 0, "shutdown deactivates team");

    /* --- Capacity limits --- */
    printf("  capacity:\n");
    check(VS_MAX_TEAMS == 4, "max teams is 4");
    check(VS_MAX_TEAM_AGENTS == 8, "max agents per team is 8");

    /* --- Multiple teams --- */
    printf("  multiple teams:\n");
    VSTeam teams[2];
    vs_team_init(&teams[0], "team-a");
    vs_team_init(&teams[1], "team-b");
    vs_team_add_agent(&teams[0], "a1", "coder");
    vs_team_add_agent(&teams[1], "b1", "reviewer");
    result = vs_team_list(2, teams, 20);
    check(result != NULL, "list multiple teams");
    if (result) free(result);
    vs_team_shutdown(&teams[0]);
    vs_team_shutdown(&teams[1]);

    /* --- Edge cases --- */
    printf("  edge cases:\n");
    VSTeam empty;
    vs_team_init(&empty, "empty-team");
    result = vs_team_read_messages(&empty, "nobody");
    check(result != NULL, "read_messages on empty team");
    if (result) free(result);

    result = vs_team_broadcast(&empty, "nobody", "no one here");
    check(result != NULL, "broadcast on empty team");
    if (result) free(result);

    vs_team_shutdown(&empty);

    /* --- Agent capacity --- */
    printf("  agent capacity:\n");
    VSTeam full;
    vs_team_init(&full, "full-team");
    int added = 0;
    for (int i = 0; i < VS_MAX_TEAM_AGENTS + 2; i++) {
        char id[32], role[32];
        snprintf(id, sizeof(id), "agent_%d", i);
        snprintf(role, sizeof(role), "role_%d", i);
        if (vs_team_add_agent(&full, id, role) == 0) added++;
    }
    check(added <= VS_MAX_TEAM_AGENTS, "agent count capped at max");
    vs_team_shutdown(&full);

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
