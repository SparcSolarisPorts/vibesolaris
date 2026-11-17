/* SPDX-License-Identifier: Unlicense */
/* Full-screen TUI compositor: alternate-screen layout with scrollback,
   status bar, input bar, and optional sidebar for sub-agent tree.

   Mirrors Claude Code's Ink-based layout:
     - Main output panel (scrollback buffer)
     - Input bar at bottom with prompt
     - Status bar showing provider, model, mode, tokens
     - Optional right sidebar showing concurrent sub-agents

   SSH-safe: auto-detects alternate screen support; falls back to
   classic prompt mode when DECSET ?1049 is unavailable. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <ctype.h>

/* ANSI escape sequences */
#define CSI "\033["
#define ALT_ENTER CSI "?1049h" CSI "h"
#define ALT_EXIT CSI "?1049l" CSI "l"
#define CLEAR_SCREEN CSI "2J" CSI "H"
#define CURSOR_HIDE CSI "?25l"
#define CURSOR_SHOW CSI "?25h"
#define CURSOR_MOVE CSI "%d;%dH"
#define ERASE_LINE CSI "2K"
#define ERASE_RESET CSI "2K" RESET
#define BOLD CSI "1m"
#define DIM CSI "2m"
#define RESET CSI "0m"
#define COLOR_CYAN CSI "36m"
#define COLOR_GREEN CSI "32m"
#define COLOR_YELLOW CSI "33m"
#define COLOR_MAGENTA CSI "35m"
#define COLOR_BLUE CSI "34m"
#define COLOR_RED CSI "31m"
#define COLOR_WHITE CSI "37m"
#define COLOR_BG CSI "40m"

static int get_terminal_size(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return 0;
    }
    *rows = 24;
    *cols = 80;
    return -1;
}

int vs_tui_supports_alt_screen(void)
{
    const char *term = getenv("TERM");
    const char *term_program = getenv("TERM_PROGRAM");

    /* SSH without X11 usually lacks alternate screen */
    if (getenv("SSH_TTY") && !getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) {
        /* Some SSH setups support it if the local terminal does */
        if (term && (strstr(term, "xterm") || strstr(term, "screen") || strstr(term, "tmux"))) {
            return 1;
        }
        return 0;
    }

    if (!term || !term[0] || !strcmp(term, "dumb")) return 0;
    if (strstr(term, "xterm") || strstr(term, "screen") || strstr(term, "tmux") ||
        strstr(term, "alacritty") || strstr(term, "kitty") || strstr(term, "foot") ||
        strstr(term, "vte") || strstr(term, "gnome") || strstr(term, "rxvt")) {
        return 1;
    }
    if (term_program && strstr(term_program, "iTerm")) return 1;

    return 0;
}

void vs_tui_compositor_init(VSTUICompositor *comp)
{
    memset(comp, 0, sizeof(*comp));
    comp->scroll_offset = 0;
    comp->term_rows = 24;
    comp->term_cols = 80;
    comp->sidebar_visible = 0;
    comp->sidebar_width = VS_TUI_SIDEBAR_WIDTH;
    comp->mode = VS_TUI_MODE_PROMPT;
    comp->alt_screen_ok = vs_tui_supports_alt_screen();
    get_terminal_size(&comp->term_rows, &comp->term_cols);
}

void vs_tui_compositor_shutdown(VSTUICompositor *comp)
{
    if (comp->mode == VS_TUI_MODE_FULLSCREEN) {
        vs_tui_compositor_exit(comp);
    }
    memset(comp, 0, sizeof(*comp));
}

int vs_tui_compositor_enter(VSTUICompositor *comp)
{
    if (!comp->alt_screen_ok) {
        comp->mode = VS_TUI_MODE_PROMPT;
        return -1;
    }
    fputs(ALT_ENTER CLEAR_SCREEN CURSOR_HIDE, stdout);
    fflush(stdout);
    comp->mode = VS_TUI_MODE_FULLSCREEN;
    vs_tui_compositor_enable_mouse(comp);
    vs_tui_compositor_resize(comp);
    return 0;
}

void vs_tui_compositor_exit(VSTUICompositor *comp)
{
    if (comp->mode != VS_TUI_MODE_FULLSCREEN) return;
    vs_tui_compositor_disable_mouse(comp);
    fputs(CURSOR_SHOW ALT_EXIT, stdout);
    fflush(stdout);
    comp->mode = VS_TUI_MODE_PROMPT;
}

void vs_tui_compositor_resize(VSTUICompositor *comp)
{
    get_terminal_size(&comp->term_rows, &comp->term_cols);
    if (comp->term_rows < 4) comp->term_rows = 4;
    if (comp->term_cols < 20) comp->term_cols = 20;
    if (comp->sidebar_width > comp->term_cols / 3) {
        comp->sidebar_width = comp->term_cols / 3;
    }
}

void vs_tui_compositor_clear(VSTUICompositor *comp)
{
    comp->line_count = 0;
    comp->scroll_offset = 0;
    comp->stream_pos = 0;
    comp->streaming = 0;
    memset(comp->lines, 0, sizeof(comp->lines));
}

void vs_tui_compositor_add_line(VSTUICompositor *comp, const char *line)
{
    if (!line) line = "";
    if (comp->line_count >= VS_TUI_SCROLLBACK_LINES) {
        /* Shift down: drop oldest lines */
        int drop = comp->line_count - VS_TUI_SCROLLBACK_LINES / 2;
        memmove(&comp->lines[0], &comp->lines[drop],
                (size_t)(VS_TUI_SCROLLBACK_LINES - drop) * sizeof(comp->lines[0]));
        comp->line_count -= drop;
    }
    strncpy(comp->lines[comp->line_count], line, VS_TUI_LINE_MAX - 1);
    comp->lines[comp->line_count][VS_TUI_LINE_MAX - 1] = 0;
    comp->line_count++;
}

void vs_tui_compositor_add_lines(VSTUICompositor *comp, const char *text)
{
    const char *p = text;
    char line[VS_TUI_LINE_MAX];
    int li = 0;

    while (*p && li < VS_TUI_LINE_MAX - 1) {
        if (*p == '\n') {
            line[li] = 0;
            vs_tui_compositor_add_line(comp, line);
            li = 0;
            p++;
        } else if (*p == '\r') {
            p++;
        } else {
            line[li++] = *p++;
        }
    }
    if (li > 0) {
        line[li] = 0;
        vs_tui_compositor_add_line(comp, line);
    }
}

/* Render the full screen layout */
void vs_tui_compositor_render(const VSTUICompositor *comp, const VSContext *ctx)
{
    if (comp->mode != VS_TUI_MODE_FULLSCREEN) return;

    int sidebar_w = comp->sidebar_visible ? comp->sidebar_width : 0;
    int main_w = comp->term_cols - sidebar_w;
    int status_h = 2; /* input + status */
    int content_h = comp->term_rows - status_h;
    int visible_lines = comp->line_count - comp->scroll_offset;

    /* Clear and position */
    fputs(CLEAR_SCREEN, stdout);

    /* Render scrollback content */
    int start = comp->line_count - visible_lines;
    if (start < 0) start = 0;
    int row = 1;
    int col = 1;

    for (int i = start; i < comp->line_count && row < content_h; i++) {
        printf(CSI "%d;%dH" ERASE_RESET, row, col);
        /* Truncate to main_w */
        fwrite(comp->lines[i], 1, (size_t)(strlen(comp->lines[i]) < (size_t)main_w ? strlen(comp->lines[i]) : (size_t)main_w), stdout);
        row++;
    }

    /* Streaming buffer */
    if (comp->streaming && comp->stream_pos > 0) {
        printf(CSI "%d;%dH" ERASE_LINE, row, col);
        fwrite(comp->stream_buf, 1, (size_t)comp->stream_pos, stdout);
        row++;
    }

    /* Sidebar */
    if (comp->sidebar_visible) {
        /* Will be filled by render_sidebar */
    }

    /* Status bar */
    vs_tui_compositor_render_status_bar(comp, ctx);

    /* Input bar */
    vs_tui_compositor_render_input((VSTUICompositor*)comp);

    /* Position cursor at end of input */
    int input_row = comp->term_rows - 1;
    int input_col = 1 + (int)strlen(comp->input_buf);
    if (input_col > main_w) input_col = main_w;
    printf(CSI "%d;%dH", input_row, input_col);

    fflush(stdout);
}

void vs_tui_compositor_render_status_bar(const VSTUICompositor *comp, const VSContext *ctx)
{
    int row = comp->term_rows;
    char status[1024];
    int len;
    int pad;

    /* Background bar */
    printf(CSI "%d;1H" ERASE_LINE COLOR_BG COLOR_WHITE BOLD, row);

    if (ctx) {
        const char *effort_str = "def";
        if (ctx->effort == 1) effort_str = "low";
        else if (ctx->effort == 2) effort_str = "med";
        else if (ctx->effort == 3) effort_str = "high";

        const char *cache_str = "";
        if (ctx->cache_enabled) cache_str = " cache:on";

        char lsp_str[32] = "";
        if (ctx->lsp_server_count > 0) {
            snprintf(lsp_str, sizeof(lsp_str), " lsp:%d", ctx->lsp_server_count);
        }
        len = snprintf(status, sizeof(status),
            " %s/%s  [%s]  effort:%s%s%s  tokens:%lu/%lu",
            ctx->provider.name, ctx->provider.model,
            vs_workflow_mode_name(ctx->workflow.mode),
            effort_str, cache_str, lsp_str,
            ctx->conversation_input_tokens, ctx->conversation_output_tokens);
    } else {
        len = (int)snprintf(status, sizeof(status), " VibeSolaris");
    }

    /* Append search mode indicator */
    if (comp->search_mode) {
        int remaining = (int)sizeof(status) - len;
        if (remaining > 0) {
            snprintf(status + len, (size_t)remaining,
                "  [SEARCH: %s (%d/%d)]",
                comp->search_query,
                comp->search_current + 1,
                comp->search_result_count);
        }
    }

    /* Right-align help hint with padding */
    len = (int)strlen(status);
    pad = comp->term_cols - len - 4;
    if (pad > 0) {
        printf("%s", status);
        while (pad-- > 0) putchar(' ');
    } else {
        printf("%s", status);
    }

    printf(" %s", RESET);
    fflush(stdout);
}

void vs_tui_compositor_render_sidebar(VSTUICompositor *comp, const VSCoordinator *coord)
{
    if (!comp->sidebar_visible || !coord) return;

    int start_col = comp->term_cols - comp->sidebar_width + 1;
    int row = 1;

    /* Sidebar header */
    printf(CSI "%d;%dH" ERASE_LINE BOLD COLOR_CYAN, row, start_col);
    printf(" Agents (%d) ", vs_coordinator_active_count(coord));
    row++;

    /* Agent list */
    for (int i = 0; i < coord->agent_count && row < comp->term_rows - 2; i++) {
        const VSCoordinatorAgent *a = &coord->agents[i];
        const char *status_str;
        const char *color;

        switch (a->status) {
            case VS_AGENT_STATUS_RUNNING: status_str = "RUN"; color = COLOR_GREEN; break;
            case VS_AGENT_STATUS_COMPLETED: status_str = "DON"; color = DIM; break;
            case VS_AGENT_STATUS_FAILED: status_str = "ERR"; color = COLOR_RED; break;
            case VS_AGENT_STATUS_CANCELLED: status_str = "STP"; color = COLOR_YELLOW; break;
            default: status_str = "..."; color = DIM; break;
        }

        printf(CSI "%d;%dH" ERASE_LINE "%s", row, start_col, color);
        char line[128];
        int n = snprintf(line, sizeof(line), " #%d [%s] %.*s",
                         a->id, status_str,
                         comp->sidebar_width - 12, a->task);
        if (n > 0 && (size_t)n < sizeof(line)) {
            fwrite(line, 1, (size_t)n, stdout);
        }
        row++;
    }

    printf("%s", RESET);
    fflush(stdout);
}

void vs_tui_compositor_render_input(VSTUICompositor *comp)
{
    int row = comp->term_rows - 1;
    int sidebar_w = comp->sidebar_visible ? comp->sidebar_width : 0;
    int main_w = comp->term_cols - sidebar_w;

    printf(CSI "%d;1H" ERASE_LINE BOLD COLOR_CYAN, row);
    printf("> ");
    printf("%s", RESET);

    /* Input text */
    int inlen = (int)strlen(comp->input_buf);
    if (inlen > main_w - 3) inlen = main_w - 3;
    if (inlen > 0) {
        fwrite(comp->input_buf, 1, (size_t)inlen, stdout);
    }

    fflush(stdout);
}

void vs_tui_compositor_scroll_up(VSTUICompositor *comp, int lines)
{
    comp->scroll_offset -= lines;
    if (comp->scroll_offset < 0) comp->scroll_offset = 0;
}

void vs_tui_compositor_scroll_down(VSTUICompositor *comp, int lines)
{
    comp->scroll_offset += lines;
    if (comp->scroll_offset >= comp->line_count) {
        comp->scroll_offset = comp->line_count;
    }
}

void vs_tui_compositor_scroll_page_up(VSTUICompositor *comp)
{
    int page = comp->term_rows - 3;
    vs_tui_compositor_scroll_up(comp, page);
}

void vs_tui_compositor_scroll_page_down(VSTUICompositor *comp)
{
    int page = comp->term_rows - 3;
    vs_tui_compositor_scroll_down(comp, page);
}

void vs_tui_compositor_start_streaming(VSTUICompositor *comp)
{
    comp->streaming = 1;
    comp->stream_pos = 0;
    comp->stream_buf[0] = 0;
}

void vs_tui_compositor_append_stream(VSTUICompositor *comp, const char *data, size_t len)
{
    if (!comp->streaming || !data || !len) return;

    /* Append to stream buffer, keeping it bounded */
    size_t space = sizeof(comp->stream_buf) - (size_t)comp->stream_pos - 1;
    if (len > space) len = space;

    memcpy(comp->stream_buf + comp->stream_pos, data, len);
    comp->stream_pos += (int)len;
    comp->stream_buf[comp->stream_pos] = 0;

    /* Render the streaming line in-place */
    int sidebar_w = comp->sidebar_visible ? comp->sidebar_width : 0;
    int main_w = comp->term_cols - sidebar_w;
    int status_h = 2;
    int row = comp->term_rows - status_h;

    printf(CSI "%d;1H" ERASE_LINE, row);
    int display_len = comp->stream_pos;
    if (display_len > main_w) display_len = main_w;
    if (display_len > 0) {
        fwrite(comp->stream_buf, 1, (size_t)display_len, stdout);
    }
    fflush(stdout);
}

void vs_tui_compositor_end_streaming(VSTUICompositor *comp)
{
    if (comp->streaming && comp->stream_pos > 0) {
        comp->stream_buf[comp->stream_pos] = 0;
        vs_tui_compositor_add_lines(comp, comp->stream_buf);
    }
    comp->streaming = 0;
    comp->stream_pos = 0;
    comp->stream_buf[0] = 0;
}

/* Read input in full-screen mode with raw terminal */
int vs_tui_compositor_read_input(VSTUICompositor *comp, char *out, size_t cap)
{
    struct termios oldt, raw;
    int tty = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && tcgetattr(STDIN_FILENO, &oldt) == 0;

    if (!tty) {
        comp->input_buf[0] = 0;
        comp->input_len = 0;
        comp->input_cursor = 0;
        if (!fgets(out, (int)cap, stdin)) return 0;
        out[strcspn(out, "\r\n")] = 0;
        return out[0] ? 1 : 0;
    }

    raw = oldt;
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    comp->input_buf[0] = 0;
    comp->input_len = 0;
    comp->input_cursor = 0;

    unsigned char ch;
    for (;;) {
        if (read(STDIN_FILENO, &ch, 1) != 1) break;

        if (ch == 27) {
            /* Escape sequence */
            unsigned char seq[3] = {0, 0, 0};
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) == 1) {
                    if (seq[1] == 'A') {
                        /* Up arrow - history */
                        if (comp->input_history_pos < comp->input_history_count) {
                            comp->input_history_pos++;
                            snprintf(comp->input_buf, sizeof(comp->input_buf), "%s", comp->input_history[comp->input_history_count - comp->input_history_pos]);
                            comp->input_len = (int)strlen(comp->input_buf);
                            comp->input_cursor = comp->input_len;
                        }
                    } else if (seq[1] == 'B') {
                        /* Down arrow - history */
                        if (comp->input_history_pos > 0) {
                            comp->input_history_pos--;
                            if (comp->input_history_pos == 0) {
                                comp->input_buf[0] = 0;
                                comp->input_len = 0;
                                comp->input_cursor = 0;
                            } else {
                                snprintf(comp->input_buf, sizeof(comp->input_buf), "%s", comp->input_history[comp->input_history_count - comp->input_history_pos]);
                                comp->input_len = (int)strlen(comp->input_buf);
                                comp->input_cursor = comp->input_len;
                            }
                        }
                    } else if (seq[1] == 'C' || seq[1] == 'D') {
                        /* Left/Right arrow */
                        if (seq[1] == 'C' && comp->input_cursor < comp->input_len) {
                            comp->input_cursor++;
                        } else if (seq[1] == 'D' && comp->input_cursor > 0) {
                            comp->input_cursor--;
                        }
                    } else if (seq[1] == '<') {
                        /* SGR mouse event: ESC [ < B ; COL ; ROW M/m */
                        unsigned char mbuf[32];
                        int mi = 0, mrow, mcol, mbtn;
                        mbuf[mi++] = '<';
                        while (mi < (int)sizeof(mbuf) - 1) {
                            unsigned char mc;
                            if (read(STDIN_FILENO, &mc, 1) != 1) break;
                            mbuf[mi++] = mc;
                            if (mc == 'M' || mc == 'm') break;
                        }
                        mbuf[mi] = 0;
                        if (vs_tui_compositor_parse_mouse(mbuf, &mrow, &mcol, &mbtn)) {
                            if (mbtn == 64) {
                                vs_tui_compositor_scroll_page_up(comp);
                            } else if (mbtn == 65) {
                                vs_tui_compositor_scroll_page_down(comp);
                            } else if (mbtn == 0 && mrow == comp->term_rows - 1) {
                                /* Left click on the input bar: place the cursor. */
                                int pos = mcol - 1;
                                if (pos < 0) pos = 0;
                                if (pos > comp->input_len) pos = comp->input_len;
                                comp->input_cursor = pos;
                            }
                        }
                    } else if (seq[1] == '~') {
                        /* ESC [ ~ — some terminals use this for Home/End */
                        (void)seq;
                    } else if (seq[1] >= '1' && seq[1] <= '6') {
                        /* ESC [N~ — Delete(3), PageUp(5), PageDown(6), Home(1), End(4) */
                        unsigned char trail;
                        if (read(STDIN_FILENO, &trail, 1) == 1 && trail == '~') {
                            if (seq[1] == '3') {
                                /* Delete key */
                                if (comp->input_cursor < comp->input_len) {
                                    memmove(comp->input_buf + comp->input_cursor,
                                            comp->input_buf + comp->input_cursor + 1,
                                            (size_t)(comp->input_len - comp->input_cursor));
                                    comp->input_len--;
                                    comp->input_buf[comp->input_len] = 0;
                                }
                            } else if (seq[1] == '5') {
                                vs_tui_compositor_scroll_page_up(comp);
                            } else if (seq[1] == '6') {
                                vs_tui_compositor_scroll_page_down(comp);
                            }
                        }
                    }
                }
            }
            vs_tui_compositor_render_input(comp);
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            comp->input_buf[comp->input_len] = 0;
            /* Save to history */
            if (comp->input_len > 0 && comp->input_buf[0]) {
                if (comp->input_history_count == 0 ||
                    strcmp(comp->input_history[comp->input_history_count - 1], comp->input_buf)) {
                    snprintf(comp->input_history[comp->input_history_count], sizeof(comp->input_history[0]), "%s", comp->input_buf);
                    comp->input_history_count++;
                    if (comp->input_history_count >= 32) {
                        memmove(&comp->input_history[1], &comp->input_history[0], 31 * sizeof(comp->input_history[0]));
                        comp->input_history_count = 31;
                    }
                }
                comp->input_history_pos = 0;
            }
            snprintf(out, cap, "%s", comp->input_buf);
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
            return comp->input_len > 0 ? 1 : 0;
        }

        if (ch == 127 || (ch == 8 && comp->input_cursor > 0)) {
            /* Backspace */
            memmove(comp->input_buf + comp->input_cursor - 1,
                    comp->input_buf + comp->input_cursor,
                    (size_t)(comp->input_len - comp->input_cursor + 1));
            comp->input_len--;
            comp->input_cursor--;
            vs_tui_compositor_render_input(comp);
            continue;
        }

        if (ch >= 32 && ch < 127 && comp->input_len + 1 < (int)sizeof(comp->input_buf)) {
            memmove(comp->input_buf + comp->input_cursor + 1,
                    comp->input_buf + comp->input_cursor,
                    (size_t)(comp->input_len - comp->input_cursor));
            comp->input_buf[comp->input_cursor] = (char)ch;
            comp->input_len++;
            comp->input_cursor++;
            vs_tui_compositor_render_input(comp);
            continue;
        }
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
    comp->input_buf[comp->input_len] = 0;
    snprintf(out, cap, "%s", comp->input_buf);
    return comp->input_len > 0 ? 1 : 0;
}

/* ---- Mouse Support ---- */

void vs_tui_compositor_enable_mouse(VSTUICompositor *comp)
{
    if (comp->mouse_enabled) return;
    fputs(CSI "?1000h" CSI "?1002h" CSI "?1006h", stdout);
    fflush(stdout);
    comp->mouse_enabled = 1;
}

void vs_tui_compositor_disable_mouse(VSTUICompositor *comp)
{
    if (!comp->mouse_enabled) return;
    fputs(CSI "?1006l" CSI "?1002l" CSI "?1000l", stdout);
    fflush(stdout);
    comp->mouse_enabled = 0;
}

/* Parse X11 mouse escape sequence in SGR mode: ESC [ < B ; COL ; ROW M/m
   (seq points at the '<' and must be NUL-terminated).
   Returns 1 on success, 0 on failure.
   button: 0=left, 1=middle, 2=right, 3=release, 64=scroll-up, 65=scroll-down */
int vs_tui_compositor_parse_mouse(unsigned char *seq, int *row, int *col, int *button)
{
    if (!seq || !row || !col || !button) return 0;

    int b = 0, c = 0, r = 0;
    /* SGR mode only: ESC [ < button ; col ; row M/m (plain decimal values) */
    if (seq[0] == '<') {
        int i = 1;
        while (seq[i] >= '0' && seq[i] <= '9') {
            b = b * 10 + (seq[i] - '0');
            i++;
        }
        if (seq[i] != ';') return 0;
        i++;
        while (seq[i] >= '0' && seq[i] <= '9') {
            c = c * 10 + (seq[i] - '0');
            i++;
        }
        if (seq[i] != ';') return 0;
        i++;
        while (seq[i] >= '0' && seq[i] <= '9') {
            r = r * 10 + (seq[i] - '0');
            i++;
        }
        if (seq[i] != 'M' && seq[i] != 'm') return 0;
        *button = b;
        *col = c;
        *row = r;
        return 1;
    }
    return 0;
}

/* ---- Scrollback Search ---- */

/* Search scrollback buffer for query. Returns number of matches found. */
int vs_tui_compositor_search(VSTUICompositor *comp, const char *query)
{
    if (!comp || !query || !*query) return 0;

    comp->search_result_count = 0;
    comp->search_current = 0;
    strncpy(comp->search_query, query, sizeof(comp->search_query) - 1);
    comp->search_query[sizeof(comp->search_query) - 1] = 0;
    comp->search_mode = 1;

    /* Case-insensitive search through all lines */
    for (int i = 0; i < comp->line_count && comp->search_result_count < 64; i++) {
        char lower_line[VS_TUI_LINE_MAX];
        char lower_query[256];
        size_t j;
        for (j = 0; comp->lines[i][j] && j < VS_TUI_LINE_MAX - 1; j++) {
            lower_line[j] = (char)tolower((unsigned char)comp->lines[i][j]);
        }
        lower_line[j] = 0;
        for (j = 0; query[j] && j < 255; j++) {
            lower_query[j] = (char)tolower((unsigned char)query[j]);
        }
        lower_query[j] = 0;

        if (strstr(lower_line, lower_query)) {
            comp->search_result_lines[comp->search_result_count++] = i;
        }
    }

    /* If we found matches, scroll to the first one */
    if (comp->search_result_count > 0) {
        /* Scroll to show the first match */
        int match_line = comp->search_result_lines[0];
        int visible_h = comp->term_rows - 3;
        comp->scroll_offset = comp->line_count - visible_h;
        if (match_line < comp->line_count - comp->scroll_offset) {
            comp->scroll_offset = match_line - 2;
            if (comp->scroll_offset < 0) comp->scroll_offset = 0;
        }
    }

    return comp->search_result_count;
}

/* Jump to next search result */
void vs_tui_compositor_search_next(VSTUICompositor *comp)
{
    if (!comp || !comp->search_mode || comp->search_result_count == 0) return;
    comp->search_current++;
    if (comp->search_current >= comp->search_result_count) {
        comp->search_current = 0; /* Wrap around */
    }
    /* Scroll to show the current match */
    int match_line = comp->search_result_lines[comp->search_current];
    int visible_h = comp->term_rows - 3;
    comp->scroll_offset = comp->line_count - visible_h;
    if (match_line < comp->line_count - comp->scroll_offset) {
        comp->scroll_offset = match_line - 2;
        if (comp->scroll_offset < 0) comp->scroll_offset = 0;
    }
}

/* Jump to previous search result */
void vs_tui_compositor_search_prev(VSTUICompositor *comp)
{
    if (!comp || !comp->search_mode || comp->search_result_count == 0) return;
    comp->search_current--;
    if (comp->search_current < 0) {
        comp->search_current = comp->search_result_count - 1; /* Wrap around */
    }
    int match_line = comp->search_result_lines[comp->search_current];
    int visible_h = comp->term_rows - 3;
    comp->scroll_offset = comp->line_count - visible_h;
    if (match_line < comp->line_count - comp->scroll_offset) {
        comp->scroll_offset = match_line - 2;
        if (comp->scroll_offset < 0) comp->scroll_offset = 0;
    }
}

/* Exit search mode */
void vs_tui_compositor_search_exit(VSTUICompositor *comp)
{
    if (!comp) return;
    comp->search_mode = 0;
    comp->search_query[0] = 0;
    comp->search_result_count = 0;
    comp->search_current = 0;
}
