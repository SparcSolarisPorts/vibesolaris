/* SPDX-License-Identifier: Unlicense */
/* Unit tests for Edit tool: targeted file modifications (src/edit.c). */
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

#define TEST_FILE "/tmp/vs_edit_test.txt"

int main(void) {
    char *result;
    char *content;
    int reps;

    printf("Edit tool tests:\n");

    /* --- Error cases --- */
    printf("  errors:\n");

    result = vs_edit_file(NULL, "search", "replace", &reps);
    check(result != NULL && strstr(result, "ERROR") != NULL, "NULL path returns error");
    free(result);

    result = vs_edit_file("", "search", "replace", &reps);
    check(result != NULL && strstr(result, "ERROR") != NULL, "empty path returns error");
    free(result);

    result = vs_edit_file(TEST_FILE, NULL, "replace", &reps);
    check(result != NULL && strstr(result, "ERROR") != NULL, "NULL search returns error");
    free(result);

    result = vs_edit_file(TEST_FILE, "", "replace", &reps);
    check(result != NULL && strstr(result, "ERROR") != NULL, "empty search returns error");
    free(result);

    result = vs_edit_file("/nonexistent/file/path.txt", "search", "replace", &reps);
    check(result != NULL && strstr(result, "ERROR") != NULL, "nonexistent file returns error");
    free(result);

    /* --- Basic single replacement --- */
    printf("  basic replacement:\n");

    write_file(TEST_FILE, "Hello world, how are you?");
    result = vs_edit_file(TEST_FILE, "world", "universe", &reps);
    check(result != NULL && strstr(result, "OK:") != NULL, "basic replace returns OK");
    check(reps == 1, "basic replace count is 1");
    content = read_file(TEST_FILE);
    check(content != NULL && strcmp(content, "Hello universe, how are you?") == 0, "file content replaced");
    free(content);
    free(result);

    /* --- Multiple replacements --- */
    printf("  multiple replacements:\n");

    write_file(TEST_FILE, "foo bar foo baz foo");
    result = vs_edit_file(TEST_FILE, "foo", "qux", &reps);
    check(result != NULL && strstr(result, "OK:") != NULL, "multiple replace returns OK");
    check(reps == 3, "multiple replace count is 3");
    content = read_file(TEST_FILE);
    check(content != NULL && strcmp(content, "qux bar qux baz qux") == 0, "all occurrences replaced");
    free(content);
    free(result);

    /* --- Search not found --- */
    printf("  not found:\n");

    write_file(TEST_FILE, "Hello world");
    result = vs_edit_file(TEST_FILE, "xyz_not_found", "replace", &reps);
    check(result != NULL && strstr(result, "ERROR") != NULL, "not found returns error");
    check(strstr(result, "not found") != NULL, "error mentions not found");
    free(result);

    /* Verify file unchanged */
    content = read_file(TEST_FILE);
    check(content != NULL && strcmp(content, "Hello world") == 0, "file unchanged on not found");
    free(content);

    /* --- Replace with empty string (deletion) --- */
    printf("  delete text:\n");

    write_file(TEST_FILE, "aaa bbb ccc");
    result = vs_edit_file(TEST_FILE, " bbb", "", &reps);
    check(result != NULL && strstr(result, "OK:") != NULL, "delete returns OK");
    content = read_file(TEST_FILE);
    check(content != NULL && strcmp(content, "aaa ccc") == 0, "text deleted");
    free(content);
    free(result);

    /* --- Replace with NULL (treated as empty) --- */
    printf("  null replace:\n");

    write_file(TEST_FILE, "keep this remove_me end");
    result = vs_edit_file(TEST_FILE, " remove_me", NULL, &reps);
    check(result != NULL && strstr(result, "OK:") != NULL, "NULL replace returns OK");
    content = read_file(TEST_FILE);
    check(content != NULL && strcmp(content, "keep this end") == 0, "NULL replace deletes text");
    free(content);
    free(result);

    /* --- Multi-line replacement --- */
    printf("  multiline:\n");

    write_file(TEST_FILE, "line1\nline2\nline3\n");
    result = vs_edit_file(TEST_FILE, "line2", "replaced", &reps);
    check(result != NULL && strstr(result, "OK:") != NULL, "multiline replace OK");
    content = read_file(TEST_FILE);
    check(content != NULL && strcmp(content, "line1\nreplaced\nline3\n") == 0, "multiline content correct");
    free(content);
    free(result);

    /* --- Replace entire file content --- */
    printf("  full content replace:\n");

    write_file(TEST_FILE, "old content");
    result = vs_edit_file(TEST_FILE, "old content", "new content here", &reps);
    check(result != NULL && strstr(result, "OK:") != NULL, "full content replace OK");
    content = read_file(TEST_FILE);
    check(content != NULL && strcmp(content, "new content here") == 0, "full content replaced");
    free(content);
    free(result);

    /* --- Special characters in search/replace --- */
    printf("  special characters:\n");

    write_file(TEST_FILE, "int x = 5; // old comment");
    result = vs_edit_file(TEST_FILE, "// old comment", "// new comment", &reps);
    check(result != NULL && strstr(result, "OK:") != NULL, "special chars replace OK");
    content = read_file(TEST_FILE);
    check(content != NULL && strcmp(content, "int x = 5; // new comment") == 0, "special chars content correct");
    free(content);
    free(result);

    /* --- Overwrite and cleanup --- */
    unlink(TEST_FILE);

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
