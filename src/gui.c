/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <errno.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_MAX_MESSAGES 128
#define UI_INPUT_MAX 8192
#define UI_SIDEBAR_W 228
#define UI_TOPBAR_H 58
#define UI_COMPOSER_H 132
#define UI_ROLE_USER 0
#define UI_ROLE_ASSISTANT 1
#define UI_ROLE_TRACE 2

typedef struct {
    int role;
    char *text;
    int collapsed;
    char summary[192];
} UIMessage;

typedef enum {
    MODAL_NONE,
    MODAL_ATTACH,
    MODAL_PROVIDER,
    MODAL_PROTOCOL,
    MODAL_MODEL,
    MODAL_KEY,
    MODAL_BASE,
    MODAL_ACCOUNT,
    MODAL_OAUTH_CONFIG,
    MODAL_OAUTH_EDIT,
    MODAL_GLOBAL_CONFIG,
    MODAL_MCP
} ModalType;

typedef enum {
    SEL_NONE,
    SEL_INPUT,
    SEL_MODAL,
    SEL_MESSAGE
} SelectionKind;

typedef struct {
    SelectionKind kind;
    int message_index;
    size_t anchor;
    size_t cursor;
    int dragging;
} UISelection;

typedef struct {
    unsigned long bg;
    unsigned long sidebar;
    unsigned long panel;
    unsigned long text;
    unsigned long muted;
    unsigned long border;
    unsigned long soft;
    unsigned long accent;
    unsigned long accent_dark;
    unsigned long overlay;
    unsigned long danger;
    unsigned long selection;
} UIColor;

typedef struct {
    Display *dpy;
    int screen;
    Window win;
    GC gc;
    Atom wm_delete;
    Atom clipboard_atom;
    Atom utf8_atom;
    Atom targets_atom;
    Atom paste_atom;
    Atom incr_atom;
    Atom xdnd_aware_atom;
    Atom xdnd_enter_atom;
    Atom xdnd_position_atom;
    Atom xdnd_status_atom;
    Atom xdnd_leave_atom;
    Atom xdnd_drop_atom;
    Atom xdnd_finished_atom;
    Atom xdnd_selection_atom;
    Atom xdnd_action_copy_atom;
    Atom xdnd_type_list_atom;
    Atom uri_list_atom;
    Atom xdnd_property_atom;
    XFontStruct *font;
    XFontStruct *bold;
    XFontStruct *small;
    UIColor c;
    int width;
    int height;
    VSContext ctx;
    UIMessage messages[UI_MAX_MESSAGES];
    int message_count;
    char input[UI_INPUT_MAX];
    size_t input_len;
    size_t input_cursor;
    int scroll_y;
    int auto_scroll;
    ModalType modal;
    char modal_text[VS_MAX_PATH * 2];
    size_t modal_len;
    size_t modal_cursor;
    int provider_sel;
    int protocol_sel;
    int oauth_field;
    VSOAuthFlow oauth_flow;
    char oauth_url[4096];
    char status[512];
    int cursor_visible;
    UISelection selection;
    char *clipboard_text;
    Pixmap back_buffer;
    Drawable canvas;
    int back_w;
    int back_h;
    int has_focus;
    SelectionKind paste_target;
    Atom paste_selection;
    Atom paste_requested_target;
    int paste_incr;
    char *paste_data;
    size_t paste_len;
    size_t paste_cap;
    Window xdnd_source;
    int xdnd_version;
    Atom xdnd_type;
    int xdnd_accept;
    int xdnd_hover;
    int xdnd_pending;
} App;

static const char *provider_names[] = {
    "openai", "claude", "gemini", "glm", "glm-coding",
    "kimi", "qwen", "ernie", "deepseek", "custom"
};
static const char *provider_labels[] = {
    "OpenAI API", "Claude", "Gemini", "GLM", "GLM Coder Plan",
    "Kimi", "Qwen", "ERNIE", "DeepSeek", "Custom API"
};
#define PROVIDER_COUNT 10


static char *ui_dup(const char *s)
{
    size_t n;
    char *p;
    if (!s) s = "";
    n = strlen(s);
    p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

static int mini(int a, int b) { return a < b ? a : b; }
static int maxi(int a, int b) { return a > b ? a : b; }

static int shortcut_mod(unsigned int state)
{
    return (state & (ControlMask | Mod1Mask | Mod4Mask)) != 0;
}

static void selection_clear(App *a)
{
    a->selection.kind = SEL_NONE;
    a->selection.message_index = -1;
    a->selection.anchor = a->selection.cursor = 0;
    a->selection.dragging = 0;
}

static int selection_bounds(const App *a, size_t *lo, size_t *hi)
{
    size_t x, y;
    if (a->selection.kind == SEL_NONE || a->selection.anchor == a->selection.cursor) return 0;
    x = a->selection.anchor; y = a->selection.cursor;
    if (x > y) { size_t t = x; x = y; y = t; }
    if (lo) *lo = x;
    if (hi) *hi = y;
    return 1;
}

static const char *selection_source(const App *a, size_t *len)
{
    const char *s = NULL;
    if (a->selection.kind == SEL_INPUT) { s = a->input; if (len) *len = a->input_len; }
    else if (a->selection.kind == SEL_MODAL) { s = a->modal_text; if (len) *len = a->modal_len; }
    else if (a->selection.kind == SEL_MESSAGE && a->selection.message_index >= 0 && a->selection.message_index < a->message_count) {
        s = a->messages[a->selection.message_index].text;
        if (len) *len = s ? strlen(s) : 0;
    }
    else if (len) *len = 0;
    return s;
}

static char *selected_text(const App *a)
{
    size_t lo, hi, n, slen;
    const char *src = selection_source(a, &slen);
    char *out;
    if (!src || !selection_bounds(a, &lo, &hi)) return NULL;
    if (lo > slen) lo = slen;
    if (hi > slen) hi = slen;
    if (hi <= lo) return NULL;
    n = hi - lo;
    out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, src + lo, n);
    out[n] = 0;
    return out;
}

static void own_selection(App *a, int clipboard)
{
    char *s = selected_text(a);
    if (!s) return;
    free(a->clipboard_text);
    a->clipboard_text = s;
    XSetSelectionOwner(a->dpy, XA_PRIMARY, a->win, CurrentTime);
    if (clipboard) XSetSelectionOwner(a->dpy, a->clipboard_atom, a->win, CurrentTime);
}

static void handle_selection_request(App *a, XSelectionRequestEvent *req)
{
    XEvent out;
    Atom property = req->property ? req->property : req->target;
    memset(&out, 0, sizeof(out));
    out.xselection.type = SelectionNotify;
    out.xselection.display = req->display;
    out.xselection.requestor = req->requestor;
    out.xselection.selection = req->selection;
    out.xselection.target = req->target;
    out.xselection.time = req->time;
    out.xselection.property = None;
    if (req->target == a->targets_atom) {
        Atom targets[3];
        targets[0] = a->utf8_atom; targets[1] = XA_STRING; targets[2] = a->targets_atom;
        XChangeProperty(a->dpy, req->requestor, property, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)targets, 3);
        out.xselection.property = property;
    } else if (a->clipboard_text && (req->target == a->utf8_atom || req->target == XA_STRING)) {
        XChangeProperty(a->dpy, req->requestor, property, req->target, 8, PropModeReplace,
                        (unsigned char *)a->clipboard_text, (int)strlen(a->clipboard_text));
        out.xselection.property = property;
    }
    XSendEvent(a->dpy, req->requestor, False, 0, &out);
    XFlush(a->dpy);
}

static int delete_edit_selection(App *a, SelectionKind kind)
{
    size_t lo, hi, len;
    char *buf;
    size_t *cursor;
    if (a->selection.kind != kind || !selection_bounds(a, &lo, &hi)) return 0;
    if (kind == SEL_INPUT) { buf = a->input; len = a->input_len; cursor = &a->input_cursor; }
    else { buf = a->modal_text; len = a->modal_len; cursor = &a->modal_cursor; }
    if (hi > len) hi = len;
    if (lo > hi) lo = hi;
    memmove(buf + lo, buf + hi, len - hi + 1);
    len -= hi - lo;
    if (kind == SEL_INPUT) a->input_len = len; else a->modal_len = len;
    *cursor = lo;
    selection_clear(a);
    return 1;
}

static void select_all_edit(App *a)
{
    if (a->modal != MODAL_NONE && a->modal != MODAL_PROVIDER && a->modal != MODAL_PROTOCOL &&
        a->modal != MODAL_ACCOUNT && a->modal != MODAL_OAUTH_CONFIG && a->modal != MODAL_GLOBAL_CONFIG) {
        a->selection.kind = SEL_MODAL;
        a->selection.message_index = -1;
        a->selection.anchor = 0;
        a->selection.cursor = a->modal_len;
        a->modal_cursor = a->modal_len;
    } else if (a->selection.kind == SEL_MESSAGE && a->selection.message_index >= 0 && a->selection.message_index < a->message_count) {
        a->selection.anchor = 0;
        a->selection.cursor = strlen(a->messages[a->selection.message_index].text);
    } else {
        a->selection.kind = SEL_INPUT;
        a->selection.message_index = -1;
        a->selection.anchor = 0;
        a->selection.cursor = a->input_len;
        a->input_cursor = a->input_len;
    }
}

static unsigned long alloc_color(App *a, const char *name, unsigned long fallback)
{
    XColor exact, screen;
    Colormap cm;
    cm = DefaultColormap(a->dpy, a->screen);
    if (XAllocNamedColor(a->dpy, cm, name, &screen, &exact)) return screen.pixel;
    return fallback;
}

static void init_colors(App *a)
{
    unsigned long black, white;
    black = BlackPixel(a->dpy, a->screen);
    white = WhitePixel(a->dpy, a->screen);
    a->c.bg = alloc_color(a, "#ffffff", white);
    a->c.sidebar = alloc_color(a, "#f7f7f8", white);
    a->c.panel = alloc_color(a, "#ffffff", white);
    a->c.text = alloc_color(a, "#202123", black);
    a->c.muted = alloc_color(a, "#6b6b74", black);
    a->c.border = alloc_color(a, "#dedee3", black);
    a->c.soft = alloc_color(a, "#f1f1f3", white);
    a->c.accent = alloc_color(a, "#10a37f", black);
    a->c.accent_dark = alloc_color(a, "#0d8f70", black);
    a->c.overlay = alloc_color(a, "#e9e9ec", white);
    a->c.danger = alloc_color(a, "#b42318", black);
    a->c.selection = alloc_color(a, "#bfe9df", white);
}

static XFontStruct *load_font(App *a, const char *pattern, const char *fallback)
{
    XFontStruct *f;
    f = XLoadQueryFont(a->dpy, pattern);
    if (!f) f = XLoadQueryFont(a->dpy, fallback);
    return f;
}

static void set_font(App *a, XFontStruct *f)
{
    if (f) XSetFont(a->dpy, a->gc, f->fid);
}

static int text_w(XFontStruct *f, const char *s)
{
    if (!s || !*s) return 0;
    return XTextWidth(f, s, (int)strlen(s));
}

static void fill_round(App *a, int x, int y, int w, int h, int r, unsigned long color)
{
    if (w <= 0 || h <= 0) return;
    if (r < 1) r = 1;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    XSetForeground(a->dpy, a->gc, color);
    XFillRectangle(a->dpy, a->canvas, a->gc, x + r, y, (unsigned int)maxi(1, w - 2 * r), (unsigned int)h);
    XFillRectangle(a->dpy, a->canvas, a->gc, x, y + r, (unsigned int)w, (unsigned int)maxi(1, h - 2 * r));
    XFillArc(a->dpy, a->canvas, a->gc, x, y, (unsigned int)(2 * r), (unsigned int)(2 * r), 90 * 64, 90 * 64);
    XFillArc(a->dpy, a->canvas, a->gc, x + w - 2 * r, y, (unsigned int)(2 * r), (unsigned int)(2 * r), 0, 90 * 64);
    XFillArc(a->dpy, a->canvas, a->gc, x, y + h - 2 * r, (unsigned int)(2 * r), (unsigned int)(2 * r), 180 * 64, 90 * 64);
    XFillArc(a->dpy, a->canvas, a->gc, x + w - 2 * r, y + h - 2 * r, (unsigned int)(2 * r), (unsigned int)(2 * r), 270 * 64, 90 * 64);
}

static void stroke_round(App *a, int x, int y, int w, int h, int r, unsigned long color)
{
    XSetForeground(a->dpy, a->gc, color);
    XDrawLine(a->dpy, a->canvas, a->gc, x + r, y, x + w - r, y);
    XDrawLine(a->dpy, a->canvas, a->gc, x + r, y + h - 1, x + w - r, y + h - 1);
    XDrawLine(a->dpy, a->canvas, a->gc, x, y + r, x, y + h - r);
    XDrawLine(a->dpy, a->canvas, a->gc, x + w - 1, y + r, x + w - 1, y + h - r);
    XDrawArc(a->dpy, a->canvas, a->gc, x, y, (unsigned int)(2 * r), (unsigned int)(2 * r), 90 * 64, 90 * 64);
    XDrawArc(a->dpy, a->canvas, a->gc, x + w - 2 * r, y, (unsigned int)(2 * r), (unsigned int)(2 * r), 0, 90 * 64);
    XDrawArc(a->dpy, a->canvas, a->gc, x, y + h - 2 * r, (unsigned int)(2 * r), (unsigned int)(2 * r), 180 * 64, 90 * 64);
    XDrawArc(a->dpy, a->canvas, a->gc, x + w - 2 * r, y + h - 2 * r, (unsigned int)(2 * r), (unsigned int)(2 * r), 270 * 64, 90 * 64);
}

static void draw_text(App *a, XFontStruct *f, unsigned long color, int x, int y, const char *s)
{
    if (!s) return;
    set_font(a, f);
    XSetForeground(a->dpy, a->gc, color);
    XDrawString(a->dpy, a->canvas, a->gc, x, y, s, (int)strlen(s));
}

static void draw_ellipsis(App *a, XFontStruct *f, unsigned long color, int x, int y, int maxw, const char *s)
{
    char b[512];
    int n;
    if (!s) s = "";
    if (text_w(f, s) <= maxw) {
        draw_text(a, f, color, x, y, s);
        return;
    }
    n = (int)strlen(s);
    if (n > (int)sizeof(b) - 4) n = (int)sizeof(b) - 4;
    memcpy(b, s, (size_t)n);
    b[n] = 0;
    while (n > 1) {
        b[n - 1] = 0;
        n--;
        if (n + 3 < (int)sizeof(b)) {
            b[n] = '.'; b[n + 1] = '.'; b[n + 2] = '.'; b[n + 3] = 0;
        }
        if (text_w(f, b) <= maxw) break;
    }
    draw_text(a, f, color, x, y, b);
}

static int hit(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

static const char *oauth_status_label(const VSContext *c)
{
    if (vs_oauth_is_signed_in(c)) return "Signed in";
    if (c->oauth.access_token[0]) return "Refresh needed";
    if (vs_oauth_is_configured(c)) return "Ready to sign in";
    return "Not configured";
}

static void start_oauth_login(App *a)
{
    char err[512];
    if (a->oauth_flow.active) {
        strcpy(a->status, "OAuth login is already waiting for the browser callback");
        return;
    }
    err[0] = 0;
    a->oauth_url[0] = 0;
    if (vs_oauth_begin(&a->ctx, &a->oauth_flow, a->oauth_url, sizeof(a->oauth_url), err, sizeof(err)) != 0) {
        snprintf(a->status, sizeof(a->status), "OAuth login could not start: %.430s", err);
        return;
    }
    if (err[0]) snprintf(a->status, sizeof(a->status), "%.470s", err);
    else strcpy(a->status, "Browser opened. Complete ChatGPT/OpenAI authorisation; VibeSolaris is waiting on the loopback callback.");
}

static void draw_caret(App *a, int x, int baseline)
{
    if (!a->cursor_visible) return;
    XSetForeground(a->dpy, a->gc, a->c.text);
    XDrawLine(a->dpy, a->canvas, a->gc, x, baseline - 14, x, baseline + 3);
}

static int wrap_line(XFontStruct *f, const char *p, int maxw, char *out, int cap, const char **next)
{
    int i, last_space, use, width;
    if (!p || !*p) {
        out[0] = 0;
        *next = p;
        return 0;
    }
    if (*p == '\n') {
        out[0] = 0;
        *next = p + 1;
        return 1;
    }
    i = 0;
    last_space = -1;
    while (p[i] && p[i] != '\n' && i < cap - 1) {
        out[i] = p[i];
        out[i + 1] = 0;
        if (p[i] == ' ' || p[i] == '\t') last_space = i;
        width = XTextWidth(f, out, i + 1);
        if (width > maxw) break;
        i++;
    }
    if (p[i] == '\n') {
        use = i;
        *next = p + i + 1;
    } else if (!p[i]) {
        use = i;
        *next = p + i;
    } else if (i >= cap - 1) {
        use = i;
        *next = p + i;
    } else {
        if (last_space > 0) use = last_space;
        else use = maxi(1, i);
        *next = p + use;
        while (**next == ' ' || **next == '\t') (*next)++;
    }
    while (use > 0 && (p[use - 1] == ' ' || p[use - 1] == '\t')) use--;
    if (use >= cap) use = cap - 1;
    memcpy(out, p, (size_t)use);
    out[use] = 0;
    return 1;
}

static int wrapped_height(XFontStruct *f, const char *s, int maxw, int lineh)
{
    const char *p, *n;
    char line[2048];
    int lines;
    if (!s || !*s) return lineh;
    p = s;
    lines = 0;
    while (*p) {
        if (!wrap_line(f, p, maxw, line, sizeof(line), &n)) break;
        lines++;
        if (n == p) break;
        p = n;
    }
    if (lines < 1) lines = 1;
    return lines * lineh;
}

static void draw_selection_span(App *a, XFontStruct *f, int x, int baseline,
                                const char *line, size_t line_off, size_t lo, size_t hi)
{
    size_t llen, a0, a1;
    int x0, x1, h;
    llen = strlen(line);
    a0 = lo > line_off ? lo : line_off;
    a1 = hi < line_off + llen ? hi : line_off + llen;
    if (a1 <= a0) return;
    x0 = x + XTextWidth(f, line, (int)(a0 - line_off));
    x1 = x + XTextWidth(f, line, (int)(a1 - line_off));
    h = f->ascent + f->descent + 2;
    XSetForeground(a->dpy, a->gc, a->c.selection);
    XFillRectangle(a->dpy, a->canvas, a->gc, x0, baseline - f->ascent - 1,
                   (unsigned int)maxi(1, x1 - x0), (unsigned int)h);
}

static int draw_wrapped_message(App *a, int message_index, XFontStruct *f, unsigned long color,
                                int x, int y, int maxw, int lineh, int clip_top, int clip_bottom,
                                const char *s)
{
    const char *p, *n;
    char line[2048];
    int yy, total;
    size_t lo=0, hi=0;
    int has_sel = a->selection.kind == SEL_MESSAGE && a->selection.message_index == message_index &&
                  selection_bounds(a,&lo,&hi);
    if (!s) s = "";
    p = s; yy = y; total = 0;
    set_font(a, f);
    if (!*p) return lineh;
    while (*p) {
        size_t off;
        if (!wrap_line(f, p, maxw, line, sizeof(line), &n)) break;
        off = (size_t)(p - s);
        if (yy >= clip_top - lineh && yy <= clip_bottom + lineh) {
            if (has_sel) draw_selection_span(a,f,x,yy,line,off,lo,hi);
            XSetForeground(a->dpy, a->gc, color);
            XDrawString(a->dpy, a->canvas, a->gc, x, yy, line, (int)strlen(line));
        }
        yy += lineh; total += lineh;
        if (n == p) break;
        p = n;
    }
    return total > 0 ? total : lineh;
}

static const char *base_name(const char *path)
{
    const char *p;
    if (!path) return "";
    p = strrchr(path, '/');
    return p ? p + 1 : path;
}

static void free_messages(App *a)
{
    int i;
    for (i = 0; i < a->message_count; i++) free(a->messages[i].text);
    memset(a->messages, 0, sizeof(a->messages));
    a->message_count = 0;
}

static void add_message(App *a, int role, const char *text)
{
    int i;
    UIMessage *m;
    if (a->message_count >= UI_MAX_MESSAGES) {
        free(a->messages[0].text);
        for (i = 1; i < a->message_count; i++) a->messages[i - 1] = a->messages[i];
        a->message_count--;
    }
    m = &a->messages[a->message_count];
    memset(m, 0, sizeof(*m));
    m->role = role;
    m->text = ui_dup(text ? text : "");
    if (m->text) a->message_count++;
    a->auto_scroll = 1;
}

static void trace_summary(const VSContext *c, char *out, size_t cap)
{
    int i, models, tools, mcps, errors;
    unsigned long steps;
    models = tools = mcps = errors = 0;
    if (!out || cap == 0) return;
    if (!c) { snprintf(out, cap, "Activity"); return; }
    for (i = 0; i < c->trace_count; i++) {
        const char *k = c->trace[i].kind;
        if (!strcmp(k, "model-request")) models++;
        else if (!strcmp(k, "tool-read") || !strcmp(k, "tool-run") || !strcmp(k, "tool-write")) tools++;
        else if (!strcmp(k, "mcp-call")) mcps++;
        if (strstr(k, "error")) errors++;
    }
    steps = (unsigned long)c->trace_count + c->trace_dropped;
    if (errors > 0)
        snprintf(out, cap, "Activity  |  %lu steps  |  %d model  |  %d tool  |  %d MCP  |  %d error%s",
                 steps, models, tools, mcps, errors, errors == 1 ? "" : "s");
    else
        snprintf(out, cap, "Activity  |  %lu steps  |  %d model  |  %d tool  |  %d MCP",
                 steps, models, tools, mcps);
}

static void add_trace_message(App *a, const char *text)
{
    UIMessage *m;
    add_message(a, UI_ROLE_TRACE, text);
    if (a->message_count <= 0) return;
    m = &a->messages[a->message_count - 1];
    m->collapsed = 1;
    trace_summary(&a->ctx, m->summary, sizeof(m->summary));
}

static void remove_attachment(App *a, int idx)
{
    int i;
    if (idx < 0 || idx >= a->ctx.attachment_count) return;
    for (i = idx + 1; i < a->ctx.attachment_count; i++)
        a->ctx.attachments[i - 1] = a->ctx.attachments[i];
    a->ctx.attachment_count--;
}

static int content_x(App *a, int *cw)
{
    int mainw, w;
    mainw = a->width - UI_SIDEBAR_W;
    w = mainw - 64;
    if (w > 780) w = 780;
    if (w < 420) w = maxi(300, mainw - 28);
    if (cw) *cw = w;
    return UI_SIDEBAR_W + (mainw - w) / 2;
}

static int trace_body_height(App *a, const UIMessage *m, int cw)
{
    int w;
    if (!m || m->collapsed) return 0;
    w = cw - 70;
    if (w < 120) w = 120;
    return wrapped_height(a->small, m->text ? m->text : "", w, 18);
}

static int message_step_height(App *a, int index, int cw)
{
    UIMessage *m;
    int maxbubble, th;
    if (index < 0 || index >= a->message_count) return 0;
    m = &a->messages[index];
    maxbubble = (cw * 72) / 100;
    if (m->role == UI_ROLE_USER) {
        th = wrapped_height(a->font, m->text, maxbubble - 28, 20);
        return th + 34;
    }
    if (m->role == UI_ROLE_TRACE) {
        th = trace_body_height(a, m, cw);
        return m->collapsed ? 48 : 62 + th;
    }
    th = wrapped_height(a->font, m->text, cw - 50, 20);
    return maxi(th + 20, 46) + 18;
}

static int message_total_height(App *a, int cw)
{
    int i, h;
    h = 8;
    for (i = 0; i < a->message_count; i++) h += message_step_height(a, i, cw);
    return h;
}

static void draw_sidebar_button(App *a, int y, const char *label, const char *value, int active)
{
    int x, w;
    x = 14;
    w = UI_SIDEBAR_W - 28;
    fill_round(a, x, y, w, 36, 8, active ? a->c.soft : a->c.sidebar);
    if (active) stroke_round(a, x, y, w, 36, 8, a->c.border);
    draw_text(a, a->small, a->c.muted, x + 11, y + 14, label);
    if (value && *value) draw_ellipsis(a, a->font, a->c.text, x + 11, y + 29, w - 22, value);
}

static int protocol_switchable(const App *a)
{
    return vs_provider_supports_protocol(a->ctx.provider.kind,VS_PROTOCOL_OPENAI) &&
           vs_provider_supports_protocol(a->ctx.provider.kind,VS_PROTOCOL_ANTHROPIC);
}

static void draw_sidebar(App *a)
{
    char b[256];
    int y, cache_title_y, cache_btn_y, project_y, global_btn_y, mcp_btn_y, ti, ty;
    int is_openai = a->ctx.provider.kind == VS_PROVIDER_OPENAI;
    XSetForeground(a->dpy, a->gc, a->c.sidebar);
    XFillRectangle(a->dpy, a->canvas, a->gc, 0, 0, UI_SIDEBAR_W, (unsigned int)a->height);
    XSetForeground(a->dpy, a->gc, a->c.border);
    XDrawLine(a->dpy, a->canvas, a->gc, UI_SIDEBAR_W - 1, 0, UI_SIDEBAR_W - 1, a->height);

    fill_round(a, 14, 16, UI_SIDEBAR_W - 28, 38, 9, a->c.panel);
    stroke_round(a, 14, 16, UI_SIDEBAR_W - 28, 38, 9, a->c.border);
    draw_text(a, a->bold, a->c.text, 28, 41, "+  New chat");

    draw_text(a, a->small, a->c.muted, 18, 82, "CONNECTION");
    draw_sidebar_button(a, 94, "Provider", a->ctx.provider.name, 1);
    draw_sidebar_button(a, 136, "Model", a->ctx.provider.model, 1);
    draw_sidebar_button(a, 178, "Protocol", vs_protocol_name(a->ctx.provider.protocol), protocol_switchable(a));
    draw_sidebar_button(a, 220, "API URL", a->ctx.provider.base_url, 1);

    draw_text(a, a->small, a->c.muted, 18, 274, "AUTHENTICATION");
    if (is_openai) {
        draw_sidebar_button(a, 286, "ChatGPT OAuth", oauth_status_label(&a->ctx), 1);
        draw_sidebar_button(a, 328, "OpenAI API key", a->ctx.provider.api_key[0] ? "Configured" : "Not set", 1);
        cache_title_y = 382; cache_btn_y = 394; project_y = 530;
    } else {
        snprintf(b,sizeof(b),"%s API key",a->ctx.provider.name);
        draw_sidebar_button(a, 286, b, a->ctx.provider.api_key[0] ? "Configured" : "Not set", 1);
        cache_title_y = 340; cache_btn_y = 352; project_y = 488;
    }

    draw_text(a, a->small, a->c.muted, 18, cache_title_y, "CACHE");
    draw_sidebar_button(a, cache_btn_y, "Prompt cache", a->ctx.cache_enabled ? "On" : "Off", 1);
    draw_sidebar_button(a, cache_btn_y + 42, "Cache data", "Clear local cache", 1);
    global_btn_y = cache_btn_y + 84;
    draw_sidebar_button(a, global_btn_y, vs_secure_config_is_per_user() ? "User config" : "System config", vs_global_config_exists() ? "Encrypted config present" : "Encrypted config", 1);
    mcp_btn_y = global_btn_y + 42;
    snprintf(b,sizeof(b),"%d server(s), %d tool(s)",a->ctx.mcp_server_count,a->ctx.mcp_tool_count);
    draw_sidebar_button(a,mcp_btn_y,"MCP",b,1);

    y = project_y + 42;
    draw_text(a, a->small, a->c.muted, 18, y, "PROJECT");
    draw_text(a, a->small, a->c.muted, 18, y + 25, "Working directory");
    draw_ellipsis(a, a->font, a->c.text, 18, y + 43, UI_SIDEBAR_W - 36, a->ctx.cwd);
    draw_text(a, a->small, a->c.muted, 18, y + 69, "System");
    snprintf(b, sizeof(b), "%.90s %.90s / %.60s", a->ctx.os_name, a->ctx.os_release, a->ctx.arch);
    draw_ellipsis(a, a->font, a->c.text, 18, y + 87, UI_SIDEBAR_W - 36, b);
    draw_text(a, a->small, a->c.muted, 18, y + 113, "AGENT.MD");
    draw_text(a, a->font, a->ctx.agent_md[0] ? a->c.accent_dark : a->c.muted,
              18, y + 132, a->ctx.agent_md[0] ? "Loaded" : "Not found");

    if (a->height > y + 205) {
        ty=y+164;draw_text(a,a->small,a->c.muted,18,ty,"ACTIVITY");
        for(ti=maxi(0,a->ctx.trace_count-5);ti<a->ctx.trace_count;ti++){
            ty+=18;snprintf(b,sizeof(b),"%s: %.90s",a->ctx.trace[ti].kind,a->ctx.trace[ti].detail);
            draw_ellipsis(a,a->small,a->c.text,18,ty,UI_SIDEBAR_W-36,b);
        }
    }
    if (a->height > y + 150) {
        if(a->ctx.conversation_usage_responses>0)
            snprintf(b,sizeof(b),"Tokens %lu total",a->ctx.conversation_total_tokens);
        else
            snprintf(b,sizeof(b),"Tokens not reported yet");
        draw_ellipsis(a,a->small,a->c.text,18,a->height-57,UI_SIDEBAR_W-36,b);
        snprintf(b,sizeof(b),"%lu input / %lu output",a->ctx.conversation_input_tokens,a->ctx.conversation_output_tokens);
        draw_ellipsis(a,a->small,a->c.muted,18,a->height-39,UI_SIDEBAR_W-36,b);
        snprintf(b, sizeof(b), "Cache %lu hit / %lu miss", a->ctx.file_cache_hits, a->ctx.file_cache_misses);
        draw_text(a, a->small, a->c.muted, 18, a->height - 21, b);
    }
}

static void draw_topbar(App *a)
{
    char b[256],tb[96];
    int cw, cx, right, token_x, token_right;
    XSetForeground(a->dpy, a->gc, a->c.panel);
    XFillRectangle(a->dpy, a->canvas, a->gc, UI_SIDEBAR_W, 0,
                   (unsigned int)(a->width - UI_SIDEBAR_W), UI_TOPBAR_H);
    XSetForeground(a->dpy, a->gc, a->c.border);
    XDrawLine(a->dpy, a->canvas, a->gc, UI_SIDEBAR_W, UI_TOPBAR_H - 1, a->width, UI_TOPBAR_H - 1);
    cx = content_x(a, &cw);
    draw_text(a, a->bold, a->c.text, cx, 35, "VibeSolaris");
    token_x=cx+text_w(a->bold,"VibeSolaris")+14;
    if(a->ctx.conversation_usage_responses>0)snprintf(tb,sizeof(tb),"%lu tokens",a->ctx.conversation_total_tokens);
    else snprintf(tb,sizeof(tb),"0 tokens");
    draw_text(a,a->small,a->c.muted,token_x,34,tb);
    token_right=token_x+text_w(a->small,tb)+18;
    snprintf(b, sizeof(b), "%s  /  %s  /  %s", a->ctx.provider.name, vs_protocol_name(a->ctx.provider.protocol), a->ctx.provider.model);
    right = cx + cw - text_w(a->small, b);
    if (right < token_right) right = token_right;
    draw_ellipsis(a, a->small, a->c.muted, right, 34, cx + cw - right, b);
}

static void draw_empty_state(App *a, int chat_top, int chat_bottom, int cx, int cw)
{
    int y, cardw, gap;
    y = chat_top + (chat_bottom - chat_top) / 2 - 86;
    draw_text(a, a->bold, a->c.text, cx + (cw - text_w(a->bold, "What are you working on?")) / 2,
              y, "What are you working on?");
    draw_text(a, a->font, a->c.muted,
              cx + (cw - text_w(a->font, "Ask about code, attach a file, or let the agent work in this directory.")) / 2,
              y + 28, "Ask about code, attach a file, or let the agent work in this directory.");
    cardw = (cw - 16) / 2;
    gap = 16;
    fill_round(a, cx, y + 55, cardw, 64, 10, a->c.sidebar);
    stroke_round(a, cx, y + 55, cardw, 64, 10, a->c.border);
    draw_text(a, a->bold, a->c.text, cx + 14, y + 79, "Attach files or images");
    draw_text(a, a->small, a->c.muted, cx + 14, y + 99, "Use + or drag files here; up to 16 attachments");
    fill_round(a, cx + cardw + gap, y + 55, cardw, 64, 10, a->c.sidebar);
    stroke_round(a, cx + cardw + gap, y + 55, cardw, 64, 10, a->c.border);
    draw_text(a, a->bold, a->c.text, cx + cardw + gap + 14, y + 79, "Agent mode");
    draw_text(a, a->small, a->c.muted, cx + cardw + gap + 14, y + 99, "Read, run commands, and edit files");
}

static void draw_messages(App *a, int chat_top, int chat_bottom, int cx, int cw)
{
    int total, viewport, maxscroll, i, y, maxbubble, tw, th, bw, bx, by;
    int panel_x, panel_w, panel_h, body_h;
    char role[16];
    viewport = chat_bottom - chat_top;
    total = message_total_height(a, cw);
    maxscroll = maxi(0, total - viewport);
    if (a->auto_scroll) {
        a->scroll_y = maxscroll;
        a->auto_scroll = 0;
    }
    if (a->scroll_y < 0) a->scroll_y = 0;
    if (a->scroll_y > maxscroll) a->scroll_y = maxscroll;
    y = chat_top + 8 - a->scroll_y;
    maxbubble = (cw * 72) / 100;
    for (i = 0; i < a->message_count; i++) {
        if (a->messages[i].role == UI_ROLE_USER) {
            th = wrapped_height(a->font, a->messages[i].text, maxbubble - 28, 20);
            if (!strchr(a->messages[i].text, '\n')) {
                tw = text_w(a->font, a->messages[i].text) + 28;
                bw = mini(maxbubble, maxi(76, tw));
            } else bw = maxbubble;
            bx = cx + cw - bw;
            by = y + 5;
            if (by + th + 20 >= chat_top && by <= chat_bottom) {
                fill_round(a, bx, by, bw, th + 20, 14, a->c.soft);
                draw_wrapped_message(a, i, a->font, a->c.text, bx + 14, by + 18, bw - 28, 20,
                                     chat_top, chat_bottom, a->messages[i].text);
            }
        } else if (a->messages[i].role == UI_ROLE_TRACE) {
            panel_x = cx + 42;
            panel_w = cw - 42;
            body_h = trace_body_height(a, &a->messages[i], cw);
            panel_h = a->messages[i].collapsed ? 34 : 48 + body_h;
            by = y + 2;
            if (by + panel_h >= chat_top && by <= chat_bottom) {
                fill_round(a, panel_x, by, panel_w, panel_h, 10, a->c.sidebar);
                stroke_round(a, panel_x, by, panel_w, panel_h, 10, a->c.border);
                draw_text(a, a->bold, a->c.muted, panel_x + 12, by + 22,
                          a->messages[i].collapsed ? ">" : "v");
                draw_ellipsis(a, a->small, a->c.muted, panel_x + 31, by + 22,
                              panel_w - 43, a->messages[i].summary[0] ? a->messages[i].summary : "Activity");
                if (!a->messages[i].collapsed) {
                    XSetForeground(a->dpy, a->gc, a->c.border);
                    XDrawLine(a->dpy, a->canvas, a->gc, panel_x + 12, by + 34, panel_x + panel_w - 12, by + 34);
                    draw_wrapped_message(a, i, a->small, a->c.muted, panel_x + 14, by + 52,
                                         panel_w - 28, 18, chat_top, chat_bottom, a->messages[i].text);
                }
            }
        } else {
            th = wrapped_height(a->font, a->messages[i].text, cw - 50, 20);
            if (y + maxi(th, 26) + 28 >= chat_top && y <= chat_bottom) {
                fill_round(a, cx, y + 4, 28, 28, 14, a->c.accent);
                draw_text(a, a->bold, a->c.panel, cx + 9, y + 24, "V");
                strcpy(role, "VibeSolaris");
                draw_text(a, a->bold, a->c.text, cx + 42, y + 18, role);
                draw_wrapped_message(a, i, a->font, a->c.text, cx + 42, y + 40, cw - 50, 20,
                                     chat_top, chat_bottom, a->messages[i].text);
            }
        }
        y += message_step_height(a, i, cw);
    }
    if (maxscroll > 0) {
        int trackh, thumbh, thumby;
        trackh = viewport - 8;
        thumbh = maxi(28, (trackh * viewport) / maxi(viewport, total));
        thumby = chat_top + 4 + ((trackh - thumbh) * a->scroll_y) / maxscroll;
        fill_round(a, a->width - 8, thumby, 4, thumbh, 2, a->c.border);
    }
}

static int draw_attachment_chips(App *a, int x, int y, int maxw)
{
    int i, xx, w, shown;
    const char *name;
    char b[128];
    xx = x;
    shown = 0;
    for (i = 0; i < a->ctx.attachment_count; i++) {
        name = base_name(a->ctx.attachments[i].path);
        snprintf(b, sizeof(b), "%s %s  x", a->ctx.attachments[i].is_image ? "IMG" : "FILE", name);
        w = text_w(a->small, b) + 18;
        if (w > 210) w = 210;
        if (xx + w > x + maxw) break;
        fill_round(a, xx, y, w, 24, 8, a->c.soft);
        stroke_round(a, xx, y, w, 24, 8, a->c.border);
        draw_ellipsis(a, a->small, a->c.text, xx + 9, y + 16, w - 18, b);
        xx += w + 7;
        shown++;
    }
    if (shown < a->ctx.attachment_count) {
        snprintf(b, sizeof(b), "+%d", a->ctx.attachment_count - shown);
        draw_text(a, a->small, a->c.muted, xx, y + 16, b);
    }
    return shown;
}

static void draw_plus_icon(App *a, int cx, int cy, unsigned long color)
{
    XSetForeground(a->dpy, a->gc, color);
    XDrawLine(a->dpy, a->canvas, a->gc, cx - 5, cy, cx + 5, cy);
    XDrawLine(a->dpy, a->canvas, a->gc, cx, cy - 5, cx, cy + 5);
}

static void draw_send_icon(App *a, int cx, int cy, unsigned long color)
{
    XPoint p[3];
    p[0].x = (short)(cx - 5); p[0].y = (short)(cy + 5);
    p[1].x = (short)(cx + 6); p[1].y = (short)cy;
    p[2].x = (short)(cx - 5); p[2].y = (short)(cy - 5);
    XSetForeground(a->dpy, a->gc, color);
    XFillPolygon(a->dpy, a->canvas, a->gc, p, 3, Convex, CoordModeOrigin);
}

static void draw_input_text(App *a, int x, int y, int maxw, int max_lines)
{
    const char *p, *n;
    char lines[8][1024];
    size_t offs[8], nextoffs[8];
    char line[1024];
    int count, stored, i, start, idx, baseline, caret_drawn;
    size_t lo=0, hi=0;
    int has_sel = a->selection.kind == SEL_INPUT && selection_bounds(a,&lo,&hi);
    p = a->input; count = 0; stored = 0;
    while (*p && count < 64) {
        size_t off, noff;
        if (!wrap_line(a->font, p, maxw, line, sizeof(line), &n)) break;
        off = (size_t)(p - a->input); noff = (size_t)(n - a->input);
        if (stored < 8) {
            strcpy(lines[stored], line); offs[stored]=off; nextoffs[stored]=noff; stored++;
        } else {
            for (i=1;i<8;i++){strcpy(lines[i-1],lines[i]);offs[i-1]=offs[i];nextoffs[i-1]=nextoffs[i];}
            strcpy(lines[7],line);offs[7]=off;nextoffs[7]=noff;
        }
        count++;
        if (n == p) break;
        p = n;
    }
    if (count == 0) {
        if (!has_sel) draw_caret(a, x, y);
        draw_text(a, a->font, a->c.muted, x + 5, y, "Message VibeSolaris...");
        return;
    }
    start = count > max_lines ? count - max_lines : 0;
    caret_drawn = 0;
    for (i = start; i < count && i - start < max_lines; i++) {
        size_t off,noff,llen,rel;
        idx = count > stored ? i - (count - stored) : i;
        if (idx < 0 || idx >= stored) continue;
        baseline = y + (i - start) * 19;
        off=offs[idx];noff=nextoffs[idx];llen=strlen(lines[idx]);
        if (has_sel) draw_selection_span(a,a->font,x,baseline,lines[idx],off,lo,hi);
        draw_text(a,a->font,a->c.text,x,baseline,lines[idx]);
        if (!has_sel && !caret_drawn && a->input_cursor >= off && a->input_cursor <= noff) {
            rel = a->input_cursor > off ? a->input_cursor - off : 0;
            if (rel > llen) rel = llen;
            draw_caret(a,x+XTextWidth(a->font,lines[idx],(int)rel)+1,baseline);
            caret_drawn=1;
        }
    }
    if (!has_sel && !caret_drawn && stored>0) {
        idx=stored-1; baseline=y+(mini(count,max_lines)-1)*19;
        draw_caret(a,x+text_w(a->font,lines[idx])+1,baseline);
    }
}

static void draw_composer(App *a, int cx, int cw)
{
    int py, ph, chips_y, text_y, plusx, sendx;
    char b[256];
    py = a->height - 114;
    ph = 82;
    fill_round(a, cx, py, cw, ph, 16, a->c.panel);
    stroke_round(a, cx, py, cw, ph, 16, a->xdnd_hover ? a->c.accent : a->c.border);

    chips_y = py + 9;
    if (a->ctx.attachment_count > 0) {
        draw_attachment_chips(a, cx + 13, chips_y, cw - 26);
        text_y = py + 51;
    } else text_y = py + 30;
    draw_input_text(a, cx + 48, text_y, cw - 104, a->ctx.attachment_count ? 1 : 2);

    plusx = cx + 25;
    fill_round(a, plusx - 14, py + ph - 34, 28, 28, 14, a->c.soft);
    draw_plus_icon(a, plusx, py + ph - 20, a->c.text);

    sendx = cx + cw - 25;
    fill_round(a, sendx - 14, py + ph - 34, 28, 28, 14,
               a->input_len ? a->c.accent : a->c.border);
    draw_send_icon(a, sendx, py + ph - 20, a->c.panel);

    if (a->status[0]) {
        draw_ellipsis(a, a->small, a->c.muted, cx, a->height - 13, cw, a->status);
    } else {
        snprintf(b, sizeof(b), "Enter send  -  Ctrl/Meta+V paste  -  Ctrl+C copy  -  Ctrl+O attach  -  drag files to attach");
        draw_text(a, a->small, a->c.muted, cx + (cw - text_w(a->small, b)) / 2,
                  a->height - 13, b);
    }
}

static const char *oauth_field_label(int f)
{
    switch (f) {
        case 0: return "OAuth Client ID";
        case 1: return "Authorisation URL";
        case 2: return "Token URL";
        case 3: return "Scopes";
        case 4: return "Loopback redirect URI";
        default: return "OAuth setting";
    }
}

static const char *oauth_field_value(const VSContext *c, int f)
{
    switch (f) {
        case 0: return c->oauth.client_id;
        case 1: return c->oauth.authorize_url;
        case 2: return c->oauth.token_url;
        case 3: return c->oauth.scopes;
        case 4: return c->oauth.redirect_uri;
        default: return "";
    }
}

static void oauth_field_store(VSContext *c, int f, const char *v)
{
    char *dst;
    size_t cap;
    dst = NULL; cap = 0;
    if (f == 0) { dst = c->oauth.client_id; cap = sizeof(c->oauth.client_id); }
    else if (f == 1) { dst = c->oauth.authorize_url; cap = sizeof(c->oauth.authorize_url); }
    else if (f == 2) { dst = c->oauth.token_url; cap = sizeof(c->oauth.token_url); }
    else if (f == 3) { dst = c->oauth.scopes; cap = sizeof(c->oauth.scopes); }
    else if (f == 4) { dst = c->oauth.redirect_uri; cap = sizeof(c->oauth.redirect_uri); }
    if (dst && cap) {
        size_t n;
        if (!v) v = "";
        n = strlen(v);
        if (n >= cap) n = cap - 1;
        memcpy(dst, v, n);
        dst[n] = 0;
    }
}

static void modal_geometry(App *a, int *x, int *y, int *w, int *h)
{
    *w = mini(580, a->width - 80);
    if (a->modal == MODAL_PROVIDER) *h = 352;
    else if (a->modal == MODAL_PROTOCOL) *h = 220;
    else if (a->modal == MODAL_ACCOUNT) *h = 390;
    else if (a->modal == MODAL_OAUTH_CONFIG) *h = 430;
    else if (a->modal == MODAL_GLOBAL_CONFIG) *h = 280;
    else *h = 188;
    *x = (a->width - *w) / 2;
    *y = (a->height - *h) / 2;
}

static void draw_modal(App *a)
{
    int x, y, w, h, i, row, col, bx, by, bw;
    const char *title, *hint;
    char masked[256];
    if (a->modal == MODAL_NONE) return;

    XSetForeground(a->dpy, a->gc, a->c.overlay);
    XFillRectangle(a->dpy, a->canvas, a->gc, UI_SIDEBAR_W, UI_TOPBAR_H,
                   (unsigned int)(a->width - UI_SIDEBAR_W),
                   (unsigned int)(a->height - UI_TOPBAR_H));
    modal_geometry(a, &x, &y, &w, &h);
    fill_round(a, x, y, w, h, 14, a->c.panel);
    stroke_round(a, x, y, w, h, 14, a->c.border);

    if (a->modal == MODAL_PROVIDER) {
        draw_text(a, a->bold, a->c.text, x + 24, y + 34, "Choose provider");
        draw_text(a, a->small, a->c.muted, x + 24, y + 55,
                  "Select an API backend. Provider settings stay lightweight and local.");
        bw = (w - 62) / 2;
        for (i = 0; i < PROVIDER_COUNT; i++) {
            row = i / 2;
            col = i % 2;
            bx = x + 22 + col * (bw + 18);
            by = y + 76 + row * 48;
            fill_round(a, bx, by, bw, 38, 8,
                       i == a->provider_sel ? a->c.soft : a->c.panel);
            stroke_round(a, bx, by, bw, 38, 8,
                         i == a->provider_sel ? a->c.accent : a->c.border);
            draw_ellipsis(a, a->font, a->c.text, bx + 12, by + 24, bw - 24, provider_labels[i]);
        }
        draw_text(a, a->small, a->c.muted, x + 24, y + h - 18,
                  "Arrow keys + Enter also work. Esc closes this window.");
        return;
    }

    if (a->modal == MODAL_PROTOCOL) {
        const char *names[2] = {"OpenAI-compatible", "Anthropic Messages"};
        VSProtocolKind protocols[2] = {VS_PROTOCOL_OPENAI, VS_PROTOCOL_ANTHROPIC};
        draw_text(a, a->bold, a->c.text, x + 24, y + 34, "Choose API protocol");
        draw_text(a, a->small, a->c.muted, x + 24, y + 55,
                  "Qwen, GLM, DeepSeek, and custom endpoints can switch between OpenAI and Anthropic protocols.");
        for (i = 0; i < 2; i++) {
            int enabled = vs_provider_supports_protocol(a->ctx.provider.kind, protocols[i]);
            by = y + 78 + i * 52;
            fill_round(a, x + 24, by, w - 48, 40, 8, i == a->protocol_sel ? a->c.soft : a->c.panel);
            stroke_round(a, x + 24, by, w - 48, 40, 8, i == a->protocol_sel ? a->c.accent : a->c.border);
            draw_text(a, a->font, enabled ? a->c.text : a->c.muted, x + 38, by + 25, names[i]);
        }
        return;
    }

    if (a->modal == MODAL_ACCOUNT) {
        const char *stext;
        unsigned long scolor;
        stext = oauth_status_label(&a->ctx);
        scolor = vs_oauth_is_signed_in(&a->ctx) ? a->c.accent_dark : a->c.text;
        draw_text(a, a->bold, a->c.text, x + 24, y + 35, "ChatGPT / OpenAI authentication");
        draw_text(a, a->small, a->c.muted, x + 24, y + 58,
                  "OAuth 2.0 Authorisation Code + PKCE for a native desktop client.");
        draw_text(a, a->small, a->c.muted, x + 24, y + 77,
                  "VibeSolaris never asks for your ChatGPT password and does not read browser cookies.");

        fill_round(a, x + 24, y + 94, w - 48, 58, 9, a->c.soft);
        stroke_round(a, x + 24, y + 94, w - 48, 58, 9, a->c.border);
        draw_text(a, a->small, a->c.muted, x + 38, y + 115, "OAuth status");
        draw_text(a, a->bold, scolor, x + 38, y + 138, stext);

        fill_round(a, x + 24, y + 169, w - 48, 44, 9,
                   (!vs_oauth_is_configured(&a->ctx) || !vs_oauth_is_signed_in(&a->ctx)) ? a->c.accent : a->c.soft);
        if (a->oauth_flow.active) {
            draw_text(a, a->bold, a->c.panel,
                      x + 24 + ((w - 48) - text_w(a->bold, "Waiting for browser callback...")) / 2,
                      y + 197, "Waiting for browser callback...");
        } else if (!vs_oauth_is_configured(&a->ctx)) {
            draw_text(a, a->bold, a->c.panel,
                      x + 24 + ((w - 48) - text_w(a->bold, "Configure ChatGPT OAuth")) / 2,
                      y + 197, "Configure ChatGPT OAuth");
        } else if (vs_oauth_is_signed_in(&a->ctx)) {
            draw_text(a, a->bold, a->c.text,
                      x + 24 + ((w - 48) - text_w(a->bold, "OAuth session active")) / 2,
                      y + 197, "OAuth session active");
        } else {
            draw_text(a, a->bold, a->c.panel,
                      x + 24 + ((w - 48) - text_w(a->bold, "Sign in with ChatGPT / OpenAI")) / 2,
                      y + 197, "Sign in with ChatGPT / OpenAI");
        }

        fill_round(a, x + 24, y + 232, 178, 38, 8, a->c.panel);
        stroke_round(a, x + 24, y + 232, 178, 38, 8, a->c.border);
        draw_text(a, a->font, a->c.text, x + 47, y + 256, "OAuth settings");

        fill_round(a, x + 214, y + 232, 142, 38, 8, a->c.panel);
        stroke_round(a, x + 214, y + 232, 142, 38, 8, a->c.border);
        draw_text(a, a->font, a->c.text, x + 238, y + 256, "API key");

        if (a->oauth_flow.active) {
            fill_round(a, x + 24, y + 286, 142, 36, 8, a->c.panel);
            stroke_round(a, x + 24, y + 286, 142, 36, 8, a->c.danger);
            draw_text(a, a->font, a->c.danger, x + 48, y + 309, "Cancel login");
        } else if (a->ctx.oauth.access_token[0]) {
            fill_round(a, x + 24, y + 286, 142, 36, 8, a->c.panel);
            stroke_round(a, x + 24, y + 286, 142, 36, 8, a->c.danger);
            draw_text(a, a->font, a->c.danger, x + 54, y + 309, "Sign out");
        }

        fill_round(a, x + w - 100, y + 286, 76, 36, 8, a->c.panel);
        stroke_round(a, x + w - 100, y + 286, 76, 36, 8, a->c.border);
        draw_text(a, a->font, a->c.text, x + w - 81, y + 309, "Close");

        draw_ellipsis(a, a->small, a->c.muted, x + 24, y + 347, w - 48,
                      "OAuth credentials are not bundled; enter only values officially issued for VibeSolaris.");
        return;
    }

    if (a->modal == MODAL_OAUTH_CONFIG) {
        draw_text(a, a->bold, a->c.text, x + 24, y + 34, "OAuth / PKCE settings");
        draw_text(a, a->small, a->c.muted, x + 24, y + 55,
                  "Use the exact values supplied when VibeSolaris is registered as a native/public OAuth client.");
        draw_text(a, a->small, a->c.muted, x + 24, y + 73,
                  "Click a field to edit it. Settings and tokens are saved under ~/.vibesolaris with mode 0600.");
        for (i = 0; i < 5; i++) {
            by = y + 88 + i * 54;
            fill_round(a, x + 24, by, w - 48, 44, 8, i == a->oauth_field ? a->c.soft : a->c.panel);
            stroke_round(a, x + 24, by, w - 48, 44, 8, i == a->oauth_field ? a->c.accent : a->c.border);
            draw_text(a, a->small, a->c.muted, x + 36, by + 15, oauth_field_label(i));
            draw_ellipsis(a, a->font, a->c.text, x + 36, by + 34, w - 72,
                          oauth_field_value(&a->ctx, i)[0] ? oauth_field_value(&a->ctx, i) : "(not set)");
        }
        fill_round(a, x + 24, y + h - 50, 160, 32, 8, a->c.panel);
        stroke_round(a, x + 24, y + h - 50, 160, 32, 8, a->c.border);
        draw_text(a, a->font, a->c.text, x + 45, y + h - 29, "Back to authentication");
        fill_round(a, x + w - 105, y + h - 50, 81, 32, 8, a->c.accent);
        draw_text(a, a->font, a->c.panel, x + w - 86, y + h - 29, "Save");
        return;
    }

    if (a->modal == MODAL_GLOBAL_CONFIG) {
        char pathbuf[VS_MAX_PATH];
        int per_user = vs_secure_config_is_per_user();
        if (vs_secure_config_path(pathbuf,sizeof(pathbuf)) != 0) snprintf(pathbuf,sizeof(pathbuf),"(path unavailable)");
        draw_text(a,a->bold,a->c.text,x+24,y+35,per_user ? "Encrypted per-user configuration" : "Encrypted system configuration");
        draw_text(a,a->small,a->c.muted,x+24,y+58,per_user ? "System config is not writable; settings are stored privately in your home directory." : "Provider settings and API keys are stored in the writable system configuration.");
        draw_text(a,a->small,a->c.muted,x+24,y+79,"AES-256-CBC + HMAC-SHA256; key and config default to mode 0600.");
        fill_round(a,x+24,y+96,w-48,58,8,a->c.soft);
        stroke_round(a,x+24,y+96,w-48,58,8,a->c.border);
        draw_text(a,a->small,a->c.muted,x+36,y+117,per_user ? "User config" : "System config");
        draw_ellipsis(a,a->font,a->c.text,x+36,y+139,w-72,pathbuf);
        draw_text(a,a->small,vs_global_config_exists()?a->c.accent_dark:a->c.muted,x+36,y+153,
                  vs_global_config_exists()?"Encrypted config present":"Encrypted config not found");
        fill_round(a,x+24,y+177,150,38,8,a->c.panel);
        stroke_round(a,x+24,y+177,150,38,8,a->c.border);
        draw_text(a,a->font,a->c.text,x+58,y+201,"Load config");
        fill_round(a,x+187,y+177,150,38,8,a->c.accent);
        draw_text(a,a->font,a->c.panel,x+221,y+201,"Save config");
        fill_round(a,x+w-100,y+h-48,76,32,8,a->c.panel);
        stroke_round(a,x+w-100,y+h-48,76,32,8,a->c.border);
        draw_text(a,a->font,a->c.text,x+w-81,y+h-27,"Close");
        draw_text(a,a->small,a->c.muted,x+24,y+h-27,per_user ? "Per-user fallback: ~/.vibesolaris (no root required)." : "Using writable /etc/vibesolaris system storage.");
        return;
    }

    title = "Edit setting";
    hint = "Type a value and press Enter.";
    if (a->modal == MODAL_ATTACH) { title = "Attach file or image"; hint = "Enter an absolute or relative path."; }
    else if (a->modal == MODAL_MODEL) { title = "Model"; hint = a->ctx.provider.kind==VS_PROVIDER_DEEPSEEK ? "DeepSeek options: deepseek-v4-pro or deepseek-v4-flash." : "Enter the provider model ID."; }
    else if (a->modal == MODAL_KEY) { title = "API key"; hint = vs_secure_config_is_per_user() ? "Autosaved encrypted in ~/.vibesolaris/config.enc." : "Autosaved encrypted in /etc/vibesolaris/config.enc."; }
    else if (a->modal == MODAL_BASE) { title = "API URL / Base URL"; hint = "Enter a provider base URL or a full request URL; VibeSolaris adds the protocol path when needed."; }
    else if (a->modal == MODAL_OAUTH_EDIT) { title = oauth_field_label(a->oauth_field); hint = "Enter the exact OAuth application value issued for VibeSolaris."; }
    else if (a->modal == MODAL_MCP) { title = "Add MCP server"; hint = "local: NAME|stdio|COMMAND (stdin alias accepted)   remote: NAME|http|URL|TOKEN(optional)"; }
    draw_text(a, a->bold, a->c.text, x + 24, y + 35, title);
    draw_text(a, a->small, a->c.muted, x + 24, y + 57, hint);
    fill_round(a, x + 24, y + 76, w - 48, 40, 8, a->c.bg);
    stroke_round(a, x + 24, y + 76, w - 48, 40, 8, a->c.accent);
    {
        const char *shown;
        size_t lo=0,hi=0,cur;
        int has_sel = a->selection.kind==SEL_MODAL && selection_bounds(a,&lo,&hi);
        if (a->modal == MODAL_KEY && a->modal_len) {
            size_t n,j;n=a->modal_len;if(n>sizeof(masked)-1)n=sizeof(masked)-1;
            for(j=0;j<n;j++) masked[j]='*';
            masked[n]=0;shown=masked;
        } else shown=a->modal_text;
        if(has_sel) draw_selection_span(a,a->font,x+35,y+101,shown,0,lo,hi);
        draw_ellipsis(a,a->font,a->c.text,x+35,y+101,w-70,shown);
        if(a->cursor_visible && !has_sel){
            int caret;cur=a->modal_cursor;if(cur>strlen(shown))cur=strlen(shown);
            caret=x+35+XTextWidth(a->font,shown,(int)cur)+1;if(caret>x+w-38)caret=x+w-38;
            draw_caret(a,caret,y+101);
        }
    }

    fill_round(a, x + w - 190, y + 132, 72, 34, 8, a->c.panel);
    stroke_round(a, x + w - 190, y + 132, 72, 34, 8, a->c.border);
    draw_text(a, a->font, a->c.text, x + w - 174, y + 154, "Cancel");
    fill_round(a, x + w - 106, y + 132, 82, 34, 8, a->c.accent);
    draw_text(a, a->font, a->c.panel, x + w - 88, y + 154,
              a->modal == MODAL_ATTACH ? "Attach" : "Apply");
}

static int ensure_back_buffer(App *a)
{
    if (a->back_buffer && a->back_w == a->width && a->back_h == a->height) {
        a->canvas = a->back_buffer;
        return 1;
    }
    if (a->back_buffer) {
        XFreePixmap(a->dpy, a->back_buffer);
        a->back_buffer = (Pixmap)0;
    }
    a->back_buffer = XCreatePixmap(a->dpy, a->win, (unsigned int)a->width, (unsigned int)a->height,
                                   (unsigned int)DefaultDepth(a->dpy, a->screen));
    if (!a->back_buffer) {
        a->canvas = a->win;
        a->back_w = a->back_h = 0;
        return 0;
    }
    a->back_w = a->width;
    a->back_h = a->height;
    a->canvas = a->back_buffer;
    return 1;
}

static void redraw(App *a)
{
    int cx, cw, chat_top, chat_bottom, buffered;
    buffered = ensure_back_buffer(a);
    XSetForeground(a->dpy, a->gc, a->c.bg);
    XFillRectangle(a->dpy, a->canvas, a->gc, 0, 0, (unsigned int)a->width, (unsigned int)a->height);
    draw_sidebar(a);
    draw_topbar(a);
    cx = content_x(a, &cw);
    chat_top = UI_TOPBAR_H + 10;
    chat_bottom = a->height - UI_COMPOSER_H + 5;
    if (a->message_count == 0) draw_empty_state(a, chat_top, chat_bottom, cx, cw);
    else draw_messages(a, chat_top, chat_bottom, cx, cw);
    draw_composer(a, cx, cw);
    draw_modal(a);
    if (buffered) {
        XCopyArea(a->dpy, a->back_buffer, a->win, a->gc, 0, 0,
                  (unsigned int)a->width, (unsigned int)a->height, 0, 0);
    }
    XFlush(a->dpy);
}

static void open_modal(App *a, ModalType m, const char *initial)
{
    int i;
    a->modal = m;
    a->cursor_visible = 1;
    selection_clear(a);
    a->modal_text[0] = 0;
    a->modal_len = 0;
    if (initial) {
        strncpy(a->modal_text, initial, sizeof(a->modal_text) - 1);
        a->modal_text[sizeof(a->modal_text) - 1] = 0;
        a->modal_len = strlen(a->modal_text);
        a->modal_cursor = a->modal_len;
    } else {
        a->modal_cursor = 0;
    }
    if (m == MODAL_PROVIDER) {
        a->provider_sel = 0;
        for (i = 0; i < PROVIDER_COUNT; i++)
            if (!strcmp(a->ctx.provider.name, provider_names[i])) a->provider_sel = i;
    } else if (m == MODAL_PROTOCOL) {
        a->protocol_sel = a->ctx.provider.protocol == VS_PROTOCOL_ANTHROPIC ? 1 : 0;
    } else if (m == MODAL_OAUTH_CONFIG) {
        if (a->oauth_field < 0 || a->oauth_field > 4) a->oauth_field = 0;
    }
}

static void apply_modal(App *a)
{
    int rc;
    char b[256];
    if (a->modal == MODAL_ATTACH) {
        rc = vs_attach(&a->ctx, a->modal_text);
        if (rc == 0) snprintf(a->status, sizeof(a->status), "Attached: %.220s", base_name(a->modal_text));
        else snprintf(a->status, sizeof(a->status), "Could not attach: %.210s", a->modal_text);
    } else if (a->modal == MODAL_MODEL) {
        vs_set_model(&a->ctx,a->modal_text);
        if (vs_persist_settings(&a->ctx) == 0)
            snprintf(a->status, sizeof(a->status), "Model set to %s and saved for %s", a->ctx.provider.model, a->ctx.provider.name);
        else
            snprintf(a->status, sizeof(a->status), "Model changed to %s, but config save failed", a->ctx.provider.model);
    } else if (a->modal == MODAL_KEY) {
        vs_set_api_key(&a->ctx, a->modal_text);
        strcpy(a->status, "API key updated for selected provider");
    } else if (a->modal == MODAL_BASE) {
        vs_set_base_url(&a->ctx, a->modal_text);
        strcpy(a->status, "API URL updated for this provider/protocol");
    } else if (a->modal == MODAL_PROTOCOL) {
        if (vs_set_protocol(&a->ctx, a->protocol_sel ? "anthropic" : "openai") == 0)
            snprintf(a->status, sizeof(a->status), "Protocol set to %s", vs_protocol_name(a->ctx.provider.protocol));
        else strcpy(a->status, "That protocol is not supported by this provider");
    } else if (a->modal == MODAL_PROVIDER) {
        vs_set_provider(&a->ctx, provider_names[a->provider_sel]);
        snprintf(b, sizeof(b), "Provider set to %s", provider_labels[a->provider_sel]);
        strncpy(a->status, b, sizeof(a->status) - 1);
        a->status[sizeof(a->status) - 1] = 0;
    } else if (a->modal == MODAL_MCP) {
        char tmp[sizeof(a->modal_text)],*name,*transport,*target,*auth,*p1,*p2,*p3;
        strncpy(tmp,a->modal_text,sizeof(tmp)-1);tmp[sizeof(tmp)-1]=0;
        name=tmp;p1=strchr(name,'|');if(!p1){strcpy(a->status,"MCP format: NAME|stdio|COMMAND or NAME|http|URL|TOKEN");a->modal=MODAL_NONE;return;}*p1=0;transport=p1+1;p2=strchr(transport,'|');if(!p2){strcpy(a->status,"MCP format is incomplete");a->modal=MODAL_NONE;return;}*p2=0;target=p2+1;auth=NULL;p3=strchr(target,'|');if(p3){*p3=0;auth=p3+1;}
        if(!strcmp(transport,"stdio")||!strcmp(transport,"stdin"))rc=vs_mcp_add_stdio(&a->ctx,name,target);else if(!strcmp(transport,"http")||!strcmp(transport,"remote"))rc=vs_mcp_add_http(&a->ctx,name,target,auth);else rc=-1;
        if(rc==0){(void)vs_mcp_refresh_all(&a->ctx,1);snprintf(a->status,sizeof(a->status),"MCP server %.120s saved to encrypted config",name);}else strcpy(a->status,"Could not add MCP server");
    } else if (a->modal == MODAL_OAUTH_EDIT) {
        oauth_field_store(&a->ctx, a->oauth_field, a->modal_text);
        if (vs_oauth_save_profile(&a->ctx) == 0) {
            (void)vs_persist_settings(&a->ctx);
            snprintf(a->status, sizeof(a->status), "%s saved", oauth_field_label(a->oauth_field));
        }
        else
            snprintf(a->status, sizeof(a->status), "%s updated, but OAuth profile save failed", oauth_field_label(a->oauth_field));
        a->modal = MODAL_OAUTH_CONFIG;
        return;
    }
    a->modal = MODAL_NONE;
}

static void set_edit_cursor(App *a, SelectionKind kind, size_t oldpos, size_t newpos, int extend)
{
    if (extend) {
        if (a->selection.kind != kind) {
            a->selection.kind = kind;
            a->selection.message_index = -1;
            a->selection.anchor = oldpos;
        }
        a->selection.cursor = newpos;
    } else selection_clear(a);
    if (kind == SEL_INPUT) a->input_cursor = newpos;
    else a->modal_cursor = newpos;
}

static int insert_edit_text(App *a, SelectionKind kind, const char *text, size_t n)
{
    char *buf; size_t *lenp,*curp,cap,len,cur;
    if (!text || !n) return 0;
    if (kind == SEL_INPUT) { buf=a->input;lenp=&a->input_len;curp=&a->input_cursor;cap=sizeof(a->input); }
    else { buf=a->modal_text;lenp=&a->modal_len;curp=&a->modal_cursor;cap=sizeof(a->modal_text); }
    delete_edit_selection(a,kind);
    len=*lenp;cur=*curp;if(cur>len)cur=len;
    if(len+n>=cap)n=cap-len-1;
    if(!n)return 0;
    memmove(buf+cur+n,buf+cur,len-cur+1);memcpy(buf+cur,text,n);
    *lenp=len+n;*curp=cur+n;selection_clear(a);return 1;
}


static int modal_accepts_text(const App *a)
{
    return a->modal == MODAL_ATTACH || a->modal == MODAL_MODEL || a->modal == MODAL_KEY ||
           a->modal == MODAL_BASE || a->modal == MODAL_OAUTH_EDIT;
}

static SelectionKind active_edit_target(const App *a)
{
    return modal_accepts_text(a) ? SEL_MODAL : SEL_INPUT;
}

static void paste_reset(App *a)
{
    free(a->paste_data);
    a->paste_data = NULL;
    a->paste_len = a->paste_cap = 0;
    a->paste_incr = 0;
}

static int paste_append(App *a, const unsigned char *data, size_t n)
{
    char *tmp;
    size_t need, cap;
    if (!data || !n) return 0;
    if (a->paste_len + n > (size_t)(UI_INPUT_MAX * 2)) n = (size_t)(UI_INPUT_MAX * 2) - a->paste_len;
    if (!n) return 0;
    need = a->paste_len + n + 1;
    if (need > a->paste_cap) {
        cap = a->paste_cap ? a->paste_cap : 1024;
        while (cap < need) cap *= 2;
        tmp = (char *)realloc(a->paste_data, cap);
        if (!tmp) return -1;
        a->paste_data = tmp;
        a->paste_cap = cap;
    }
    memcpy(a->paste_data + a->paste_len, data, n);
    a->paste_len += n;
    a->paste_data[a->paste_len] = 0;
    return 0;
}

static void paste_commit(App *a)
{
    char clean[UI_INPUT_MAX];
    size_t i, j;
    SelectionKind target = a->paste_target;
    if (!a->paste_data || !a->paste_len) { paste_reset(a); return; }
    j = 0;
    for (i = 0; i < a->paste_len && j < sizeof(clean) - 1; i++) {
        unsigned char ch = (unsigned char)a->paste_data[i];
        if (ch == 0) continue;
        if (ch == '\r') {
            if (i + 1 < a->paste_len && a->paste_data[i + 1] == '\n') continue;
            ch = '\n';
        }
        clean[j++] = (char)ch;
    }
    clean[j] = 0;
    if (target == SEL_MODAL && !modal_accepts_text(a)) target = SEL_INPUT;
    insert_edit_text(a, target, clean, j);
    snprintf(a->status, sizeof(a->status), "Pasted %lu byte%s", (unsigned long)j, j == 1 ? "" : "s");
    a->cursor_visible = 1;
    paste_reset(a);
}

static void request_paste(App *a, Atom selection)
{
    paste_reset(a);
    a->paste_target = active_edit_target(a);
    a->paste_selection = selection;
    a->paste_requested_target = a->utf8_atom;
    XDeleteProperty(a->dpy, a->win, a->paste_atom);
    XConvertSelection(a->dpy, selection, a->utf8_atom, a->paste_atom, a->win, CurrentTime);
    XFlush(a->dpy);
}

static void handle_paste_selection(App *a, XSelectionEvent *se)
{
    Atom type;
    int format;
    unsigned long nitems, after;
    unsigned char *data = NULL;
    if (se->selection != a->paste_selection || se->property == None) {
        if (se->selection == a->paste_selection && a->paste_requested_target == a->utf8_atom) {
            a->paste_requested_target = XA_STRING;
            XConvertSelection(a->dpy, a->paste_selection, XA_STRING, a->paste_atom, a->win, CurrentTime);
            XFlush(a->dpy);
        }
        return;
    }
    if (XGetWindowProperty(a->dpy, a->win, se->property, 0, 0x1fffffffL, True,
                           AnyPropertyType, &type, &format, &nitems, &after, &data) != Success) return;
    if (type == a->incr_atom) {
        a->paste_incr = 1;
        if (data) XFree(data);
        return;
    }
    if (data && format == 8 && nitems) paste_append(a, data, (size_t)nitems);
    if (data) XFree(data);
    paste_commit(a);
}

static void handle_paste_property(App *a, XPropertyEvent *pe)
{
    Atom type;
    int format;
    unsigned long nitems, after;
    unsigned char *data = NULL;
    if (!a->paste_incr || pe->atom != a->paste_atom || pe->state != PropertyNewValue) return;
    if (XGetWindowProperty(a->dpy, a->win, a->paste_atom, 0, 0x1fffffffL, True,
                           AnyPropertyType, &type, &format, &nitems, &after, &data) != Success) return;
    if (format == 8 && nitems) paste_append(a, data, (size_t)nitems);
    if (data) XFree(data);
    if (nitems == 0) paste_commit(a);
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int uri_to_path(const char *uri, char *out, size_t cap)
{
    const char *p;
    size_t j = 0;
    int h1, h2;
    if (!uri || !*uri || !out || cap < 2) return -1;
    p = uri;
    if (!strncmp(p, "file://", 7)) {
        p += 7;
        if (*p && *p != '/') {
            if (!strncmp(p, "localhost/", 10)) p += 9;
            else return -1; /* remote host: do not silently fetch/network-mount */
        }
    } else if (!strncmp(p, "file:", 5)) p += 5;
    else if (*p != '/') return -1;
    while (*p && j + 1 < cap) {
        if (*p == '%' && p[1] && p[2] && (h1 = hexval((unsigned char)p[1])) >= 0 &&
            (h2 = hexval((unsigned char)p[2])) >= 0) {
            out[j++] = (char)((h1 << 4) | h2);
            p += 3;
        } else {
            out[j++] = *p++;
        }
    }
    out[j] = 0;
    return j ? 0 : -1;
}

static int attach_dropped_uri_list(App *a, const unsigned char *data, size_t len)
{
    char *copy, *line, *next, path[VS_MAX_PATH];
    int added = 0, skipped = 0;
    struct stat st;
    copy = (char *)malloc(len + 1);
    if (!copy) return 0;
    memcpy(copy, data, len); copy[len] = 0;
    line = copy;
    while (line && *line) {
        char *end;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        end = line + strlen(line);
        while (end > line && (end[-1] == '\r' || end[-1] == '\n')) *--end = 0;
        if (*line && *line != '#') {
            if (uri_to_path(line, path, sizeof(path)) == 0 && stat(path, &st) == 0 &&
                S_ISREG(st.st_mode) && vs_attach(&a->ctx, path) == 0) added++;
            else skipped++;
        }
        line = next;
    }
    free(copy);
    if (added) snprintf(a->status, sizeof(a->status), "Attached %d dropped file%s%s", added, added == 1 ? "" : "s", skipped ? " (some items skipped)" : "");
    else strcpy(a->status, "No readable regular files were found in the drop");
    return added;
}

static void xdnd_send_status(App *a)
{
    XEvent e;
    if (!a->xdnd_source) return;
    memset(&e, 0, sizeof(e));
    e.xclient.type = ClientMessage;
    e.xclient.display = a->dpy;
    e.xclient.window = a->xdnd_source;
    e.xclient.message_type = a->xdnd_status_atom;
    e.xclient.format = 32;
    e.xclient.data.l[0] = (long)a->win;
    e.xclient.data.l[1] = a->xdnd_accept ? 1L : 0L;
    e.xclient.data.l[2] = 0;
    e.xclient.data.l[3] = 0;
    e.xclient.data.l[4] = a->xdnd_accept ? (long)a->xdnd_action_copy_atom : (long)None;
    XSendEvent(a->dpy, a->xdnd_source, False, NoEventMask, &e);
    XFlush(a->dpy);
}

static void xdnd_send_finished(App *a, int success)
{
    XEvent e;
    if (!a->xdnd_source) return;
    memset(&e, 0, sizeof(e));
    e.xclient.type = ClientMessage;
    e.xclient.display = a->dpy;
    e.xclient.window = a->xdnd_source;
    e.xclient.message_type = a->xdnd_finished_atom;
    e.xclient.format = 32;
    e.xclient.data.l[0] = (long)a->win;
    e.xclient.data.l[1] = success ? 1L : 0L;
    e.xclient.data.l[2] = success ? (long)a->xdnd_action_copy_atom : (long)None;
    XSendEvent(a->dpy, a->xdnd_source, False, NoEventMask, &e);
    XFlush(a->dpy);
    a->xdnd_pending = 0;
    a->xdnd_hover = 0;
}

static int xdnd_choose_type(App *a, XClientMessageEvent *ce)
{
    Atom offered[3];
    Atom *list = NULL, actual;
    int format, i, n = 0;
    unsigned long count = 0, after = 0;
    unsigned char *prop = NULL;
    if (ce->data.l[1] & 1L) {
        if (XGetWindowProperty(a->dpy, (Window)ce->data.l[0], a->xdnd_type_list_atom, 0, 1024, False,
                               XA_ATOM, &actual, &format, &count, &after, &prop) == Success && prop && format == 32) {
            list = (Atom *)prop;
            n = (int)count;
        }
    } else {
        offered[0] = (Atom)ce->data.l[2]; offered[1] = (Atom)ce->data.l[3]; offered[2] = (Atom)ce->data.l[4];
        list = offered; n = 3;
    }
    a->xdnd_type = None;
    for (i = 0; i < n; i++) if (list[i] == a->uri_list_atom) { a->xdnd_type = a->uri_list_atom; break; }
    if (prop) XFree(prop);
    return a->xdnd_type != None;
}

static void handle_xdnd_client(App *a, XClientMessageEvent *ce)
{
    if (ce->message_type == a->xdnd_enter_atom) {
        a->xdnd_source = (Window)ce->data.l[0];
        a->xdnd_version = (int)((unsigned long)ce->data.l[1] >> 24);
        a->xdnd_accept = xdnd_choose_type(a, ce);
        a->xdnd_hover = a->xdnd_accept;
    } else if (ce->message_type == a->xdnd_position_atom) {
        if ((Window)ce->data.l[0] != a->xdnd_source) return;
        xdnd_send_status(a);
    } else if (ce->message_type == a->xdnd_leave_atom) {
        a->xdnd_hover = 0;
        a->xdnd_accept = 0;
        a->xdnd_source = (Window)0;
    } else if (ce->message_type == a->xdnd_drop_atom) {
        Time t;
        if ((Window)ce->data.l[0] != a->xdnd_source || !a->xdnd_accept || a->xdnd_type == None) {
            xdnd_send_finished(a, 0);
            return;
        }
        t = a->xdnd_version >= 1 ? (Time)ce->data.l[2] : CurrentTime;
        a->xdnd_pending = 1;
        XConvertSelection(a->dpy, a->xdnd_selection_atom, a->xdnd_type, a->xdnd_property_atom, a->win, t);
        XFlush(a->dpy);
    }
}

static void handle_xdnd_selection(App *a, XSelectionEvent *se)
{
    Atom type;
    int format, added = 0;
    unsigned long nitems, after;
    unsigned char *data = NULL;
    if (!a->xdnd_pending || se->selection != a->xdnd_selection_atom) return;
    if (se->property != None && XGetWindowProperty(a->dpy, a->win, se->property, 0, 0x1fffffffL, True,
                                                    AnyPropertyType, &type, &format, &nitems, &after, &data) == Success) {
        if (data && format == 8 && type == a->uri_list_atom) added = attach_dropped_uri_list(a, data, (size_t)nitems);
    }
    if (data) XFree(data);
    xdnd_send_finished(a, added > 0);
}

static void new_chat(App *a)
{
    free_messages(a);
    vs_history_clear(&a->ctx);
    vs_clear_attachments(&a->ctx);
    a->input[0] = 0;
    a->input_len = 0;
    a->input_cursor = 0;
    selection_clear(a);
    a->scroll_y = 0;
    strcpy(a->status, "New chat");
}

static void send_message(App *a)
{
    char *reply;
    char *usercopy;
    if (!a->input_len) return;
    usercopy = ui_dup(a->input);
    if (!usercopy) return;
    add_message(a, UI_ROLE_USER, usercopy);
    strcpy(a->status, "Working...");
    redraw(a);
    XSync(a->dpy, False);
    reply = vs_agent_turn(&a->ctx, usercopy);
    {
        size_t cap, len;
        char *tr;
        int i;
        cap = 1024; len = 0; tr = (char *)malloc(cap);
        if (tr) {
            tr[0] = 0;
            for (i = 0; i < a->ctx.trace_count; i++) {
                char line[600];
                size_t n;
                snprintf(line, sizeof(line), "%d. [%s] %s\n", i + 1,
                         a->ctx.trace[i].kind, a->ctx.trace[i].detail);
                n = strlen(line);
                if (len + n + 1 > cap) {
                    char *nt;
                    while (len + n + 1 > cap) cap *= 2;
                    nt = (char *)realloc(tr, cap);
                    if (!nt) { free(tr); tr = NULL; break; }
                    tr = nt;
                }
                if (tr) { memcpy(tr + len, line, n); len += n; tr[len] = 0; }
            }
            if (tr && a->ctx.trace_dropped) {
                char line[160];
                size_t n;
                snprintf(line, sizeof(line), "... %lu older trace events dropped\n", a->ctx.trace_dropped);
                n = strlen(line);
                if (len + n + 1 > cap) {
                    char *nt;
                    while (len + n + 1 > cap) cap *= 2;
                    nt = (char *)realloc(tr, cap);
                    if (!nt) { free(tr); tr = NULL; }
                    else tr = nt;
                }
                if (tr) { memcpy(tr + len, line, n); len += n; tr[len] = 0; }
            }
            if (tr && len) add_trace_message(a, tr);
            if (tr) free(tr);
        }
    }
    add_message(a, UI_ROLE_ASSISTANT, reply ? reply : "The provider did not return a response.");
    if (reply) free(reply);
    free(usercopy);
    a->input[0] = 0;
    a->input_len = 0;
    a->input_cursor = 0;
    selection_clear(a);
    vs_clear_attachments(&a->ctx);
    a->status[0] = 0;
}

static void handle_modal_key(App *a, XKeyEvent *ke)
{
    KeySym k; char b[64]; int n; unsigned int st; int extend;
    n = XLookupString(ke, b, sizeof(b), &k, 0); st=ke->state; extend=(st&ShiftMask)!=0;
    if (k == XK_Escape) {
        selection_clear(a);
        if (a->modal == MODAL_OAUTH_EDIT) a->modal = MODAL_OAUTH_CONFIG;
        else if (a->modal == MODAL_OAUTH_CONFIG) a->modal = MODAL_ACCOUNT;
        else a->modal = MODAL_NONE;
        return;
    }
    if (a->modal == MODAL_ACCOUNT) {
        if (k == XK_Return || k == XK_KP_Enter) {
            if (a->oauth_flow.active) return;
            if (!vs_oauth_is_configured(&a->ctx)) open_modal(a, MODAL_OAUTH_CONFIG, "");
            else if (!vs_oauth_is_signed_in(&a->ctx)) start_oauth_login(a);
        }
        return;
    }
    if (a->modal == MODAL_OAUTH_CONFIG) {
        if (k == XK_Up && a->oauth_field > 0) a->oauth_field--;
        else if (k == XK_Down && a->oauth_field < 4) a->oauth_field++;
        else if (k == XK_Return || k == XK_KP_Enter)
            open_modal(a, MODAL_OAUTH_EDIT, oauth_field_value(&a->ctx, a->oauth_field));
        return;
    }
    if (a->modal == MODAL_PROTOCOL) {
        if ((k == XK_Up || k == XK_Left) && a->protocol_sel > 0) a->protocol_sel--;
        else if ((k == XK_Down || k == XK_Right) && a->protocol_sel < 1) a->protocol_sel++;
        else if (k == XK_Return || k == XK_KP_Enter) apply_modal(a);
        return;
    }
    if (a->modal == MODAL_PROVIDER) {
        if (k == XK_Left && a->provider_sel > 0) a->provider_sel--;
        else if (k == XK_Right && a->provider_sel < PROVIDER_COUNT - 1) a->provider_sel++;
        else if (k == XK_Up && a->provider_sel >= 2) a->provider_sel -= 2;
        else if (k == XK_Down && a->provider_sel + 2 < PROVIDER_COUNT) a->provider_sel += 2;
        else if (k == XK_Return || k == XK_KP_Enter) apply_modal(a);
        return;
    }
    if (shortcut_mod(st) && (k==XK_a||k==XK_A)) { select_all_edit(a); return; }
    if (shortcut_mod(st) && (k==XK_c||k==XK_C)) { own_selection(a,1); return; }
    if (shortcut_mod(st) && (k==XK_v||k==XK_V)) { request_paste(a,a->clipboard_atom); return; }
    if (shortcut_mod(st) && (k==XK_x||k==XK_X)) { own_selection(a,1); delete_edit_selection(a,SEL_MODAL); return; }
    if (k == XK_Return || k == XK_KP_Enter) { apply_modal(a); return; }
    if (k == XK_Left) {
        size_t old=a->modal_cursor,newp=old?old-1:0; set_edit_cursor(a,SEL_MODAL,old,newp,extend); return;
    }
    if (k == XK_Right) {
        size_t old=a->modal_cursor,newp=old<a->modal_len?old+1:a->modal_len; set_edit_cursor(a,SEL_MODAL,old,newp,extend); return;
    }
    if (k == XK_Home) { size_t old=a->modal_cursor;set_edit_cursor(a,SEL_MODAL,old,0,extend);return; }
    if (k == XK_End) { size_t old=a->modal_cursor;set_edit_cursor(a,SEL_MODAL,old,a->modal_len,extend);return; }
    if (k == XK_BackSpace) {
        if (!delete_edit_selection(a,SEL_MODAL) && a->modal_cursor>0) {
            size_t p=a->modal_cursor-1;memmove(a->modal_text+p,a->modal_text+a->modal_cursor,a->modal_len-a->modal_cursor+1);
            a->modal_len--;a->modal_cursor=p;
        }
        return;
    }
    if (k == XK_Delete) {
        if (!delete_edit_selection(a,SEL_MODAL) && a->modal_cursor<a->modal_len) {
            memmove(a->modal_text+a->modal_cursor,a->modal_text+a->modal_cursor+1,a->modal_len-a->modal_cursor);
            a->modal_len--;
        }
        return;
    }
    if (n > 0) insert_edit_text(a,SEL_MODAL,b,(size_t)n);
}

static void handle_key(App *a, XKeyEvent *ke)
{
    KeySym k; char b[64]; int n; unsigned int st; int extend;
    a->cursor_visible=1;n=XLookupString(ke,b,sizeof(b),&k,0);st=ke->state;extend=(st&ShiftMask)!=0;
    if (a->modal != MODAL_NONE) { handle_modal_key(a, ke); return; }
    if (shortcut_mod(st) && (k==XK_a||k==XK_A)) { select_all_edit(a); return; }
    if (shortcut_mod(st) && (k==XK_c||k==XK_C)) { own_selection(a,1); if(a->clipboard_text)strcpy(a->status,"Copied selection"); return; }
    if (shortcut_mod(st) && (k==XK_v||k==XK_V)) { request_paste(a,a->clipboard_atom); return; }
    if (shortcut_mod(st) && (k==XK_x||k==XK_X)) { own_selection(a,1);delete_edit_selection(a,SEL_INPUT);return; }
    if (shortcut_mod(st) && (k==XK_o||k==XK_O)) { open_modal(a,MODAL_ATTACH,"");return; }
    if (shortcut_mod(st) && (k==XK_p||k==XK_P)) { open_modal(a,MODAL_PROVIDER,"");return; }
    if (shortcut_mod(st) && (k==XK_m||k==XK_M)) { open_modal(a,MODAL_MODEL,a->ctx.provider.model);return; }
    if (shortcut_mod(st) && (k==XK_k||k==XK_K)) { open_modal(a,MODAL_KEY,"");return; }
    if (shortcut_mod(st) && (k==XK_u||k==XK_U)) { open_modal(a,MODAL_BASE,a->ctx.provider.base_url);return; }
    if (shortcut_mod(st) && (k==XK_r||k==XK_R) && protocol_switchable(a)) { open_modal(a,MODAL_PROTOCOL,"");return; }
    if (shortcut_mod(st) && (k==XK_t||k==XK_T)) { a->ctx.cache_enabled=!a->ctx.cache_enabled;return; }
    if (shortcut_mod(st) && (k==XK_n||k==XK_N)) { new_chat(a);return; }
    if (shortcut_mod(st) && (k==XK_y||k==XK_Y)) { new_chat(a);return; }
    if (shortcut_mod(st) && (k==XK_l||k==XK_L)) { a->input[0]=0;a->input_len=0;a->input_cursor=0;selection_clear(a);return; }
    if (k == XK_Left) { size_t old=a->input_cursor,newp=old?old-1:0;set_edit_cursor(a,SEL_INPUT,old,newp,extend);return; }
    if (k == XK_Right) { size_t old=a->input_cursor,newp=old<a->input_len?old+1:a->input_len;set_edit_cursor(a,SEL_INPUT,old,newp,extend);return; }
    if (k == XK_Home) { size_t old=a->input_cursor;set_edit_cursor(a,SEL_INPUT,old,0,extend);return; }
    if (k == XK_End) { size_t old=a->input_cursor;set_edit_cursor(a,SEL_INPUT,old,a->input_len,extend);return; }
    if (k == XK_BackSpace) {
        if (!delete_edit_selection(a,SEL_INPUT) && a->input_cursor>0) {
            size_t p=a->input_cursor-1;memmove(a->input+p,a->input+a->input_cursor,a->input_len-a->input_cursor+1);
            a->input_len--;a->input_cursor=p;
        }
        return;
    }
    if (k == XK_Delete) {
        if (!delete_edit_selection(a,SEL_INPUT) && a->input_cursor<a->input_len) {
            memmove(a->input+a->input_cursor,a->input+a->input_cursor+1,a->input_len-a->input_cursor);
            a->input_len--;
        }
        return;
    }
    if ((k == XK_Return || k == XK_KP_Enter) && (st & ShiftMask)) { insert_edit_text(a,SEL_INPUT,"\n",1);return; }
    if (k == XK_Return || k == XK_KP_Enter) { send_message(a);return; }
    if (k == XK_Page_Up) { a->scroll_y-=260;return; }
    if (k == XK_Page_Down) { a->scroll_y+=260;return; }
    if (n > 0) { if(a->selection.kind==SEL_MESSAGE)selection_clear(a);insert_edit_text(a,SEL_INPUT,b,(size_t)n); }
}

static size_t line_offset_for_x(XFontStruct *f,const char *line,int relx)
{
    size_t i,n=strlen(line);int prev=0,w;
    if(relx<=0)return 0;
    for(i=1;i<=n;i++){
        w=XTextWidth(f,line,(int)i);
        if(relx < prev + (w-prev)/2) return i-1;
        prev=w;
    }
    return n;
}

static size_t wrapped_offset_at(XFontStruct *f,const char *text,int maxw,int x,int first_baseline,
                                int px,int py,int lineh)
{
    const char *p,*n;char line[2048];int line_no=0,target;
    if(!text||!*text)return 0;
    target=(py-(first_baseline-f->ascent))/lineh;
    if(target<0)target=0;
    p=text;
    while(*p){
        size_t off;
        if(!wrap_line(f,p,maxw,line,sizeof(line),&n))break;
        off=(size_t)(p-text);
        if(line_no==target || !*n) return off+line_offset_for_x(f,line,px-x);
        line_no++;if(n==p)break;p=n;
    }
    return strlen(text);
}

static int trace_header_at(App *a, int px, int py, int *message_index)
{
    int cw, cx, chat_top, chat_bottom, total, viewport, maxscroll, y, i;
    cx = content_x(a, &cw);
    chat_top = UI_TOPBAR_H + 10;
    chat_bottom = a->height - UI_COMPOSER_H + 5;
    if (py < chat_top || py > chat_bottom) return 0;
    viewport = chat_bottom - chat_top;
    total = message_total_height(a, cw);
    maxscroll = maxi(0, total - viewport);
    if (a->scroll_y < 0) a->scroll_y = 0;
    if (a->scroll_y > maxscroll) a->scroll_y = maxscroll;
    y = chat_top + 8 - a->scroll_y;
    for (i = 0; i < a->message_count; i++) {
        if (a->messages[i].role == UI_ROLE_TRACE &&
            hit(px, py, cx + 42, y + 2, cw - 42, 34)) {
            if (message_index) *message_index = i;
            return 1;
        }
        y += message_step_height(a, i, cw);
    }
    return 0;
}

static int message_offset_at(App *a,int px,int py,int *message_index,size_t *offset)
{
    int cw,cx,chat_top,chat_bottom,total,viewport,maxscroll,y,maxbubble,i,th,tw,bw,bx,by;
    int tx,baseline,maxw,bottom;
    cx=content_x(a,&cw);chat_top=UI_TOPBAR_H+10;chat_bottom=a->height-UI_COMPOSER_H+5;
    if(py<chat_top||py>chat_bottom)return 0;
    viewport=chat_bottom-chat_top;total=message_total_height(a,cw);maxscroll=maxi(0,total-viewport);
    if(a->scroll_y<0)a->scroll_y=0;
    if(a->scroll_y>maxscroll)a->scroll_y=maxscroll;
    y=chat_top+8-a->scroll_y;maxbubble=(cw*72)/100;
    for(i=0;i<a->message_count;i++){
        if(a->messages[i].role==UI_ROLE_USER){
            th=wrapped_height(a->font,a->messages[i].text,maxbubble-28,20);
            if(!strchr(a->messages[i].text,'\n')){tw=text_w(a->font,a->messages[i].text)+28;bw=mini(maxbubble,maxi(76,tw));}else bw=maxbubble;
            bx=cx+cw-bw;by=y+5;tx=bx+14;baseline=by+18;maxw=bw-28;bottom=baseline+th+a->font->descent;
            if(py>=baseline-a->font->ascent-3&&py<=bottom&&px>=tx-4&&px<=tx+maxw+4){
                *message_index=i;*offset=wrapped_offset_at(a->font,a->messages[i].text,maxw,tx,baseline,px,py,20);return 1;
            }
        }else if(a->messages[i].role==UI_ROLE_TRACE){
            if(!a->messages[i].collapsed){
                th=trace_body_height(a,&a->messages[i],cw);tx=cx+56;baseline=y+54;maxw=cw-70;bottom=baseline+th+a->small->descent;
                if(py>=baseline-a->small->ascent-3&&py<=bottom&&px>=tx-4&&px<=tx+maxw+4){
                    *message_index=i;*offset=wrapped_offset_at(a->small,a->messages[i].text,maxw,tx,baseline,px,py,18);return 1;
                }
            }
        }else{
            th=wrapped_height(a->font,a->messages[i].text,cw-50,20);tx=cx+42;baseline=y+40;maxw=cw-50;bottom=baseline+th+a->font->descent;
            if(py>=baseline-a->font->ascent-3&&py<=bottom&&px>=tx-4&&px<=tx+maxw+4){
                *message_index=i;*offset=wrapped_offset_at(a->font,a->messages[i].text,maxw,tx,baseline,px,py,20);return 1;
            }
        }
        y+=message_step_height(a,i,cw);
    }
    return 0;
}

static size_t input_offset_at(App *a,int px,int py,int cx,int cw)
{
    const char *p,*n;char line[1024];int count=0,target,visible_start,max_lines;
    int panel_y=a->height-114,text_y,maxw=cw-104,x=cx+48;size_t result=a->input_len;
    max_lines=a->ctx.attachment_count?1:2;text_y=a->ctx.attachment_count?panel_y+51:panel_y+30;
    p=a->input;while(*p){if(!wrap_line(a->font,p,maxw,line,sizeof(line),&n))break;count++;if(n==p)break;p=n;}
    if(count==0)return 0;
    visible_start=count>max_lines?count-max_lines:0;
    target=(py-(text_y-a->font->ascent))/19;if(target<0)target=0;if(target>=max_lines)target=max_lines-1;target+=visible_start;
    p=a->input;count=0;while(*p){size_t off;if(!wrap_line(a->font,p,maxw,line,sizeof(line),&n))break;off=(size_t)(p-a->input);if(count==target||!*n){result=off+line_offset_for_x(a->font,line,px-x);break;}count++;if(n==p)break;p=n;}
    if(result>a->input_len)result=a->input_len;
    return result;
}

static int chip_at(App *a, int px, int py, int cx, int cw)
{
    int py0, xx, i, w;
    const char *name;
    char b[128];
    if (!a->ctx.attachment_count) return -1;
    py0 = a->height - 114 + 9;
    if (py < py0 || py >= py0 + 24) return -1;
    xx = cx + 13;
    for (i = 0; i < a->ctx.attachment_count; i++) {
        name = base_name(a->ctx.attachments[i].path);
        snprintf(b, sizeof(b), "%s %s  x", a->ctx.attachments[i].is_image ? "IMG" : "FILE", name);
        w = text_w(a->small, b) + 18;
        if (w > 210) w = 210;
        if (xx + w > cx + 13 + cw - 26) break;
        if (hit(px, py, xx, py0, w, 24)) return i;
        xx += w + 7;
    }
    return -1;
}

static void handle_modal_click(App *a, int px, int py)
{
    int x, y, w, h, i, row, col, bx, by, bw;
    modal_geometry(a, &x, &y, &w, &h);
    if (a->modal == MODAL_PROVIDER) {
        bw = (w - 62) / 2;
        for (i = 0; i < PROVIDER_COUNT; i++) {
            row = i / 2; col = i % 2;
            bx = x + 22 + col * (bw + 18);
            by = y + 76 + row * 48;
            if (hit(px, py, bx, by, bw, 38)) {
                a->provider_sel = i;
                apply_modal(a);
                return;
            }
        }
        return;
    }
    if (a->modal == MODAL_PROTOCOL) {
        for (i = 0; i < 2; i++) {
            by = y + 78 + i * 52;
            if (hit(px, py, x + 24, by, w - 48, 40)) {
                a->protocol_sel = i;
                apply_modal(a);
                return;
            }
        }
        return;
    }
    if (a->modal == MODAL_ACCOUNT) {
        if (hit(px, py, x + 24, y + 169, w - 48, 44)) {
            if (a->oauth_flow.active) return;
            if (!vs_oauth_is_configured(&a->ctx)) open_modal(a, MODAL_OAUTH_CONFIG, "");
            else if (!vs_oauth_is_signed_in(&a->ctx)) start_oauth_login(a);
        } else if (hit(px, py, x + 24, y + 232, 178, 38)) {
            open_modal(a, MODAL_OAUTH_CONFIG, "");
        } else if (hit(px, py, x + 214, y + 232, 142, 38)) {
            open_modal(a, MODAL_KEY, "");
        } else if (hit(px, py, x + 24, y + 286, 142, 36)) {
            if (a->oauth_flow.active) {
                vs_oauth_cancel(&a->oauth_flow);
                strcpy(a->status, "OAuth login cancelled");
            } else if (a->ctx.oauth.access_token[0]) {
                vs_oauth_logout(&a->ctx);
                strcpy(a->status, "OAuth tokens cleared");
            }
        } else if (hit(px, py, x + w - 100, y + 286, 76, 36)) {
            a->modal = MODAL_NONE;
        }
        return;
    }
    if (a->modal == MODAL_OAUTH_CONFIG) {
        for (i = 0; i < 5; i++) {
            by = y + 88 + i * 54;
            if (hit(px, py, x + 24, by, w - 48, 44)) {
                a->oauth_field = i;
                open_modal(a, MODAL_OAUTH_EDIT, oauth_field_value(&a->ctx, i));
                return;
            }
        }
        if (hit(px, py, x + 24, y + h - 50, 160, 32)) {
            a->modal = MODAL_ACCOUNT;
        } else if (hit(px, py, x + w - 105, y + h - 50, 81, 32)) {
            if (vs_oauth_save_profile(&a->ctx) == 0) { (void)vs_persist_settings(&a->ctx); strcpy(a->status, "OAuth profile + encrypted config saved"); }
            else strcpy(a->status, "Could not save OAuth profile");
            a->modal = MODAL_ACCOUNT;
        }
        return;
    }
    if (a->modal == MODAL_GLOBAL_CONFIG) {
        char msg[512];
        msg[0]=0;
        if (hit(px,py,x+24,y+177,150,38)) {
            if(vs_global_config_load(&a->ctx,msg,sizeof(msg))==0) snprintf(a->status,sizeof(a->status),"%s",msg);
            else snprintf(a->status,sizeof(a->status),"Global load failed: %.450s",msg[0]?msg:"unknown error");
            a->modal=MODAL_NONE;
        } else if (hit(px,py,x+187,y+177,150,38)) {
            if(vs_global_config_save(&a->ctx,msg,sizeof(msg))==0) snprintf(a->status,sizeof(a->status),"%s",msg);
            else snprintf(a->status,sizeof(a->status),"Global save failed: %.450s",msg[0]?msg:"unknown error");
            a->modal=MODAL_NONE;
        } else if (hit(px,py,x+w-100,y+h-48,76,32)) a->modal=MODAL_NONE;
        return;
    }

    if (hit(px,py,x+24,y+76,w-48,40)) {
        const char *shown=a->modal_text;
        char masked_local[sizeof(a->modal_text)];size_t j;
        if(a->modal==MODAL_KEY){for(j=0;j<a->modal_len&&j<sizeof(masked_local)-1;j++)masked_local[j]='*';masked_local[j]=0;shown=masked_local;}
        a->modal_cursor=line_offset_for_x(a->font,shown,px-(x+35));
        if(a->modal_cursor>a->modal_len)a->modal_cursor=a->modal_len;
        selection_clear(a);a->selection.kind=SEL_MODAL;a->selection.anchor=a->selection.cursor=a->modal_cursor;
        return;
    }

    if (hit(px, py, x + w - 190, y + 132, 72, 34)) {
        if (a->modal == MODAL_OAUTH_EDIT) a->modal = MODAL_OAUTH_CONFIG;
        else a->modal = MODAL_NONE;
    } else if (hit(px, py, x + w - 106, y + 132, 82, 34)) apply_modal(a);
}

static void handle_click(App *a, XButtonEvent *be)
{
    int x,y,cw,cx,panel_y,panel_h,idx,cache_y,is_openai,mi;size_t off;
    x=be->x;y=be->y;a->cursor_visible=1;
    if(be->button==Button4){a->scroll_y-=72;a->auto_scroll=0;return;}
    if(be->button==Button5){a->scroll_y+=72;a->auto_scroll=0;return;}
    if(be->button==Button2){request_paste(a,XA_PRIMARY);return;}
    if(be->button!=Button1)return;
    if(a->modal!=MODAL_NONE){handle_modal_click(a,x,y);return;}

    is_openai=a->ctx.provider.kind==VS_PROVIDER_OPENAI;
    if(x<UI_SIDEBAR_W){
        if(hit(x,y,14,16,UI_SIDEBAR_W-28,38))new_chat(a);
        else if(hit(x,y,14,94,UI_SIDEBAR_W-28,36))open_modal(a,MODAL_PROVIDER,"");
        else if(hit(x,y,14,136,UI_SIDEBAR_W-28,36))open_modal(a,MODAL_MODEL,a->ctx.provider.model);
        else if(hit(x,y,14,178,UI_SIDEBAR_W-28,36)){
            if(protocol_switchable(a))open_modal(a,MODAL_PROTOCOL,"");
            else snprintf(a->status,sizeof(a->status),"%s uses the %s protocol",a->ctx.provider.name,vs_protocol_name(a->ctx.provider.protocol));
        }
        else if(hit(x,y,14,220,UI_SIDEBAR_W-28,36))open_modal(a,MODAL_BASE,a->ctx.provider.base_url);
        else if(is_openai&&hit(x,y,14,286,UI_SIDEBAR_W-28,36))open_modal(a,MODAL_ACCOUNT,"");
        else if(is_openai&&hit(x,y,14,328,UI_SIDEBAR_W-28,36))open_modal(a,MODAL_KEY,"");
        else if(!is_openai&&hit(x,y,14,286,UI_SIDEBAR_W-28,36))open_modal(a,MODAL_KEY,"");
        else{
            cache_y=is_openai?394:352;
            if(hit(x,y,14,cache_y,UI_SIDEBAR_W-28,36)){
                a->ctx.cache_enabled=!a->ctx.cache_enabled;(void)vs_persist_settings(&a->ctx);
                strcpy(a->status,a->ctx.cache_enabled?"Prompt cache enabled":"Prompt cache disabled");
            }else if(hit(x,y,14,cache_y+42,UI_SIDEBAR_W-28,36)){
                vs_cache_clear(&a->ctx);strcpy(a->status,"Local cache and cache statistics cleared");
            }else if(hit(x,y,14,cache_y+84,UI_SIDEBAR_W-28,36)){
                open_modal(a,MODAL_GLOBAL_CONFIG,"");
            }else if(hit(x,y,14,cache_y+126,UI_SIDEBAR_W-28,36)){
                open_modal(a,MODAL_MCP,"");
            }
        }
        return;
    }

    cx=content_x(a,&cw);panel_y=a->height-114;panel_h=82;
    if (trace_header_at(a, x, y, &mi)) {
        a->messages[mi].collapsed = !a->messages[mi].collapsed;
        selection_clear(a);
        a->auto_scroll = 0;
        return;
    }
    idx=chip_at(a,x,y,cx,cw);if(idx>=0){remove_attachment(a,idx);return;}
    if(hit(x,y,cx+11,panel_y+panel_h-34,28,28)){open_modal(a,MODAL_ATTACH,"");return;}
    if(hit(x,y,cx+cw-39,panel_y+panel_h-34,28,28)){send_message(a);return;}

    if(y>=panel_y&&y<=panel_y+panel_h){
        off=input_offset_at(a,x,y,cx,cw);a->input_cursor=off;
        a->selection.kind=SEL_INPUT;a->selection.message_index=-1;a->selection.anchor=off;a->selection.cursor=off;a->selection.dragging=1;
        return;
    }
    if(message_offset_at(a,x,y,&mi,&off)){
        a->selection.kind=SEL_MESSAGE;a->selection.message_index=mi;a->selection.anchor=off;a->selection.cursor=off;a->selection.dragging=1;
        return;
    }
    selection_clear(a);
}

static int handle_motion(App *a,XMotionEvent *me)
{
    int cw,cx,mi;size_t off,old;
    if(!a->selection.dragging)return 0;
    old=a->selection.cursor;
    if(a->selection.kind==SEL_INPUT){
        cx=content_x(a,&cw);off=input_offset_at(a,me->x,me->y,cx,cw);a->selection.cursor=off;a->input_cursor=off;
    }else if(a->selection.kind==SEL_MESSAGE){
        if(message_offset_at(a,me->x,me->y,&mi,&off)&&mi==a->selection.message_index)a->selection.cursor=off;
    }
    return old != a->selection.cursor;
}

static void handle_release(App *a,XButtonEvent *be)
{
    (void)be;
    if(a->selection.dragging){a->selection.dragging=0;if(selection_bounds(a,0,0))own_selection(a,0);}
}

static void init_app(App *a, int argc, char **argv)
{
    XSetWindowAttributes attrs;
    XSizeHints hints;
    memset(a, 0, sizeof(*a));
    vs_init(&a->ctx);
    if (argc > 1) vs_load_config(&a->ctx, argv[1]);
    a->width = 1080;
    a->height = 760;
    a->auto_scroll = 1;
    a->cursor_visible = 1;
    a->oauth_flow.listener_fd = -1;
    selection_clear(a);
    a->dpy = XOpenDisplay(NULL);
    if (!a->dpy) return;
    a->screen = DefaultScreen(a->dpy);
    init_colors(a);
    attrs.background_pixel = a->c.bg;
    a->win = XCreateWindow(a->dpy, RootWindow(a->dpy, a->screen), 70, 50,
                           (unsigned int)a->width, (unsigned int)a->height, 0,
                           CopyFromParent, InputOutput, CopyFromParent,
                           CWBackPixel, &attrs);
    XStoreName(a->dpy, a->win, "VibeSolaris");
    XSelectInput(a->dpy, a->win, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask |
                 PointerMotionMask | StructureNotifyMask | PropertyChangeMask | FocusChangeMask);
    a->canvas = a->win;
    a->has_focus = 1;
    a->gc = XCreateGC(a->dpy, a->win, 0, NULL);
    a->font = load_font(a, "-*-helvetica-medium-r-normal--14-*-*-*-*-*-*-*", "fixed");
    a->bold = load_font(a, "-*-helvetica-bold-r-normal--14-*-*-*-*-*-*-*", "fixed");
    a->small = load_font(a, "-*-helvetica-medium-r-normal--12-*-*-*-*-*-*-*", "fixed");
    a->wm_delete = XInternAtom(a->dpy, "WM_DELETE_WINDOW", False);
    a->clipboard_atom = XInternAtom(a->dpy, "CLIPBOARD", False);
    a->utf8_atom = XInternAtom(a->dpy, "UTF8_STRING", False);
    a->targets_atom = XInternAtom(a->dpy, "TARGETS", False);
    a->paste_atom = XInternAtom(a->dpy, "VIBESOLARIS_PASTE", False);
    a->incr_atom = XInternAtom(a->dpy, "INCR", False);
    a->xdnd_aware_atom = XInternAtom(a->dpy, "XdndAware", False);
    a->xdnd_enter_atom = XInternAtom(a->dpy, "XdndEnter", False);
    a->xdnd_position_atom = XInternAtom(a->dpy, "XdndPosition", False);
    a->xdnd_status_atom = XInternAtom(a->dpy, "XdndStatus", False);
    a->xdnd_leave_atom = XInternAtom(a->dpy, "XdndLeave", False);
    a->xdnd_drop_atom = XInternAtom(a->dpy, "XdndDrop", False);
    a->xdnd_finished_atom = XInternAtom(a->dpy, "XdndFinished", False);
    a->xdnd_selection_atom = XInternAtom(a->dpy, "XdndSelection", False);
    a->xdnd_action_copy_atom = XInternAtom(a->dpy, "XdndActionCopy", False);
    a->xdnd_type_list_atom = XInternAtom(a->dpy, "XdndTypeList", False);
    a->uri_list_atom = XInternAtom(a->dpy, "text/uri-list", False);
    a->xdnd_property_atom = XInternAtom(a->dpy, "VIBESOLARIS_XDND", False);
    {
        unsigned long xdnd_version = 5;
        XChangeProperty(a->dpy, a->win, a->xdnd_aware_atom, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)&xdnd_version, 1);
    }
    XSetWMProtocols(a->dpy, a->win, &a->wm_delete, 1);
    memset(&hints, 0, sizeof(hints));
    hints.flags = PMinSize;
    hints.min_width = 760;
    hints.min_height = 560;
    XSetWMNormalHints(a->dpy, a->win, &hints);
    XMapWindow(a->dpy, a->win);
}

static void destroy_app(App *a)
{
    if (a->oauth_flow.active) vs_oauth_cancel(&a->oauth_flow);
    free_messages(a);
    free(a->clipboard_text);
    free(a->paste_data);
    if (a->back_buffer) XFreePixmap(a->dpy, a->back_buffer);
    if (a->font) XFreeFont(a->dpy, a->font);
    if (a->bold) XFreeFont(a->dpy, a->bold);
    if (a->small) XFreeFont(a->dpy, a->small);
    if (a->gc) XFreeGC(a->dpy, a->gc);
    if (a->win) XDestroyWindow(a->dpy, a->win);
    if (a->dpy) XCloseDisplay(a->dpy);
    vs_shutdown(&a->ctx);
}

int main(int argc, char **argv)
{
    App a;
    XEvent ev;
    fd_set rfds;
    struct timeval tv;
    int xfd, rc, running, maxfd, orc;
    char oauth_msg[512];
    init_app(&a, argc, argv);
    if (!a.dpy) {
        fprintf(stderr, "VibeSolaris GUI: cannot open X display\n");
        vs_shutdown(&a.ctx);
        return 1;
    }
    xfd = ConnectionNumber(a.dpy);
    running = 1;
    while (running) {
        while (XPending(a.dpy)) {
            XNextEvent(a.dpy, &ev);
            if (ev.type == ClientMessage && ev.xclient.message_type == XInternAtom(a.dpy, "WM_PROTOCOLS", False) &&
                (Atom)ev.xclient.data.l[0] == a.wm_delete) {
                running = 0;
                break;
            }
            if (ev.type == ClientMessage && (ev.xclient.message_type == a.xdnd_enter_atom ||
                                             ev.xclient.message_type == a.xdnd_position_atom ||
                                             ev.xclient.message_type == a.xdnd_leave_atom ||
                                             ev.xclient.message_type == a.xdnd_drop_atom)) {
                handle_xdnd_client(&a, &ev.xclient);
                redraw(&a);
            } else if (ev.type == ConfigureNotify) {
                a.width = ev.xconfigure.width;
                a.height = ev.xconfigure.height;
                redraw(&a);
            } else if (ev.type == Expose) {
                if (ev.xexpose.count == 0) redraw(&a);
            } else if (ev.type == KeyPress) {
                handle_key(&a, &ev.xkey);
                redraw(&a);
            } else if (ev.type == ButtonPress) {
                handle_click(&a, &ev.xbutton);
                redraw(&a);
            } else if (ev.type == MotionNotify) {
                XEvent last = ev;
                while (XCheckTypedWindowEvent(a.dpy, a.win, MotionNotify, &ev)) last = ev;
                if (handle_motion(&a, &last.xmotion)) redraw(&a);
            } else if (ev.type == ButtonRelease) {
                handle_release(&a, &ev.xbutton);
                redraw(&a);
            } else if (ev.type == SelectionRequest) {
                handle_selection_request(&a, &ev.xselectionrequest);
            } else if (ev.type == SelectionNotify) {
                if (ev.xselection.selection == a.xdnd_selection_atom) handle_xdnd_selection(&a, &ev.xselection);
                else handle_paste_selection(&a, &ev.xselection);
                redraw(&a);
            } else if (ev.type == PropertyNotify) {
                handle_paste_property(&a, &ev.xproperty);
                if (a.paste_incr || ev.xproperty.atom == a.paste_atom) redraw(&a);
            } else if (ev.type == FocusIn) {
                a.has_focus = 1; a.cursor_visible = 1; redraw(&a);
            } else if (ev.type == FocusOut) {
                a.has_focus = 0; a.cursor_visible = 0; redraw(&a);
            }
        }
        if (!running) break;

        FD_ZERO(&rfds);
        FD_SET(xfd, &rfds);
        maxfd = xfd;
        if (a.oauth_flow.active && a.oauth_flow.listener_fd >= 0) {
            FD_SET(a.oauth_flow.listener_fd, &rfds);
            if (a.oauth_flow.listener_fd > maxfd) maxfd = a.oauth_flow.listener_fd;
        }
        tv.tv_sec = 0;
        tv.tv_usec = 500000;
        rc = select(maxfd + 1, &rfds, (fd_set *)0, (fd_set *)0, &tv);
        if (a.oauth_flow.active) {
            oauth_msg[0] = 0;
            orc = vs_oauth_poll(&a.ctx, &a.oauth_flow, oauth_msg, sizeof(oauth_msg));
            if (orc != 0) {
                snprintf(a.status, sizeof(a.status), "%s", oauth_msg[0] ? oauth_msg : (orc > 0 ? "OAuth login completed" : "OAuth login failed"));
                redraw(&a);
            }
        }
        if (rc == 0) {
            if (a.has_focus) {
                a.cursor_visible = !a.cursor_visible;
                redraw(&a);
            }
        } else if (rc < 0 && errno != EINTR) {
            break;
        }
    }
    destroy_app(&a);
    return 0;
}
