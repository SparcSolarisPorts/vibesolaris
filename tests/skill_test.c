/* SPDX-License-Identifier: Unlicense */
/* Unit tests for skill system (src/skill.c). */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

static void mkdirs(const char *path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", path);
    system(cmd);
}

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%s", content); fclose(f); }
}

#define SKILL_DIR "/tmp/vs_skill_test"

int main(void) {
    VSSkill skills[VS_MAX_SKILLS];
    int count;
    VSSkill *found;
    char *result;
    char list_buf[4096];

    printf("Skill tests:\n");

    /* Setup skill directories */
    mkdirs(SKILL_DIR "/greeting");
    mkdirs(SKILL_DIR "/calculator");
    mkdirs(SKILL_DIR "/fork_skill");

    /* Skill with plain content (no frontmatter) */
    write_file(SKILL_DIR "/greeting/SKILL.md",
        "# Greeting Skill\n\nAlways greet the user by name.");

    /* Skill with YAML frontmatter */
    write_file(SKILL_DIR "/calculator/SKILL.md",
        "---\nmode: inline\ntitle: Calculator\n---\n\n"
        "# Calculator Skill\n\nPerform arithmetic on user input.");

    /* Skill with fork mode */
    write_file(SKILL_DIR "/fork_skill/SKILL.md",
        "---\nmode: fork\n---\n\n"
        "# Fork Skill\n\nRun in a separate process.");

    /* --- vs_skill_load_file --- */
    printf("  load_file:\n");

    VSSkill skill;
    int rc = vs_skill_load_file(&skill, SKILL_DIR "/greeting/SKILL.md");
    check(rc == 0, "load_file returns 0");
    check(strcmp(skill.name, "greeting") == 0, "load_file extracts name");
    check(strstr(skill.prompt, "Greeting Skill") != NULL, "load_file extracts prompt");
    check(skill.mode == VS_SKILL_MODE_INLINE, "load_file default mode inline");
    check(skill.enabled == 1, "load_file enabled");

    /* Load with frontmatter */
    rc = vs_skill_load_file(&skill, SKILL_DIR "/calculator/SKILL.md");
    check(rc == 0, "load_file with frontmatter");
    check(strcmp(skill.name, "calculator") == 0, "frontmatter skill name");
    check(strstr(skill.prompt, "Calculator Skill") != NULL, "frontmatter prompt extracted");
    check(strstr(skill.prompt, "---") == NULL, "frontmatter stripped from prompt");
    check(skill.mode == VS_SKILL_MODE_INLINE, "frontmatter inline mode");

    /* Load fork mode skill */
    rc = vs_skill_load_file(&skill, SKILL_DIR "/fork_skill/SKILL.md");
    check(rc == 0, "load_file fork skill");
    check(skill.mode == VS_SKILL_MODE_FORK, "fork mode detected");
    check(strstr(skill.prompt, "Fork Skill") != NULL, "fork skill prompt");

    /* Load nonexistent file */
    rc = vs_skill_load_file(&skill, "/nonexistent/SKILL.md");
    check(rc == -1, "load_file nonexistent returns -1");

    /* --- vs_skill_load_dir --- */
    printf("  load_dir:\n");

    memset(skills, 0, sizeof(skills));
    count = 0;
    rc = vs_skill_load_dir(skills, &count, SKILL_DIR);
    check(rc >= 0, "load_dir returns non-negative");
    check(count >= 3, "load_dir finds all skills");

    /* Check loaded skill names */
    int found_greeting = 0, found_calculator = 0, found_fork = 0;
    int i;
    for (i = 0; i < count; i++) {
        if (strcmp(skills[i].name, "greeting") == 0) found_greeting = 1;
        if (strcmp(skills[i].name, "calculator") == 0) found_calculator = 1;
        if (strcmp(skills[i].name, "fork_skill") == 0) found_fork = 1;
    }
    check(found_greeting, "load_dir includes greeting");
    check(found_calculator, "load_dir includes calculator");
    check(found_fork, "load_dir includes fork_skill");

    /* Load nonexistent directory */
    {
        int tmp_count = 0;
        VSSkill tmp_skills[VS_MAX_SKILLS];
        rc = vs_skill_load_dir(tmp_skills, &tmp_count, "/nonexistent/dir");
        check(rc == -1, "load_dir nonexistent returns -1");
    }

    /* --- vs_skill_find --- */
    printf("  find:\n");

    found = vs_skill_find(skills, count, "greeting");
    check(found != NULL, "find greeting");
    check(strcmp(found->name, "greeting") == 0, "find returns correct skill");

    found = vs_skill_find(skills, count, "nonexistent");
    check(found == NULL, "find nonexistent returns NULL");

    found = vs_skill_find(NULL, 0, "greeting");
    check(found == NULL, "find NULL skills returns NULL");

    /* --- vs_skill_execute --- */
    printf("  execute:\n");

    vs_skill_load_file(&skill, SKILL_DIR "/greeting/SKILL.md");
    result = vs_skill_execute(&skill, "Hello, my name is Alice");
    check(result != NULL, "execute returns non-NULL");
    check(strstr(result, "SKILL_INSTRUCTIONS:") != NULL, "execute includes instructions");
    check(strstr(result, "USER_INPUT:") != NULL, "execute includes user input");
    check(strstr(result, "Hello, my name is Alice") != NULL, "execute includes input text");
    free(result);

    /* Execute with NULL input */
    result = vs_skill_execute(&skill, NULL);
    check(result != NULL, "execute NULL input returns non-NULL");
    check(strstr(result, "(no input)") != NULL, "execute NULL input placeholder");
    free(result);

    /* Execute unloaded skill */
    VSSkill empty_skill;
    memset(&empty_skill, 0, sizeof(empty_skill));
    result = vs_skill_execute(&empty_skill, "test");
    check(result != NULL && strstr(result, "ERROR") != NULL, "execute unloaded returns error");
    free(result);

    /* --- vs_skill_list --- */
    printf("  list:\n");

    rc = vs_skill_list(skills, count, list_buf, sizeof(list_buf));
    check(rc >= 0, "list returns non-negative");
    check(strstr(list_buf, "greeting") != NULL, "list includes greeting");
    check(strstr(list_buf, "enabled") != NULL, "list shows enabled");

    /* List with zero capacity */
    rc = vs_skill_list(skills, count, NULL, 0);
    check(rc == -1, "list NULL buffer returns -1");

    /* --- vs_skill_free --- */
    printf("  free:\n");

    vs_skill_load_file(&skill, SKILL_DIR "/greeting/SKILL.md");
    check(skill.name[0] != 0, "skill has name before free");
    vs_skill_free(&skill);
    check(skill.name[0] == 0, "free clears name");
    check(skill.prompt[0] == 0, "free clears prompt");
    check(skill.enabled == 0, "free disables skill");

    vs_skill_free(NULL); /* Should not crash */

    /* Cleanup */
    system("rm -rf " SKILL_DIR);

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
