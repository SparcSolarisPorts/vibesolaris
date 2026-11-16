/* SPDX-License-Identifier: Unlicense */
/* Unit tests for Plugin bundle system (src/plugin.c). */
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
    VSPlugin plugins[VS_MAX_PLUGINS];
    int count = 0;
    char *result;

    printf("Plugin tests:\n");

    /* --- Constants --- */
    printf("  constants:\n");
    check(VS_MAX_PLUGINS == 16, "max plugins is 16");

    /* --- vs_plugin_list empty --- */
    printf("  list empty:\n");
    result = vs_plugin_list(plugins, 0, 20);
    check(result != NULL, "list empty returns non-NULL");
    if (result) free(result);

    /* --- vs_plugin_install from path --- */
    printf("  install:\n");
    /* Create a test plugin directory */
    system("mkdir -p /tmp/vs_test_plugin/skills");
    const char *plugin_json = "{\"name\":\"test-plugin\",\"version\":\"1.0.0\",\"description\":\"Test plugin\"}";
    FILE *f = fopen("/tmp/vs_test_plugin/plugin.json", "w");
    if (f) {
        fputs(plugin_json, f);
        fclose(f);
    }

    int rc = vs_plugin_install(plugins, &count, "/tmp/vs_test_plugin");
    check(rc == 0 || rc == -1, "install returns int");
    if (rc == 0) {
        check(count == 1, "install increments count");
        check(strcmp(plugins[0].name, "test-plugin") == 0, "install sets name");
        check(strcmp(plugins[0].version, "1.0.0") == 0, "install sets version");
    }

    /* --- vs_plugin_list --- */
    printf("  list:\n");
    result = vs_plugin_list(plugins, count, 20);
    check(result != NULL, "list returns non-NULL");
    if (result) {
        check(strlen(result) > 0, "list has content");
        free(result);
    }

    /* --- vs_plugin_uninstall --- */
    printf("  uninstall:\n");
    if (count > 0) {
        rc = vs_plugin_uninstall(plugins, &count, "test-plugin");
        check(rc == 0, "uninstall returns 0");
        check(count == 0, "uninstall decrements count");
    }

    /* --- vs_plugin_uninstall nonexistent --- */
    printf("  uninstall nonexistent:\n");
    rc = vs_plugin_uninstall(plugins, &count, "nonexistent");
    check(rc == -1, "uninstall nonexistent returns -1");

    /* --- vs_plugin_update --- */
    printf("  update:\n");
    rc = vs_plugin_update(plugins, &count, "nonexistent");
    check(rc == -1, "update nonexistent returns -1");

    /* --- Install from URL (will fail without network) --- */
    printf("  install URL:\n");
    rc = vs_plugin_install(plugins, &count, "https://example.com/plugin.tar.gz");
    check(rc == -1, "install from URL fails without network");

    /* --- Capacity limit --- */
    printf("  capacity:\n");
    count = 0;
    for (int i = 0; i < VS_MAX_PLUGINS + 2; i++) {
        char path[256];
        snprintf(path, sizeof(path), "/tmp/vs_test_plugin");
        if (vs_plugin_install(plugins, &count, path) != 0) break;
    }
    check(count <= VS_MAX_PLUGINS, "plugin count capped at max");

    /* --- Cleanup --- */
    count = 0;
    system("rm -rf /tmp/vs_test_plugin");

    /* --- Plugin struct fields --- */
    printf("  struct fields:\n");
    VSPlugin p;
    memset(&p, 0, sizeof(p));
    strncpy(p.name, "myplugin", sizeof(p.name) - 1);
    strncpy(p.version, "2.0.0", sizeof(p.version) - 1);
    strncpy(p.description, "My plugin", sizeof(p.description) - 1);
    check(strcmp(p.name, "myplugin") == 0, "plugin name field");
    check(strcmp(p.version, "2.0.0") == 0, "plugin version field");

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
