/* SPDX-License-Identifier: Unlicense */
/* VibeSolaris Workflow Engine — JSON-based scriptable subagent orchestration */
/*
 * Parses a JSON workflow file defining steps (sequential, parallel, conditional).
 * Executes steps in order, spawning subagents for parallel steps via the
 * coordinator. Supports conditional steps based on previous step results.
 *
 * Workflow JSON format:
 * {
 *   "name": "my workflow",
 *   "steps": [
 *     {"type": "sequential", "prompt": "do something"},
 *     {"type": "parallel", "tasks": [{"prompt": "task1"}, {"prompt": "task2"}]},
 *     {"type": "conditional", "if": "step_0.success", "then": {"prompt": "if true"}}
 *   ]
 * }
 */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VS_WORKFLOW_MAX_STEPS 32
#define VS_WORKFLOW_MAX_PARALLEL 8
#define VS_WORKFLOW_PROMPT_MAX 2048

/* ---- Internal helpers ---- */

static char *wf_dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* Simple JSON string extraction: find "key":"value" and return value + end pointer */
static const char *wf_json_string(const char *json, const char *key, const char **out_end)
{
    char search[256];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *pos = strstr(json, search);
    if (!pos) return NULL;
    pos += strlen(search);
    while (*pos == ' ' || *pos == '\t' || *pos == '\n') pos++;
    if (*pos != '"') return NULL;
    pos++;
    const char *end = strchr(pos, '"');
    if (!end) return NULL;
    /* Skip escaped quotes */
    while (end[1] == '\\' && end[2]) end += 2;
    if (end[0] != '"') return NULL;
    if (out_end) *out_end = end;
    return pos;
}

/* Find a JSON array: "key":[...], return pointer to '[' and end ']' */
static int wf_json_array(const char *json, const char *key,
                         const char **start, const char **end)
{
    char search[256];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *pos = strstr(json, search);
    if (!pos) return 0;
    pos += strlen(search);
    while (*pos == ' ' || *pos == '\t' || *pos == '\n') pos++;
    if (*pos != '[') return 0;
    *start = pos + 1;
    /* Find matching ']' */
    int depth = 1;
    const char *p = pos + 1;
    while (*p && depth > 0) {
        if (*p == '[') depth++;
        else if (*p == ']') depth--;
        p++;
    }
    *end = p - 1;
    return 1;
}

/* ---- Public API ---- */

void vs_workflow_engine_init(VSWorkflowEngine *engine)
{
    if (!engine) return;
    memset(engine, 0, sizeof(*engine));
}

int vs_workflow_engine_parse(VSWorkflowEngine *engine, const char *json)
{
    if (!engine || !json) return -1;
    vs_workflow_engine_init(engine);

    /* Extract name */
    const char *name_end;
    const char *name = wf_json_string(json, "name", &name_end);
    if (name) {
        size_t len = (size_t)(name_end - name);
        if (len >= sizeof(engine->name)) len = sizeof(engine->name) - 1;
        memcpy(engine->name, name, len);
        engine->name[len] = 0;
    }

    /* Parse steps array */
    const char *arr_start, *arr_end;
    if (!wf_json_array(json, "steps", &arr_start, &arr_end))
        return -1;

    /* Split array into individual step objects */
    const char *p = arr_start;
    int step_count = 0;

    while (p < arr_end && step_count < VS_WORKFLOW_MAX_STEPS) {
        /* Skip whitespace and commas */
        while (p < arr_end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == ','))
            p++;
        if (p >= arr_end || *p != '{') break;

        /* Find matching '}' */
        const char *obj_start = p;
        int depth = 1;
        p++;
        while (p < arr_end && depth > 0) {
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
            p++;
        }
        const char *obj_end = p;

        /* Extract step object into buffer */
        size_t obj_len = (size_t)(obj_end - obj_start);
        if (obj_len >= VS_WORKFLOW_PROMPT_MAX) obj_len = VS_WORKFLOW_PROMPT_MAX - 1;
        char obj_buf[VS_WORKFLOW_PROMPT_MAX];
        memcpy(obj_buf, obj_start, obj_len);
        obj_buf[obj_len] = 0;

        VSWorkflowStep *step = &engine->steps[step_count];
        memset(step, 0, sizeof(*step));
        step->id = step_count;

        /* Parse type */
        const char *type_end;
        const char *type = wf_json_string(obj_buf, "type", &type_end);
        if (type) {
            size_t tlen = (size_t)(type_end - type);
            char type_buf[64];
            if (tlen >= sizeof(type_buf)) tlen = sizeof(type_buf) - 1;
            memcpy(type_buf, type, tlen);
            type_buf[tlen] = 0;
            if (strcmp(type_buf, "parallel") == 0)
                step->type = VS_WORKFLOW_STEP_PARALLEL;
            else if (strcmp(type_buf, "conditional") == 0)
                step->type = VS_WORKFLOW_STEP_CONDITIONAL;
            else
                step->type = VS_WORKFLOW_STEP_SEQUENTIAL;
        }

        /* Parse prompt */
        const char *prompt_end;
        const char *prompt = wf_json_string(obj_buf, "prompt", &prompt_end);
        if (prompt) {
            size_t plen = (size_t)(prompt_end - prompt);
            if (plen >= sizeof(step->prompt)) plen = sizeof(step->prompt) - 1;
            memcpy(step->prompt, prompt, plen);
            step->prompt[plen] = 0;
        }

        /* Parse condition */
        const char *condition_end;
        const char *condition = wf_json_string(obj_buf, "if", &condition_end);
        if (condition) {
            size_t clen = (size_t)(condition_end - condition);
            if (clen >= sizeof(step->condition)) clen = sizeof(step->condition) - 1;
            memcpy(step->condition, condition, clen);
            step->condition[clen] = 0;
        }

        /* Parse parallel tasks */
        if (step->type == VS_WORKFLOW_STEP_PARALLEL) {
            const char *tasks_start, *tasks_end;
            if (wf_json_array(obj_buf, "tasks", &tasks_start, &tasks_end)) {
                const char *tp = tasks_start;
                int task_count = 0;
                while (tp < tasks_end && task_count < VS_WORKFLOW_MAX_PARALLEL) {
                    while (tp < tasks_end && (*tp == ' ' || *tp == '\t' || *tp == '\n' || *tp == ','))
                        tp++;
                    if (tp >= tasks_end || *tp != '{') break;
                    const char *tobj_start = tp;
                    int tdepth = 1;
                    tp++;
                    while (tp < tasks_end && tdepth > 0) {
                        if (*tp == '{') tdepth++;
                        else if (*tp == '}') tdepth--;
                        tp++;
                    }
                    size_t tlen = (size_t)(tp - tobj_start);
                    if (tlen >= VS_WORKFLOW_PROMPT_MAX) tlen = VS_WORKFLOW_PROMPT_MAX - 1;
                    char tbuf[VS_WORKFLOW_PROMPT_MAX];
                    memcpy(tbuf, tobj_start, tlen);
                    tbuf[tlen] = 0;
                    const char *tprompt_end;
                    const char *tprompt = wf_json_string(tbuf, "prompt", &tprompt_end);
                    if (tprompt) {
                        size_t tplen = (size_t)(tprompt_end - tprompt);
                        if (tplen >= sizeof(step->parallel_tasks[task_count].prompt))
                            tplen = sizeof(step->parallel_tasks[task_count].prompt) - 1;
                        memcpy(step->parallel_tasks[task_count].prompt, tprompt, tplen);
                        step->parallel_tasks[task_count].prompt[tplen] = 0;
                    }
                    task_count++;
                }
                step->parallel_task_count = task_count;
            }
        }

        step_count++;
    }

    engine->step_count = step_count;
    return step_count;
}

int vs_workflow_engine_run(VSWorkflowEngine *engine, VSContext *ctx)
{
    if (!engine || !ctx || engine->step_count == 0) return -1;

    engine->status = VS_WF_ENGINE_RUNNING;
    engine->current_step = 0;

    for (int i = 0; i < engine->step_count; i++) {
        VSWorkflowStep *step = &engine->steps[i];
        engine->current_step = i;

        /* Check condition for conditional steps */
        if (step->type == VS_WORKFLOW_STEP_CONDITIONAL && strlen(step->condition) > 0) {
            /* Simple condition: "step_N.success" */
            int cond_met = 0;
            char cond_buf[256];
            snprintf(cond_buf, sizeof(cond_buf), "step_%d.success", i);
            if (strcmp(step->condition, cond_buf) == 0) {
                /* Check if previous step succeeded */
                if (i > 0 && engine->steps[i-1].status == VS_WORKFLOW_STEP_SUCCESS)
                    cond_met = 1;
            }
            if (!cond_met) {
                step->status = VS_WORKFLOW_STEP_SKIPPED;
                continue;
            }
        }

        step->status = VS_WORKFLOW_STEP_RUNNING;

        if (step->type == VS_WORKFLOW_STEP_SEQUENTIAL) {
            /* Execute as a single prompt */
            char *result = vs_chat(ctx, step->prompt);
            if (result) {
                size_t rlen = strlen(result);
                if (rlen >= sizeof(step->result)) rlen = sizeof(step->result) - 1;
                memcpy(step->result, result, rlen);
                step->result[rlen] = 0;
                step->status = VS_WORKFLOW_STEP_SUCCESS;
                free(result);
            } else {
                step->status = VS_WORKFLOW_STEP_FAILED;
            }
        } else if (step->type == VS_WORKFLOW_STEP_PARALLEL) {
            /* Execute parallel tasks */
            int all_success = 1;
            for (int t = 0; t < step->parallel_task_count; t++) {
                char *result = vs_chat(ctx, step->parallel_tasks[t].prompt);
                if (result) {
                    size_t rlen = strlen(result);
                    if (rlen >= sizeof(step->parallel_tasks[t].result))
                        rlen = sizeof(step->parallel_tasks[t].result) - 1;
                    memcpy(step->parallel_tasks[t].result, result, rlen);
                    step->parallel_tasks[t].result[rlen] = 0;
                    step->parallel_tasks[t].status = VS_WORKFLOW_STEP_SUCCESS;
                    free(result);
                } else {
                    step->parallel_tasks[t].status = VS_WORKFLOW_STEP_FAILED;
                    all_success = 0;
                }
            }
            step->status = all_success ? VS_WORKFLOW_STEP_SUCCESS : VS_WORKFLOW_STEP_FAILED;
        }

        /* Check for cancellation */
        if (ctx->cancel_requested) {
            engine->status = VS_WF_ENGINE_CANCELLED;
            return -1;
        }
    }

    engine->status = VS_WF_ENGINE_COMPLETED;
    return 0;
}

char *vs_workflow_engine_status(VSWorkflowEngine *engine, int max_lines)
{
    if (!engine) return wf_dupstr("No workflow engine initialized");

    char *buf = (char *)malloc(8192);
    if (!buf) return NULL;

    int pos = 0;
    int lines = 0;

    const char *status_str = "idle";
    switch (engine->status) {
        case VS_WF_ENGINE_RUNNING: status_str = "running"; break;
        case VS_WF_ENGINE_COMPLETED: status_str = "completed"; break;
        case VS_WF_ENGINE_FAILED: status_str = "failed"; break;
        case VS_WF_ENGINE_CANCELLED: status_str = "cancelled"; break;
    }

    pos += snprintf(buf + pos, 8192 - pos,
                    "Workflow: \"%s\" (%s, %d steps)\n\n",
                    engine->name, status_str, engine->step_count);
    lines++;

    if (engine->step_count == 0) {
        pos += snprintf(buf + pos, 8192 - pos, "No steps defined.\n");
        return buf;
    }

    for (int i = 0; i < engine->step_count && lines < max_lines; i++) {
        VSWorkflowStep *step = &engine->steps[i];
        const char *step_status = "pending";
        switch (step->status) {
            case VS_WORKFLOW_STEP_RUNNING: step_status = "running"; break;
            case VS_WORKFLOW_STEP_SUCCESS: step_status = "success"; break;
            case VS_WORKFLOW_STEP_FAILED: step_status = "failed"; break;
            case VS_WORKFLOW_STEP_SKIPPED: step_status = "skipped"; break;
        }
        const char *step_type = "sequential";
        if (step->type == VS_WORKFLOW_STEP_PARALLEL) step_type = "parallel";
        if (step->type == VS_WORKFLOW_STEP_CONDITIONAL) step_type = "conditional";

        pos += snprintf(buf + pos, 8192 - pos,
                        "  [%d] (%s) %s: %s\n",
                        i, step_type, step_status, step->prompt);
        if (pos >= 8000) break;
        lines++;
    }

    return buf;
}

void vs_workflow_engine_clear(VSWorkflowEngine *engine)
{
    if (!engine) return;
    vs_workflow_engine_init(engine);
}
