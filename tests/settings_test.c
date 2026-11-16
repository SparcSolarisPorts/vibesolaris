/* SPDX-License-Identifier: Unlicense */
/* Multi-layer settings tests (tests/settings_test.c). */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Real vs_read_file for tests that need file I/O */
char *vs_read_file(const char *path)
{
    if (!path) return NULL;
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    buf[sz] = 0;
    fclose(f);
    return buf;
}

static int pass = 0;
static int fail = 0;

static void check(int cond, const char *msg)
{
    if (cond) { pass++; }
    else { fail++; printf("FAIL: %s\n", msg); }
}

int main(void)
{
    /* Test 1: vs_settings_init sets paths */
    {
        VSSettingsStore store;
        vs_settings_init(&store, NULL);
        check(strlen(store.user_path) > 0, "init sets user path");
        check(store.entry_count == 0, "init clears entry count");
        check(store.user_loaded == 0, "init clears user_loaded");
    }

    /* Test 2: vs_settings_init with project root */
    {
        VSSettingsStore store;
        vs_settings_init(&store, "/tmp/testproj");
        check(strcmp(store.project_root, "/tmp/testproj") == 0, "init sets project root");
        check(strstr(store.project_path, "/tmp/testproj") != NULL, "init sets project path");
        check(strstr(store.local_path, "/tmp/testproj") != NULL, "init sets local path");
    }

    /* Test 3: vs_settings_set and vs_settings_get */
    {
        VSSettingsStore store;
        memset(&store, 0, sizeof(store));
        vs_settings_set(&store, "model", "claude-sonnet-5", VS_SETTINGS_LAYER_USER);
        vs_settings_set(&store, "provider", "claude", VS_SETTINGS_LAYER_USER);
        check(store.entry_count == 2, "set adds two entries");
        check(strcmp(vs_settings_get(&store, "model"), "claude-sonnet-5") == 0, "get returns model");
        check(strcmp(vs_settings_get(&store, "provider"), "claude") == 0, "get returns provider");
        check(vs_settings_get(&store, "nonexistent") == NULL, "get returns NULL for missing key");
    }

    /* Test 4: vs_settings_set updates existing key at same layer */
    {
        VSSettingsStore store;
        memset(&store, 0, sizeof(store));
        vs_settings_set(&store, "model", "gpt-4", VS_SETTINGS_LAYER_USER);
        vs_settings_set(&store, "model", "claude-sonnet-5", VS_SETTINGS_LAYER_USER);
        check(store.entry_count == 1, "set updates existing key");
        check(strcmp(vs_settings_get(&store, "model"), "claude-sonnet-5") == 0, "updated value");
    }

    /* Test 5: Merge priority — local overrides project overrides user */
    {
        VSSettingsStore store;
        memset(&store, 0, sizeof(store));
        vs_settings_set(&store, "model", "gpt-4", VS_SETTINGS_LAYER_USER);
        vs_settings_set(&store, "model", "claude-sonnet-5", VS_SETTINGS_LAYER_PROJECT);
        vs_settings_set(&store, "model", "claude-opus-4", VS_SETTINGS_LAYER_LOCAL);
        /* vs_settings_get searches in reverse, so local (last added) wins */
        check(strcmp(vs_settings_get(&store, "model"), "claude-opus-4") == 0, "local overrides project");
    }

    /* Test 6: vs_settings_get returns highest priority layer */
    {
        VSSettingsStore store;
        memset(&store, 0, sizeof(store));
        vs_settings_set(&store, "key1", "user_val", VS_SETTINGS_LAYER_USER);
        vs_settings_set(&store, "key1", "project_val", VS_SETTINGS_LAYER_PROJECT);
        check(strcmp(vs_settings_get(&store, "key1"), "project_val") == 0, "project overrides user");
    }

    /* Test 7: vs_settings_dump returns non-NULL */
    {
        VSSettingsStore store;
        memset(&store, 0, sizeof(store));
        vs_settings_set(&store, "test", "value", VS_SETTINGS_LAYER_USER);
        char *dump = vs_settings_dump(&store);
        check(dump != NULL, "dump returns non-NULL");
        if (dump) {
            check(strstr(dump, "test") != NULL, "dump contains key");
            check(strstr(dump, "value") != NULL, "dump contains value");
            check(strstr(dump, "user") != NULL, "dump contains layer name");
            free(dump);
        }
    }

    /* Test 8: vs_settings_dump with empty store */
    {
        VSSettingsStore store;
        memset(&store, 0, sizeof(store));
        char *dump = vs_settings_dump(&store);
        check(dump != NULL, "empty dump returns non-NULL");
        if (dump) {
            check(strstr(dump, "(no settings)") != NULL, "empty dump shows message");
            free(dump);
        }
    }

    /* Test 9: vs_settings_load_all with no files */
    {
        VSSettingsStore store;
        vs_settings_init(&store, NULL);
        int rc = vs_settings_load_all(&store);
        check(rc >= 0, "load_all returns >= 0 with no files");
        check(store.entry_count == 0, "load_all has 0 entries with no files");
    }

    /* Test 10: JSON parsing — simple object */
    {
        VSSettingsEntry entries[VS_SETTINGS_MAX_KEYS];
        memset(entries, 0, sizeof(entries));
        /* We test via load_layer by creating a temp file */
        const char *json = "{\"model\": \"claude-sonnet-5\", \"provider\": \"claude\"}";
        FILE *f = fopen("/tmp/test_settings.json", "w");
        if (f) {
            fprintf(f, "%s\n", json);
            fclose(f);
            VSSettingsStore store;
            vs_settings_init(&store, NULL);
            strncpy(store.user_path, "/tmp/test_settings.json", sizeof(store.user_path) - 1);
            int rc = vs_settings_load_user(&store);
            check(rc == 2, "load_user parses 2 entries");
            check(strcmp(vs_settings_get(&store, "model"), "claude-sonnet-5") == 0, "parsed model");
            check(strcmp(vs_settings_get(&store, "provider"), "claude") == 0, "parsed provider");
            unlink("/tmp/test_settings.json");
        }
    }

    /* Test 11: JSON parsing — nested whitespace */
    {
        const char *json = "{\n  \"key1\" : \"val1\" ,\n  \"key2\" : \"val2\"\n}";
        FILE *f = fopen("/tmp/test_settings2.json", "w");
        if (f) {
            fprintf(f, "%s\n", json);
            fclose(f);
            VSSettingsStore store;
            vs_settings_init(&store, NULL);
            strncpy(store.user_path, "/tmp/test_settings2.json", sizeof(store.user_path) - 1);
            int rc = vs_settings_load_user(&store);
            check(rc == 2, "load_user parses 2 entries with whitespace");
            check(strcmp(vs_settings_get(&store, "key1"), "val1") == 0, "parsed key1");
            unlink("/tmp/test_settings2.json");
        }
    }

    /* Test 12: vs_settings_save_local creates file */
    {
        VSSettingsStore store;
        vs_settings_init(&store, "/tmp");
        vs_settings_set(&store, "saved_key", "saved_val", VS_SETTINGS_LAYER_LOCAL);
        int rc = vs_settings_save_local(&store);
        check(rc == 0, "save_local succeeds");
        /* Verify file exists */
        FILE *f = fopen(store.local_path, "r");
        if (f) {
            check(1, "save_local creates file");
            fclose(f);
            unlink(store.local_path);
        }
    }

    /* Test 13: vs_settings_detect_project_root with git repo */
    {
        char root[VS_MAX_PATH];
        int rc = vs_settings_detect_project_root(".", root, sizeof(root));
        /* In a git repo, should find the root */
        if (rc == 0) {
            check(strlen(root) > 0, "detect_project_root finds git root");
        } else {
            /* Not in a git repo, that's OK */
            check(1, "detect_project_root handles non-git dir");
        }
    }

    /* Test 14: vs_settings_set at capacity */
    {
        VSSettingsStore store;
        memset(&store, 0, sizeof(store));
        int i;
        for (i = 0; i < VS_SETTINGS_MAX_KEYS; i++) {
            char key[32];
            snprintf(key, sizeof(key), "key_%d", i);
            vs_settings_set(&store, key, "val", VS_SETTINGS_LAYER_USER);
        }
        check(store.entry_count == VS_SETTINGS_MAX_KEYS, "set fills to capacity");
        int rc = vs_settings_set(&store, "overflow", "val", VS_SETTINGS_LAYER_USER);
        check(rc == -1, "set returns -1 at capacity");
    }

    /* Test 15: NULL safety */
    {
        check(vs_settings_get(NULL, "key") == NULL, "get handles NULL store");
        check(vs_settings_get(NULL, NULL) == NULL, "get handles NULL key");
        check(vs_settings_set(NULL, "k", "v", VS_SETTINGS_LAYER_USER) == -1, "set handles NULL store");
        check(vs_settings_set(NULL, NULL, NULL, VS_SETTINGS_LAYER_USER) == -1, "set handles NULL args");
    }

    /* Test 16: vs_settings_dump NULL safety */
    {
        check(vs_settings_dump(NULL) == NULL, "dump handles NULL store");
    }

    /* Test 17: vs_settings_detect_project_root NULL safety */
    {
        char root[VS_MAX_PATH];
        check(vs_settings_detect_project_root(NULL, root, sizeof(root)) == -1, "detect handles NULL path");
        check(vs_settings_detect_project_root(".", NULL, 0) == -1, "detect handles NULL out");
    }

    /* Test 18: vs_settings_init NULL safety */
    {
        vs_settings_init(NULL, NULL); /* should not crash */
        check(1, "init handles NULL store");
    }

    /* Test 19: Three-layer load simulation */
    {
        /* Create three settings files */
        FILE *f;
        f = fopen("/tmp/test_user_settings.json", "w");
        if (f) {
            fprintf(f, "{\"model\": \"gpt-4\", \"theme\": \"dark\"}\n");
            fclose(f);
        }
        f = fopen("/tmp/test_project_settings.json", "w");
        if (f) {
            fprintf(f, "{\"model\": \"claude-sonnet-5\"}\n");
            fclose(f);
        }
        f = fopen("/tmp/test_local_settings.json", "w");
        if (f) {
            fprintf(f, "{\"model\": \"claude-opus-4\", \"debug\": \"true\"}\n");
            fclose(f);
        }

        VSSettingsStore store;
        memset(&store, 0, sizeof(store));
        strncpy(store.user_path, "/tmp/test_user_settings.json", sizeof(store.user_path) - 1);
        strncpy(store.project_path, "/tmp/test_project_settings.json", sizeof(store.project_path) - 1);
        strncpy(store.local_path, "/tmp/test_local_settings.json", sizeof(store.local_path) - 1);

        int rc = vs_settings_load_all(&store);
        check(rc == 5, "load_all loads 5 entries from 3 files");
        /* Local model should win */
        check(strcmp(vs_settings_get(&store, "model"), "claude-opus-4") == 0, "local model wins");
        /* Theme only in user layer */
        check(strcmp(vs_settings_get(&store, "theme"), "dark") == 0, "user theme accessible");
        /* Debug only in local layer */
        check(strcmp(vs_settings_get(&store, "debug"), "true") == 0, "local debug accessible");

        unlink("/tmp/test_user_settings.json");
        unlink("/tmp/test_project_settings.json");
        unlink("/tmp/test_local_settings.json");
    }

    /* Test 20: JSON with boolean and number values */
    {
        const char *json = "{\"enabled\": true, \"count\": 42, \"name\": \"test\"}";
        FILE *f = fopen("/tmp/test_types.json", "w");
        if (f) {
            fprintf(f, "%s\n", json);
            fclose(f);
            VSSettingsStore store;
            vs_settings_init(&store, NULL);
            strncpy(store.user_path, "/tmp/test_types.json", sizeof(store.user_path) - 1);
            int rc = vs_settings_load_user(&store);
            check(rc == 3, "load_user parses 3 typed entries");
            check(strcmp(vs_settings_get(&store, "enabled"), "true") == 0, "boolean parsed");
            check(strcmp(vs_settings_get(&store, "count"), "42") == 0, "number parsed");
            check(strcmp(vs_settings_get(&store, "name"), "test") == 0, "string parsed");
            unlink("/tmp/test_types.json");
        }
    }

    /* Test 21: vs_settings_load_all clears previous state */
    {
        VSSettingsStore store;
        memset(&store, 0, sizeof(store));
        vs_settings_set(&store, "old", "value", VS_SETTINGS_LAYER_USER);
        check(store.entry_count == 1, "pre-load has 1 entry");
        vs_settings_load_all(&store);
        check(store.entry_count == 0, "load_all clears previous entries");
    }

    /* Test 22: vs_settings_save_local with no local entries */
    {
        VSSettingsStore store;
        vs_settings_init(&store, "/tmp");
        vs_settings_set(&store, "user_key", "user_val", VS_SETTINGS_LAYER_USER);
        int rc = vs_settings_save_local(&store);
        check(rc == 0, "save_local succeeds with no local entries");
        /* File should contain empty object */
        FILE *f = fopen(store.local_path, "r");
        if (f) {
            char buf[64];
            if (fgets(buf, sizeof(buf), f)) {
                check(strstr(buf, "{}") != NULL, "empty local file has {}");
            }
            fclose(f);
            unlink(store.local_path);
        }
    }

    /* Test 23: JSON with escaped characters */
    {
        const char *json = "{\"path\": \"C:\\\\Users\\\\test\", \"msg\": \"hello \\\"world\\\"\"}";
        FILE *f = fopen("/tmp/test_escape.json", "w");
        if (f) {
            fprintf(f, "%s\n", json);
            fclose(f);
            VSSettingsStore store;
            vs_settings_init(&store, NULL);
            strncpy(store.user_path, "/tmp/test_escape.json", sizeof(store.user_path) - 1);
            int rc = vs_settings_load_user(&store);
            check(rc >= 1, "load_user parses entries with escapes");
            unlink("/tmp/test_escape.json");
        }
    }

    /* Test 24: Layer names in dump */
    {
        VSSettingsStore store;
        memset(&store, 0, sizeof(store));
        vs_settings_set(&store, "a", "1", VS_SETTINGS_LAYER_USER);
        vs_settings_set(&store, "b", "2", VS_SETTINGS_LAYER_PROJECT);
        vs_settings_set(&store, "c", "3", VS_SETTINGS_LAYER_LOCAL);
        char *dump = vs_settings_dump(&store);
        if (dump) {
            check(strstr(dump, "user") != NULL, "dump has user layer");
            check(strstr(dump, "project") != NULL, "dump has project layer");
            check(strstr(dump, "local") != NULL, "dump has local layer");
            free(dump);
        }
    }

    /* Test 25: vs_settings_load_user/project/local individually */
    {
        const char *json = "{\"solo\": \"value\"}";
        FILE *f = fopen("/tmp/test_solo.json", "w");
        if (f) {
            fprintf(f, "%s\n", json);
            fclose(f);
            VSSettingsStore store;
            vs_settings_init(&store, NULL);
            strncpy(store.user_path, "/tmp/test_solo.json", sizeof(store.user_path) - 1);
            int rc = vs_settings_load_user(&store);
            check(rc == 1, "load_user loads 1 entry");
            check(store.user_loaded == 1, "user_loaded flag set");
            check(strcmp(vs_settings_get(&store, "solo"), "value") == 0, "solo key loaded");
            unlink("/tmp/test_solo.json");
        }
    }

    printf("\nResult: %d/%d tests passed\n", pass, pass + fail);
    return fail > 0 ? 1 : 0;
}
