/* SPDX-License-Identifier: Unlicense */
/* Unit tests for LSP tool: Language Server Protocol (src/lsp.c). */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    VSLSPServer servers[VS_MAX_LSP_SERVERS];
    int count = 0;

    printf("LSP tool tests:\n");

    /* --- Error cases --- */
    printf("  error cases:\n");
    result = vs_lsp_diagnostics(servers, &count, NULL);
    check(result != NULL && strstr(result, "ERROR") != NULL, "NULL path returns error");
    free(result);

    result = vs_lsp_definition(servers, &count, NULL, 1, 1);
    check(result != NULL && strstr(result, "ERROR") != NULL, "NULL path returns error");
    free(result);

    result = vs_lsp_definition(servers, &count, "/tmp/test.c", 0, 1);
    check(result != NULL && strstr(result, "ERROR") != NULL, "invalid line returns error");
    free(result);

    result = vs_lsp_definition(servers, &count, "/tmp/test.c", 1, 0);
    check(result != NULL && strstr(result, "ERROR") != NULL, "invalid col returns error");
    free(result);

    result = vs_lsp_hover(servers, &count, NULL, 1, 1);
    check(result != NULL && strstr(result, "ERROR") != NULL, "hover NULL path returns error");
    free(result);

    result = vs_lsp_rename(servers, &count, "/tmp/test.c", 1, 1, "");
    check(result != NULL && strstr(result, "ERROR") != NULL, "rename empty name returns error");
    free(result);

    result = vs_lsp_rename(servers, &count, "/tmp/test.c", 1, 1, NULL);
    check(result != NULL && strstr(result, "ERROR") != NULL, "rename NULL name returns error");
    free(result);

    /* --- No LSP server configured --- */
    printf("  no LSP server:\n");
    /* Unset any LSP env vars */
    unsetenv("VIBESOLARIS_LSP_SERVER");
    unsetenv("VIBESOLARIS_TYPESCRIPT_LSP");
    unsetenv("VIBESOLARIS_PYTHON_LSP");
    unsetenv("VIBESOLARIS_C_LSP");
    unsetenv("VIBESOLARIS_JAVA_LSP");
    unsetenv("VIBESOLARIS_RUST_LSP");

    memset(servers, 0, sizeof(servers));
    count = 0;

    result = vs_lsp_diagnostics(servers, &count, "/tmp/test.c");
    check(result != NULL, "no LSP returns non-NULL");
    check(strstr(result, "LSP not configured") != NULL, "indicates LSP not configured");
    free(result);

    result = vs_lsp_definition(servers, &count, "/tmp/test.ts", 1, 1);
    check(result != NULL, "no LSP definition returns non-NULL");
    check(strstr(result, "LSP not configured") != NULL, "indicates LSP not configured");
    free(result);

    result = vs_lsp_hover(servers, &count, "/tmp/test.py", 1, 1);
    check(result != NULL, "no LSP hover returns non-NULL");
    check(strstr(result, "LSP not configured") != NULL, "indicates LSP not configured for hover");
    free(result);

    result = vs_lsp_rename(servers, &count, "/tmp/test.c", 1, 1, "newname");
    check(result != NULL, "no LSP rename returns non-NULL");
    check(strstr(result, "LSP not configured") != NULL, "indicates LSP not configured for rename");
    free(result);

    /* --- vs_lsp_find_server with no config --- */
    printf("  find server:\n");
    memset(servers, 0, sizeof(servers));
    count = 0;
    VSLSPServer *srv = vs_lsp_find_server(servers, &count, "/tmp/test.c");
    check(srv == NULL, "find_server returns NULL when no LSP configured");

    /* --- With LSP server configured --- */
    printf("  with LSP server:\n");
    setenv("VIBESOLARIS_LSP_SERVER", "cat", 1);

    memset(servers, 0, sizeof(servers));
    count = 0;

    srv = vs_lsp_find_server(servers, &count, "/tmp/test.c");
    check(srv != NULL, "find_server returns server when configured");
    check(count == 1, "server count incremented");

    /* Spawn the server */
    int spawn_rc = vs_lsp_spawn_server(srv, "cat", NULL);
    check(spawn_rc == 0, "spawn server succeeds");
    check(srv->pid > 0, "server has valid PID");

    /* Initialize will fail with 'cat' but server should be marked errored */
    int init_rc = vs_lsp_initialize(srv);
    /* cat will not respond with valid JSON-RPC, so init should fail */
    check(init_rc != 0 || srv->error, "init fails with non-LSP server or marks error");

    /* Shutdown */
    vs_lsp_shutdown_all(servers, &count);
    check(count == 0, "shutdown resets count");
    check(srv->pid == 0, "shutdown clears PID");

    /* --- Server capacity limit --- */
    printf("  capacity limits:\n");
    setenv("VIBESOLARIS_LSP_SERVER", "cat", 1);
    memset(servers, 0, sizeof(servers));
    count = 0;
    int i;
    /* Fill all slots by spawning servers */
    for(i = 0; i < VS_MAX_LSP_SERVERS; i++){
        VSLSPServer *s = vs_lsp_find_server(servers, &count, "/tmp/test.c");
        check(s != NULL, "find_server returns slot within capacity");
        check(s->pid == 0, "new slot has no PID");
        vs_lsp_spawn_server(s, "cat", NULL);
        check(s->pid > 0, "spawned server has PID");
    }
    check(count == VS_MAX_LSP_SERVERS, "count equals max capacity");
    /* Now all slots are filled — find_server should return NULL (no free slot) */
    VSLSPServer *extra = vs_lsp_find_server(servers, &count, "/tmp/test.c");
    check(extra == NULL, "find_server returns NULL when all slots have spawned servers");
    check(count == VS_MAX_LSP_SERVERS, "count does not exceed max capacity");
    /* Clean up */
    vs_lsp_shutdown_all(servers, &count);

    /* --- Language-specific LSP detection --- */
    printf("  language-specific LSP:\n");
    unsetenv("VIBESOLARIS_LSP_SERVER");
    unsetenv("VIBESOLARIS_TYPESCRIPT_LSP");
    unsetenv("VIBESOLARIS_PYTHON_LSP");
    unsetenv("VIBESOLARIS_C_LSP");

    setenv("VIBESOLARIS_C_LSP", "clangd", 1);
    memset(servers, 0, sizeof(servers));
    count = 0;
    srv = vs_lsp_find_server(servers, &count, "/tmp/test.c");
    check(srv != NULL, "C LSP detected for .c files");
    vs_lsp_shutdown_all(servers, &count);

    unsetenv("VIBESOLARIS_C_LSP");
    setenv("VIBESOLARIS_TYPESCRIPT_LSP", "typescript-language-server --stdio", 1);
    memset(servers, 0, sizeof(servers));
    count = 0;
    srv = vs_lsp_find_server(servers, &count, "/tmp/test.ts");
    check(srv != NULL, "TypeScript LSP detected for .ts files");
    vs_lsp_shutdown_all(servers, &count);

    unsetenv("VIBESOLARIS_TYPESCRIPT_LSP");
    setenv("VIBESOLARIS_PYTHON_LSP", "pyright-langserver --stdio", 1);
    memset(servers, 0, sizeof(servers));
    count = 0;
    srv = vs_lsp_find_server(servers, &count, "/tmp/test.py");
    check(srv != NULL, "Python LSP detected for .py files");
    vs_lsp_shutdown_all(servers, &count);
    unsetenv("VIBESOLARIS_PYTHON_LSP");

    /* --- Notification functions (no-op when not initialized) --- */
    printf("  notifications:\n");
    memset(servers, 0, sizeof(servers));
    /* These should not crash when server is not initialized */
    vs_lsp_notify_did_open(&servers[0], "/tmp/test.c", "int main() {}");
    check(1, "did_open does not crash on uninitialized server");
    vs_lsp_notify_did_change(&servers[0], "/tmp/test.c", "int main() {}");
    check(1, "did_change does not crash on uninitialized server");
    vs_lsp_notify_did_close(&servers[0], "/tmp/test.c");
    check(1, "did_close does not crash on uninitialized server");

    /* --- Shutdown with NULL --- */
    printf("  shutdown edge cases:\n");
    vs_lsp_shutdown_all(NULL, &count);
    check(1, "shutdown_all with NULL servers does not crash");
    vs_lsp_shutdown_all(servers, NULL);
    check(1, "shutdown_all with NULL count does not crash");

    /* Cleanup */
    unsetenv("VIBESOLARIS_LSP_SERVER");
    unsetenv("VIBESOLARIS_C_LSP");
    unsetenv("VIBESOLARIS_JAVA_LSP");
    unsetenv("VIBESOLARIS_RUST_LSP");

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
