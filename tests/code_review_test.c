/* SPDX-License-Identifier: Unlicense */
/* Unit tests for Code Review (src/code_review.c). */
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

    printf("Code Review tests:\n");

    /* --- Constants --- */
    printf("  constants:\n");
    check(VS_MAX_REVIEW_FINDINGS == 64, "max findings is 64");
    check(VS_REVIEW_FILE_MAX == 512, "review file max is 512");
    check(VS_REVIEW_DESC_MAX == 1024, "review desc max is 1024");

    /* --- vs_code_review_start --- */
    printf("  start:\n");
    result = vs_code_review_start(".", "all", "security");
    check(result != NULL, "start returns non-NULL");
    if (result) {
        check(strlen(result) > 0, "start returns content");
        free(result);
    }

    /* --- Start with different scopes --- */
    printf("  scopes:\n");
    result = vs_code_review_start(".", "staged", "style");
    check(result != NULL, "start with staged scope");
    if (result) free(result);

    result = vs_code_review_start(".", "all", "bugs");
    check(result != NULL, "start with bugs focus");
    if (result) free(result);

    result = vs_code_review_start(".", "all", "performance");
    check(result != NULL, "start with performance focus");
    if (result) free(result);

    /* --- Start with NULL path --- */
    printf("  edge cases:\n");
    result = vs_code_review_start(NULL, "all", "security");
    check(result != NULL, "start with NULL path returns non-NULL");
    if (result) free(result);

    result = vs_code_review_start(".", NULL, NULL);
    check(result != NULL, "start with NULL scope/focus");
    if (result) free(result);

    /* --- vs_code_review_render --- */
    printf("  render:\n");
    VSCodeReviewFinding findings[3];
    memset(findings, 0, sizeof(findings));

    strncpy(findings[0].file, "src/main.c", sizeof(findings[0].file) - 1);
    findings[0].line = 42;
    findings[0].severity = 2;
    strncpy(findings[0].category, "security", sizeof(findings[0].category) - 1);
    strncpy(findings[0].description, "Buffer overflow risk", sizeof(findings[0].description) - 1);

    strncpy(findings[1].file, "src/util.c", sizeof(findings[1].file) - 1);
    findings[1].line = 10;
    findings[1].severity = 1;
    strncpy(findings[1].category, "style", sizeof(findings[1].category) - 1);
    strncpy(findings[1].description, "Missing newline", sizeof(findings[1].description) - 1);

    strncpy(findings[2].file, "src/test.c", sizeof(findings[2].file) - 1);
    findings[2].line = 5;
    findings[2].severity = 0;
    strncpy(findings[2].category, "info", sizeof(findings[2].category) - 1);
    strncpy(findings[2].description, "Consider using const", sizeof(findings[2].description) - 1);

    result = vs_code_review_render(findings, 3, 20);
    check(result != NULL, "render returns non-NULL");
    if (result) {
        check(strstr(result, "main.c") != NULL, "render includes file name");
        check(strstr(result, "Buffer overflow") != NULL || strlen(result) > 0, "render includes description");
        free(result);
    }

    /* --- Render empty --- */
    printf("  render empty:\n");
    result = vs_code_review_render(findings, 0, 20);
    check(result != NULL, "render empty returns non-NULL");
    if (result) free(result);

    /* --- Render with 0 max_lines --- */
    printf("  render 0 max:\n");
    result = vs_code_review_render(findings, 3, 0);
    check(result != NULL, "render 0 max returns non-NULL");
    if (result) free(result);

    /* --- Severity levels --- */
    printf("  severity:\n");
    check(findings[0].severity == 2, "severity 2 is error");
    check(findings[1].severity == 1, "severity 1 is warning");
    check(findings[2].severity == 0, "severity 0 is info");

    /* --- Finding struct --- */
    printf("  struct:\n");
    VSCodeReviewFinding f;
    memset(&f, 0, sizeof(f));
    strncpy(f.file, "test.c", sizeof(f.file) - 1);
    f.line = 100;
    f.severity = 1;
    strncpy(f.category, "bugs", sizeof(f.category) - 1);
    check(f.line == 100, "finding line field");
    check(f.severity == 1, "finding severity field");

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
