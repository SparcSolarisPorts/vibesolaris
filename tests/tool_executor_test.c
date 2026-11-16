/* SPDX-License-Identifier: Unlicense */
/* Unit tests for parallel tool executor (src/tool_executor.c).
 *
 * Tests the directive parsing, read-only classification, and result
 * collection without requiring a full provider runtime. */
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

/* --- Stubs for tool executor thread functions --- */

char *vs_cached_read_file(VSContext *c, const char *p) {
    (void)c;
    char *r = (char *)malloc(256);
    if (!r) return NULL;
    snprintf(r, 256, "file_content:%s", p ? p : "(null)");
    return r;
}

char *vs_run_command_ctx(VSContext *c, const char *cmd, int *status) {
    (void)c;
    if (status) *status = 0;
    char *r = (char *)malloc(512);
    if (!r) return NULL;
    snprintf(r, 512, "command_output:%s", cmd ? cmd : "(null)");
    return r;
}

char *vs_run_command(const char *cmd, int *status) {
    return vs_run_command_ctx(NULL, cmd, status);
}

const char *vs_command_shell_name(void) {
    return "/bin/sh";
}

char *vs_web_search(VSContext *c, const char *q, int n) {
    (void)c; (void)n;
    char *r = (char *)malloc(256);
    if (!r) return NULL;
    snprintf(r, 256, "web_results_for:%s", q ? q : "(null)");
    return r;
}

char *vs_web_fetch(VSContext *c, const char *u, size_t n) {
    (void)c; (void)n;
    char *r = (char *)malloc(256);
    if (!r) return NULL;
    snprintf(r, 256, "fetched_page:%s", u ? u : "(null)");
    return r;
}

int vs_graph_build(VSContext *c, const char *r, VSGraphMode m, VSGraph *g) {
    (void)c; (void)r;
    if (g) { memset(g, 0, sizeof(*g)); g->mode = m; }
    return 0;
}

char *vs_graph_summary(const VSGraph *g, int n) {
    (void)n;
    if (!g) return NULL;
    char *r = (char *)malloc(128);
    if (!r) return NULL;
    snprintf(r, 128, "graph_summary:mode=%d", g->mode);
    return r;
}

char *vs_mcp_call(VSContext *c, const char *s, const char *t, const char *a) {
    (void)c; (void)s; (void)a;
    char *r = (char *)malloc(256);
    if (!r) return NULL;
    snprintf(r, 256, "mcp_result:%s", t ? t : "(null)");
    return r;
}

/* --- End stubs --- */

int main(void) {
    VSToolExecutor exec;
    char *result;

    printf("Tool executor tests:\n");

    /* --- vs_tool_executor_init --- */
    printf("  init:\n");
    vs_tool_executor_init(&exec);
    check(exec.parallel_readonly == 1, "init enables parallel readonly");
    check(exec.timeout_seconds == 0, "init clears timeout");
    check(exec.job_count == 0, "init clears job_count");

    /* --- vs_tool_executor_is_readonly --- */
    printf("  is_readonly:\n");

    check(vs_tool_executor_is_readonly("[[VS_TOOL read path=\"f\"]]", 0) == 1, "read is readonly");
    check(vs_tool_executor_is_readonly("[[VS_TOOL list path=\"d\"]]", 0) == 1, "list is readonly");
    check(vs_tool_executor_is_readonly("[[VS_TOOL search query=\"q\"]]", 0) == 1, "search is readonly");
    check(vs_tool_executor_is_readonly("[[VS_TOOL graph path=\".\"]]", 0) == 1, "graph is readonly");
    check(vs_tool_executor_is_readonly("[[VS_TOOL web_search query=\"q\"]]", 0) == 1, "web_search is readonly");
    check(vs_tool_executor_is_readonly("[[VS_TOOL web_fetch url=\"u\"]]", 0) == 1, "web_fetch is readonly");

    check(vs_tool_executor_is_readonly("[[VS_TOOL write path=\"f\" content=\"c\"]]", 0) == 0, "write is not readonly");
    check(vs_tool_executor_is_readonly("[[VS_TOOL run cmd=\"ls\"]]", 0) == 0, "run is not readonly");
    check(vs_tool_executor_is_readonly("[[VS_TOOL subagent task=\"t\"]]", 0) == 0, "subagent is not readonly");

    check(vs_tool_executor_is_readonly("[[VS_MCP server=\"s\" tool=\"t\" args=\"a\"]]", 1) == 0, "mcp is not readonly");
    check(vs_tool_executor_is_readonly(NULL, 0) == 0, "NULL is not readonly");

    /* --- vs_tool_executor_parse_jobs --- */
    printf("  parse_jobs:\n");

    const char *answer =
        "Here are the results:\n"
        "[[VS_TOOL read task_id=\"1\" path=\"/tmp/file1.txt\"]]\n"
        "Some prose in between.\n"
        "[[VS_TOOL read task_id=\"2\" path=\"/tmp/file2.txt\"]]\n"
        "More prose.\n"
        "[[VS_TOOL run task_id=\"3\" cmd=\"echo hello\"]]\n";

    int count = vs_tool_executor_parse_jobs(&exec, answer);
    check(count == 3, "parse_jobs finds 3 directives");
    check(strstr(exec.jobs[0].directive, "read") != NULL, "job 0 is read");
    check(strstr(exec.jobs[1].directive, "read") != NULL, "job 1 is read");
    check(strstr(exec.jobs[2].directive, "run") != NULL, "job 2 is run");
    check(exec.jobs[0].is_mcp == 0, "job 0 is not MCP");
    check(exec.jobs[2].is_mcp == 0, "job 2 is not MCP");

    /* Parse with no directives */
    vs_tool_executor_init(&exec);
    count = vs_tool_executor_parse_jobs(&exec, "No directives here.");
    check(count == 0, "parse_jobs no directives returns 0");

    /* Parse with MCP directive */
    vs_tool_executor_init(&exec);
    const char *mcp_answer = "[[VS_MCP server=\"s\" tool=\"t\" args=\"a\" task_id=\"1\"]]";
    count = vs_tool_executor_parse_jobs(&exec, mcp_answer);
    check(count == 1, "parse_jobs finds MCP directive");
    check(exec.jobs[0].is_mcp == 1, "MCP job marked as MCP");

    /* --- vs_tool_executor_run_parallel --- */
    printf("  run_parallel:\n");

    vs_tool_executor_init(&exec);
    vs_tool_executor_parse_jobs(&exec,
        "[[VS_TOOL read task_id=\"1\" path=\"/tmp/test1\"]]\n"
        "[[VS_TOOL read task_id=\"2\" path=\"/tmp/test2\"]]\n");

    VSContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    vs_tool_executor_run_parallel(&exec, &ctx);

    /* After parallel run, readonly jobs should be done */
    check(exec.jobs[0].done == 1, "readonly job 0 done");
    check(exec.jobs[1].done == 1, "readonly job 1 done");

    /* --- vs_tool_executor_collect_results --- */
    printf("  collect_results:\n");

    result = vs_tool_executor_collect_results(&exec);
    check(result != NULL, "collect_results returns non-NULL");
    check(strstr(result, "Tool result") != NULL, "collect includes tool result header");
    free(result);

    /* Collect with no jobs */
    vs_tool_executor_init(&exec);
    result = vs_tool_executor_collect_results(&exec);
    check(result != NULL && result[0] == 0, "collect empty returns empty string");
    free(result);

    /* --- vs_tool_executor_clear --- */
    printf("  clear:\n");

    vs_tool_executor_init(&exec);
    vs_tool_executor_parse_jobs(&exec,
        "[[VS_TOOL read task_id=\"1\" path=\"/tmp/test\"]]\n");
    check(exec.job_count == 1, "has job before clear");

    vs_tool_executor_clear(&exec);
    check(exec.job_count == 0, "clear resets job_count");
    check(exec.parallel_readonly == 0, "clear resets parallel_readonly");

    /* --- Mixed readonly and side-effecting --- */
    printf("  mixed jobs:\n");

    vs_tool_executor_init(&exec);
    vs_tool_executor_parse_jobs(&exec,
        "[[VS_TOOL read task_id=\"1\" path=\"/tmp/a\"]]\n"
        "[[VS_TOOL run task_id=\"2\" cmd=\"echo hi\"]]\n"
        "[[VS_TOOL search task_id=\"3\" query=\"test\"]]\n");

    check(exec.job_count == 3, "mixed parse finds 3 jobs");
    check(vs_tool_executor_is_readonly(exec.jobs[0].directive, 0) == 1, "job 0 readonly");
    check(vs_tool_executor_is_readonly(exec.jobs[1].directive, 0) == 0, "job 1 not readonly");
    check(vs_tool_executor_is_readonly(exec.jobs[2].directive, 0) == 1, "job 2 readonly");

    vs_tool_executor_run_parallel(&exec, &ctx);

    vs_tool_executor_clear(&exec);

    /* --- Max jobs limit --- */
    printf("  max jobs:\n");

    vs_tool_executor_init(&exec);

    /* Build a string with many directives */
    char big_answer[65536];
    size_t pos = 0;
    int i;
    for (i = 0; i < 25; i++) {
        pos += (size_t)snprintf(big_answer + pos, sizeof(big_answer) - pos,
            "[[VS_TOOL read task_id=\"%d\" path=\"/tmp/f%d\"]]\n", i, i);
    }
    big_answer[sizeof(big_answer) - 1] = 0;

    count = vs_tool_executor_parse_jobs(&exec, big_answer);
    check(count <= VS_MAX_PARALLEL_TOOLS, "parse_jobs respects max limit");

    vs_tool_executor_clear(&exec);

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
