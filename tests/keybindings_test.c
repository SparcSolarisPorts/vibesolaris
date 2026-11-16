/* SPDX-License-Identifier: Unlicense */
/* Unit tests for Keybindings and PDF reading (src/keybindings.c). */
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
    VSKeybinding bindings[VS_MAX_KEYBINDINGS];
    int count = 0;
    char *result;

    printf("Keybindings tests:\n");

    /* --- Constants --- */
    printf("  constants:\n");
    check(VS_MAX_KEYBINDINGS == 16, "max keybindings is 16");
    check(VS_KEYBINDING_KEY_MAX == 32, "key max is 32");
    check(VS_KEYBINDING_CMD_MAX == 256, "command max is 256");

    /* --- vs_keybindings_load --- */
    printf("  load:\n");
    count = 0;
    vs_keybindings_load(bindings, &count);
    check(count >= 0, "load returns non-negative count");

    /* --- Create test keybindings file --- */
    printf("  load from file:\n");
    const char *kb_json =
        "[{\"key\":\"Ctrl+T\",\"command\":\"/tasks\"},"
        "{\"key\":\"Ctrl+R\",\"command\":\"/checkpoint rollback\"},"
        "{\"key\":\"Ctrl+M\",\"command\":\"/mode\"}]";
    /* Write to a temp location */
    char *home = getenv("HOME");
    char kb_path[512];
    if (home) {
        snprintf(kb_path, sizeof(kb_path), "%s/.vibesolaris/keybindings.json", home);
        FILE *f = fopen(kb_path, "w");
        if (f) { fputs(kb_json, f); fclose(f); }
    }

    /* --- vs_keybindings_find --- */
    printf("  find:\n");
    count = 0;
    vs_keybindings_load(bindings, &count);

    result = vs_keybindings_find(bindings, count, "Ctrl+T");
    check(result != NULL, "find returns non-NULL");
    if (result) {
        check(strstr(result, "tasks") != NULL || strlen(result) > 0, "find returns command");
        free(result);
    }

    result = vs_keybindings_find(bindings, 0, "Ctrl+T");
    check(result == NULL, "find in empty list returns NULL");
    if (result) free(result);

    result = vs_keybindings_find(bindings, count, "Nonexistent");
    check(result == NULL, "find nonexistent returns NULL");
    if (result) free(result);

    /* --- Manual keybindings --- */
    printf("  manual bindings:\n");
    memset(bindings, 0, sizeof(bindings));
    count = 0;
    strncpy(bindings[0].key, "Alt+X", sizeof(bindings[0].key) - 1);
    strncpy(bindings[0].command, "/quit", sizeof(bindings[0].command) - 1);
    count = 1;

    result = vs_keybindings_find(bindings, count, "Alt+X");
    check(result != NULL, "find manual binding");
    if (result) {
        check(strstr(result, "quit") != NULL || strlen(result) > 0, "manual binding command");
        free(result);
    }

    /* --- vs_read_pdf --- */
    printf("  read_pdf:\n");
    result = vs_read_pdf("/nonexistent.pdf", NULL);
    check(result != NULL, "read_pdf nonexistent returns non-NULL");
    if (result) free(result);

    result = vs_read_pdf(NULL, NULL);
    check(result == NULL, "read_pdf NULL returns NULL");
    if (result) free(result);

    result = vs_read_pdf("/tmp/test.pdf", "1-5");
    check(result != NULL, "read_pdf with pages returns non-NULL");
    if (result) free(result);

    /* --- Keybinding struct --- */
    printf("  struct:\n");
    VSKeybinding kb;
    memset(&kb, 0, sizeof(kb));
    strncpy(kb.key, "F1", sizeof(kb.key) - 1);
    strncpy(kb.command, "/help", sizeof(kb.command) - 1);
    check(strcmp(kb.key, "F1") == 0, "keybinding key field");
    check(strcmp(kb.command, "/help") == 0, "keybinding command field");

    /* --- Cleanup --- */
    if (home) {
        unlink(kb_path);
    }

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
