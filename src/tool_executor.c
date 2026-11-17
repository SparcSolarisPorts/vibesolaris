/* SPDX-License-Identifier: Unlicense */
/* Parallel tool executor: runs read-only tools concurrently, side-effecting
   tools serially.  Mirrors Claude Code's StreamingToolExecutor pattern.

   Read-only tools (read, list, search, graph, web_search, web_fetch) can run
   in parallel because they don't modify state.  Write, run, subagent, and MCP
   tools remain serial for safety. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static char *dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* Check if a directive is read-only (safe for parallel execution) */
int vs_tool_executor_is_readonly(const char *directive, int is_mcp)
{
    if (is_mcp) return 0; /* MCP tools are always considered side-effecting */
    if (!directive) return 0;
    return !strncmp(directive, "[[VS_TOOL read ", 15) ||
           !strncmp(directive, "[[VS_TOOL list ", 15) ||
           !strncmp(directive, "[[VS_TOOL search ", 17) ||
           !strncmp(directive, "[[VS_TOOL graph ", 16) ||
           !strncmp(directive, "[[VS_TOOL web_search ", 21) ||
           !strncmp(directive, "[[VS_TOOL web_fetch ", 20) ||
           !strncmp(directive, "[[VS_TOOL glob ", 15) ||
           !strncmp(directive, "[[VS_TOOL grep ", 15) ||
           !strncmp(directive, "[[VS_TOOL notebook_read ", 21) ||
           !strncmp(directive, "[[VS_TOOL monitor_output ", 24) ||
           !strncmp(directive, "[[VS_TOOL task_list ", 19) ||
           !strncmp(directive, "[[VS_TOOL task_search ", 21) ||
           !strncmp(directive, "[[VS_TOOL lsp ", 14) ||
           !strncmp(directive, "[[VS_TOOL tool_search ", 21) ||
           !strncmp(directive, "[[VS_TOOL list_mcp_resources ", 28) ||
           !strncmp(directive, "[[VS_TOOL read_mcp_resource ", 29) ||
           !strncmp(directive, "[[VS_TOOL computer_use_screenshot ", 33);
}

/* Thread entry for parallel tool execution */
typedef struct {
    VSContext *ctx;
    char *directive;
    int is_mcp;
    VSToolJob *job;      /* back-reference to store result */
    int error;
    int timeout_seconds;
} ToolJobArg;

static void *tool_job_thread(void *arg)
{
    ToolJobArg *ta = (ToolJobArg *)arg;
    VSContext *ctx = ta->ctx;
    char *directive = ta->directive;
    int is_mcp = ta->is_mcp;
    char *result = NULL;

    if (is_mcp) {
        /* Parse MCP directive and call */
        char *server = NULL, *tool = NULL, *args = NULL;
        /* Simple attribute extraction */
        {
            const char *p = strstr(directive, "server=\"");
            if (p) { p += 8; const char *e = strchr(p, '"'); if (e) { size_t n = (size_t)(e - p); server = (char *)malloc(n + 1); if (server) { memcpy(server, p, n); server[n] = 0; } } }
            p = strstr(directive, "tool=\"");
            if (p) { p += 6; const char *e = strchr(p, '"'); if (e) { size_t n = (size_t)(e - p); tool = (char *)malloc(n + 1); if (tool) { memcpy(tool, p, n); tool[n] = 0; } } }
            p = strstr(directive, "args=\"");
            if (p) { p += 6; const char *e = directive; while (*e == ']' && *(e + 1) == ']') e++; e = strchr(p + 1, '"'); if (e) { size_t n = (size_t)(e - p); args = (char *)malloc(n + 1); if (args) { memcpy(args, p, n); args[n] = 0; } } }
        }
        if (server && tool && args) {
            result = vs_mcp_call(ctx, server, tool, args);
        } else {
            result = dupstr("ERROR: could not parse MCP directive");
        }
        free(server);
        free(tool);
        free(args);
    } else {
        /* For native tools, we need to extract the operation and call the
           appropriate function. For parallel execution, we only run read-only
           tools here. Side-effecting tools are run serially in the main thread. */
        if (strstr(directive, "read ")) {
            char *path = NULL;
            const char *p = strstr(directive, "path=\"");
            if (p) { p += 6; const char *e = strchr(p, '"'); if (e) { size_t n = (size_t)(e - p); path = (char *)malloc(n + 1); if (path) { memcpy(path, p, n); path[n] = 0; } } }
            if (path) {
                result = vs_cached_read_file(ctx, path);
                if (!result) result = dupstr("ERROR: could not read file");
                free(path);
            }
        } else if (strstr(directive, "list ")) {
            /* Directory listing - use run command */
            int exit_code;
            char *path = NULL;
            const char *p = strstr(directive, "path=\"");
            if (p) { p += 6; const char *e = strchr(p, '"'); if (e) { size_t n = (size_t)(e - p); path = (char *)malloc(n + 1); if (path) { memcpy(path, p, n); path[n] = 0; } } }
            char cmd[2048];
            if (path) {
                snprintf(cmd, sizeof(cmd), "ls -la '%s' 2>&1", path);
                result = vs_run_command_ctx(ctx, cmd, &exit_code);
                free(path);
            } else {
                result = vs_run_command_ctx(ctx, "ls -la 2>&1", &exit_code);
            }
            if (!result) result = dupstr("(empty listing)");
        } else if (strstr(directive, "search ")) {
            char *query = NULL;
            const char *p = strstr(directive, "query=\"");
            if (p) { p += 7; const char *e = strchr(p, '"'); if (e) { size_t n = (size_t)(e - p); query = (char *)malloc(n + 1); if (query) { memcpy(query, p, n); query[n] = 0; } } }
            if (query) {
                int exit_code;
                char cmd[2048];
                snprintf(cmd, sizeof(cmd), "grep -rn --include='*' -l '%s' . 2>&1 | head -50", query);
                result = vs_run_command_ctx(ctx, cmd, &exit_code);
                free(query);
                if (!result) result = dupstr("(no matches)");
            }
        } else if (strstr(directive, "web_search ")) {
            char *query = NULL;
            const char *p = strstr(directive, "query=\"");
            if (p) { p += 7; const char *e = strchr(p, '"'); if (e) { size_t n = (size_t)(e - p); query = (char *)malloc(n + 1); if (query) { memcpy(query, p, n); query[n] = 0; } } }
            if (query) {
                result = vs_web_search(ctx, query, 8);
                free(query);
                if (!result) result = dupstr("web search failed");
            }
        } else if (strstr(directive, "web_fetch ")) {
            char *url = NULL;
            const char *p = strstr(directive, "url=\"");
            if (p) { p += 5; const char *e = strchr(p, '"'); if (e) { size_t n = (size_t)(e - p); url = (char *)malloc(n + 1); if (url) { memcpy(url, p, n); url[n] = 0; } } }
            if (url) {
                result = vs_web_fetch(ctx, url, 128 * 1024);
                free(url);
                if (!result) result = dupstr("web fetch failed");
            }
        } else if (strstr(directive, "graph ")) {
            VSGraph g;
            char *summary;
            if (vs_graph_build(ctx, ".", VS_GRAPH_FILES, &g) < 0) {
                result = dupstr("graph build failed");
            } else {
                summary = vs_graph_summary(&g, 100);
                if (summary) {
                    result = summary;
                } else {
                    result = dupstr("(empty graph)");
                }
            }
        } else {
            result = dupstr("ERROR: unknown or unsupported tool for parallel execution");
        }
    }

    if (ta->job) {
        ta->job->result = result;
        ta->job->done = 1;
        ta->job->error = (result == NULL);
    }
    free(ta->directive);
    free(ta);
    return NULL;
}

void vs_tool_executor_init(VSToolExecutor *exec)
{
    memset(exec, 0, sizeof(*exec));
    exec->parallel_readonly = 1;
    exec->timeout_seconds = 0;
}

int vs_tool_executor_parse_jobs(VSToolExecutor *exec, const char *answer)
{
    const char *p = answer;
    int count = 0;

    while (count < VS_MAX_PARALLEL_TOOLS && p) {
        const char *d = strstr(p, "[[VS_TOOL ");
        const char *m = strstr(p, "[[VS_MCP ");
        const char *dir = NULL;
        int is_mcp = 0;

        if (!d && !m) break;
        if (d && (!m || d < m)) { dir = d; is_mcp = 0; }
        else { dir = m; is_mcp = 1; }

        const char *end = strstr(dir, "]]");
        if (!end) break;

        size_t len = (size_t)(end + 2 - dir);
        if (len > sizeof(exec->jobs[count].directive) - 1) {
            len = sizeof(exec->jobs[count].directive) - 1;
        }
        memcpy(exec->jobs[count].directive, dir, len);
        exec->jobs[count].directive[len] = 0;
        exec->jobs[count].is_mcp = is_mcp;
        exec->jobs[count].result = NULL;
        exec->jobs[count].done = 0;
        exec->jobs[count].error = 0;

        count++;
        p = end + 2;
    }

    exec->job_count = count;
    return count;
}

void vs_tool_executor_run_parallel(VSToolExecutor *exec, VSContext *ctx)
{
    int i;
    pthread_t threads[VS_MAX_PARALLEL_TOOLS];
    ToolJobArg *args[VS_MAX_PARALLEL_TOOLS];
    int parallel_count = 0;

    /* Separate read-only (parallel) from side-effecting (serial) */
    for (i = 0; i < exec->job_count; i++) {
        int readonly = vs_tool_executor_is_readonly(exec->jobs[i].directive, exec->jobs[i].is_mcp);

        if (readonly && exec->parallel_readonly) {
            /* Queue for parallel execution */
            args[parallel_count] = (ToolJobArg *)malloc(sizeof(ToolJobArg));
            if (args[parallel_count]) {
                args[parallel_count]->ctx = ctx;
                args[parallel_count]->directive = dupstr(exec->jobs[i].directive);
                args[parallel_count]->is_mcp = exec->jobs[i].is_mcp;
                args[parallel_count]->job = &exec->jobs[i];
                args[parallel_count]->error = 0;
                args[parallel_count]->timeout_seconds = exec->timeout_seconds;

                pthread_t tid;
                if (pthread_create(&tid, NULL, tool_job_thread, args[parallel_count]) == 0) {
                    threads[parallel_count] = tid;
                    parallel_count++;
                } else {
                    /* Thread creation failed, mark as error */
                    free(args[parallel_count]->directive);
                    free(args[parallel_count]);
                    exec->jobs[i].result = dupstr("ERROR: unable to start parallel thread");
                    exec->jobs[i].done = 1;
                    exec->jobs[i].error = 1;
                }
            }
        }
        /* Side-effecting tools are left with done=0 so agent.c knows to run them serially */
    }

    /* Wait for parallel threads — results are stored in exec->jobs[i].result by the thread */
    for (i = 0; i < parallel_count; i++) {
        void *res;
        pthread_join(threads[i], &res);
        (void)res;
    }
}

char *vs_tool_executor_collect_results(VSToolExecutor *exec)
{
    int i;
    size_t cap = 1024;
    char *out = (char *)malloc(cap);
    size_t pos = 0;

    if (!out) return NULL;
    out[0] = 0;

    for (i = 0; i < exec->job_count; i++) {
        VSToolJob *job = &exec->jobs[i];
        char buf[256];
        int n;

        n = snprintf(buf, sizeof(buf),
            "\n--- Tool result #%d ---\n%s\n",
            i + 1, job->result ? job->result : "(no result)");
        if (n < 0) continue;

        size_t blen = (size_t)n;
        if (pos + blen + 1 > cap) {
            cap = (pos + blen + 1) * 2;
            char *new_out = (char *)realloc(out, cap);
            if (!new_out) {
                free(out);
                return NULL;
            }
            out = new_out;
        }
        memcpy(out + pos, buf, blen);
        pos += blen;
        out[pos] = 0;
    }

    return out;
}

void vs_tool_executor_clear(VSToolExecutor *exec)
{
    int i;
    for (i = 0; i < exec->job_count; i++) {
        if (exec->jobs[i].result) {
            free(exec->jobs[i].result);
            exec->jobs[i].result = NULL;
        }
    }
    memset(exec, 0, sizeof(*exec));
}
