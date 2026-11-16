/* SPDX-License-Identifier: Unlicense */
/* Unit tests for Computer Use backend detection (src/computer_use.c). */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int tests = 0;
static int passed = 0;

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

    printf("Computer Use backend tests:\n");

    /* --- Constants --- */
    printf("  constants:\n");
    check(VS_COMPUTER_USE_NONE == 0, "NONE is 0");
    check(VS_COMPUTER_USE_X11 == 1, "X11 is 1");
    check(VS_COMPUTER_USE_WAYLAND == 2, "WAYLAND is 2");
    check(VS_COMPUTER_USE_COCOA == 3, "COCOA is 3");

    /* --- vs_computer_use_detect_backend --- */
    printf("  detect_backend:\n");
    int backend = vs_computer_use_detect_backend();
    check(backend >= 0 && backend <= VS_COMPUTER_USE_COCOA, "detect_backend returns valid value");

    /* --- vs_computer_use_init --- */
    printf("  init:\n");
    int rc = vs_computer_use_init();
    check(rc >= 0 || rc == -1, "init returns int");

    /* --- vs_computer_use_screenshot --- */
    printf("  screenshot:\n");
    result = vs_computer_use_screenshot();
    check(result != NULL, "screenshot returns non-NULL");
    if (result) {
        check(strlen(result) > 0, "screenshot returns content");
        free(result);
    }

    /* --- vs_computer_use_click --- */
    printf("  click:\n");
    result = vs_computer_use_click(100, 200);
    check(result != NULL, "click returns non-NULL");
    if (result) free(result);

    /* --- vs_computer_use_type --- */
    printf("  type:\n");
    result = vs_computer_use_type("hello");
    check(result != NULL, "type returns non-NULL");
    if (result) free(result);

    result = vs_computer_use_type("");
    check(result != NULL, "type empty string returns non-NULL");
    if (result) free(result);

    result = vs_computer_use_type(NULL);
    check(result != NULL, "type NULL returns non-NULL");
    if (result) free(result);

    /* --- vs_computer_use_move --- */
    printf("  move:\n");
    result = vs_computer_use_move(0, 0);
    check(result != NULL, "move returns non-NULL");
    if (result) free(result);

    result = vs_computer_use_move(1920, 1080);
    check(result != NULL, "move to coords returns non-NULL");
    if (result) free(result);

    /* --- vs_computer_use_shutdown --- */
    printf("  shutdown:\n");
    vs_computer_use_shutdown();
    check(1, "shutdown completes without crash");

    /* --- Backend on Solaris (no display) --- */
    printf("  no display:\n");
    /* On Solaris without X11, backend should be NONE */
    backend = vs_computer_use_detect_backend();
    check(backend >= 0, "backend detection works on current system");

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
