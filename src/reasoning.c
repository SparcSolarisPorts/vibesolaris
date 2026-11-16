/* SPDX-License-Identifier: Unlicense */
/* Phase 7: Reasoning Quality & Autonomy Engine
 *
 * Provides chain-of-thought analysis, task decomposition, confidence scoring,
 * self-correction from tool failures, and proactive suggestions.
 * All functions are deterministic heuristics — no LLM calls — so they run
 * instantly before/after each agent turn to guide the model's behavior. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- Internal helpers ---- */

static int str_contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return 0;
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i;
        for (i = 0; i < n; i++) {
            unsigned char a = (unsigned char)hay[i], b = (unsigned char)needle[i];
            if (!a) return 0;
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
        }
        if (i == n) return 1;
    }
    return 0;
}

static int count_words(const char *s) {
    int words = 0, in_word = 0;
    if (!s) return 0;
    for (; *s; s++) {
        if (isspace((unsigned char)*s)) { in_word = 0; continue; }
        if (!in_word) { words++; in_word = 1; }
    }
    return words;
}

/* Classify task complexity: 0=simple, 1=moderate, 2=complex, 3=very complex */
static int task_complexity(const char *task) {
    if (!task) return 0;
    int words = count_words(task);
    int complexity = 0;

    /* Word count heuristic */
    if (words > 40) complexity = 3;
    else if (words > 20) complexity = 2;
    else if (words > 8) complexity = 1;

    /* Keyword heuristics for complexity */
    if (str_contains_ci(task, "refactor") || str_contains_ci(task, "migrate") ||
        str_contains_ci(task, "rewrite") || str_contains_ci(task, "implement from scratch") ||
        str_contains_ci(task, "all files") || str_contains_ci(task, "entire project")) {
        if (complexity < 3) complexity = 3;
    }
    if (str_contains_ci(task, "multiple") || str_contains_ci(task, "several") ||
        str_contains_ci(task, "each file") || str_contains_ci(task, "every file")) {
        if (complexity < 2) complexity = 2;
    }
    if (str_contains_ci(task, "test") && (str_contains_ci(task, "build") || str_contains_ci(task, "compile"))) {
        if (complexity < 2) complexity = 2;
    }

    return complexity;
}

/* Detect task type for targeted analysis */
typedef enum {
    VS_TASK_TYPE_UNKNOWN = 0,
    VS_TASK_TYPE_INSPECT,    /* read, list, search, review */
    VS_TASK_TYPE_FIX,        /* fix bug, resolve error */
    VS_TASK_TYPE_FEATURE,    /* add feature, implement */
    VS_TASK_TYPE_REFACTOR,   /* refactor, clean up */
    VS_TASK_TYPE_BUILD,      /* build, compile, test */
    VS_TASK_TYPE_DEPLOY,     /* deploy, release */
    VS_TASK_TYPE_DOCUMENT    /* document, comment */
} VSTaskType;

static VSTaskType detect_task_type(const char *task) {
    if (!task) return VS_TASK_TYPE_UNKNOWN;
    if (str_contains_ci(task, "inspect") || str_contains_ci(task, "review") ||
        str_contains_ci(task, "look at") || str_contains_ci(task, "analyze") ||
        str_contains_ci(task, "explore") || str_contains_ci(task, "understand")) {
        return VS_TASK_TYPE_INSPECT;
    }
    if (str_contains_ci(task, "fix") || str_contains_ci(task, "resolve") ||
        str_contains_ci(task, "debug") || str_contains_ci(task, "patch") ||
        str_contains_ci(task, "error") || str_contains_ci(task, "crash") ||
        str_contains_ci(task, "broken")) {
        return VS_TASK_TYPE_FIX;
    }
    /* Check refactor before feature, since "new" appears in both */
    if (str_contains_ci(task, "refactor") || str_contains_ci(task, "clean up") ||
        str_contains_ci(task, "reorganize") || str_contains_ci(task, "rename") ||
        str_contains_ci(task, "simplify")) {
        return VS_TASK_TYPE_REFACTOR;
    }
    if (str_contains_ci(task, "add") || str_contains_ci(task, "implement") ||
        str_contains_ci(task, "create") || str_contains_ci(task, "build") ||
        str_contains_ci(task, "feature") || str_contains_ci(task, "new")) {
        return VS_TASK_TYPE_FEATURE;
    }
    if (str_contains_ci(task, "compile") || str_contains_ci(task, "test") ||
        str_contains_ci(task, "make") || str_contains_ci(task, "cmake")) {
        return VS_TASK_TYPE_BUILD;
    }
    if (str_contains_ci(task, "deploy") || str_contains_ci(task, "release") ||
        str_contains_ci(task, "publish")) {
        return VS_TASK_TYPE_DEPLOY;
    }
    if (str_contains_ci(task, "document") || str_contains_ci(task, "comment") ||
        str_contains_ci(task, "readme") || str_contains_ci(task, "docstring")) {
        return VS_TASK_TYPE_DOCUMENT;
    }
    return VS_TASK_TYPE_UNKNOWN;
}

/* ---- Public API ---- */

void vs_reasoning_init(VSReasoningEngine *eng) {
    if (!eng) return;
    memset(eng, 0, sizeof(*eng));
    eng->depth = 2; /* default: standard */
    const char *e = getenv("VIBESOLARIS_REASONING_DEPTH");
    if (e && *e) {
        char *ep = NULL;
        long v = strtol(e, &ep, 10);
        if (ep && *ep == 0 && v >= 1 && v <= 3) eng->depth = (int)v;
    }
    eng->enabled = 1;
    const char *disable = getenv("VIBESOLARIS_REASONING_DISABLE");
    if (disable && (strcmp(disable, "1") == 0 || strcasecmp(disable, "on") == 0 || strcasecmp(disable, "true") == 0)) {
        eng->enabled = 0;
    }
}

/* Generate chain-of-thought analysis for a task.
 * Returns a heap-allocated string with structured analysis. */
char *vs_reasoning_analyze(const VSReasoningEngine *eng, const char *task) {
    if (!eng || !task || !eng->enabled) return NULL;

    int complexity = task_complexity(task);
    VSTaskType type = detect_task_type(task);
    int words = count_words(task);

    /* Build analysis based on depth */
    char *out;
    size_t cap = 2048;
    size_t n = 0;

    out = (char *)malloc(cap);
    if (!out) return NULL;
    out[0] = 0;

    /* Header */
    n += (size_t)snprintf(out + n, cap - n, "REASONING_ANALYSIS:\n");
    n += (size_t)snprintf(out + n, cap - n, "  Task type: %s\n",
        type == VS_TASK_TYPE_INSPECT ? "inspection" :
        type == VS_TASK_TYPE_FIX ? "bug fix" :
        type == VS_TASK_TYPE_FEATURE ? "new feature" :
        type == VS_TASK_TYPE_REFACTOR ? "refactoring" :
        type == VS_TASK_TYPE_BUILD ? "build/test" :
        type == VS_TASK_TYPE_DEPLOY ? "deployment" :
        type == VS_TASK_TYPE_DOCUMENT ? "documentation" : "general");
    n += (size_t)snprintf(out + n, cap - n, "  Complexity: %s (%d words)\n",
        complexity == 0 ? "simple" : complexity == 1 ? "moderate" : complexity == 2 ? "complex" : "very complex",
        words);

    if (eng->depth >= 2) {
        /* Edge case analysis */
        n += (size_t)snprintf(out + n, cap - n, "\n  Edge cases to consider:\n");
        if (type == VS_TASK_TYPE_FIX) {
            n += (size_t)snprintf(out + n, cap - n, "    - Root cause may be in a different file than the error location\n");
            n += (size_t)snprintf(out + n, cap - n, "    - The fix may introduce regressions in related code\n");
            n += (size_t)snprintf(out + n, cap - n, "    - Check for similar patterns elsewhere in the codebase\n");
        } else if (type == VS_TASK_TYPE_FEATURE) {
            n += (size_t)snprintf(out + n, cap - n, "    - Existing code may need updates to support the new feature\n");
            n += (size_t)snprintf(out + n, cap - n, "    - Consider backward compatibility\n");
            n += (size_t)snprintf(out + n, cap - n, "    - Tests should cover both happy path and error cases\n");
        } else if (type == VS_TASK_TYPE_REFACTOR) {
            n += (size_t)snprintf(out + n, cap - n, "    - Refactoring may affect callers of changed functions\n");
            n += (size_t)snprintf(out + n, cap - n, "    - Ensure behavior is preserved after changes\n");
            n += (size_t)snprintf(out + n, cap - n, "    - Consider creating a checkpoint before refactoring\n");
        } else {
            n += (size_t)snprintf(out + n, cap - n, "    - Verify the task scope matches the user's intent\n");
            n += (size_t)snprintf(out + n, cap - n, "    - Check for dependencies between sub-tasks\n");
        }
    }

    if (eng->depth >= 3) {
        /* Dependency analysis */
        n += (size_t)snprintf(out + n, cap - n, "\n  Dependencies:\n");
        if (str_contains_ci(task, "test")) {
            n += (size_t)snprintf(out + n, cap - n, "    - Tests depend on the code being correct\n");
            n += (size_t)snprintf(out + n, cap - n, "    - Build must succeed before tests can run\n");
        }
        if (str_contains_ci(task, "build") || str_contains_ci(task, "compile")) {
            n += (size_t)snprintf(out + n, cap - n, "    - Build depends on correct dependencies and configuration\n");
            n += (size_t)snprintf(out + n, cap - n, "    - Check build system files (Makefile, CMakeLists.txt, etc.)\n");
        }
        n += (size_t)snprintf(out + n, cap - n, "    - Inspect relevant files before making changes\n");
    }

    out[n] = 0;
    return out;
}

/* Decompose a complex task into sub-tasks.
 * Returns a heap-allocated string with sub-task list. */
char *vs_reasoning_decompose(const VSReasoningEngine *eng, const char *task) {
    if (!eng || !task || !eng->enabled) return NULL;

    int complexity = task_complexity(task);
    if (complexity < 2) {
        /* Simple tasks don't need decomposition */
        return NULL;
    }

    VSTaskType type = detect_task_type(task);
    char *out;
    size_t cap = 4096;
    size_t n = 0;

    out = (char *)malloc(cap);
    if (!out) return NULL;
    out[0] = 0;

    n += (size_t)snprintf(out + n, cap - n, "TASK_DECOMPOSITION:\n");
    n += (size_t)snprintf(out + n, cap - n, "  Original task: %.200s...\n\n", task);
    n += (size_t)snprintf(out + n, cap - n, "  Suggested sub-tasks:\n");

    if (type == VS_TASK_TYPE_FEATURE) {
        n += (size_t)snprintf(out + n, cap - n, "    1. Inspect existing code structure and identify where to add the feature\n");
        n += (size_t)snprintf(out + n, cap - n, "    2. Implement the core logic in the appropriate file(s)\n");
        n += (size_t)snprintf(out + n, cap - n, "    3. Update related files (headers, configs, etc.)\n");
        n += (size_t)snprintf(out + n, cap - n, "    4. Build and verify compilation\n");
        n += (size_t)snprintf(out + n, cap - n, "    5. Test the new feature\n");
    } else if (type == VS_TASK_TYPE_FIX) {
        n += (size_t)snprintf(out + n, cap - n, "    1. Reproduce the error and identify the root cause\n");
        n += (size_t)snprintf(out + n, cap - n, "    2. Locate the relevant code section(s)\n");
        n += (size_t)snprintf(out + n, cap - n, "    3. Implement the fix\n");
        n += (size_t)snprintf(out + n, cap - n, "    4. Verify the fix resolves the issue\n");
        n += (size_t)snprintf(out + n, cap - n, "    5. Check for similar issues in related code\n");
    } else if (type == VS_TASK_TYPE_REFACTOR) {
        n += (size_t)snprintf(out + n, cap - n, "    1. Create a checkpoint before refactoring\n");
        n += (size_t)snprintf(out + n, cap - n, "    2. Analyze the current code structure\n");
        n += (size_t)snprintf(out + n, cap - n, "    3. Identify all callers and dependencies\n");
        n += (size_t)snprintf(out + n, cap - n, "    4. Apply the refactoring incrementally\n");
        n += (size_t)snprintf(out + n, cap - n, "    5. Verify behavior is preserved\n");
    } else {
        n += (size_t)snprintf(out + n, cap - n, "    1. Understand the current state of the codebase\n");
        n += (size_t)snprintf(out + n, cap - n, "    2. Plan the specific changes needed\n");
        n += (size_t)snprintf(out + n, cap - n, "    3. Implement the changes\n");
        n += (size_t)snprintf(out + n, cap - n, "    4. Verify the changes work correctly\n");
    }

    n += (size_t)snprintf(out + n, cap - n, "\n  Use [[VS_TOOL todo items=\"...\"]] to track progress.\n");
    out[n] = 0;
    return out;
}

/* Score confidence (0-100) for a proposed action.
 * Higher score = more confident the action will succeed. */
int vs_reasoning_confidence(const VSReasoningEngine *eng, const char *task, const char *action) {
    if (!eng || !task || !action || !eng->enabled) return 50;

    int confidence = 70; /* base confidence */
    int complexity = task_complexity(task);

    /* Adjust based on complexity */
    confidence -= complexity * 10;

    /* Adjust based on action type */
    if (str_contains_ci(action, "read") || str_contains_ci(action, "list") ||
        str_contains_ci(action, "search") || str_contains_ci(action, "glob") ||
        str_contains_ci(action, "grep")) {
        confidence = 90; /* Read-only actions are high confidence */
    } else if (str_contains_ci(action, "edit")) {
        confidence = 80; /* Targeted edits are fairly confident */
    } else if (str_contains_ci(action, "write")) {
        confidence = 65; /* Full writes have more risk */
    } else if (str_contains_ci(action, "run") || str_contains_ci(action, "bash")) {
        confidence = 60; /* Shell commands vary widely */
    }

    /* Adjust based on task keywords */
    if (str_contains_ci(task, "refactor") || str_contains_ci(task, "migrate")) {
        confidence -= 15;
    }
    if (str_contains_ci(task, "simple") || str_contains_ci(task, "trivial")) {
        confidence += 10;
    }
    if (str_contains_ci(task, "complex") || str_contains_ci(task, "difficult")) {
        confidence -= 10;
    }

    /* Clamp */
    if (confidence < 10) confidence = 10;
    if (confidence > 95) confidence = 95;

    return confidence;
}

const char *vs_reasoning_confidence_label(int score) {
    if (score >= 80) return "high";
    if (score >= 50) return "medium";
    if (score >= 30) return "low";
    return "very low";
}

/* Analyze a tool failure and suggest an alternative approach.
 * Returns a heap-allocated string with the correction suggestion. */
char *vs_reasoning_self_correct(const VSReasoningEngine *eng, const char *task, const char *tool, const char *error) {
    if (!eng || !tool || !error || !eng->enabled) return NULL;

    char *out;
    size_t cap = 2048;
    size_t n = 0;

    out = (char *)malloc(cap);
    if (!out) return NULL;
    out[0] = 0;

    n += (size_t)snprintf(out + n, cap - n, "SELF_CORRECTION:\n");
    n += (size_t)snprintf(out + n, cap - n, "  Tool: %s\n", tool);
    n += (size_t)snprintf(out + n, cap - n, "  Error: %.300s\n", error);
    n += (size_t)snprintf(out + n, cap - n, "\n  Suggested alternative:\n");

    /* Command execution failures */
    if (str_contains_ci(tool, "run") || str_contains_ci(tool, "bash")) {
        if (str_contains_ci(error, "not found") || str_contains_ci(error, "command not found")) {
            n += (size_t)snprintf(out + n, cap - n, "    - The command may not be installed. Try: web_search for installation instructions\n");
            n += (size_t)snprintf(out + n, cap - n, "    - Check if the command has a different name on this OS\n");
            n += (size_t)snprintf(out + n, cap - n, "    - Use native VibeSolaris tools (read/list/search) instead\n");
        } else if (str_contains_ci(error, "permission") || str_contains_ci(error, "denied")) {
            n += (size_t)snprintf(out + n, cap - n, "    - Permission denied. Avoid sudo; use worktree or user-writable paths\n");
        } else if (str_contains_ci(error, "illegal option") || str_contains_ci(error, "unknown option")) {
            n += (size_t)snprintf(out + n, cap - n, "    - Command option mismatch (likely GNU vs POSIX). Use POSIX syntax or native tools\n");
            n += (size_t)snprintf(out + n, cap - n, "    - On Solaris: use /usr/xpg4/bin/ variants for GNU-compatible behavior\n");
        } else if (str_contains_ci(error, "no such file") || str_contains_ci(error, "cannot open")) {
            n += (size_t)snprintf(out + n, cap - n, "    - File not found. Use [[VS_TOOL list]] to verify the path exists\n");
            n += (size_t)snprintf(out + n, cap - n, "    - Check for typos in the file path\n");
        } else {
            n += (size_t)snprintf(out + n, cap - n, "    - Analyze the error message and adjust the command\n");
            n += (size_t)snprintf(out + n, cap - n, "    - Consider using native VibeSolaris tools instead of shell\n");
        }
    }
    /* File read failures */
    else if (str_contains_ci(tool, "read")) {
        if (str_contains_ci(error, "unable") || str_contains_ci(error, "error")) {
            n += (size_t)snprintf(out + n, cap - n, "    - File may not exist. Use [[VS_TOOL list]] to find the correct path\n");
            n += (size_t)snprintf(out + n, cap - n, "    - Check for case sensitivity in the file name\n");
        }
    }
    /* File edit failures */
    else if (str_contains_ci(tool, "edit")) {
        if (str_contains_ci(error, "not found") || str_contains_ci(error, "no match")) {
            n += (size_t)snprintf(out + n, cap - n, "    - Search text not found. Re-read the file to get the exact text\n");
            n += (size_t)snprintf(out + n, cap - n, "    - Check for whitespace differences or encoding issues\n");
        } else if (str_contains_ci(error, "multiple")) {
            n += (size_t)snprintf(out + n, cap - n, "    - Search text matches multiple times. Use more specific search text\n");
        }
    }
    /* Write failures */
    else if (str_contains_ci(tool, "write")) {
        n += (size_t)snprintf(out + n, cap - n, "    - Write failed. Check file permissions and disk space\n");
        n += (size_t)snprintf(out + n, cap - n, "    - Try writing to a different path in the worktree\n");
    }
    /* Generic fallback */
    else {
        n += (size_t)snprintf(out + n, cap - n, "    - Analyze the specific error and try a different approach\n");
        n += (size_t)snprintf(out + n, cap - n, "    - Consider breaking the task into smaller steps\n");
        n += (size_t)snprintf(out + n, cap - n, "    - If the error persists, ask the user for clarification\n");
    }

    out[n] = 0;
    return out;
}

/* Generate proactive next-step suggestions after a successful action.
 * Returns a heap-allocated string with suggestions. */
char *vs_reasoning_suggest(const VSReasoningEngine *eng, const char *task, const char *last_action, const char *last_result) {
    if (!eng || !task || !eng->enabled) return NULL;

    char *out;
    size_t cap = 2048;
    size_t n = 0;

    out = (char *)malloc(cap);
    if (!out) return NULL;
    out[0] = 0;

    n += (size_t)snprintf(out + n, cap - n, "PROACTIVE_SUGGESTIONS:\n");

    VSTaskType type = detect_task_type(task);

    /* After a successful edit/write */
    if (last_action && (str_contains_ci(last_action, "edit") || str_contains_ci(last_action, "write"))) {
        n += (size_t)snprintf(out + n, cap - n, "  - Build the project to verify the changes compile\n");
        if (type == VS_TASK_TYPE_FIX) {
            n += (size_t)snprintf(out + n, cap - n, "  - Run the relevant tests to confirm the fix\n");
            n += (size_t)snprintf(out + n, cap - n, "  - Check for similar issues in related files\n");
        }
        if (type == VS_TASK_TYPE_FEATURE) {
            n += (size_t)snprintf(out + n, cap - n, "  - Add tests for the new feature\n");
            n += (size_t)snprintf(out + n, cap - n, "  - Update documentation if applicable\n");
        }
        n += (size_t)snprintf(out + n, cap - n, "  - Consider creating a checkpoint to save the current state\n");
    }
    /* After a successful read/inspection */
    else if (last_action && (str_contains_ci(last_action, "read") || str_contains_ci(last_action, "list") ||
             str_contains_ci(last_action, "search") || str_contains_ci(last_action, "glob"))) {
        if (type == VS_TASK_TYPE_FIX) {
            n += (size_t)snprintf(out + n, cap - n, "  - Now that you've inspected the code, identify the root cause\n");
            n += (size_t)snprintf(out + n, cap - n, "  - Propose a specific fix with the edit tool\n");
        }
        if (type == VS_TASK_TYPE_FEATURE) {
            n += (size_t)snprintf(out + n, cap - n, "  - Now that you understand the structure, implement the feature\n");
            n += (size_t)snprintf(out + n, cap - n, "  - Start with the core logic, then update related files\n");
        }
        if (type == VS_TASK_TYPE_REFACTOR) {
            n += (size_t)snprintf(out + n, cap - n, "  - Create a checkpoint before refactoring\n");
            n += (size_t)snprintf(out + n, cap - n, "  - Apply changes incrementally and verify after each step\n");
        }
    }
    /* After a successful build/test */
    else if (last_action && (str_contains_ci(last_action, "run") || str_contains_ci(last_action, "bash"))) {
        if (last_result && str_contains_ci(last_result, "error") && !str_contains_ci(last_result, "OK")) {
            n += (size_t)snprintf(out + n, cap - n, "  - The build/test failed. Analyze the error output\n");
            n += (size_t)snprintf(out + n, cap - n, "  - Fix the first error and retry\n");
        } else if (last_result && (str_contains_ci(last_result, "OK") || str_contains_ci(last_result, "passed") ||
                   str_contains_ci(last_result, "success"))) {
            n += (size_t)snprintf(out + n, cap - n, "  - Build/test succeeded. Consider running the full test suite\n");
            n += (size_t)snprintf(out + n, cap - n, "  - Mark the current sub-task as complete in the todo list\n");
        }
    }

    /* General suggestions */
    n += (size_t)snprintf(out + n, cap - n, "  - Update the todo list to reflect progress\n");
    n += (size_t)snprintf(out + n, cap - n, "  - If the task is complete, use [[VS_FINAL]] to signal completion\n");

    out[n] = 0;
    return out;
}

/* Check if a task is complex enough to warrant reasoning analysis.
 * Returns 1 if analysis is recommended, 0 if the task is simple enough to skip. */
int vs_reasoning_should_analyze(const VSReasoningEngine *eng, const char *task) {
    if (!eng || !task || !eng->enabled) return 0;
    int complexity = task_complexity(task);
    return complexity >= (eng->depth >= 3 ? 1 : 2);
}

/* Build a reasoning prompt fragment for injection into the system prompt.
 * Returns a heap-allocated string. */
char *vs_reasoning_prompt_fragment(const VSReasoningEngine *eng) {
    if (!eng || !eng->enabled) return NULL;

    char *out;
    size_t cap = 2048;
    size_t n = 0;

    out = (char *)malloc(cap);
    if (!out) return NULL;
    out[0] = 0;

    n += (size_t)snprintf(out + n, cap - n, "REASONING GUIDELINES:\n");

    if (eng->depth >= 1) {
        n += (size_t)snprintf(out + n, cap - n, "  - Before executing complex tasks, briefly analyze the approach\n");
        n += (size_t)snprintf(out + n, cap - n, "  - If a tool fails, analyze the error and try a different approach before asking the user\n");
        n += (size_t)snprintf(out + n, cap - n, "  - For multi-step tasks, use [[VS_TOOL todo items=\"...\"]] to track progress\n");
    }

    if (eng->depth >= 2) {
        n += (size_t)snprintf(out + n, cap - n, "  - Consider edge cases: what if a file doesn't exist? what if the build fails?\n");
        n += (size_t)snprintf(out + n, cap - n, "  - Before file-modifying actions, rate your confidence (high/medium/low)\n");
        n += (size_t)snprintf(out + n, cap - n, "  - After successful changes, proactively suggest the next step (build, test, verify)\n");
        n += (size_t)snprintf(out + n, cap - n, "  - When fixing bugs, check for similar patterns elsewhere in the codebase\n");
    }

    if (eng->depth >= 3) {
        n += (size_t)snprintf(out + n, cap - n, "  - For very complex tasks, decompose into sub-tasks with dependencies\n");
        n += (size_t)snprintf(out + n, cap - n, "  - Before refactoring, create a checkpoint and verify behavior is preserved\n");
        n += (size_t)snprintf(out + n, cap - n, "  - When implementing features, consider backward compatibility and related code\n");
        n += (size_t)snprintf(out + n, cap - n, "  - After each step, verify the result before proceeding to the next\n");
    }

    out[n] = 0;
    return out;
}
