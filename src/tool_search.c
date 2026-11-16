/* SPDX-License-Identifier: Unlicense */
/* Tool search: search available tools by name/description (src/tool_search.c).
 *
 * Mirrors Claude Code's ToolSearch.  Maintains a registry of all available
 * tools with names and descriptions, then searches using simple string matching. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* Tool registry — all available tools with descriptions */
static const struct {
    const char *name;
    const char *description;
} tool_registry[] = {
    {"Read", "Read file content with line numbers"},
    {"Write", "Write content to a file"},
    {"Edit", "Make targeted search/replace edits in a file"},
    {"MultiEdit", "Apply multiple search/replace edits atomically"},
    {"Bash", "Run a shell command"},
    {"Glob", "Pattern-based file discovery (supports **, *, ?)"},
    {"Grep", "Content search with context lines and file/line numbers"},
    {"List", "List directory contents"},
    {"Search", "Search files by name"},
    {"Graph", "Generate dependency graph of files"},
    {"WebSearch", "Search the web for information"},
    {"WebFetch", "Fetch content from a URL"},
    {"TodoWrite", "Manage a visible session task list"},
    {"Subagent", "Spawn a parallel sub-agent for a delegated task"},
    {"NotebookRead", "Read a Jupyter .ipynb notebook file"},
    {"NotebookEdit", "Edit cells in a Jupyter notebook"},
    {"Monitor", "Start a background process with streaming output"},
    {"MonitorOutput", "Read accumulated output from a background process"},
    {"MonitorKill", "Terminate a background process"},
    {"KillShell", "Terminate a running shell session"},
    {"LSP", "Language Server Protocol diagnostics and go-to-definition"},
    {"TaskCreate", "Create a cross-session task"},
    {"TaskGet", "Get a single task by ID"},
    {"TaskUpdate", "Update a task's status, title, or description"},
    {"TaskList", "List all cross-session tasks"},
    {"TaskDelete", "Delete a task by ID"},
    {"TaskSearch", "Search tasks by query and status"},
    {"TaskStop", "Stop (cancel) a background task"},
    {"TaskOutput", "Read output from a background task"},
    {"Permission", "Add allow/deny rules for tool execution"},
    {"ExitPlanMode", "Exit plan mode and enter execution mode"},
    {"SlashCommand", "Execute a custom slash command"},
    {"ToolSearch", "Search available tools by name or description"},
    {"Skill", "Invoke a loaded skill by name"},
    {"EnterWorktree", "Enter a git worktree for isolated file changes"},
    {"ExitWorktree", "Exit the current git worktree"},
    {"AskUserQuestion", "Ask the user a structured question"},
    {"ListMcpResources", "List resources available from an MCP server"},
    {"ReadMcpResource", "Read a resource from an MCP server"},
    {"Image", "Analyze an image file"},
    {NULL, NULL}
};

char *vs_tool_search(const char *query)
{
    char *buf;
    size_t cap, len;
    int i, count = 0;
    char query_lower[256];
    size_t qlen;

    if (!query || !query[0]) return dupstr("ERROR: tool_search requires a query");

    qlen = strlen(query);
    if (qlen >= sizeof(query_lower)) qlen = sizeof(query_lower) - 1;
    for (size_t j = 0; j <= qlen; j++) {
        query_lower[j] = (char)((query[j] >= 'A' && query[j] <= 'Z')
                                 ? query[j] - 'A' + 'a' : query[j]);
    }
    query_lower[qlen] = 0;

    cap = 8192;
    buf = (char *)malloc(cap);
    if (!buf) return dupstr("ERROR: out of memory");
    buf[0] = 0;
    len = 0;

    for (i = 0; tool_registry[i].name; i++) {
        char name_lower[64], desc_lower[256];
        size_t nlen = strlen(tool_registry[i].name);
        if (nlen >= sizeof(name_lower)) nlen = sizeof(name_lower) - 1;
        for (size_t j = 0; j <= nlen; j++) {
            name_lower[j] = (char)((tool_registry[i].name[j] >= 'A' && tool_registry[i].name[j] <= 'Z')
                                    ? tool_registry[i].name[j] - 'A' + 'a'
                                    : tool_registry[i].name[j]);
        }
        name_lower[nlen] = 0;

        size_t dlen = strlen(tool_registry[i].description);
        if (dlen >= sizeof(desc_lower)) dlen = sizeof(desc_lower) - 1;
        for (size_t j = 0; j <= dlen; j++) {
            desc_lower[j] = (char)((tool_registry[i].description[j] >= 'A' && tool_registry[i].description[j] <= 'Z')
                                    ? tool_registry[i].description[j] - 'A' + 'a'
                                    : tool_registry[i].description[j]);
        }
        desc_lower[dlen] = 0;

        int match = (strstr(name_lower, query_lower) != NULL) ||
                    (strstr(desc_lower, query_lower) != NULL);

        if (!match) continue;

        char entry[512];
        int n;
        n = snprintf(entry, sizeof(entry), "  %s: %s\n", tool_registry[i].name, tool_registry[i].description);
        if (n > 0 && (size_t)n < cap - len) {
            memcpy(buf + len, entry, (size_t)n);
            len += (size_t)n;
        }
        count++;
    }

    if (count == 0) {
        snprintf(buf, cap, "No tools matching \"%s\".\n", query);
    } else {
        char header[128];
        int h = snprintf(header, sizeof(header), "Found %d tool(s) matching \"%s\":\n\n", count, query);
        if (h > 0 && (size_t)h < cap - len) {
            memmove(buf, header, (size_t)h);
            len += (size_t)h;
        }
        /* Re-append results after header */
        /* Simple approach: rebuild */
        buf[0] = 0;
        len = 0;
        snprintf(buf, cap, "Found %d tool(s) matching \"%s\":\n\n", count, query);
        len = strlen(buf);
        for (i = 0; tool_registry[i].name && count > 0; i++) {
            char name_lower[64], desc_lower[256];
            size_t nlen = strlen(tool_registry[i].name);
            if (nlen >= sizeof(name_lower)) nlen = sizeof(name_lower) - 1;
            for (size_t j = 0; j <= nlen; j++) {
                name_lower[j] = (char)((tool_registry[i].name[j] >= 'A' && tool_registry[i].name[j] <= 'Z')
                                        ? tool_registry[i].name[j] - 'A' + 'a'
                                        : tool_registry[i].name[j]);
            }
            name_lower[nlen] = 0;

            size_t dlen = strlen(tool_registry[i].description);
            if (dlen >= sizeof(desc_lower)) dlen = sizeof(desc_lower) - 1;
            for (size_t j = 0; j <= dlen; j++) {
                desc_lower[j] = (char)((tool_registry[i].description[j] >= 'A' && tool_registry[i].description[j] <= 'Z')
                                        ? tool_registry[i].description[j] - 'A' + 'a'
                                        : tool_registry[i].description[j]);
            }
            desc_lower[dlen] = 0;

            int match = (strstr(name_lower, query_lower) != NULL) ||
                        (strstr(desc_lower, query_lower) != NULL);
            if (!match) continue;

            char entry[512];
            int n;
            n = snprintf(entry, sizeof(entry), "  %s: %s\n", tool_registry[i].name, tool_registry[i].description);
            if (n > 0 && (size_t)n < cap - len) {
                memcpy(buf + len, entry, (size_t)n);
                len += (size_t)n;
            }
        }
    }

    return buf;
}
