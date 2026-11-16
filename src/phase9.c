/* SPDX-License-Identifier: Unlicense */
/* VibeSolaris Phase 9 — Additional Claude Code features */
/*
 * Implements: /btw, /model, /plan, custom slash commands, /branch,
 * /diff, extended effort levels, /commit, /pr, PreCompact hooks,
 * markdown table rendering, and @-mention context resolution.
 */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

/* ========================================================================
 * BTW — Side comments without polluting main thread
 * ======================================================================== */

void vs_btw_init(VSBTWStore *store)
{
    if (!store) return;
    memset(store, 0, sizeof(*store));
}

int vs_btw_add(VSBTWStore *store, const char *prompt, const char *response)
{
    if (!store || !prompt) return -1;

    /* Evict oldest if at capacity */
    if (store->message_count >= VS_MAX_BTW_MESSAGES) {
        memmove(&store->messages[0], &store->messages[1],
                sizeof(VSBTWMessage) * (VS_MAX_BTW_MESSAGES - 1));
        store->message_count--;
    }

    VSBTWMessage *m = &store->messages[store->message_count];
    snprintf(m->prompt, sizeof(m->prompt), "%s", prompt);
    if (response)
        snprintf(m->response, sizeof(m->response), "%s", response);
    m->timestamp = (long)time(NULL);
    store->message_count++;
    return store->message_count - 1;
}

char *vs_btw_list(VSBTWStore *store, int max_lines)
{
    if (!store) {
        char *buf = (char *)malloc(64);
        if (buf) snprintf(buf, 64, "No BTW store\n");
        return buf;
    }

    char *buf = (char *)malloc(8192);
    if (!buf) return NULL;

    int pos = 0;
    int lines = 0;

    pos += snprintf(buf + pos, 8192 - pos,
                    "Side comments (BTW): %d messages\n", store->message_count);
    lines++;

    for (int i = 0; i < store->message_count && lines < max_lines; i++) {
        VSBTWMessage *m = &store->messages[i];
        char time_str[64];
        time_t t = (time_t)m->timestamp;
        struct tm tm_buf;
        if (localtime_r(&t, &tm_buf)) {
            strftime(time_str, sizeof(time_str), "%H:%M", &tm_buf);
        } else {
            snprintf(time_str, sizeof(time_str), "%ld", (long)t);
        }
        pos += snprintf(buf + pos, 8192 - pos,
                        "  [%s] %s\n", time_str, m->prompt);
        if (m->response[0]) {
            pos += snprintf(buf + pos, 8192 - pos, "    → %s\n", m->response);
        }
        lines++;
        if (pos >= 8100) break;
    }

    return buf;
}

void vs_btw_clear(VSBTWStore *store)
{
    if (!store) return;
    store->message_count = 0;
}

/* ========================================================================
 * Plan mode — Propose without execute
 * ======================================================================== */

void vs_plan_mode_init(VSPlanMode *mode)
{
    if (!mode) return;
    memset(mode, 0, sizeof(*mode));
}

void vs_plan_mode_enable(VSPlanMode *mode)
{
    if (!mode) return;
    mode->enabled = 1;
    mode->preview_len = 0;
    mode->preview[0] = '\0';
}

void vs_plan_mode_disable(VSPlanMode *mode)
{
    if (!mode) return;
    mode->enabled = 0;
}

char *vs_plan_mode_preview(const VSPlanMode *mode)
{
    if (!mode) return NULL;
    if (mode->preview_len == 0) {
        char *buf = (char *)malloc(32);
        if (buf) snprintf(buf, 32, "No preview\n");
        return buf;
    }
    char *buf = (char *)malloc(mode->preview_len + 1);
    if (!buf) return NULL;
    memcpy(buf, mode->preview, mode->preview_len);
    buf[mode->preview_len] = '\0';
    return buf;
}

/* ========================================================================
 * Custom slash commands from markdown files
 * ======================================================================== */

void vs_custom_slash_init(VSCustomSlashStore *store, const char *dir)
{
    if (!store) return;
    memset(store, 0, sizeof(*store));
    if (dir)
        snprintf(store->commands_dir, sizeof(store->commands_dir), "%s", dir);
    else
        snprintf(store->commands_dir, sizeof(store->commands_dir),
                 "~/.vibesolaris/commands");
}

static int custom_slash_parse_markdown(const char *path, VSCustomSlashCommand *cmd)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    /* Extract filename without extension as command name */
    const char *base = strrchr(path, '/');
    if (base) base++; else base = path;
    const char *dot = strrchr(base, '.');
    if (dot && dot > base) {
        snprintf(cmd->name, sizeof(cmd->name), "%.*s", (int)(dot - base), base);
    } else {
        snprintf(cmd->name, sizeof(cmd->name), "%s", base);
    }

    /* Read YAML frontmatter if present (--- ... ---) */
    char line[256];
    int in_frontmatter = 0;
    int body_start = 0;
    int pos = 0;

    while (fgets(line, sizeof(line), f)) {
        int len = strlen(line);
        /* Skip trailing newline */
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (!in_frontmatter) {
            if (len == 3 && strncmp(line, "---", 3) == 0) {
                in_frontmatter = 1;
                continue;
            }
            /* No frontmatter, this is body */
            if (pos == 0) body_start = ftell(f) - len - 1;
            if (pos < (int)sizeof(cmd->body) - 2) {
                pos += snprintf(cmd->body + pos, sizeof(cmd->body) - pos, "%s\n", line);
            }
        } else {
            if (len == 3 && strncmp(line, "---", 3) == 0) {
                in_frontmatter = 0;
                body_start = ftell(f);
                continue;
            }
            /* Skip frontmatter content */
        }
        if (pos >= (int)sizeof(cmd->body) - 2) break;
    }

    /* If we never found frontmatter delimiter, body starts from beginning */
    if (body_start == 0 && pos > 0) {
        /* Body was already accumulated above */
    }

    cmd->body[pos] = '\0';
    snprintf(cmd->path, sizeof(cmd->path), "%s", path);
    cmd->loaded_at = (long)time(NULL);

    fclose(f);
    return 0;
}

int vs_custom_slash_load_dir(VSCustomSlashStore *store)
{
    if (!store) return -1;

    /* Expand ~ in path */
    char expanded[VS_MAX_PATH];
    if (store->commands_dir[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) home = "/tmp";
        snprintf(expanded, sizeof(expanded), "%s%s", home, store->commands_dir + 1);
    } else {
        snprintf(expanded, sizeof(expanded), "%s", store->commands_dir);
    }

    DIR *d = opendir(expanded);
    if (!d) return -1;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && store->command_count < VS_MAX_CUSTOM_SLASH) {
        /* Skip . and .. */
        if (entry->d_name[0] == '.') continue;

        /* Only process .md files */
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".md") != 0) continue;

        char path[VS_MAX_PATH];
        snprintf(path, sizeof(path), "%s/%s", expanded, entry->d_name);

        /* Check if it's a regular file */
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        VSCustomSlashCommand *cmd = &store->commands[store->command_count];
        if (custom_slash_parse_markdown(path, cmd) == 0) {
            store->command_count++;
        }
    }

    closedir(d);
    return store->command_count;
}

VSCustomSlashCommand *vs_custom_slash_find(VSCustomSlashStore *store, const char *name)
{
    if (!store || !name) return NULL;
    for (int i = 0; i < store->command_count; i++) {
        if (strcmp(store->commands[i].name, name) == 0) {
            return &store->commands[i];
        }
    }
    return NULL;
}

char *vs_custom_slash_execute(VSCustomSlashStore *store, const char *name, const char *args)
{
    if (!store || !name) return NULL;

    VSCustomSlashCommand *cmd = vs_custom_slash_find(store, name);
    if (!cmd) {
        char *buf = (char *)malloc(128);
        if (buf) snprintf(buf, 128, "Custom command not found: %s\n", name);
        return buf;
    }

    /* Replace $ARGUMENTS in body with args */
    char *body = (char *)malloc(sizeof(cmd->body));
    if (!body) return NULL;
    snprintf(body, sizeof(cmd->body), "%s", cmd->body);

    if (args && strlen(args) > 0) {
        char *pos = strstr(body, "$ARGUMENTS");
        if (pos) {
            int before = pos - body;
            int after_len = strlen(pos + strlen("$ARGUMENTS"));
            int new_len = before + strlen(args) + after_len;
            if (new_len < (int)sizeof(cmd->body)) {
                memmove(body + before + strlen(args), pos + strlen("$ARGUMENTS"), after_len + 1);
                memcpy(body + before, args, strlen(args));
            }
        }
    }

    /* Handle !shell commands */
    char *shell_cmd = strstr(body, "!");
    if (shell_cmd && shell_cmd > body && shell_cmd[-1] == '\n') {
        shell_cmd++; /* skip ! */
        /* Trim leading whitespace */
        while (*shell_cmd == ' ' || *shell_cmd == '\t') shell_cmd++;
        int *exit_code = NULL;
        char *result = vs_run_command(shell_cmd, exit_code);
        free(body);
        return result;
    }

    char *result = (char *)malloc(strlen(body) + 64);
    if (result) {
        snprintf(result, strlen(body) + 64, "Custom command '%s':\n%s", name, body);
    }
    free(body);
    return result;
}

char *vs_custom_slash_list(VSCustomSlashStore *store, int max_lines)
{
    if (!store) {
        char *buf = (char *)malloc(64);
        if (buf) snprintf(buf, 64, "No custom slash store\n");
        return buf;
    }

    char *buf = (char *)malloc(4096);
    if (!buf) return NULL;

    int pos = 0;
    pos += snprintf(buf + pos, 4096 - pos,
                    "Custom slash commands: %d loaded from %s\n",
                    store->command_count, store->commands_dir);

    for (int i = 0; i < store->command_count && i < max_lines; i++) {
        VSCustomSlashCommand *cmd = &store->commands[i];
        pos += snprintf(buf + pos, 4096 - pos, "  /%s (%s)\n", cmd->name, cmd->path);
        if (pos >= 4000) break;
    }

    return buf;
}

/* ========================================================================
 * Markdown table rendering
 * ======================================================================== */

int vs_table_parse(const char *text, VSTable *table)
{
    if (!text || !table) return -1;

    memset(table, 0, sizeof(*table));

    char buf[VS_MAX_TEXT];
    snprintf(buf, sizeof(buf), "%s", text);
    char *b = buf;

    int row_idx = 0;
    while (*b && row_idx < VS_TABLE_MAX_ROWS) {
        /* Skip empty lines */
        while (*b && (*b == '\n' || *b == '\r')) b++;
        if (!*b) break;

        /* Parse a row: split by | */
        VSTableRow *row = &table->rows[row_idx];
        int col = 0;

        /* Skip leading | */
        if (*b == '|') b++;

        while (*b && col < VS_TABLE_MAX_COLS) {
            char *cell = row->cells[col];
            int cell_len = 0;
            int had_content = 0;
            while (*b && *b != '|' && *b != '\n' && *b != '\r' && cell_len < VS_TABLE_CELL_MAX - 1) {
                /* Trim leading spaces */
                if (*b == ' ' && cell_len == 0) { b++; continue; }
                had_content = 1;
                cell[cell_len++] = *b++;
            }
            /* Trim trailing spaces */
            while (cell_len > 0 && cell[cell_len-1] == ' ') cell_len--;
            cell[cell_len] = '\0';
            if (had_content || col < row->col_count) {
                row->col_count = col + 1;
            }
            col++;

            if (*b == '|') b++;
        }
        /* Trim trailing empty columns */
        while (row->col_count > 0) {
            int last = row->col_count - 1;
            if (strlen(row->cells[last]) == 0) {
                row->col_count = last;
            } else break;
        }

        /* Skip newline at end of current row */
        while (*b && (*b == '\n' || *b == '\r')) b++;

        /* Check if next line is a separator (---|---|---) */
        int is_separator = 0;
        if (*b) {
            char *sep_check = b;
            int all_sep = 1;
            while (*sep_check && *sep_check != '\n' && *sep_check != '\r') {
                if (*sep_check != '-' && *sep_check != ':' && *sep_check != ' ' && *sep_check != '|') {
                    all_sep = 0;
                    break;
                }
                sep_check++;
            }
            if (all_sep && sep_check > b && (*sep_check == '\n' || *sep_check == '\r')) {
                is_separator = 1;
            }
        }
        if (is_separator) {
            table->header_row = 1;
            /* Skip the separator line */
            while (*b && (*b != '\n' && *b != '\r')) b++;
            while (*b && (*b == '\n' || *b == '\r')) b++;
        }

        /* Skip newline */
        while (*b && (*b == '\n' || *b == '\r')) b++;

        row_idx++;
    }

    table->row_count = row_idx;
    if (row_idx > 0) {
        table->col_count = table->rows[0].col_count;
    }

    return row_idx;
}

char *vs_table_render(const VSTable *table, int width)
{
    if (!table) return NULL;

    /* Calculate column widths */
    int col_widths[VS_TABLE_MAX_COLS] = {0};
    for (int r = 0; r < table->row_count; r++) {
        for (int c = 0; c < table->rows[r].col_count && c < VS_TABLE_MAX_COLS; c++) {
            int w = (int)strlen(table->rows[r].cells[c]);
            if (w > col_widths[c]) col_widths[c] = w;
        }
    }

    /* Ensure minimum width of 3 for separator */
    for (int c = 0; c < VS_TABLE_MAX_COLS; c++) {
        if (col_widths[c] < 3) col_widths[c] = 3;
    }

    /* Calculate total width */
    int total_width = 0;
    int max_cols = table->col_count;
    for (int c = 0; c < max_cols; c++) {
        total_width += col_widths[c] + 4; /* cell + padding + | */
    }

    char *buf = (char *)malloc(width * (table->row_count + 2) + 1024);
    if (!buf) return NULL;

    int pos = 0;
    int buf_size = width * (table->row_count + 2) + 1024;

    /* Render each row */
    for (int r = 0; r < table->row_count; r++) {
        /* Start of row */
        pos += snprintf(buf + pos, buf_size - pos, "| ");
        for (int c = 0; c < max_cols; c++) {
            const char *cell = (c < table->rows[r].col_count) ? table->rows[r].cells[c] : "";
            pos += snprintf(buf + pos, buf_size - pos, "%-*s | ", col_widths[c], cell);
        }
        pos += snprintf(buf + pos, buf_size - pos, "\n");

        /* Separator after header */
        if (r == 0 && table->header_row) {
            pos += snprintf(buf + pos, buf_size - pos, "|-");
            for (int c = 0; c < max_cols; c++) {
                for (int s = 0; s < col_widths[c] + 2; s++)
                    pos += snprintf(buf + pos, buf_size - pos, "-");
                pos += snprintf(buf + pos, buf_size - pos, "|-");
            }
            pos += snprintf(buf + pos, buf_size - pos, "\n");
        }
    }

    return buf;
}

/* ========================================================================
 * @-mention context resolution
 * ======================================================================== */

int vs_mention_parse(const char *text, VSMention *mentions, int max_mentions)
{
    if (!text || !mentions) return 0;

    int count = 0;
    const char *p = text;
    while (*p && count < max_mentions) {
        if (*p == '@' && *(p+1) && *(p+1) != ' ') {
            const char *start = p + 1;
            while (*p && *p != ' ' && *p != '\n' && *p != '\t') p++;
            int len = (int)(p - start);
            if (len > 0 && len < VS_MENTION_TEXT_MAX) {
                snprintf(mentions[count].text, sizeof(mentions[count].text), "%.*s", len, start);
                mentions[count].type = 0; /* default: file */
                mentions[count].resolved[0] = '\0';
                count++;
            }
        } else {
            p++;
        }
    }
    return count;
}

char *vs_mention_resolve(VSContext *ctx, const char *mention_text, int type)
{
    if (!mention_text) return NULL;

    char *buf = (char *)malloc(VS_MENTION_RESOLVED_MAX);
    if (!buf) return NULL;

    switch (type) {
        case 0: /* File */
            if (ctx) {
                char *content = vs_read_file(mention_text);
                if (content) {
                    snprintf(buf, VS_MENTION_RESOLVED_MAX,
                             "File: %s\n```\n%s\n```", mention_text, content);
                    free(content);
                } else {
                    snprintf(buf, VS_MENTION_RESOLVED_MAX,
                             "[File not found: %s]", mention_text);
                }
            } else {
                snprintf(buf, VS_MENTION_RESOLVED_MAX,
                         "[File: %s (no context)]", mention_text);
            }
            break;
        case 1: /* Memory */
            if (ctx) {
                VSMemoryEntry *entry = vs_memory_get(&ctx->memory, mention_text);
                if (entry) {
                    snprintf(buf, VS_MENTION_RESOLVED_MAX,
                             "[Memory: %s] %s", mention_text, entry->value);
                } else {
                    snprintf(buf, VS_MENTION_RESOLVED_MAX,
                             "[Memory not found: %s]", mention_text);
                }
            } else {
                snprintf(buf, VS_MENTION_RESOLVED_MAX,
                         "[Memory: %s (no context)]", mention_text);
            }
            break;
        case 2: /* Skill */
            if (ctx) {
                VSSkill *skill = vs_skill_find(ctx->skills, ctx->skill_count, mention_text);
                if (skill) {
                    snprintf(buf, VS_MENTION_RESOLVED_MAX,
                             "[Skill: %s] %s", mention_text, skill->path);
                } else {
                    snprintf(buf, VS_MENTION_RESOLVED_MAX,
                             "[Skill not found: %s]", mention_text);
                }
            } else {
                snprintf(buf, VS_MENTION_RESOLVED_MAX,
                         "[Skill: %s (no context)]", mention_text);
            }
            break;
        case 3: /* Task */
            snprintf(buf, VS_MENTION_RESOLVED_MAX,
                     "[Task reference: %s]", mention_text);
            break;
        default:
            snprintf(buf, VS_MENTION_RESOLVED_MAX,
                     "[Mention: %s]", mention_text);
            break;
    }

    return buf;
}

char *vs_mention_inject_context(const VSMention *mentions, int count, const char *original)
{
    if (!mentions || !original) {
        char *buf = (char *)malloc(256);
        if (buf) snprintf(buf, 256, "%s", original ? original : "");
        return buf;
    }

    /* Build context block with all mentions */
    char *context = (char *)malloc(VS_MENTION_RESOLVED_MAX * count + 1024);
    if (!context) return NULL;

    int pos = 0;
    pos += snprintf(context + pos, VS_MENTION_RESOLVED_MAX * count + 1024 - pos,
                    "Context from @-mentions:\n");
    for (int i = 0; i < count; i++) {
        if (mentions[i].resolved[0]) {
            pos += snprintf(context + pos, VS_MENTION_RESOLVED_MAX * count + 1024 - pos,
                           "---\n%s\n", mentions[i].resolved);
        }
    }
    pos += snprintf(context + pos, VS_MENTION_RESOLVED_MAX * count + 1024 - pos,
                   "\nOriginal message:\n%s\n", original);

    return context;
}

/* ========================================================================
 * Extended effort levels
 * ======================================================================== */

const char *vs_effort_label(int effort)
{
    switch (effort) {
        case VS_EFFORT_LOW: return "low";
        case VS_EFFORT_MEDIUM: return "medium";
        case VS_EFFORT_HIGH: return "high";
        case VS_EFFORT_XHIGH: return "xhigh";
        case VS_EFFORT_MAX: return "max";
        case VS_EFFORT_ULTRACODE: return "ultracode";
        default: return "default";
    }
}

int vs_effort_from_string(const char *s)
{
    if (!s) return 0;
    if (strcmp(s, "low") == 0) return VS_EFFORT_LOW;
    if (strcmp(s, "medium") == 0) return VS_EFFORT_MEDIUM;
    if (strcmp(s, "high") == 0) return VS_EFFORT_HIGH;
    if (strcmp(s, "xhigh") == 0) return VS_EFFORT_XHIGH;
    if (strcmp(s, "max") == 0) return VS_EFFORT_MAX;
    if (strcmp(s, "ultracode") == 0) return VS_EFFORT_ULTRACODE;
    return 0;
}

/* ========================================================================
 * PreCompact hooks
 * ======================================================================== */

int vs_hooks_run_pre_compact(VSHooksStore *store, const char *context_summary)
{
    if (!store || !context_summary) return -1;

    /* Run all hooks with VS_HOOK_PRE_COMPACT type */
    int ran = 0;
    for (int i = 0; i < store->hook_count; i++) {
        VSHook *hook = &store->hooks[i];
        if (hook->hook_type == VS_HOOK_PRE_COMPACT) {
            if (hook->command[0]) {
                char cmd[1024];
                snprintf(cmd, sizeof(cmd), "%s \"%s\"", hook->command, context_summary);
                int *exit_code = NULL;
                char *result = vs_run_command(cmd, exit_code);
                if (result) {
                    free(result);
                    ran++;
                }
            }
            if (hook->webhook_url[0]) {
                /* Send webhook notification */
                char payload[2048];
                snprintf(payload, sizeof(payload),
                         "{\"event\":\"pre_compact\",\"context_summary\":\"%s\"}",
                         context_summary);
                const char *headers[] = {"Content-Type: application/json"};
                long status;
                char *result = vs_http_post(hook->webhook_url, headers, 1, payload, &status);
                if (result) {
                    free(result);
                    ran++;
                }
            }
        }
    }
    return ran;
}

/* ========================================================================
 * Git branch automation
 * ======================================================================== */

char *vs_git_branch_create(const char *name, const char *path)
{
    if (!name) return NULL;

    char cmd[VS_MAX_PATH + 128];
    if (path)
        snprintf(cmd, sizeof(cmd), "cd '%s' && git checkout -b '%s' 2>&1", path, name);
    else
        snprintf(cmd, sizeof(cmd), "git checkout -b '%s' 2>&1", name);

    int *exit_code = NULL;
    return vs_run_command(cmd, exit_code);
}

char *vs_git_branch_list(const char *path, int max_lines)
{
    char cmd[VS_MAX_PATH + 128];
    if (path)
        snprintf(cmd, sizeof(cmd), "cd '%s' && git branch -a 2>&1 | head -n %d", path, max_lines);
    else
        snprintf(cmd, sizeof(cmd), "git branch -a 2>&1 | head -n %d", max_lines);

    int *exit_code = NULL;
    return vs_run_command(cmd, exit_code);
}

char *vs_git_diff_staged(const char *path, int max_lines)
{
    char cmd[VS_MAX_PATH + 128];
    if (path)
        snprintf(cmd, sizeof(cmd), "cd '%s' && git diff --staged 2>&1 | head -n %d", path, max_lines);
    else
        snprintf(cmd, sizeof(cmd), "git diff --staged 2>&1 | head -n %d", max_lines);

    int *exit_code = NULL;
    return vs_run_command(cmd, exit_code);
}

char *vs_git_commit_generate_message(const char *path)
{
    /* Generate a commit message based on staged changes */
    char cmd[VS_MAX_PATH + 256];
    if (path)
        snprintf(cmd, sizeof(cmd),
                 "cd '%s' && git diff --staged --stat 2>&1 | head -20", path);
    else
        snprintf(cmd, sizeof(cmd),
                 "git diff --staged --stat 2>&1 | head -20");

    int *exit_code = NULL;
    char *stat_output = vs_run_command(cmd, exit_code);

    char *message = (char *)malloc(1024);
    if (!message) { free(stat_output); return NULL; }

    if (stat_output && strlen(stat_output) > 0) {
        /* Create a summary-based commit message */
        snprintf(message, 1024,
                 "Auto-generated commit:\n\nChanges summary:\n%s\n\n(This message was generated based on staged changes)",
                 stat_output);
    } else {
        snprintf(message, 1024, "No staged changes detected");
    }

    free(stat_output);
    return message;
}

char *vs_git_pr_description(const char *path, const char *branch, int max_lines)
{
    char cmd[VS_MAX_PATH + 256];
    if (path && branch) {
        snprintf(cmd, sizeof(cmd),
                 "cd '%s' && git log --oneline main..%s 2>&1 | head -n %d",
                 path, branch, max_lines);
    } else if (branch) {
        snprintf(cmd, sizeof(cmd),
                 "git log --oneline main..%s 2>&1 | head -n %d",
                 branch, max_lines);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "git log --oneline -20 2>&1");
    }

    int *exit_code = NULL;
    char *log_output = vs_run_command(cmd, exit_code);

    char *desc = (char *)malloc(4096);
    if (!desc) { free(log_output); return NULL; }

    if (branch) {
        snprintf(desc, 4096,
                 "# Pull Request: %s\n\n## Changes\n\n%s\n\n## Testing\n\n- [ ] Unit tests pass\n- [ ] Manual verification complete\n\n## Notes\n\nAuto-generated PR description",
                 branch, log_output ? log_output : "No commit history found");
    } else {
        snprintf(desc, 4096,
                 "# Pull Request\n\n## Changes\n\n%s\n\n## Testing\n\n- [ ] Unit tests pass\n\n## Notes\n\nAuto-generated PR description",
                 log_output ? log_output : "No commit history found");
    }

    free(log_output);
    return desc;
}
