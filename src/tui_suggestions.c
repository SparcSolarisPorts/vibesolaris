/* SPDX-License-Identifier: Unlicense */
/* TUI Suggestions engine: context-aware command and file suggestions */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

void vs_tui_suggestions_init(VSTUISuggestion *suggestions, int *count)
{
    if (count) *count = 0;
    if (suggestions) memset(suggestions, 0, sizeof(VSTUISuggestion));
}

int vs_tui_suggestions_add(VSTUISuggestion *suggestions, int *count, const char *text, int type)
{
    if (!suggestions || !count || !text || *count >= VS_TUI_SUGGESTION_MAX) return -1;
    VSTUISuggestion *s = &suggestions[*count];
    strncpy(s->text, text, VS_TUI_SUGGESTION_TEXT_MAX - 1);
    s->text[VS_TUI_SUGGESTION_TEXT_MAX - 1] = 0;
    s->score = 0;
    s->type = type;
    (*count)++;
    return 0;
}

/* Fuzzy match: returns score (higher = better), 0 = no match */
int vs_tui_suggestions_fuzzy_match(const char *input, const char *candidate)
{
    if (!input || !candidate || !*input || !*candidate) return 0;

    int score = 0;
    const char *p = input;
    const char *q = candidate;
    int consecutive = 0;
    int start_match = 0;

    /* Check if candidate starts with input (highest priority) */
    if (strncasecmp(p, q, strlen(p)) == 0) {
        start_match = 1;
    }

    /* Fuzzy matching */
    int matched = 0;
    while (*p && *q) {
        if (tolower((unsigned char)*p) == tolower((unsigned char)*q)) {
            matched++;
            consecutive++;
            /* Bonus for consecutive matches */
            score += 2 + consecutive;
            /* Bonus for matching at word boundary */
            if (q == candidate || * (q - 1) == ' ' || *(q - 1) == '-' || *(q - 1) == '_') {
                score += 3;
            }
            p++;
        } else {
            consecutive = 0;
            q++;
        }
    }

    /* All input chars must match */
    if (*p != 0) return 0;

    /* Bonus for start match */
    if (start_match) score += 10;

    /* Penalty for longer candidates (prefer shorter matches) */
    score -= (int)strlen(candidate) / 2;

    return score > 0 ? score : 0;
}

int vs_tui_suggestions_match(VSTUISuggestion *suggestions, int count, const char *input,
                              VSTUISuggestion *results, int max_results)
{
    if (!suggestions || !input || !results || max_results <= 0) return 0;

    /* Score all suggestions */
    for (int i = 0; i < count; i++) {
        suggestions[i].score = vs_tui_suggestions_fuzzy_match(input, suggestions[i].text);
    }

    /* Sort by score (simple insertion sort) */
    for (int i = 1; i < count; i++) {
        VSTUISuggestion tmp = suggestions[i];
        int j = i - 1;
        while (j >= 0 && suggestions[j].score < tmp.score) {
            suggestions[j + 1] = suggestions[j];
            j--;
        }
        suggestions[j + 1] = tmp;
    }

    /* Copy top results */
    int n = 0;
    for (int i = 0; i < count && n < max_results; i++) {
        if (suggestions[i].score > 0) {
            results[n++] = suggestions[i];
        }
    }
    return n;
}

void vs_tui_suggestions_render(const VSTUISuggestion *results, int count, int width)
{
    if (!results || count <= 0 || width < 4) return;

    /* Find max width of suggestions */
    int max_w = 0;
    for (int i = 0; i < count; i++) {
        int w = (int)strlen(results[i].text);
        if (w > max_w) max_w = w;
    }
    if (max_w > width - 4) max_w = width - 4;

    /* Calculate columns */
    int cols = width / (max_w + 2);
    if (cols < 1) cols = 1;
    if (cols > 8) cols = 8;

    /* Render in columns */
    int rows = (count + cols - 1) / cols;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = c * rows + r;
            if (idx >= count) break;

            if (c > 0) {
                /* Padding between columns */
                printf("%*s", 2, "");
            }

            const char *text = results[idx].text;
            const char *color = "";
            const char *reset = "\033[0m";

            /* Color by type */
            switch (results[idx].type) {
                case 0: color = "\033[36m"; break; /* command: cyan */
                case 1: color = "\033[33m"; break; /* file: yellow */
                case 2: color = "\033[34m"; break; /* path: blue */
                case 3: color = "\033[35m"; break; /* tool: magenta */
                default: color = "\033[37m"; break;
            }

            /* Highlight first item */
            if (idx == 0) {
                printf("\033[1m%s%s.%s", color, text, reset);
            } else {
                printf("%s%s%s", color, text, reset);
            }
        }
        printf("\n");
    }
}

/* ---- Slash-command completion table ---------------------------------- */
/* Keep in sync with the /help text in src/tui.c.  Order matters only for
 * display; matching is a stable prefix filter over this array. */
static const char *const tui_slash_commands[] = {
    "/help","/quit","/effort","/effort default","/effort low","/effort medium","/effort high","/usage","/trace","/trace full","/output compact","/output normal","/output full","/output status",
    "/provider openai","/provider claude","/provider gemini","/provider glm","/provider glm-coding","/provider kimi","/provider qwen","/provider ernie","/provider deepseek","/provider custom",
    "/protocol openai","/protocol anthropic","/anthropic-auth bearer","/anthropic-auth x-api-key","/model ","/base ","/key ","/proxy status","/proxy on","/proxy off",
    "/oauth status","/oauth login","/oauth logout","/oauth save","/login","/logout","/claude-oauth status","/claude-oauth login","/claude-oauth logout","/subagent ","/subagents",
    "/attach ","/paste-image","/attachments","/attach-remove ","/clearattach","/read ","/run ","/web ","/fetch ","/graph files","/graph fields","/graph dot files ","/graph dot fields ",
    "/cache status","/cache on","/cache off","/cache clear","/history status","/history clear",
    "/globalconfig status","/globalconfig load","/globalconfig save","/saveconfig ",
    "/mcp list","/mcp refresh","/mcp tools","/mcp add-stdio ","/mcp add-http ","/mcp remove ",
    "/colour auto","/colour on","/colour off","/colour status","/mode auto","/mode plan","/mode manual","/mode status","/plan","/plan accept","/plan approve-action","/plan reject","/plan revise",
     "/permission list","/permission allow ","/permission deny ","/tasks list","/tasks search ","/tasks create ","/skills list","/memory list","/memory search ","/memory save ","/context","/compact","/checkpoint create","/checkpoint rollback","/checkpoint list","/dream","/dream off","/tui fullscreen","/tui prompt",
    "/auto-mode on","/auto-mode off","/auto-mode status","/auto-mode rules",
    "/team create ","/team add ","/team list","/team shutdown ",
    "/cost","/clear","/export","/export ","/doctor","/bashes","/hooks","/hooks list","/rename ",
    "/plugin install ","/plugin list","/plugin uninstall ",
    "/review ","/goal set ","/goal show","/goal clear","/init ","/pause",
    "/style compact","/style verbose","/style default","/keybindings",
    "/worktree ","/worktree-exit","/config","/config layer",
    "/search ","/search-next","/search-prev","/search-exit",
    "/reason ","/reason on","/reason off","/reason depth",
    "/rewind","/rewind 1","/rewind 2","/rewind mark ","/rewind to ","/rewind list",
    "/security-review ","/security-review . staged",
    "/loop start ","/loop stop ","/loop list",
    "/workflow run ","/workflow status",
    "/sessions list","/sessions send ","/sessions inbox",
    "/btw ","/btw list","/btw clear",
    "/plan on","/plan off","/plan status",
    "/model ","/model list",
    "/branch ","/branch list",
    "/diff","/diff staged",
    "/commit","/commit generate",
    "/pr ","/pr generate",
    "/custom-commands list",
    "/effort low","/effort medium","/effort high","/effort xhigh","/effort max","/effort ultracode",NULL
};

/* Prefix -> description map; vs_tui_slash_desc picks the LONGEST matching
 * prefix so "/plan accept" inherits the "/plan" text while "/plan" itself
 * can still have its own entry if ever needed. */
static const struct { const char *prefix; const char *desc; } tui_slash_descs[] = {
    {"/help","show command help"},
    {"/quit","exit VibeSolaris"},
    {"/effort","set effort level (low..ultracode)"},
    {"/usage","provider token usage for this conversation"},
    {"/trace","activity trace (add 'full' for the stored trace)"},
    {"/output","command/trace output verbosity"},
    {"/provider","switch provider"},
    {"/protocol","wire protocol for the current provider"},
    {"/anthropic-auth","auth header style for Anthropic protocol"},
    {"/model","switch model mid-session (add 'list' to browse)"},
    {"/base","set provider base URL"},
    {"/key","set provider API key"},
    {"/proxy","local request proxy"},
    {"/oauth","OAuth session for the OpenAI provider"},
    {"/login","log in to the selected provider"},
    {"/logout","log out of the selected provider"},
    {"/claude-oauth","Claude (Anthropic) OAuth session"},
    {"/subagent","run a task in a fresh subagent context"},
    {"/subagents","list running subagents"},
    {"/attach","attach a file or image to the next message"},
    {"/paste-image","attach the clipboard image (macOS)"},
    {"/attachments","list current attachments"},
    {"/attach-remove","remove one attachment"},
    {"/clearattach","remove all attachments"},
    {"/read","read a file with the native reader"},
    {"/run","run a shell command"},
    {"/web","search the web"},
    {"/fetch","fetch a web page as readable text"},
    {"/graph","project/file relationship graph"},
    {"/cache","prompt-cache state and statistics"},
    {"/history","conversation history"},
    {"/globalconfig","encrypted configuration store"},
    {"/saveconfig","save a legacy/plain config file"},
    {"/mcp","MCP server management"},
    {"/colour","ANSI colour control (/color alias)"},
    {"/mode","workflow mode (auto/plan/manual)"},
    {"/plan","plan approval and plan-mode control"},
    {"/permission","tool permission rules"},
    {"/tasks","cross-session task management"},
    {"/skills","list loaded skills"},
    {"/memory","persistent agent memory"},
    {"/context","context window usage"},
    {"/compact","force context compaction"},
    {"/checkpoint","git snapshot before edits"},
    {"/dream","memory consolidation (AutoDream)"},
    {"/tui","TUI display mode"},
    {"/auto-mode","action classifier (safe/risky/blocked)"},
    {"/team","agent team management"},
    {"/cost","token cost estimate"},
    {"/clear","clear conversation, history and cache stats"},
    {"/export","export the conversation"},
    {"/doctor","diagnose configuration problems"},
    {"/bashes","recent shell commands"},
    {"/hooks","lifecycle hooks"},
    {"/rename","rename the session"},
    {"/plugin","plugin bundle management"},
    {"/review","start a multi-agent code review"},
    {"/goal","persistent project goal"},
    {"/init","auto-onboard repo (generate CLAUDE.md)"},
    {"/pause","pause session (away summary on resume)"},
    {"/style","output style (compact/verbose/default)"},
    {"/keybindings","show custom keybindings"},
    {"/worktree","enter a git worktree"},
    {"/worktree-exit","leave the current worktree"},
    {"/config","multi-layer settings view/set"},
    {"/search","search the scrollback buffer"},
    {"/reason","reasoning engine"},
    {"/rewind","rewind conversation messages and markers"},
    {"/security-review","scan for vulnerabilities"},
    {"/loop","recurring prompt"},
    {"/workflow","JSON workflow file"},
    {"/sessions","other VibeSolaris sessions"},
    {"/btw","side comment (off the main thread)"},
    {"/branch","git branch create/list"},
    {"/diff","preview working-tree or staged changes"},
    {"/commit","git commit (add 'generate' for message)"},
    {"/pr","generate a PR description"},
    {"/custom-commands","custom slash commands from .md files"},
    {"/export ","export the conversation to a file"},
    {NULL,NULL}
};

int vs_tui_slash_command_count(void)
{
    int n = 0;
    while (tui_slash_commands[n]) n++;
    return n;
}

const char *vs_tui_slash_command_name(int index)
{
    if (index < 0 || index >= vs_tui_slash_command_count()) return NULL;
    return tui_slash_commands[index];
}

const char *vs_tui_slash_desc(const char *name)
{
    size_t best = 0, len;
    const char *desc = "";
    int i;
    if (!name) return "";
    for (i = 0; tui_slash_descs[i].prefix; i++) {
        len = strlen(tui_slash_descs[i].prefix);
        if (len > best && strncmp(name, tui_slash_descs[i].prefix, len) == 0) {
            best = len;
            desc = tui_slash_descs[i].desc;
        }
    }
    return desc;
}

/* Prefix-filter over the command table for the composer's dropdown.
 * Returns the number of candidate commands; names/descs (optional, cap
 * entries) are filled in table order.  Buffers that are not completing a
 * bare slash command (no leading '/', or containing whitespace/newline)
 * yield 0 — argument-taking entries like "/model " are terminal nodes. */
int vs_tui_slash_match(const char *buf, const char **names, const char **descs, int cap)
{
    int i, n = 0;
    size_t len;
    if (!buf || buf[0] != '/' || !buf[0]) return 0;
    if (strchr(buf, '\n') || strchr(buf, ' ') || strchr(buf, '\t')) return 0;
    len = strlen(buf);
    for (i = 0; tui_slash_commands[i] && n < cap; i++) {
        if (strncmp(tui_slash_commands[i], buf, len) != 0) continue;
        if (names) names[n] = tui_slash_commands[i];
        if (descs) descs[n] = vs_tui_slash_desc(tui_slash_commands[i]);
        n++;
    }
    return n;
}
