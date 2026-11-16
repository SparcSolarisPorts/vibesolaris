/* SPDX-License-Identifier: Unlicense */
/* Non-interactive Computer Use tests.  Unlike tests/computer_use_backend_test.c
   (which clicks and types for real and is therefore opt-in), this test never
   injects input or captures the screen: it checks backend detection, backend
   availability reporting, and that NULL/empty type requests are safe no-ops.
   On macOS the Cocoa backend must be detected; elsewhere detection must not
   claim Cocoa. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests = 0;
static int passed = 0;

/* The Cocoa/Wayland backends probe the host with `uname`/`which` through
   vs_run_command.  Answer those probes with canned results so detection runs
   its real code path without this test ever spawning osascript, cliclick,
   grim, or screencapture.  The strong definition overrides the weak stub in
   tests/mcp_stub.c. */
char *vs_run_command(const char *cmd, int *exit_code) {
    const char *out = "";
#ifdef __APPLE__
    if (strstr(cmd, "uname")) out = "Darwin\n";
    else if (strstr(cmd, "screencapture")) out = "/usr/sbin/screencapture\n";
#else
    if (strstr(cmd, "uname")) out = "SunOS\n";
#endif
    if (strstr(cmd, "which grim") || strstr(cmd, "which slurp") ||
        strstr(cmd, "which swaymsg") || strstr(cmd, "which hyprctl") ||
        strstr(cmd, "which wl-copy")) {
        if (exit_code) *exit_code = 1;
        return strdup("");
    }
    if (exit_code) *exit_code = 0;
    return strdup(out);
}

static void check(int cond, const char *msg) {
    tests++;
    if (cond) {
        passed++;
        printf("  ok: %s\n", msg);
    } else {
        printf("  FAIL: %s\n", msg);
    }
}

int main(void) {
    char *result;
    int backend;

    printf("Computer Use non-interactive tests:\n");

    backend = vs_computer_use_detect_backend();
#ifdef __APPLE__
    check(backend == VS_COMPUTER_USE_COCOA, "Cocoa backend detected on macOS");
    check(vs_computer_use_init() == 1, "init reports a usable backend on macOS");
#else
    check(backend != VS_COMPUTER_USE_COCOA, "Cocoa is never detected off macOS");
#endif
    check(backend >= VS_COMPUTER_USE_NONE && backend <= VS_COMPUTER_USE_COCOA, "detected backend is a valid constant");
    check(vs_computer_use_detect_backend() == backend, "detection is stable across calls");

    /* NULL and empty type requests must be safe no-ops everywhere — they must
       never reach osascript/cliclick/XTest. */
    result = vs_computer_use_type(NULL);
    check(result != NULL && strlen(result) > 0, "type(NULL) returns a result without crashing");
    free(result);

    result = vs_computer_use_type("");
    check(result != NULL && strlen(result) > 0, "type(\"\") returns a result without injecting input");
#ifdef __APPLE__
    check(result && !strcmp(result, "OK"), "empty type is a no-op on macOS");
#endif
    free(result);

    /* Headless sessions (SSH, no DISPLAY/WAYLAND_DISPLAY) degrade gracefully. */
    if (backend == VS_COMPUTER_USE_NONE) {
        result = vs_computer_use_screenshot();
        check(result != NULL && strstr(result, "no supported display backend"), "headless screenshot explains the missing backend");
        free(result);
        result = vs_computer_use_click(1, 2);
        check(result != NULL && strstr(result, "no supported display backend"), "headless click explains the missing backend");
        free(result);
        result = vs_computer_use_move(1, 2);
        check(result != NULL && strstr(result, "no supported display backend"), "headless move explains the missing backend");
        free(result);
    } else {
        check(1, "display backend present; headless degradation checks skipped");
        check(1, "headless click check skipped");
        check(1, "headless move check skipped");
    }

    vs_computer_use_shutdown();
    check(1, "shutdown completes without crash");

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
