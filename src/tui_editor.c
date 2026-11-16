/* SPDX-License-Identifier: Unlicense */
/* TUI Editor: multiline text editor with vi-style navigation */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void vs_tui_editor_init(VSTUIEditor *ed)
{
    memset(ed, 0, sizeof(*ed));
    ed->line_count = 1;
    ed->cursor_line = 0;
    ed->cursor_col = 0;
    ed->scroll_offset = 0;
    ed->mode = 0; /* insert mode */
    ed->lines[0][0] = 0;
}

void vs_tui_editor_insert_char(VSTUIEditor *ed, char ch)
{
    if (ed->mode != 0) return; /* normal mode — don't insert */
    if (ed->cursor_line >= VS_TUI_EDITOR_MAX_LINES) return;

    char *line = ed->lines[ed->cursor_line];
    int len = (int)strlen(line);

    /* Bounds check */
    if (len >= VS_TUI_EDITOR_LINE_MAX - 1) return;

    /* Insert at cursor position */
    int col = ed->cursor_col;
    if (col < 0) col = 0;
    if (col > len) col = len;

    /* Shift right */
    memmove(line + col + 1, line + col, (size_t)(len - col + 1));
    line[col] = ch;
    ed->cursor_col = col + 1;
}

void vs_tui_editor_backspace(VSTUIEditor *ed)
{
    if (ed->mode != 0) return;
    char *line = ed->lines[ed->cursor_line];

    if (ed->cursor_col > 0) {
        int col = ed->cursor_col - 1;
        memmove(line + col, line + col + 1, strlen(line + col + 1) + 1);
        ed->cursor_col = col;
    } else if (ed->cursor_line > 0) {
        /* Join with previous line */
        int prev = ed->cursor_line - 1;
        int curr = ed->cursor_line;
        char *prev_line = ed->lines[prev];
        int prev_len = (int)strlen(prev_line);
        int curr_len = (int)strlen(line);

        if (prev_len + curr_len < VS_TUI_EDITOR_LINE_MAX - 1) {
            memcpy(prev_line + prev_len, ed->lines[curr], (size_t)(curr_len + 1));
        }

        /* Shift lines up from curr+1 */
        for (int i = curr; i < ed->line_count - 1; i++) {
            strncpy(ed->lines[i], ed->lines[i + 1], VS_TUI_EDITOR_LINE_MAX - 1);
            ed->lines[i][VS_TUI_EDITOR_LINE_MAX - 1] = 0;
        }
        ed->line_count--;
        ed->lines[ed->line_count][0] = 0;
        ed->cursor_line = prev;
        ed->cursor_col = prev_len;
    }
}

void vs_tui_editor_newline(VSTUIEditor *ed)
{
    if (ed->mode != 0) return;
    if (ed->line_count >= VS_TUI_EDITOR_MAX_LINES - 1) return;

    char *line = ed->lines[ed->cursor_line];
    int col = ed->cursor_col;
    int len = (int)strlen(line);

    /* Shift lines down */
    for (int i = ed->line_count; i > ed->cursor_line; i--) {
        strncpy(ed->lines[i], ed->lines[i - 1], VS_TUI_EDITOR_LINE_MAX - 1);
        ed->lines[i][VS_TUI_EDITOR_LINE_MAX - 1] = 0;
    }

    /* Split current line */
    char after[VS_TUI_EDITOR_LINE_MAX];
    if (col < len) {
        strncpy(after, line + col, VS_TUI_EDITOR_LINE_MAX - 1);
        after[VS_TUI_EDITOR_LINE_MAX - 1] = 0;
    } else {
        after[0] = 0;
    }
    line[col] = 0;

    /* Set new line */
    strncpy(ed->lines[ed->cursor_line + 1], after, VS_TUI_EDITOR_LINE_MAX - 1);
    ed->lines[ed->cursor_line + 1][VS_TUI_EDITOR_LINE_MAX - 1] = 0;

    ed->line_count++;
    ed->cursor_line++;
    ed->cursor_col = 0;
}

void vs_tui_editor_move_up(VSTUIEditor *ed)
{
    if (ed->cursor_line > 0) {
        ed->cursor_line--;
        int max_col = (int)strlen(ed->lines[ed->cursor_line]);
        if (ed->cursor_col > max_col) ed->cursor_col = max_col;
    }
    /* Adjust scroll offset */
    if (ed->cursor_line < ed->scroll_offset) {
        ed->scroll_offset = ed->cursor_line;
    }
}

void vs_tui_editor_move_down(VSTUIEditor *ed)
{
    if (ed->cursor_line < ed->line_count - 1) {
        ed->cursor_line++;
        int max_col = (int)strlen(ed->lines[ed->cursor_line]);
        if (ed->cursor_col > max_col) ed->cursor_col = max_col;
    }
    /* Adjust scroll offset */
    if (ed->cursor_line >= ed->scroll_offset + 10) {
        ed->scroll_offset = ed->cursor_line - 9;
    }
}

void vs_tui_editor_move_left(VSTUIEditor *ed)
{
    if (ed->cursor_col > 0) {
        ed->cursor_col--;
    } else if (ed->cursor_line > 0) {
        ed->cursor_line--;
        ed->cursor_col = (int)strlen(ed->lines[ed->cursor_line]);
    }
}

void vs_tui_editor_move_right(VSTUIEditor *ed)
{
    int len = (int)strlen(ed->lines[ed->cursor_line]);
    if (ed->cursor_col < len) {
        ed->cursor_col++;
    } else if (ed->cursor_line < ed->line_count - 1) {
        ed->cursor_line++;
        ed->cursor_col = 0;
    }
}

void vs_tui_editor_move_home(VSTUIEditor *ed)
{
    ed->cursor_col = 0;
}

void vs_tui_editor_move_end(VSTUIEditor *ed)
{
    ed->cursor_col = (int)strlen(ed->lines[ed->cursor_line]);
}

int vs_tui_editor_get_text(const VSTUIEditor *ed, char *out, size_t cap)
{
    if (!ed || !out || cap == 0) return 0;

    size_t pos = 0;
    for (int i = 0; i < ed->line_count; i++) {
        size_t len = strlen(ed->lines[i]);
        if (pos + len + 1 >= cap) break;
        if (i > 0 && pos < cap - 1) {
            out[pos++] = '\n';
        }
        if (len > 0 && pos + len < cap) {
            memcpy(out + pos, ed->lines[i], len);
            pos += len;
        }
    }
    if (pos < cap) out[pos] = 0;
    return (int)pos;
}

void vs_tui_editor_render(const VSTUIEditor *ed, int row, int col, int width)
{
    if (!ed) return;

    int start = ed->scroll_offset;
    int end = ed->line_count;
    if (end - start > 10) end = start + 10;

    for (int i = start; i < end; i++) {
        int r = row + (i - start);
        /* Erase line and position */
        printf("\033[%d;%dH\033[2K", r, col);

        const char *line = ed->lines[i];
        int len = (int)strlen(line);
        if (len > width) len = width;

        if (len > 0) {
            fwrite(line, 1, (size_t)len, stdout);
        }

        /* Cursor */
        if (i == ed->cursor_line) {
            int c = col + ed->cursor_col;
            if (c > col + width) c = col + width;
            printf("\033[%d;%dH", r, c);
        }
    }
    fflush(stdout);
}

/* Wrap a line at the given width, returning wrapped text with newlines */
char *vs_tui_editor_wrap_line(const char *line, int width, char *out, size_t cap)
{
    if (!line || !out || cap == 0 || width <= 0) {
        if (out && cap > 0) out[0] = 0;
        return out;
    }

    size_t pos = 0;
    const char *p = line;

    while (*p) {
        /* Find next word boundary */
        const char *space = strchr(p, ' ');
        int word_len;

        if (space) {
            word_len = (int)(space - p);
        } else {
            word_len = (int)strlen(p);
        }

        /* If word is longer than width, force break at width */
        if (word_len > width) {
            int break_len = width;
            /* Add newline if not at start of line */
            int chars_on_line2 = 0;
            for (size_t k = pos; k > 0; k--) {
                if (out[k - 1] == '\n') break;
                chars_on_line2++;
            }
            if (chars_on_line2 > 0 && pos < cap - 1) {
                out[pos++] = '\n';
            }
            if (pos + (size_t)break_len < cap) {
                memcpy(out + pos, p, (size_t)break_len);
                pos += break_len;
            }
            p += break_len;
            continue;
        }

        /* Count chars on current output line */
        int chars_on_line = 0;
        for (size_t k = pos; k > 0; k--) {
            if (out[k - 1] == '\n') break;
            chars_on_line++;
        }

        /* Check if word fits on current line (with space if not first) */
        int need_space = chars_on_line > 0 ? 1 : 0;
        if (chars_on_line + need_space + word_len > width) {
            /* Need newline before this word */
            if (pos < cap - 1) {
                out[pos++] = '\n';
            }
        } else if (need_space && pos < cap - 1) {
            /* Add space before word */
            out[pos++] = ' ';
        }

        /* Copy word */
        if (pos + (size_t)word_len < cap) {
            memcpy(out + pos, p, (size_t)word_len);
            pos += word_len;
        }

        p += word_len;
        /* Skip space */
        if (*p == ' ') p++;
    }
    if (pos < cap) out[pos] = 0;
    return out;
}
