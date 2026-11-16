/* SPDX-License-Identifier: Unlicense */
/* VibeSolaris Computer Use — X11/Xtst implementation */
/*
 * Provides desktop control (screenshot, click, type, move) when X11 is
 * available at compile time AND a DISPLAY is set at runtime.
 * On systems without X11 headers, all functions return helpful error
 * messages so the TUI binary still builds cleanly.
 */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *cu_dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* Runtime backend detection.  Preference order: native Wayland, native
   Cocoa, then X11.  Wayland/Cocoa probes are compiled in only when the
   corresponding backend objects are, because their entry points do not
   exist otherwise.  The result is cached: the session environment does
   not change during a process lifetime, and the probes shell out. */
int vs_computer_use_detect_backend(void)
{
    static int cached = -1;
    const char *display;
    if (cached != -1) return cached;
    cached = VS_COMPUTER_USE_NONE;
#ifdef HAVE_WAYLAND
    if (cached == VS_COMPUTER_USE_NONE && vs_wayland_init() == 0)
        cached = VS_COMPUTER_USE_WAYLAND;
#endif
#ifdef HAVE_COCOA
    if (cached == VS_COMPUTER_USE_NONE && vs_cocoa_init() == 0)
        cached = VS_COMPUTER_USE_COCOA;
#endif
    display = getenv("DISPLAY");
    if (cached == VS_COMPUTER_USE_NONE && display && *display)
        cached = VS_COMPUTER_USE_X11;
    return cached;
}

#if !defined(HAVE_X11) && (defined(HAVE_WAYLAND) || defined(HAVE_COCOA))
/* Non-X11 build with portable backends linked in: delegate at runtime. */
#define CU_HAVE_PORTABLE_BACKENDS 1
#endif

#ifdef HAVE_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

/* Module-level Display handle */
static Display *g_display = NULL;
static int g_screen = 0;

/* Base64 encoding helpers */
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void cu_b64_encode(const unsigned char *in, int len, char *out)
{
    int i, j;
    for (i = 0, j = 0; i < len; i += 3) {
        unsigned char a = in[i];
        unsigned char b = (i + 1 < len) ? in[i + 1] : 0;
        unsigned char c = (i + 2 < len) ? in[i + 2] : 0;
        unsigned int n = (a << 16) | (b << 8) | c;
        out[j++] = b64_table[(n >> 18) & 0x3F];
        out[j++] = b64_table[(n >> 12) & 0x3F];
        out[j++] = b64_table[(n >> 6) & 0x3F];
        out[j++] = b64_table[n & 0x3F];
    }
    /* Pad */
    int mod = len % 3;
    if (mod == 1) { out[j - 2] = '='; out[j - 1] = '='; }
    else if (mod == 2) out[j - 1] = '=';
    out[j] = 0;
}

int vs_computer_use_init(void)
{
    const char *display = getenv("DISPLAY");
    if (!display || !*display) return 0;

    g_display = XOpenDisplay(display);
    if (!g_display) return 0;
    g_screen = DefaultScreen(g_display);
    return 1;
}

char *vs_computer_use_screenshot(void)
{
    if (!g_display) {
        return cu_dupstr("Computer Use: no X11 display (set DISPLAY or run on a system with X11)");
    }

    Screen *screen = ScreenOfDisplay(g_display, g_screen);
    int width = WidthOfScreen(screen);
    int height = HeightOfScreen(screen);

    /* Limit resolution to avoid huge buffers — max 1280x720 */
    if (width > 1280) width = 1280;
    if (height > 720) height = 720;

    Window root = RootWindow(g_display, g_screen);
    XImage *img = XGetImage(g_display, root, 0, 0, width, height, AllPlanes, ZPixmap);
    if (!img) {
        return cu_dupstr("Computer Use: screenshot failed (XGetImage error)");
    }

    /* Extract color masks from the screen visual */
    unsigned long red_mask   = DefaultVisual(g_display, g_screen)->red_mask;
    unsigned long green_mask = DefaultVisual(g_display, g_screen)->green_mask;
    unsigned long blue_mask  = DefaultVisual(g_display, g_screen)->blue_mask;

    /* Convert to RGB (3 bytes per pixel) */
    int pixel_count = width * height;
    unsigned char *rgb = (unsigned char *)malloc(pixel_count * 3);
    if (!rgb) {
        XDestroyImage(img);
        return cu_dupstr("Computer Use: screenshot failed (out of memory)");
    }

    /* Find bit shifts for each color channel */
    int r_shift = 0, g_shift = 0, b_shift = 0;
    {
        unsigned long m;
        for (m = red_mask; m && !(m & 1); m >>= 1) r_shift++;
        for (m = green_mask; m && !(m & 1); m >>= 1) g_shift++;
        for (m = blue_mask; m && !(m & 1); m >>= 1) b_shift++;
    }
    int i;
    for (i = 0; i < pixel_count; i++) {
        unsigned long pixel = XGetPixel(img, i % width, i / width);
        rgb[i * 3]     = (unsigned char)((pixel & red_mask)   >> r_shift);
        rgb[i * 3 + 1] = (unsigned char)((pixel & green_mask) >> g_shift);
        rgb[i * 3 + 2] = (unsigned char)((pixel & blue_mask)  >> b_shift);
    }
    XDestroyImage(img);

    /* Base64 encode the RGB data */
    int rgb_size = pixel_count * 3;
    int b64_size = (rgb_size + 2) / 3 * 4 + 1;
    char *b64 = (char *)malloc(b64_size);
    if (!b64) {
        free(rgb);
        return cu_dupstr("Computer Use: screenshot failed (out of memory)");
    }
    cu_b64_encode(rgb, rgb_size, b64);
    free(rgb);

    /* Return as a structured result with metadata */
    char *result = (char *)malloc(strlen(b64) + 256);
    if (!result) {
        free(b64);
        return cu_dupstr("Computer Use: screenshot failed (out of memory)");
    }
    snprintf(result, strlen(b64) + 256,
             "SCREENSHOT: %dx%d RGB base64 (%d bytes)\n%s",
             width, height, (int)strlen(b64), b64);
    free(b64);
    return result;
}

char *vs_computer_use_click(int x, int y)
{
    if (!g_display) {
        return cu_dupstr("Computer Use: no X11 display (set DISPLAY or run on a system with X11)");
    }

    /* Left button down then up */
    XTestFakeButtonEvent(g_display, 1, True, CurrentTime);
    XTestFakeButtonEvent(g_display, 1, False, CurrentTime);
    XFlush(g_display);

    char result[128];
    snprintf(result, sizeof(result), "Clicked at (%d, %d)", x, y);
    return cu_dupstr(result);
}

char *vs_computer_use_type(const char *text)
{
    if (!g_display || !text || !*text) {
        return cu_dupstr("Computer Use: no X11 display or empty text");
    }

    const char *p = text;
    while (*p) {
        KeySym keysym = XStringToKeysym(p);
        if (keysym == NoSymbol) {
            /* Fallback: try single character */
            keysym = (KeySym)(unsigned char)*p;
        }

        /* Key down */
        KeyCode keycode = XKeysymToKeycode(g_display, keysym);
        if (keycode != 0) {
            XTestFakeKeyEvent(g_display, keycode, True, CurrentTime);
            XTestFakeKeyEvent(g_display, keycode, False, CurrentTime);
        }
        p++;
    }
    XFlush(g_display);

    char result[256];
    snprintf(result, sizeof(result), "Typed %d characters", (int)strlen(text));
    return cu_dupstr(result);
}

char *vs_computer_use_move(int x, int y)
{
    if (!g_display) {
        return cu_dupstr("Computer Use: no X11 display (set DISPLAY or run on a system with X11)");
    }

    XWarpPointer(g_display, None, RootWindow(g_display, g_screen),
                 0, 0, 0, 0, x, y);
    XFlush(g_display);

    char result[128];
    snprintf(result, sizeof(result), "Moved cursor to (%d, %d)", x, y);
    return cu_dupstr(result);
}

/* Cleanup on shutdown */
void vs_computer_use_shutdown(void)
{
    if (g_display) {
        XCloseDisplay(g_display);
        g_display = NULL;
    }
}

#else /* !HAVE_X11 — no X11 headers available at compile time */

int vs_computer_use_init(void)
{
    const char *display = getenv("DISPLAY");
#ifdef CU_HAVE_PORTABLE_BACKENDS
    if (vs_computer_use_detect_backend() != VS_COMPUTER_USE_NONE) return 1;
#endif
    return (display && *display) ? 1 : 0;
}

char *vs_computer_use_screenshot(void)
{
#ifdef CU_HAVE_PORTABLE_BACKENDS
    int backend = vs_computer_use_detect_backend();
#ifdef HAVE_WAYLAND
    if (backend == VS_COMPUTER_USE_WAYLAND) return vs_wayland_screenshot();
#endif
#ifdef HAVE_COCOA
    if (backend == VS_COMPUTER_USE_COCOA) return vs_cocoa_screenshot();
#endif
#endif
    return cu_dupstr("Computer Use: no supported display backend on this system (X11, Wayland, or macOS Cocoa required)");
}

char *vs_computer_use_click(int x, int y)
{
#ifdef CU_HAVE_PORTABLE_BACKENDS
    int backend = vs_computer_use_detect_backend();
#ifdef HAVE_WAYLAND
    if (backend == VS_COMPUTER_USE_WAYLAND) return vs_wayland_click(x, y);
#endif
#ifdef HAVE_COCOA
    if (backend == VS_COMPUTER_USE_COCOA) return vs_cocoa_click(x, y);
#endif
#else
    (void)x; (void)y;
#endif
    return cu_dupstr("Computer Use: no supported display backend on this system (X11, Wayland, or macOS Cocoa required)");
}

char *vs_computer_use_type(const char *text)
{
#ifdef CU_HAVE_PORTABLE_BACKENDS
    int backend;
    if (!text) text = "";
    backend = vs_computer_use_detect_backend();
#ifdef HAVE_WAYLAND
    if (backend == VS_COMPUTER_USE_WAYLAND) return vs_wayland_type(text);
#endif
#ifdef HAVE_COCOA
    if (backend == VS_COMPUTER_USE_COCOA) return vs_cocoa_type(text);
#endif
#else
    (void)text;
#endif
    return cu_dupstr("Computer Use: no supported display backend on this system (X11, Wayland, or macOS Cocoa required)");
}

char *vs_computer_use_move(int x, int y)
{
#ifdef CU_HAVE_PORTABLE_BACKENDS
    int backend = vs_computer_use_detect_backend();
#ifdef HAVE_WAYLAND
    if (backend == VS_COMPUTER_USE_WAYLAND) return vs_wayland_move(x, y);
#endif
#ifdef HAVE_COCOA
    if (backend == VS_COMPUTER_USE_COCOA) return vs_cocoa_move(x, y);
#endif
#else
    (void)x; (void)y;
#endif
    return cu_dupstr("Computer Use: no supported display backend on this system (X11, Wayland, or macOS Cocoa required)");
}

void vs_computer_use_shutdown(void)
{
    /* The portable backends are stateless; nothing to clean up. */
}

#endif /* HAVE_X11 */
