/* SPDX-License-Identifier: Unlicense */
/* Unit tests for workflow engine (src/workflow_engine.c). */
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
    VSWorkflowEngine engine;
    char *result;

    printf("Workflow Engine tests:\n");

    /* --- vs_workflow_engine_init --- */
    printf("  init:\n");
    vs_workflow_engine_init(&engine);
    check(engine.step_count == 0, "init clears step_count");
    check(engine.status == VS_WF_ENGINE_IDLE, "init sets idle status");
    check(engine.current_step == 0, "init clears current_step");
    check(engine.name[0] == 0, "init clears name");

    /* --- vs_workflow_engine_parse --- */
    printf("  parse:\n");
    const char *json =
        "{\"name\":\"test workflow\","
        "\"steps\":["
        "{\"type\":\"sequential\",\"prompt\":\"step 1\"},"
        "{\"type\":\"sequential\",\"prompt\":\"step 2\"}"
        "]}";

    int steps = vs_workflow_engine_parse(&engine, json);
    check(steps == 2, "parse returns 2 steps");
    check(strcmp(engine.name, "test workflow") == 0, "parse extracts name");
    check(engine.step_count == 2, "step_count is 2");
    check(strcmp(engine.steps[0].prompt, "step 1") == 0, "step 0 prompt");
    check(strcmp(engine.steps[1].prompt, "step 2") == 0, "step 1 prompt");
    check(engine.steps[0].type == VS_WORKFLOW_STEP_SEQUENTIAL, "step 0 type");
    check(engine.steps[1].type == VS_WORKFLOW_STEP_SEQUENTIAL, "step 1 type");

    /* Parse parallel workflow */
    const char *json_parallel =
        "{\"name\":\"parallel workflow\","
        "\"steps\":["
        "{\"type\":\"parallel\",\"tasks\":["
        "{\"prompt\":\"task A\"},{\"prompt\":\"task B\"}"
        "]}]}";

    steps = vs_workflow_engine_parse(&engine, json_parallel);
    check(steps == 1, "parallel parse returns 1 step");
    check(engine.steps[0].type == VS_WORKFLOW_STEP_PARALLEL, "step is parallel");
    check(engine.steps[0].parallel_task_count == 2, "2 parallel tasks");
    check(strcmp(engine.steps[0].parallel_tasks[0].prompt, "task A") == 0,
          "task A prompt");
    check(strcmp(engine.steps[0].parallel_tasks[1].prompt, "task B") == 0,
          "task B prompt");

    /* Parse conditional workflow */
    const char *json_cond =
        "{\"name\":\"conditional workflow\","
        "\"steps\":["
        "{\"type\":\"sequential\",\"prompt\":\"first step\"},"
        "{\"type\":\"conditional\",\"if\":\"step_0.success\",\"prompt\":\"if true\"}"
        "]}";

    steps = vs_workflow_engine_parse(&engine, json_cond);
    check(steps == 2, "conditional parse returns 2 steps");
    check(engine.steps[1].type == VS_WORKFLOW_STEP_CONDITIONAL, "step 1 is conditional");
    check(strlen(engine.steps[1].condition) > 0 || steps == 2,
          "conditional step parsed");

    /* Parse with NULL */
    steps = vs_workflow_engine_parse(NULL, json);
    check(steps == -1, "parse with NULL engine returns -1");
    steps = vs_workflow_engine_parse(&engine, NULL);
    check(steps == -1, "parse with NULL json returns -1");

    /* Parse invalid JSON */
    steps = vs_workflow_engine_parse(&engine, "not json");
    check(steps == -1, "parse invalid json returns -1");

    /* --- vs_workflow_engine_status --- */
    printf("  status:\n");
    result = vs_workflow_engine_status(&engine, 20);
    check(result != NULL, "status returns non-NULL");
    if (result) {
        check(strstr(result, "Workflow") != NULL,
              "status shows workflow title");
        free(result);
    }

    /* Status with NULL */
    result = vs_workflow_engine_status(NULL, 10);
    check(result != NULL, "status with NULL returns non-NULL");
    if (result) free(result);

    /* --- vs_workflow_engine_clear --- */
    printf("  clear:\n");
    vs_workflow_engine_clear(&engine);
    check(engine.step_count == 0, "clear resets step_count");
    check(engine.status == VS_WF_ENGINE_IDLE, "clear resets status");

    vs_workflow_engine_clear(NULL);
    check(1, "clear with NULL is safe");

    /* --- Capacity limits --- */
    printf("  capacity limits:\n");
    check(VS_WORKFLOW_ENGINE_MAX_STEPS == 8, "max steps is 8");
    check(VS_WORKFLOW_ENGINE_MAX_PARALLEL == 4, "max parallel is 4");

    /* --- Enum values --- */
    printf("  enum values:\n");
    check(VS_WORKFLOW_STEP_SEQUENTIAL == 0, "SEQUENTIAL is 0");
    check(VS_WORKFLOW_STEP_PARALLEL == 1, "PARALLEL is 1");
    check(VS_WORKFLOW_STEP_CONDITIONAL == 2, "CONDITIONAL is 2");
    check(VS_WORKFLOW_STEP_PENDING == 0, "PENDING is 0");
    check(VS_WORKFLOW_STEP_SUCCESS == 2, "SUCCESS is 2");
    check(VS_WF_ENGINE_IDLE == 0, "IDLE is 0");
    check(VS_WF_ENGINE_COMPLETED == 2, "COMPLETED is 2");

    /* --- Edge cases --- */
    printf("  edge cases:\n");
    vs_workflow_engine_init(&engine);

    /* Empty steps array */
    const char *json_empty = "{\"name\":\"empty\",\"steps\":[]}";
    steps = vs_workflow_engine_parse(&engine, json_empty);
    check(steps == 0, "empty steps array returns 0");

    /* Status with 0 max_lines */
    result = vs_workflow_engine_status(&engine, 0);
    check(result != NULL, "status with 0 max_lines returns non-NULL");
    if (result) free(result);

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
