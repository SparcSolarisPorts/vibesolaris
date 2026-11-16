/* SPDX-License-Identifier: Unlicense */
/* TUI Editor tests */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests = 0;
static int passed = 0;

static void check(int cond, const char *msg)
{
    tests++;
    if (cond) passed++;
    else printf("FAIL: %s\n", msg);
}

int main(void)
{
    VSTUIEditor ed;

    /* Test 1: Init */
    vs_tui_editor_init(&ed);
    check(ed.line_count == 1, "init has 1 line");
    check(ed.cursor_line == 0, "cursor at line 0");
    check(ed.cursor_col == 0, "cursor at col 0");
    check(ed.mode == 0, "init in insert mode");
    check(ed.lines[0][0] == 0, "empty first line");

    /* Test 2: Insert characters */
    vs_tui_editor_insert_char(&ed, 'H');
    vs_tui_editor_insert_char(&ed, 'i');
    check(strcmp(ed.lines[0], "Hi") == 0, "insert Hi");
    check(ed.cursor_col == 2, "cursor after Hi");

    /* Test 3: Insert at cursor position */
    vs_tui_editor_move_home(&ed);
    vs_tui_editor_insert_char(&ed, 'X');
    check(strcmp(ed.lines[0], "XHi") == 0, "insert at beginning");
    check(ed.cursor_col == 1, "cursor after X");

    /* Test 4: Backspace */
    vs_tui_editor_move_end(&ed);
    vs_tui_editor_backspace(&ed);
    check(strcmp(ed.lines[0], "XH") == 0, "backspace removes i");
    check(ed.cursor_col == 2, "cursor at end");

    /* Test 5: Backspace at beginning of first line (no-op) */
    vs_tui_editor_move_home(&ed);
    vs_tui_editor_backspace(&ed);
    check(strcmp(ed.lines[0], "XH") == 0, "backspace at start of line 0 is no-op");
    check(ed.cursor_col == 0, "cursor at home");

    /* Test 6: Newline */
    vs_tui_editor_move_end(&ed);
    vs_tui_editor_insert_char(&ed, 'I');
    check(strcmp(ed.lines[0], "XHI") == 0, "line is XHI");
    vs_tui_editor_newline(&ed);
    check(ed.line_count == 2, "newline creates 2 lines");
    check(strcmp(ed.lines[0], "XHI") == 0, "first line XHI");
    check(strcmp(ed.lines[1], "") == 0, "second line empty");
    check(ed.cursor_line == 1, "cursor on new line");
    check(ed.cursor_col == 0, "cursor at col 0");

    /* Test 7: Newline in middle */
    vs_tui_editor_init(&ed);
    vs_tui_editor_insert_char(&ed, 'a');
    vs_tui_editor_insert_char(&ed, 'b');
    vs_tui_editor_insert_char(&ed, 'c');
    vs_tui_editor_move_home(&ed);
    vs_tui_editor_move_right(&ed);
    vs_tui_editor_newline(&ed);
    check(ed.line_count == 2, "middle newline creates 2 lines");
    check(strcmp(ed.lines[0], "a") == 0, "first line a");
    check(strcmp(ed.lines[1], "bc") == 0, "second line bc");

    /* Test 8: Move up/down */
    vs_tui_editor_init(&ed);
    vs_tui_editor_insert_char(&ed, '1');
    vs_tui_editor_newline(&ed);
    vs_tui_editor_insert_char(&ed, '2');
    vs_tui_editor_newline(&ed);
    vs_tui_editor_insert_char(&ed, '3');
    check(ed.cursor_line == 2, "cursor on line 2");
    vs_tui_editor_move_up(&ed);
    check(ed.cursor_line == 1, "move up to line 1");
    vs_tui_editor_move_up(&ed);
    check(ed.cursor_line == 0, "move up to line 0");
    vs_tui_editor_move_up(&ed);
    check(ed.cursor_line == 0, "cannot move up past 0");
    vs_tui_editor_move_down(&ed);
    check(ed.cursor_line == 1, "move down to line 1");

    /* Test 9: Move left/right */
    vs_tui_editor_init(&ed);
    vs_tui_editor_insert_char(&ed, 'a');
    vs_tui_editor_insert_char(&ed, 'b');
    vs_tui_editor_insert_char(&ed, 'c');
    vs_tui_editor_move_home(&ed);
    vs_tui_editor_move_right(&ed);
    check(ed.cursor_col == 1, "move right to col 1");
    vs_tui_editor_move_right(&ed);
    check(ed.cursor_col == 2, "move right to col 2");
    vs_tui_editor_move_left(&ed);
    check(ed.cursor_col == 1, "move left to col 1");
    vs_tui_editor_move_left(&ed);
    check(ed.cursor_col == 0, "move left to col 0");
    vs_tui_editor_move_left(&ed);
    check(ed.cursor_col == 0, "cannot move left past 0");

    /* Test 10: Move home/end */
    vs_tui_editor_init(&ed);
    vs_tui_editor_insert_char(&ed, 'a');
    vs_tui_editor_insert_char(&ed, 'b');
    vs_tui_editor_insert_char(&ed, 'c');
    vs_tui_editor_move_home(&ed);
    check(ed.cursor_col == 0, "home at col 0");
    vs_tui_editor_move_end(&ed);
    check(ed.cursor_col == 3, "end at col 3");

    /* Test 11: Get text */
    vs_tui_editor_init(&ed);
    vs_tui_editor_insert_char(&ed, 'H');
    vs_tui_editor_insert_char(&ed, 'i');
    vs_tui_editor_newline(&ed);
    vs_tui_editor_insert_char(&ed, 'W');
    vs_tui_editor_insert_char(&ed, 'o');
    vs_tui_editor_insert_char(&ed, 'r');
    vs_tui_editor_insert_char(&ed, 'l');
    vs_tui_editor_insert_char(&ed, 'd');
    char text[256];
    int len = vs_tui_editor_get_text(&ed, text, sizeof(text));
    check(len == 8, "text length 8");
    check(strcmp(text, "Hi\nWorld") == 0, "text is Hi\\nWorld");

    /* Test 12: Get text with NULL */
    check(vs_tui_editor_get_text(NULL, text, sizeof(text)) == 0, "NULL editor");
    check(vs_tui_editor_get_text(&ed, NULL, 0) == 0, "NULL output");

    /* Test 13: Backspace at col 0 on line > 0 joins lines */
    vs_tui_editor_init(&ed);
    vs_tui_editor_insert_char(&ed, 'a');
    vs_tui_editor_newline(&ed);
    vs_tui_editor_insert_char(&ed, 'b');
    /* cursor is at line 1, col 1. Move to col 0 */
    vs_tui_editor_move_home(&ed);
    vs_tui_editor_backspace(&ed);
    check(ed.line_count == 1, "backspace joins lines");
    check(strcmp(ed.lines[0], "ab") == 0, "joined line is ab");

    /* Test 14: Normal mode blocks insert */
    vs_tui_editor_init(&ed);
    ed.mode = 1; /* normal mode */
    vs_tui_editor_insert_char(&ed, 'X');
    check(strcmp(ed.lines[0], "") == 0, "normal mode blocks insert");

    /* Test 15: Normal mode blocks backspace */
    vs_tui_editor_init(&ed);
    vs_tui_editor_insert_char(&ed, 'a');
    ed.mode = 1;
    vs_tui_editor_backspace(&ed);
    check(strcmp(ed.lines[0], "a") == 0, "normal mode blocks backspace");

    /* Test 16: Normal mode blocks newline */
    vs_tui_editor_init(&ed);
    ed.mode = 1;
    vs_tui_editor_newline(&ed);
    check(ed.line_count == 1, "normal mode blocks newline");

    /* Test 17: Wrap line */
    char wrapped[256];
    vs_tui_editor_wrap_line("hello world foo bar", 10, wrapped, sizeof(wrapped));
    check(strcmp(wrapped, "hello\nworld foo\nbar") == 0, "wrap at word boundary");

    /* Test 18: Wrap long word */
    vs_tui_editor_wrap_line("superlongword", 5, wrapped, sizeof(wrapped));
    check(strcmp(wrapped, "super\nlongw\nord") == 0, "wrap long word");

    /* Test 19: Wrap NULL */
    char *r = vs_tui_editor_wrap_line(NULL, 10, wrapped, sizeof(wrapped));
    check(r == wrapped, "wrap returns output");
    check(wrapped[0] == 0, "wrap NULL gives empty");

    /* Test 20: Wrap empty */
    vs_tui_editor_wrap_line("", 10, wrapped, sizeof(wrapped));
    check(strcmp(wrapped, "") == 0, "wrap empty string");

    /* Test 21: Wrap with zero width */
    vs_tui_editor_wrap_line("hello", 0, wrapped, sizeof(wrapped));
    check(wrapped[0] == 0, "wrap zero width gives empty");

    /* Test 22: Move left wraps to previous line */
    vs_tui_editor_init(&ed);
    vs_tui_editor_insert_char(&ed, 'a');
    vs_tui_editor_newline(&ed);
    vs_tui_editor_insert_char(&ed, 'b');
    vs_tui_editor_move_up(&ed);
    vs_tui_editor_move_home(&ed);
    vs_tui_editor_move_left(&ed);
    check(ed.cursor_line == 0, "left at start stays on line 0");

    /* Test 23: Move right at end of last line stays */
    vs_tui_editor_init(&ed);
    vs_tui_editor_insert_char(&ed, 'a');
    vs_tui_editor_newline(&ed);
    vs_tui_editor_insert_char(&ed, 'b');
    vs_tui_editor_move_down(&ed);
    vs_tui_editor_move_end(&ed);
    vs_tui_editor_move_right(&ed);
    check(ed.cursor_line == 1, "right at end of last line stays");
    check(ed.cursor_col == 1, "cursor at end of last line");

    /* Test 24: Render with NULL */
    vs_tui_editor_render(NULL, 1, 1, 80); /* should not crash */
    check(1, "render NULL does not crash");

    /* Test 25: Get text truncation */
    vs_tui_editor_init(&ed);
    vs_tui_editor_insert_char(&ed, 'a');
    vs_tui_editor_insert_char(&ed, 'b');
    vs_tui_editor_newline(&ed);
    vs_tui_editor_insert_char(&ed, 'c');
    char small[3];
    len = vs_tui_editor_get_text(&ed, small, sizeof(small));
    check(len <= 2, "truncated text length");
    check(small[2] == 0, "null terminated");

    /* Test 26: Move down clamps column */
    vs_tui_editor_init(&ed);
    vs_tui_editor_insert_char(&ed, 'a');
    vs_tui_editor_insert_char(&ed, 'b');
    vs_tui_editor_insert_char(&ed, 'c');
    vs_tui_editor_newline(&ed);
    vs_tui_editor_insert_char(&ed, 'd');
    vs_tui_editor_move_up(&ed);
    vs_tui_editor_move_end(&ed);
    check(ed.cursor_col == 3, "cursor at end of line 0");
    vs_tui_editor_move_down(&ed);
    check(ed.cursor_col == 1, "column clamped to line 1 length");

    /* Test 27: Move up adjusts scroll offset */
    vs_tui_editor_init(&ed);
    ed.scroll_offset = 5;
    ed.cursor_line = 5;
    vs_tui_editor_move_up(&ed);
    check(ed.scroll_offset == 4, "scroll offset adjusted on move up");

    printf("Result: %d/%d tests passed\n", passed, tests);
    return passed == tests ? 0 : 1;
}
