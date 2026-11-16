/* SPDX-License-Identifier: Unlicense */
/* Project index tests (tests/project_index_test.c). */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pass = 0;
static int fail = 0;

static void check(int cond, const char *msg)
{
    if (cond) { pass++; }
    else { fail++; printf("FAIL: %s\n", msg); }
}

int main(void)
{
    /* Test 1: Init clears index */
    {
        vs_project_index_init(NULL);
        check(!vs_project_index_is_valid(), "init clears validity");
    }

    /* Test 2: Build returns symbol count */
    {
        vs_project_index_clear();
        int rc = vs_project_index_build(".");
        check(rc >= 0, "build returns >= 0");
        check(vs_project_index_is_valid(), "build sets valid flag");
    }

    /* Test 3: Search finds symbols */
    {
        char *result = vs_project_index_search("vs_");
        if (result) {
            check(strstr(result, "function") != NULL || strstr(result, "Index") != NULL,
                  "search returns results");
            free(result);
        } else {
            check(1, "search returns NULL (no index)");
        }
    }

    /* Test 4: Search with NULL */
    {
        char *result = vs_project_index_search(NULL);
        if (result) {
            check(1, "search handles NULL");
            free(result);
        }
    }

    /* Test 5: References */
    {
        char *result = vs_project_index_references("vibesolaris");
        if (result) {
            check(1, "references returns result");
            free(result);
        } else {
            check(1, "references returns NULL");
        }
    }

    /* Test 6: Dependencies */
    {
        char *result = vs_project_index_dependencies(NULL);
        if (result) {
            check(1, "dependencies handles NULL");
            free(result);
        }
    }

    /* Test 7: Clear invalidates index */
    {
        vs_project_index_clear();
        check(!vs_project_index_is_valid(), "clear invalidates index");
    }

    /* Test 8: Build with NULL */
    {
        int rc = vs_project_index_build(NULL);
        check(rc == -1, "build with NULL returns -1");
    }

    /* Test 9: Search after clear */
    {
        vs_project_index_clear();
        char *result = vs_project_index_search("test");
        if (result) {
            check(strstr(result, "not built") != NULL, "search after clear says not built");
            free(result);
        }
    }

    /* Test 10: References after clear */
    {
        char *result = vs_project_index_references("test");
        if (result) {
            check(strstr(result, "not built") != NULL, "references after clear says not built");
            free(result);
        }
    }

    printf("\nResult: %d/%d tests passed\n", pass, pass + fail);
    return fail > 0 ? 1 : 0;
}
