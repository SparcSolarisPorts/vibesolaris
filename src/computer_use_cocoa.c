/* SPDX-License-Identifier: Unlicense */
/* Cocoa Computer Use — macOS screen capture and input via CoreGraphics/ApplicationServices */
/* Uses external commands (screencapture, osascript) — no library linking required */
/* Compiled only when HAVE_COCOA is defined */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_COCOA

/* Check if running on macOS */
static int is_macos(void)
{
    /* Check for macOS-specific environment */
    int status;
    char *out = vs_run_command("uname -s 2>/dev/null", &status);
    if (out && strstr(out, "Darwin")) {
        free(out);
        return 1;
    }
    free(out);
    return 0;
}

int vs_cocoa_init(void)
{
    if (!is_macos()) {
        return -1;
    }
    /* Check for screencapture */
    int status;
    char *out = vs_run_command("which screencapture 2>/dev/null", &status);
    free(out);
    return status == 0 ? 0 : -1;
}

char *vs_cocoa_screenshot(void)
{
    char cmd[512];
    char *tmpfile;
    char *result;
    int status;

    if (vs_cocoa_init() != 0) {
        return strdup("ERROR: Cocoa screenshot not available (macOS required)");
    }

    /* Create temporary file for screenshot */
    tmpfile = strdup("/tmp/vibesolaris_cocoa_screenshot.png");

    /* Capture screenshot using screencapture */
    snprintf(cmd, sizeof(cmd), "screencapture -x %s 2>/dev/null", tmpfile);
    result = vs_run_command(cmd, &status);
    free(result);

    if (status != 0) {
        free(tmpfile);
        return strdup("ERROR: Failed to capture screenshot (grant screen recording permission)");
    }

    /* Read the PNG file as base64 */
    char read_cmd[600];
    snprintf(read_cmd, sizeof(read_cmd), "base64 -i %s 2>/dev/null", tmpfile);
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

char *vs_cocoa_click(int x, int y)
{
    char cmd[512];
    int status;
    char *result;

    /* Use osascript with AppleScript to move mouse and click */
    snprintf(cmd, sizeof(cmd),
        "osascript -e 'tell application \"System Events\" to tell process \"System UI\" to click at {%d,%d}' 2>/dev/null",
        x, y);

    result = vs_run_command(cmd, &status);
    free(result);

    if (status != 0) {
        /* Fallback: use cliclick if available */
        snprintf(cmd, sizeof(cmd), "cliclick c:%d,%d 2>/dev/null", x, y);
        result = vs_run_command(cmd, &status);
        free(result);
    }

    if (status == 0) {
        return strdup("OK");
    } else {
        return strdup("ERROR: Click requires Accessibility permission or cliclick utility");
    }
}

char *vs_cocoa_type(const char *text)
{
    char cmd[1024];
    int status;
    char *result;

    if (!text || !text[0]) {
        return strdup("OK");
    }

    /* Use osascript with AppleScript to type text */
    snprintf(cmd, sizeof(cmd),
        "osascript -e 'tell application \"System Events\" to keystroke \"%s\"' 2>/dev/null",
        text);

    result = vs_run_command(cmd, &status);
    free(result);

    if (status != 0) {
        /* Fallback: use cliclick if available */
        snprintf(cmd, sizeof(cmd), "cliclick t:'%s' 2>/dev/null", text);
        result = vs_run_command(cmd, &status);
        free(result);
    }

    if (status == 0) {
        return strdup("OK");
    } else {
        return strdup("ERROR: Type requires Accessibility permission or cliclick utility");
    }
}

char *vs_cocoa_move(int x, int y)
{
    char cmd[512];
    int status;
    char *result;

    /* Use osascript with AppleScript to move mouse */
    snprintf(cmd, sizeof(cmd),
        "osascript -e 'tell application \"System Events\" to tell process \"System UI\" to set position of front window to {%d,%d}' 2>/dev/null",
        x, y);

    result = vs_run_command(cmd, &status);
    free(result);

    if (status != 0) {
        /* Fallback: use cliclick if available */
        snprintf(cmd, sizeof(cmd), "cliclick m:%d,%d 2>/dev/null", x, y);
        result = vs_run_command(cmd, &status);
        free(result);
    }

    if (status == 0) {
        return strdup("OK");
    } else {
        return strdup("ERROR: Mouse move requires Accessibility permission or cliclick utility");
    }
}

#endif /* HAVE_COCOA */
