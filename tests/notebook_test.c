/* SPDX-License-Identifier: Unlicense */
/* Unit tests for Notebook tools: Jupyter .ipynb read/edit (src/notebook.c). */
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

#define NB_FILE "/tmp/vs_notebook_test.ipynb"

static void create_notebook(void) {
    FILE *f = fopen(NB_FILE, "w");
    if (f) {
        fprintf(f, "{\n");
        fprintf(f, "  \"nbformat\": 4,\n");
        fprintf(f, "  \"nbformat_minor\": 5,\n");
        fprintf(f, "  \"cells\": [\n");
        fprintf(f, "    {\n");
        fprintf(f, "      \"cell_type\": \"markdown\",\n");
        fprintf(f, "      \"source\": \"# Hello World\"\n");
        fprintf(f, "    },\n");
        fprintf(f, "    {\n");
        fprintf(f, "      \"cell_type\": \"code\",\n");
        fprintf(f, "      \"source\": \"print('hello')\"\n");
        fprintf(f, "    },\n");
        fprintf(f, "    {\n");
        fprintf(f, "      \"cell_type\": \"code\",\n");
        fprintf(f, "      \"source\": \"x = 42\"\n");
        fprintf(f, "    }\n");
        fprintf(f, "  ]\n");
        fprintf(f, "}\n");
        fclose(f);
    }
}

static void cleanup(void) {
    unlink(NB_FILE);
}

int main(void) {
    char *result;

    printf("Notebook tool tests:\n");

    /* --- Error cases --- */
    printf("  error cases:\n");
    result = vs_notebook_read(NULL);
    check(result != NULL && strstr(result, "ERROR") != NULL, "NULL path returns error");
    free(result);

    result = vs_notebook_read("/nonexistent/notebook.ipynb");
    check(result != NULL && strstr(result, "ERROR") != NULL, "nonexistent file returns error");
    free(result);

    /* --- Read notebook --- */
    printf("  read notebook:\n");
    create_notebook();

    result = vs_notebook_read(NB_FILE);
    check(result != NULL, "notebook_read returns non-NULL");
    check(strstr(result, "nbformat") != NULL, "result contains nbformat");
    check(strstr(result, "cells=3") != NULL, "result shows 3 cells");
    check(strstr(result, "markdown") != NULL, "result contains markdown cell");
    check(strstr(result, "code") != NULL, "result contains code cell");
    check(strstr(result, "Hello World") != NULL, "result contains cell content");
    check(strstr(result, "print('hello')") != NULL, "result contains code");
    free(result);

    /* --- Edit notebook --- */
    printf("  edit notebook:\n");
    result = vs_notebook_edit(NB_FILE, 1, "print('modified')");
    check(result != NULL, "notebook_edit returns non-NULL");
    check(strstr(result, "OK") != NULL, "edit returns OK");
    free(result);

    /* Verify edit - the source may be stored as a JSON string */
    result = vs_notebook_read(NB_FILE);
    check(result != NULL, "read after edit returns non-NULL");
    /* The edit replaces the source field; check the raw file for the modification */
    free(result);
    FILE *f = fopen(NB_FILE, "r");
    int found = 0;
    if (f) {
        char buf[4096];
        while (fgets(buf, sizeof(buf), f)) {
            if (strstr(buf, "modified")) { found = 1; break; }
        }
        fclose(f);
    }
    check(found == 1, "edit persisted in file");

    /* --- Edit error cases --- */
    printf("  edit error cases:\n");
    result = vs_notebook_edit(NULL, 0, "test");
    check(result != NULL && strstr(result, "ERROR") != NULL, "NULL path returns error");
    free(result);

    result = vs_notebook_edit(NB_FILE, -1, "test");
    check(result != NULL && strstr(result, "ERROR") != NULL, "negative index returns error");
    free(result);

    result = vs_notebook_edit(NB_FILE, 99, "test");
    check(result != NULL && strstr(result, "ERROR") != NULL, "out of range index returns error");
    free(result);

    result = vs_notebook_edit(NB_FILE, 0, NULL);
    check(result != NULL && strstr(result, "ERROR") != NULL, "NULL source returns error");
    free(result);

    /* --- Non-notebook file --- */
    printf("  non-notebook file:\n");
    { FILE *nf = fopen("/tmp/vs_not_notebook.txt", "w");
    if (nf) { fprintf(nf, "not a notebook\n"); fclose(nf); } }
    result = vs_notebook_read("/tmp/vs_not_notebook.txt");
    check(result != NULL && strstr(result, "ERROR") != NULL, "non-notebook returns error");
    free(result);
    unlink("/tmp/vs_not_notebook.txt");

    cleanup();

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
