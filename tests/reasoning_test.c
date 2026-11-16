/* SPDX-License-Identifier: Unlicense */
/* Phase 7: Reasoning engine tests */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vibesolaris.h"

static int tests = 0;
static int passed = 0;
static int failed = 0;

static void check(int cond, const char *msg) {
    tests++;
    if (cond) { passed++; }
    else { failed++; printf("FAIL: %s\n", msg); }
}

int main(void) {
    VSReasoningEngine eng;

    /* Test 1: Init defaults */
    vs_reasoning_init(&eng);
    check(eng.enabled == 1, "init enables reasoning");
    check(eng.depth >= 1 && eng.depth <= 3, "init sets valid depth");

    /* Test 2: Init with NULL */
    vs_reasoning_init(NULL);
    check(1, "init with NULL does not crash");

    /* Test 3: Analyze simple task */
    char *analysis = vs_reasoning_analyze(&eng, "fix the bug");
    check(analysis != NULL, "analyze returns non-NULL");
    if (analysis) {
        check(strstr(analysis, "REASONING_ANALYSIS") != NULL, "analysis has header");
        check(strstr(analysis, "bug fix") != NULL || strstr(analysis, "fix") != NULL, "analysis identifies task type");
        free(analysis);
    }

    /* Test 4: Analyze complex task */
    analysis = vs_reasoning_analyze(&eng, "refactor the entire project to use a new architecture and update all files");
    check(analysis != NULL, "analyze complex task returns non-NULL");
    if (analysis) {
        check(strstr(analysis, "very complex") != NULL || strstr(analysis, "complex") != NULL, "complex task identified");
        free(analysis);
    }

    /* Test 5: Analyze with disabled engine */
    eng.enabled = 0;
    analysis = vs_reasoning_analyze(&eng, "fix bug");
    check(analysis == NULL, "disabled engine returns NULL");
    eng.enabled = 1;

    /* Test 6: Analyze with NULL task */
    analysis = vs_reasoning_analyze(&eng, NULL);
    check(analysis == NULL, "NULL task returns NULL");

    /* Test 7: Decompose simple task (should skip) */
    char *decomp = vs_reasoning_decompose(&eng, "fix typo");
    check(decomp == NULL, "simple task skips decomposition");

    /* Test 8: Decompose complex task */
    decomp = vs_reasoning_decompose(&eng, "implement a new feature with multiple components and update all related files");
    check(decomp != NULL, "complex task gets decomposition");
    if (decomp) {
        check(strstr(decomp, "TASK_DECOMPOSITION") != NULL, "decomposition has header");
        check(strstr(decomp, "sub-tasks") != NULL || strstr(decomp, "sub task") != NULL, "decomposition lists sub-tasks");
        free(decomp);
    }

    /* Test 9: Decompose with disabled engine */
    eng.enabled = 0;
    decomp = vs_reasoning_decompose(&eng, "implement a new feature with multiple components and update all related files");
    check(decomp == NULL, "disabled engine skips decomposition");
    eng.enabled = 1;

    /* Test 10: Confidence for read action */
    int conf = vs_reasoning_confidence(&eng, "read file", "read");
    check(conf >= 80, "read action has high confidence");

    /* Test 11: Confidence for edit action */
    conf = vs_reasoning_confidence(&eng, "edit file", "edit");
    check(conf >= 60 && conf <= 90, "edit action has medium-high confidence");

    /* Test 12: Confidence for run action */
    conf = vs_reasoning_confidence(&eng, "run command", "run");
    check(conf >= 40 && conf <= 80, "run action has medium confidence");

    /* Test 13: Confidence for refactor task */
    conf = vs_reasoning_confidence(&eng, "refactor the codebase", "edit");
    check(conf < 80, "refactor task reduces confidence");

    /* Test 14: Confidence with disabled engine */
    eng.enabled = 0;
    conf = vs_reasoning_confidence(&eng, "fix", "edit");
    check(conf == 50, "disabled engine returns default confidence");
    eng.enabled = 1;

    /* Test 15: Confidence label high */
    check(strcmp(vs_reasoning_confidence_label(90), "high") == 0, "label 90 is high");

    /* Test 16: Confidence label medium */
    check(strcmp(vs_reasoning_confidence_label(60), "medium") == 0, "label 60 is medium");

    /* Test 17: Confidence label low */
    check(strcmp(vs_reasoning_confidence_label(30), "low") == 0, "label 30 is low");

    /* Test 18: Confidence label very low */
    check(strcmp(vs_reasoning_confidence_label(10), "very low") == 0, "label 10 is very low");

    /* Test 19: Self-correct command not found */
    char *corr = vs_reasoning_self_correct(&eng, "run test", "run", "command not found");
    check(corr != NULL, "self-correct returns non-NULL");
    if (corr) {
        check(strstr(corr, "SELF_CORRECTION") != NULL, "correction has header");
        check(strstr(corr, "not installed") != NULL || strstr(corr, "native") != NULL, "suggests alternative");
        free(corr);
    }

    /* Test 20: Self-correct permission denied */
    corr = vs_reasoning_self_correct(&eng, "run cmd", "run", "permission denied");
    check(corr != NULL, "self-correct permission denied");
    if (corr) {
        check(strstr(corr, "sudo") != NULL || strstr(corr, "permission") != NULL, "mentions permission");
        free(corr);
    }

    /* Test 21: Self-correct edit not found */
    corr = vs_reasoning_self_correct(&eng, "fix bug", "edit", "search text not found");
    check(corr != NULL, "self-correct edit not found");
    if (corr) {
        check(strstr(corr, "re-read") != NULL || strstr(corr, "exact") != NULL, "suggests re-reading");
        free(corr);
    }

    /* Test 22: Self-correct with disabled engine */
    eng.enabled = 0;
    corr = vs_reasoning_self_correct(&eng, "fix", "run", "error");
    check(corr == NULL, "disabled engine returns NULL");
    eng.enabled = 1;

    /* Test 23: Self-correct with NULL error */
    corr = vs_reasoning_self_correct(&eng, "fix", "run", NULL);
    check(corr == NULL, "NULL error returns NULL");

    /* Test 24: Suggest after edit */
    char *sugg = vs_reasoning_suggest(&eng, "fix the bug", "edit", "OK: file edited");
    check(sugg != NULL, "suggest returns non-NULL");
    if (sugg) {
        check(strstr(sugg, "PROACTIVE_SUGGESTIONS") != NULL, "suggestion has header");
        check(strstr(sugg, "build") != NULL || strstr(sugg, "test") != NULL, "suggests next step");
        free(sugg);
    }

    /* Test 25: Suggest after read */
    sugg = vs_reasoning_suggest(&eng, "fix the bug", "read", "file content");
    check(sugg != NULL, "suggest after read");
    if (sugg) {
        check(strstr(sugg, "root cause") != NULL || strstr(sugg, "fix") != NULL, "suggests identifying root cause");
        free(sugg);
    }

    /* Test 26: Suggest with disabled engine */
    eng.enabled = 0;
    sugg = vs_reasoning_suggest(&eng, "fix", "edit", "OK");
    check(sugg == NULL, "disabled engine returns NULL");
    eng.enabled = 1;

    /* Test 27: Should analyze simple task */
    int should = vs_reasoning_should_analyze(&eng, "fix typo");
    check(should == 0, "simple task skips analysis");

    /* Test 28: Should analyze complex task */
    should = vs_reasoning_should_analyze(&eng, "implement a new feature with multiple components and update all related files");
    check(should == 1, "complex task warrants analysis");

    /* Test 29: Should analyze with disabled engine */
    eng.enabled = 0;
    should = vs_reasoning_should_analyze(&eng, "implement a new feature with multiple components and update all related files");
    check(should == 0, "disabled engine skips analysis");
    eng.enabled = 1;

    /* Test 30: Prompt fragment */
    char *frag = vs_reasoning_prompt_fragment(&eng);
    check(frag != NULL, "prompt fragment returns non-NULL");
    if (frag) {
        check(strstr(frag, "REASONING GUIDELINES") != NULL, "fragment has header");
        check(strstr(frag, "analyze") != NULL || strstr(frag, "error") != NULL, "fragment has guidelines");
        free(frag);
    }

    /* Test 31: Prompt fragment disabled */
    eng.enabled = 0;
    frag = vs_reasoning_prompt_fragment(&eng);
    check(frag == NULL, "disabled engine returns NULL fragment");
    eng.enabled = 1;

    /* Test 32: Analyze feature task */
    analysis = vs_reasoning_analyze(&eng, "add a new authentication feature");
    check(analysis != NULL, "feature task analysis");
    if (analysis) {
        check(strstr(analysis, "feature") != NULL || strstr(analysis, "new") != NULL, "identifies feature task");
        free(analysis);
    }

    /* Test 33: Analyze refactor task */
    analysis = vs_reasoning_analyze(&eng, "refactor the database module");
    check(analysis != NULL, "refactor task analysis");
    if (analysis) {
        check(strstr(analysis, "refactoring") != NULL, "identifies refactor task");
        free(analysis);
    }

    /* Test 34: Decompose feature task */
    decomp = vs_reasoning_decompose(&eng, "add a new authentication feature with multiple components and update all related files");
    check(decomp != NULL, "feature decomposition");
    if (decomp) {
        check(strstr(decomp, "Inspect") != NULL || strstr(decomp, "implement") != NULL, "has implementation steps");
        free(decomp);
    }

    /* Test 35: Decompose refactor task */
    decomp = vs_reasoning_decompose(&eng, "refactor the database module to use a new architecture and update all related files and ensure backward compatibility");
    check(decomp != NULL, "refactor decomposition");
    if (decomp) {
        check(strstr(decomp, "checkpoint") != NULL, "refactor suggests checkpoint");
        free(decomp);
    }

    /* Test 36: Self-correct illegal option (Solaris) */
    corr = vs_reasoning_self_correct(&eng, "run grep", "run", "illegal option");
    check(corr != NULL, "Solaris command mismatch correction");
    if (corr) {
        check(strstr(corr, "POSIX") != NULL || strstr(corr, "Solaris") != NULL, "mentions POSIX/Solaris");
        free(corr);
    }

    /* Test 37: Self-correct file not found */
    corr = vs_reasoning_self_correct(&eng, "read file", "read", "unable to read file");
    check(corr != NULL, "read error correction");
    if (corr) {
        check(strstr(corr, "list") != NULL || strstr(corr, "path") != NULL, "suggests listing");
        free(corr);
    }

    /* Test 38: Suggest after build success */
    sugg = vs_reasoning_suggest(&eng, "build the project", "run", "Build OK: all tests passed");
    check(sugg != NULL, "suggest after build success");
    if (sugg) {
        check(strstr(sugg, "succeeded") != NULL || strstr(sugg, "test") != NULL, "acknowledges success");
        free(sugg);
    }

    /* Test 39: Suggest after build failure */
    sugg = vs_reasoning_suggest(&eng, "build the project", "run", "error: undefined reference");
    check(sugg != NULL, "suggest after build failure");
    if (sugg) {
        check(strstr(sugg, "error") != NULL || strstr(sugg, "fix") != NULL, "suggests fixing error");
        free(sugg);
    }

    /* Test 40: Confidence clamped to min */
    conf = vs_reasoning_confidence(&eng, "refactor the entire project and migrate all files and it is very complex and difficult", "run");
    check(conf >= 10, "confidence clamped to minimum 10");

    /* Test 41: Confidence clamped to max */
    conf = vs_reasoning_confidence(&eng, "simple read", "read");
    check(conf <= 95, "confidence clamped to maximum 95");

    /* Test 42: Analyze with NULL engine */
    analysis = vs_reasoning_analyze(NULL, "fix bug");
    check(analysis == NULL, "NULL engine returns NULL");

    /* Test 43: Decompose with NULL engine */
    decomp = vs_reasoning_decompose(NULL, "complex task with many parts");
    check(decomp == NULL, "NULL engine returns NULL");

    /* Test 44: Self-correct with NULL engine */
    corr = vs_reasoning_self_correct(NULL, "fix", "run", "error");
    check(corr == NULL, "NULL engine returns NULL");

    /* Test 45: Suggest with NULL engine */
    sugg = vs_reasoning_suggest(NULL, "fix", "edit", "OK");
    check(sugg == NULL, "NULL engine returns NULL");

    /* Test 46: Should analyze with NULL engine */
    should = vs_reasoning_should_analyze(NULL, "complex task");
    check(should == 0, "NULL engine returns 0");

    /* Test 47: Prompt fragment with NULL engine */
    frag = vs_reasoning_prompt_fragment(NULL);
    check(frag == NULL, "NULL engine returns NULL fragment");

    /* Test 48: Confidence with NULL engine */
    conf = vs_reasoning_confidence(NULL, "fix", "edit");
    check(conf == 50, "NULL engine returns default 50");

    /* Test 49: Confidence with NULL task */
    conf = vs_reasoning_confidence(&eng, NULL, "edit");
    check(conf == 50, "NULL task returns default 50");

    /* Test 50: Confidence with NULL action */
    conf = vs_reasoning_confidence(&eng, "fix", NULL);
    check(conf == 50, "NULL action returns default 50");

    /* Test 51: Analyze inspection task */
    analysis = vs_reasoning_analyze(&eng, "inspect the project structure");
    check(analysis != NULL, "inspection task analysis");
    if (analysis) {
        check(strstr(analysis, "inspection") != NULL, "identifies inspection task");
        free(analysis);
    }

    /* Test 52: Analyze build task */
    analysis = vs_reasoning_analyze(&eng, "compile and test the project");
    check(analysis != NULL, "build task analysis");
    if (analysis) {
        check(strstr(analysis, "build") != NULL || strstr(analysis, "test") != NULL, "identifies build task");
        free(analysis);
    }

    /* Test 53: Decompose fix task (needs 20+ words for complexity >= 2) */
    decomp = vs_reasoning_decompose(&eng, "fix the critical security vulnerability in the authentication module and update all related files and verify the fix works correctly across all platforms");
    check(decomp != NULL, "fix task decomposition");
    if (decomp) {
        check(strstr(decomp, "reproduce") != NULL || strstr(decomp, "root cause") != NULL, "fix suggests reproduction");
        free(decomp);
    }

    /* Test 54: Self-correct write failure */
    corr = vs_reasoning_self_correct(&eng, "write config", "write", "write failed");
    check(corr != NULL, "write error correction");
    if (corr) {
        check(strstr(corr, "permission") != NULL || strstr(corr, "disk") != NULL, "suggests checking permissions");
        free(corr);
    }

    /* Test 55: Self-correct generic tool */
    corr = vs_reasoning_self_correct(&eng, "do something", "unknown_tool", "generic error");
    check(corr != NULL, "generic error correction");
    if (corr) {
        check(strstr(corr, "different approach") != NULL || strstr(corr, "smaller steps") != NULL, "suggests general approach");
        free(corr);
    }

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return failed > 0 ? 1 : 0;
}
