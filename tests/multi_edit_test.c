/* SPDX-License-Identifier: Unlicense */
/* Unit tests for MultiEdit tool: multi-hunk file editing (src/multi_edit.c). */
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

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = 0;
    fclose(f);
    return buf;
}

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%s", content); fclose(f); }
}

#define TEST_FILE "/tmp/vs_multi_edit_test.txt"

int main(void) {
    char *result;
    char *content;
    int total;
    const char *searches[VS_MAX_MULTI_EDIT_PAIRS];
    const char *replaces[VS_MAX_MULTI_EDIT_PAIRS];

    printf("MultiEdit tool tests:\n");

    /* --- Error cases --- */
    printf("  error cases:\n");
    result = vs_multi_edit(NULL, NULL, NULL, 0, &total);
    check(result != NULL && strstr(result, "ERROR") != NULL, "NULL params returns error");
    free(result);

    result = vs_multi_edit("/nonexistent/file.txt", NULL, NULL, 0, &total);
    check(result != NULL && strstr(result, "ERROR") != NULL, "nonexistent file returns error");
    free(result);

    /* --- Single edit pair --- */
    printf("  single edit pair:\n");
    write_file(TEST_FILE, "hello world hello");
    searches[0] = "hello";
    replaces[0] = "hi";
    result = vs_multi_edit(TEST_FILE, searches, replaces, 1, &total);
    check(result != NULL, "single edit returns non-NULL");
    check(strstr(result, "OK") != NULL, "single edit returns OK");
    check(total == 2, "single edit pair found 2 replacements");
    free(result);
    content = read_file(TEST_FILE);
    check(content != NULL && strcmp(content, "hi world hi") == 0, "file content updated");
    free(content);

    /* --- Multiple edit pairs --- */
    printf("  multiple edit pairs:\n");
    write_file(TEST_FILE, "foo bar baz foo bar");
    searches[0] = "foo";
    replaces[0] = "FOO";
    searches[1] = "bar";
    replaces[1] = "BAR";
    searches[2] = "baz";
    replaces[2] = "BAZ";
    result = vs_multi_edit(TEST_FILE, searches, replaces, 3, &total);
    check(result != NULL, "multi edit returns non-NULL");
    check(strstr(result, "OK") != NULL, "multi edit returns OK");
    check(total == 5, "multi edit found 5 replacements");
    free(result);
    content = read_file(TEST_FILE);
    check(content != NULL && strcmp(content, "FOO BAR BAZ FOO BAR") == 0, "all edits applied");
    free(content);

    /* --- Empty replace (delete) --- */
    printf("  delete text:\n");
    write_file(TEST_FILE, "abc def ghi");
    searches[0] = "def ";
    replaces[0] = "";
    result = vs_multi_edit(TEST_FILE, searches, replaces, 1, &total);
    check(result != NULL, "delete edit returns non-NULL");
    check(total == 1, "delete found 1 match");
    free(result);
    content = read_file(TEST_FILE);
    check(content != NULL && strcmp(content, "abc ghi") == 0, "text deleted");
    free(content);

    /* --- Search not found --- */
    printf("  search not found:\n");
    write_file(TEST_FILE, "hello world");
    searches[0] = "nonexistent";
    replaces[0] = "replacement";
    result = vs_multi_edit(TEST_FILE, searches, replaces, 1, &total);
    check(result != NULL, "not found edit returns non-NULL");
    check(total == 0, "not found has 0 replacements");
    free(result);
    content = read_file(TEST_FILE);
    check(content != NULL && strcmp(content, "hello world") == 0, "file unchanged");
    free(content);

    /* --- Mixed: some found, some not --- */
    printf("  mixed results:\n");
    write_file(TEST_FILE, "alpha beta gamma");
    searches[0] = "alpha";
    replaces[0] = "ALPHA";
    searches[1] = "nonexistent";
    replaces[1] = "REPLACEMENT";
    searches[2] = "gamma";
    replaces[2] = "GAMMA";
    result = vs_multi_edit(TEST_FILE, searches, replaces, 3, &total);
    check(result != NULL, "mixed edit returns non-NULL");
    check(total == 2, "mixed edit found 2 replacements");
    free(result);
    content = read_file(TEST_FILE);
    check(content != NULL && strcmp(content, "ALPHA beta GAMMA") == 0, "mixed edits correct");
    free(content);

    /* Empty input and replacements that grow across successive passes. */
    write_file(TEST_FILE, "");
    searches[0] = "x"; replaces[0] = "";
    result = vs_multi_edit(TEST_FILE, searches, replaces, 1, &total);
    check(result && strstr(result, "OK") && total == 0, "empty file handled");
    free(result);
    write_file(TEST_FILE, "x");
    searches[0] = "x"; replaces[0] = "abcdefghijklmnopqrstuvwx";
    searches[1] = "a"; replaces[1] = "012345678901234567890123456789";
    result = vs_multi_edit(TEST_FILE, searches, replaces, 2, &total);
    content = read_file(TEST_FILE);
    check(result && strstr(result, "OK") && total == 2 && content && strcmp(content, "012345678901234567890123456789bcdefghijklmnopqrstuvwx") == 0, "successive growing replacements");
    free(result); free(content);

    unlink(TEST_FILE);

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
