/* SPDX-License-Identifier: Unlicense */
/* VibeSolaris Agent Teams — Peer-to-peer agent communication */
/*
 * Mirrors Claude Code's Agent Teams research preview: multiple independent
 * agent instances that communicate directly via shared scratch directories
 * and message files.
 */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#define VS_TEAM_DIR "~/.vibesolaris/teams"

static char *at_dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static int at_ensure_team_dir(const char *team_id, char *dir_out, size_t cap)
{
    char cmd[VS_MAX_PATH + 64];
    snprintf(dir_out, cap, "%s" VS_TEAM_DIR "/%s", getenv("HOME") ? getenv("HOME") : "/tmp", team_id);
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s/messages' '%s/scratch' 2>/dev/null", dir_out, dir_out);
    return (system(cmd) == 0) ? 0 : -1;
}

/* ---- Public API ---- */

int vs_team_init(VSTeam *team, const char *id)
{
    if (!team || !id) return -1;
    memset(team, 0, sizeof(*team));
    snprintf(team->id, sizeof(team->id), "%s", id);
    team->active = 1;
    
    if (at_ensure_team_dir(id, team->scratch_dir, sizeof(team->scratch_dir)) != 0) {
        return -1;
    }
    return 0;
}

int vs_team_add_agent(VSTeam *team, const char *agent_id, const char *role)
{
    if (!team || !agent_id || !team->active) return -1;
    if (team->agent_count >= VS_MAX_TEAM_AGENTS) return -1;
    
    /* Check for duplicate */
    for (int i = 0; i < team->agent_count; i++) {
        if (strcmp(team->agents[i].id, agent_id) == 0) return 0; /* already exists */
    }
    
    VSTeamAgent *a = &team->agents[team->agent_count];
    snprintf(a->id, sizeof(a->id), "%s", agent_id);
    snprintf(a->role, sizeof(a->role), "%s", role ? role : "general");
    a->active = 1;
    team->agent_count++;
    return 0;
}

char *vs_team_message(VSTeam *team, const char *from, const char *to, const char *msg)
{
    if (!team || !from || !to || !msg || !team->active) return at_dupstr("ERROR: team not active");
    
    /* Verify agents exist */
    int from_ok = 0, to_ok = 0;
    for (int i = 0; i < team->agent_count; i++) {
        if (strcmp(team->agents[i].id, from) == 0) from_ok = 1;
        if (strcmp(team->agents[i].id, to) == 0) to_ok = 1;
    }
    if (!from_ok) return at_dupstr("ERROR: sender not in team");
    if (!to_ok) return at_dupstr("ERROR: recipient not in team");
    
    /* Write message to file */
    char path[VS_MAX_PATH + 64];
    snprintf(path, sizeof(path), "%s/messages/%s_to_%s.jsonl", team->scratch_dir, from, to);
    
    FILE *f = fopen(path, "a");
    if (!f) return at_dupstr("ERROR: cannot write message");
    
    time_t now = time(NULL);
    fprintf(f, "{\"from\":\"%s\",\"to\":\"%s\",\"time\":%ld,\"msg\":\"%s\"}\n",
            from, to, (long)now, msg);
    fclose(f);
    
    char *result = (char *)malloc(256);
    if (result) snprintf(result, 256, "OK: message sent to %s", to);
    return result ? result : at_dupstr("OK: message sent");
}

char *vs_team_read_messages(VSTeam *team, const char *agent_id)
{
    if (!team || !agent_id || !team->active) return at_dupstr("ERROR: team not active");
    
    char *buf = (char *)malloc(8192);
    if (!buf) return NULL;
    int pos = 0;
    int count = 0;
    
    /* Read all messages addressed to this agent */
    for (int i = 0; i < team->agent_count && pos < 8000; i++) {
        if (strcmp(team->agents[i].id, agent_id) == 0) continue;
        
        char path[VS_MAX_PATH + 64];
        snprintf(path, sizeof(path), "%s/messages/%s_to_%s.jsonl",
                 team->scratch_dir, team->agents[i].id, agent_id);
        
        FILE *f = fopen(path, "r");
        if (!f) continue;
        
        char line[4096];
        while (fgets(line, sizeof(line), f) && pos < 8000) {
            pos += snprintf(buf + pos, 8192 - pos, "[%s→%s] %s",
                           team->agents[i].id, agent_id, line);
            count++;
            if (count >= 50) break;
        }
        fclose(f);
    }
    
    if (count == 0) {
        free(buf);
        return at_dupstr("No messages.");
    }
    return buf;
}

char *vs_team_broadcast(VSTeam *team, const char *from, const char *msg)
{
    if (!team || !from || !msg || !team->active) return at_dupstr("ERROR: team not active");
    
    int sent = 0;
    for (int i = 0; i < team->agent_count; i++) {
        if (strcmp(team->agents[i].id, from) == 0) continue;
        char *result = vs_team_message(team, from, team->agents[i].id, msg);
        if (result && strstr(result, "OK")) sent++;
        free(result);
    }
    
    char *out = (char *)malloc(128);
    if (out) snprintf(out, 128, "OK: broadcast to %d agents", sent);
    return out ? out : at_dupstr("OK: broadcast");
}

void vs_team_shutdown(VSTeam *team)
{
    if (!team || !team->active) return;
    
    char cmd[VS_MAX_PATH + 32];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' 2>/dev/null", team->scratch_dir);
    system(cmd);
    team->active = 0;
    team->agent_count = 0;
}

char *vs_team_list(int team_count, VSTeam *teams, int max_lines)
{
    char *buf = (char *)malloc(4096);
    if (!buf) return NULL;
    int pos = 0;
    int lines = 0;
    
    pos += snprintf(buf + pos, 4096 - pos, "Agent Teams (%d teams):\n", team_count);
    
    for (int t = 0; t < team_count && lines < max_lines; t++) {
        if (!teams[t].active) continue;
        pos += snprintf(buf + pos, 4096 - pos, "  [%s] %d agents:\n", teams[t].id, teams[t].agent_count);
        for (int a = 0; a < teams[t].agent_count && pos < 4080; a++) {
            pos += snprintf(buf + pos, 4096 - pos, "    - %s (role: %s)\n",
                           teams[t].agents[a].id, teams[t].agents[a].role);
        }
        lines++;
    }
    
    if (team_count == 0) {
        free(buf);
        return at_dupstr("No active teams. Use /team create to start one.");
    }
    return buf;
}
