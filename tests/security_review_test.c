/* SPDX-License-Identifier: Unlicense */
/* Unit tests for security review (src/code_review.c security functions). */
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
    VSSecurityFinding findings[VS_MAX_SECURITY_FINDINGS];
    char *result;

    printf("Security Review tests:\n");

    /* --- vs_security_severity_label --- */
    printf("  severity_label:\n");
    check(strcmp(vs_security_severity_label(VS_SEC_SEVERITY_LOW), "LOW") == 0,
          "LOW severity label");
    check(strcmp(vs_security_severity_label(VS_SEC_SEVERITY_MEDIUM), "MEDIUM") == 0,
          "MEDIUM severity label");
    check(strcmp(vs_security_severity_label(VS_SEC_SEVERITY_HIGH), "HIGH") == 0,
          "HIGH severity label");
    check(strcmp(vs_security_severity_label(VS_SEC_SEVERITY_CRITICAL), "CRITICAL") == 0,
          "CRITICAL severity label");

    /* --- vs_security_review_start with NULL --- */
    printf("  null safety:\n");
    int count = vs_security_review_start(NULL, "all", findings, 10);
    check(count == 0, "start with NULL path returns 0");

    count = vs_security_review_start(".", "all", NULL, 10);
    check(count == 0, "start with NULL findings returns 0");

    count = vs_security_review_start(".", "all", findings, 0);
    check(count == 0, "start with max_findings=0 returns 0");

    /* --- vs_security_review_start with nonexistent path --- */
    printf("  nonexistent path:\n");
    count = vs_security_review_start("/nonexistent/path/xyz", "all", findings, 10);
    check(count == 0, "nonexistent path returns 0 findings");

    /* --- vs_security_review_start with current dir --- */
    printf("  scan current dir:\n");
    count = vs_security_review_start(".", "all", findings, VS_MAX_SECURITY_FINDINGS);
    check(count >= 0, "scan returns non-negative count");
    /* The project likely has some patterns (system(), popen(), etc.) */
    if (count > 0) {
        check(findings[0].line > 0, "finding has line number");
        check(strlen(findings[0].file) > 0, "finding has file path");
        check(strlen(findings[0].cwe) > 0, "finding has CWE ID");
        check(strlen(findings[0].category) > 0, "finding has category");
        check(strlen(findings[0].description) > 0, "finding has description");
        check(findings[0].severity >= VS_SEC_SEVERITY_LOW &&
              findings[0].severity <= VS_SEC_SEVERITY_CRITICAL,
              "finding has valid severity");
    }

    /* --- vs_security_review_render --- */
    printf("  render:\n");
    result = vs_security_review_render(findings, count, 20);
    check(result != NULL, "render returns non-NULL");
    if (result) {
        check(strstr(result, "Security Review") != NULL, "render has title");
        if (count > 0) {
            check(strstr(result, findings[0].cwe) != NULL, "render shows CWE");
        }
        free(result);
    }

    /* Render with NULL findings */
    result = vs_security_review_render(NULL, 0, 10);
    check(result != NULL, "render with NULL returns non-NULL");
    if (result) free(result);

    /* Render with 0 count */
    result = vs_security_review_render(findings, 0, 10);
    check(result != NULL, "render with 0 count returns non-NULL");
    if (result) {
        check(strstr(result, "No security issues") != NULL,
              "0 findings shows clean message");
        free(result);
    }

    /* --- Manual finding construction --- */
    printf("  manual findings:\n");
    memset(findings, 0, sizeof(findings));
    snprintf(findings[0].file, sizeof(findings[0].file), "test.c");
    findings[0].line = 42;
    findings[0].severity = VS_SEC_SEVERITY_CRITICAL;
    snprintf(findings[0].cwe, sizeof(findings[0].cwe), "CWE-89");
    snprintf(findings[0].category, sizeof(findings[0].category), "sql_injection");
    snprintf(findings[0].description, sizeof(findings[0].description),
             "SQL query built with sprintf");

    snprintf(findings[1].file, sizeof(findings[1].file), "auth.c");
    findings[1].line = 10;
    findings[1].severity = VS_SEC_SEVERITY_HIGH;
    snprintf(findings[1].cwe, sizeof(findings[1].cwe), "CWE-798");
    snprintf(findings[1].category, sizeof(findings[1].category), "hardcoded_secret");
    snprintf(findings[1].description, sizeof(findings[1].description),
             "Possible hardcoded password");

    result = vs_security_review_render(findings, 2, 10);
    check(result != NULL, "render manual findings returns non-NULL");
    if (result) {
        check(strstr(result, "Critical: 1") != NULL, "render counts critical");
        check(strstr(result, "High: 1") != NULL, "render counts high");
        check(strstr(result, "CWE-89") != NULL, "render shows CWE-89");
        check(strstr(result, "CWE-798") != NULL, "render shows CWE-798");
        check(strstr(result, "test.c") != NULL, "render shows test.c");
        check(strstr(result, "auth.c") != NULL, "render shows auth.c");
        free(result);
    }

    /* --- Capacity limits --- */
    printf("  capacity limits:\n");
    check(VS_MAX_SECURITY_FINDINGS == 128, "max findings is 128");
    check(VS_SECURITY_CWE_MAX == 16, "CWE ID max is 16");
    check(VS_SECURITY_DESC_MAX == 512, "description max is 512");

    /* --- Severity enum values --- */
    printf("  severity enum:\n");
    check(VS_SEC_SEVERITY_LOW == 0, "LOW is 0");
    check(VS_SEC_SEVERITY_MEDIUM == 1, "MEDIUM is 1");
    check(VS_SEC_SEVERITY_HIGH == 2, "HIGH is 2");
    check(VS_SEC_SEVERITY_CRITICAL == 3, "CRITICAL is 3");

    /* --- Edge cases --- */
    printf("  edge cases:\n");
    result = vs_security_review_render(findings, 2, 0);
    check(result != NULL, "render with 0 max_lines returns non-NULL");
    if (result) free(result);

    result = vs_security_review_render(findings, 2, 1);
    check(result != NULL, "render with 1 max_lines returns non-NULL");
    if (result) free(result);

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
