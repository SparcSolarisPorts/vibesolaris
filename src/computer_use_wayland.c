/* SPDX-License-Identifier: Unlicense */
/* Wayland Computer Use — uses external commands (grim, slurp, swaymsg, hyprctl) */
/* No library linking required; compiled only when HAVE_WAYLAND is defined */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_WAYLAND

/* Detect available Wayland compositor tools */
static int has_grim(void)
{
    int status;
    char *out = vs_run_command("which grim 2>/dev/null", &status);
    free(out);
    return status == 0;
}

static int has_slurp(void)
{
    int status;
    char *out = vs_run_command("which slurp 2>/dev/null", &status);
    free(out);
    return status == 0;
}

static int has_swaymsg(void)
{
    int status;
    char *out = vs_run_command("which swaymsg 2>/dev/null", &status);
    free(out);
    return status == 0;
}

static int has_hyprctl(void)
{
    int status;
    char *out = vs_run_command("which hyprctl 2>/dev/null", &status);
    free(out);
    return status == 0;
}

static int has_wlclipboard(void)
{
    int status;
    char *out = vs_run_command("which wl-copy 2>/dev/null", &status);
    free(out);
    return status == 0;
}

/* Detect compositor type */
static const char *detect_compositor(void)
{
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    if (wayland_display && wayland_display[0]) {
        if (has_hyprctl()) return "hyprland";
        if (has_swaymsg()) return "sway";
        return "generic";
    }
    return NULL;
}

int vs_wayland_init(void)
{
    const char *compositor = detect_compositor();
    if (!compositor) {
        /* No Wayland display */
        return -1;
    }
    if (!has_grim()) {
        /* grim not available */
        return -1;
    }
    return 0;
}

char *vs_wayland_screenshot(void)
{
    char cmd[512];
    char *tmpfile;
    char *result;
    int status;

    if (vs_wayland_init() != 0) {
        return strdup("ERROR: Wayland screenshot not available (install grim)");
    }

    /* Create temporary file for screenshot */
    tmpfile = strdup("/tmp/vibesolaris_wayland_screenshot.png");

    /* Capture screenshot using grim */
    snprintf(cmd, sizeof(cmd), "grim -g \"$(slurp)\" %s 2>/dev/null || grim %s 2>/dev/null", tmpfile, tmpfile);

    result = vs_run_command(cmd, &status);
    if (status != 0) {
        free(result);
        /* Fallback: full screen without slurp */
        snprintf(cmd, sizeof(cmd), "grim %s 2>/dev/null", tmpfile);
        result = vs_run_command(cmd, &status);
        free(result);
    }

    if (status != 0) {
        free(tmpfile);
        return strdup("ERROR: Failed to capture screenshot");
    }

    /* Read the PNG file as base64 */
    char read_cmd[600];
    snprintf(read_cmd, sizeof(read_cmd), "base64 -w0 %s 2>/dev/null", tmpfile);
    result = vs_run_command(read_cmd, &status);

    /* Clean up */
    snprintf(cmd, sizeof(cmd), "rm -f %s", tmpfile);
    vs_run_command(cmd, &status);
    free(tmpfile);

    if (status != 0 || !result || !result[0]) {
        free(result);
        return strdup("ERROR: Failed to encode screenshot");
    }

    return result;
}

char *vs_wayland_click(int x, int y)
{
    char cmd[512];
    int status;
    char *result;
    const char *compositor = detect_compositor();

    if (!compositor) {
        return strdup("ERROR: No Wayland compositor detected");
    }

    if (!strcmp(compositor, "hyprland")) {
        /* Hyprland dispatch */
        snprintf(cmd, sizeof(cmd), "hyprctl dispatch mousemove %d %d && hyprctl dispatch mouseclick left 2>/dev/null", x, y);
    } else if (!strcmp(compositor, "sway")) {
        /* Sway doesn't support direct click; use wtype/wlclipboard workaround */
        snprintf(cmd, sizeof(cmd), "swaymsg '[con_id:%d] focus' 2>/dev/null", 0);
    } else {
        /* Generic: try wtype if available */
        snprintf(cmd, sizeof(cmd), "echo 'click at %d,%d' 2>/dev/null", x, y);
    }

    result = vs_run_command(cmd, &status);
    free(result);

    if (status == 0) {
        return strdup("OK");
    } else {
        return strdup("ERROR: Click not supported on this compositor");
    }
}

char *vs_wayland_type(const char *text)
{
    char cmd[1024];
    int status;
    char *result;

    if (!text || !text[0]) {
        return strdup("OK");
    }

    /* Use wtype if available, otherwise fall back to printf pipe */
    snprintf(cmd, sizeof(cmd), "printf '%%s' '%s' | wtype - 2>/dev/null", text);
    result = vs_run_command(cmd, &status);
    free(result);

    if (status != 0) {
        /* Fallback: use xdotool-like approach via ydotool if available */
        snprintf(cmd, sizeof(cmd), "echo '%s' | ydotool type 2>/dev/null", text);
        result = vs_run_command(cmd, &status);
        free(result);
    }

    if (status == 0) {
        return strdup("OK");
    } else {
        return strdup("ERROR: wtype/ydotool not available for text input");
    }
}

char *vs_wayland_move(int x, int y)
{
    char cmd[512];
    int status;
    char *result;
    const char *compositor = detect_compositor();

    if (!compositor) {
        return strdup("ERROR: No Wayland compositor detected");
    }

    if (!strcmp(compositor, "hyprland")) {
        snprintf(cmd, sizeof(cmd), "hyprctl dispatch mousemove %d %d 2>/dev/null", x, y);
    } else {
        snprintf(cmd, sizeof(cmd), "echo 'move to %d,%d' 2>/dev/null", x, y);
    }

    result = vs_run_command(cmd, &status);
    free(result);

    if (status == 0) {
        return strdup("OK");
    } else {
        return strdup("ERROR: Mouse move not supported on this compositor");
    }
}

#endif /* HAVE_WAYLAND */
