/* SPDX-License-Identifier: Unlicense */
/* Skill system: loads SKILL.md files from a directory, extracts the prompt,
   and executes skills either inline (injected into the model prompt) or
   via fork (running a separate agent turn with the skill's prompt).

   Mirrors Claude Code's skill system: each skill is a directory with a
   SKILL.md file containing instructions.  The agent can reference skills
   by name. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static char *dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

int vs_skill_load_file(VSSkill *skill, const char *path)
{
    FILE *f;
    char *buf;
    long size;
    struct stat st;

    memset(skill, 0, sizeof(*skill));
    skill->mode = VS_SKILL_MODE_INLINE;
    skill->enabled = 1;

    /* Extract skill name from path.
       The path is like /dir/skill_name/SKILL.md — we want "skill_name".
       Find the last /, then the one before it to get the directory component. */
    {
        const char *last_slash = strrchr(path, '/');
        if (last_slash) {
            /* Find the slash before the last component (the filename) */
            const char *prev_slash = NULL;
            const char *p = last_slash - 1;
            while (p >= path) {
                if (*p == '/') { prev_slash = p; break; }
                p--;
            }
            if (prev_slash) {
                size_t n = (size_t)(last_slash - prev_slash - 1);
                if (n >= VS_SKILL_NAME_MAX) n = VS_SKILL_NAME_MAX - 1;
                memcpy(skill->name, prev_slash + 1, n);
                skill->name[n] = 0;
            } else {
                /* No previous slash — use the component before the last */
                size_t n = (size_t)(last_slash - path);
                if (n >= VS_SKILL_NAME_MAX) n = VS_SKILL_NAME_MAX - 1;
                memcpy(skill->name, path, n);
                skill->name[n] = 0;
            }
        } else {
            strncpy(skill->name, path, VS_SKILL_NAME_MAX - 1);
        }
    }

    strncpy(skill->path, path, VS_SKILL_PATH_MAX - 1);

    /* Read SKILL.md */
    f = fopen(path, "r");
    if (!f) return -1;
    if (fstat(fileno(f), &st) != 0) { fclose(f); return -1; }
    size = st.st_size;
    if (size <= 0 || size > (long)VS_SKILL_PROMPT_MAX) { fclose(f); return -1; }

    buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return -1; }
    if ((long)fread(buf, 1, (size_t)size, f) != size) { free(buf); fclose(f); return -1; }
    buf[size] = 0;
    fclose(f);

    /* Store prompt - extract content between frontmatter if present */
    {
        const char *start = buf;
        const char *end = buf + size;

        /* Skip YAML frontmatter if present */
        if (start[0] == '-' && start[1] == '-' && start[2] == '-') {
            const char *sep = strstr(start + 3, "\n---\n");
            if (!sep) sep = strstr(start + 3, "\n---");
            if (sep) {
                start = sep + 5;
                if (start[0] == '\n') start++;
            }
        }

        size_t prompt_len = (size_t)(end - start);
        /* Trim trailing whitespace */
        while (prompt_len > 0 && (start[prompt_len - 1] == '\n' || start[prompt_len - 1] == '\r' || start[prompt_len - 1] == ' ')) {
            prompt_len--;
        }
        if (prompt_len >= VS_SKILL_PROMPT_MAX) prompt_len = VS_SKILL_PROMPT_MAX - 1;
        memcpy(skill->prompt, start, prompt_len);
        skill->prompt[prompt_len] = 0;

        /* Check for mode hint in frontmatter */
        if (strstr(buf, "mode: fork")) {
            skill->mode = VS_SKILL_MODE_FORK;
        }
    }

    free(buf);
    return 0;
}

int vs_skill_load_dir(VSSkill *skills, int *count, const char *dir)
{
    DIR *d;
    struct dirent *de;
    int n = 0;

    d = opendir(dir);
    if (!d) return -1;

    while ((de = readdir(d)) != NULL && n < VS_MAX_SKILLS) {
        if (de->d_name[0] == '.') continue;

        char skill_dir[VS_SKILL_PATH_MAX];
        char skill_file[VS_SKILL_PATH_MAX + 16];
        snprintf(skill_dir, sizeof(skill_dir), "%s/%s", dir, de->d_name);
        snprintf(skill_file, sizeof(skill_file), "%s/SKILL.md", skill_dir);

        /* Check if SKILL.md exists */
        struct stat st;
        if (stat(skill_file, &st) == 0 && S_ISREG(st.st_mode)) {
            if (vs_skill_load_file(&skills[n], skill_file) == 0) {
                n++;
            }
        }
    }
    closedir(d);

    *count = n;
    return n;
}

VSSkill *vs_skill_find(VSSkill *skills, int count, const char *name)
{
    int i;
    for (i = 0; i < count; i++) {
        if (!strcmp(skills[i].name, name)) return &skills[i];
    }
    return NULL;
}

char *vs_skill_execute(VSSkill *skill, const char *input)
{
    char *prompt;
    size_t cap;

    if (!skill || !skill->prompt[0]) return dupstr("ERROR: skill not loaded");

    cap = strlen(skill->prompt) + strlen(input ? input : "") + 256;
    prompt = (char *)malloc(cap);
    if (!prompt) return NULL;

    snprintf(prompt, cap,
        "SKILL_INSTRUCTIONS:\n%s\n\n"
        "USER_INPUT:\n%s\n\n"
        "Execute the skill with the given input.",
        skill->prompt, input ? input : "(no input)");

    return prompt;
}

void vs_skill_free(VSSkill *skill)
{
    if (!skill) return;
    skill->prompt[0] = 0;
    skill->name[0] = 0;
    skill->path[0] = 0;
    skill->mode = VS_SKILL_MODE_INLINE;
    skill->enabled = 0;
}

int vs_skill_list(VSSkill *skills, int count, char *out, size_t cap)
{
    int i;
    size_t pos = 0;

    if (!out || cap == 0) return -1;
    out[0] = 0;

    for (i = 0; i < count; i++) {
        char buf[512];
        int n = snprintf(buf, sizeof(buf),
            "%d. %-20s [%s] %s\n",
            i + 1, skills[i].name,
            skills[i].mode == VS_SKILL_MODE_FORK ? "fork" : "inline",
            skills[i].enabled ? "enabled" : "disabled");
        if (n < 0) continue;
        size_t blen = (size_t)n;
        if (pos + blen >= cap) break;
        memcpy(out + pos, buf, blen);
        pos += blen;
    }

    return (int)pos;
}

char *vs_skill_invoke(VSSkill *skills, int *count, const char *name, const char *input)
{
    VSSkill *skill;

    if (!name || !name[0]) return dupstr("ERROR: skill requires name");
    if (!skills || !count || *count <= 0) return dupstr("No skills loaded");

    skill = vs_skill_find(skills, *count, name);
    if (!skill) {
        char buf[512];
        int n = snprintf(buf, sizeof(buf), "ERROR: skill \"%s\" not found. Available skills:", name);
        char *result = (char *)malloc((size_t)n + 256);
        if (!result) return dupstr("ERROR: out of memory");
        memcpy(result, buf, (size_t)n);
        int i;
        for (i = 0; i < *count && i < 8; i++) {
            char sep[8];
            int s = snprintf(sep, sizeof(sep), "%s%s", i == 0 ? " " : ", ", skills[i].name);
            memcpy(result + n, sep, (size_t)s);
            n += s;
        }
        result[n] = 0;
        return result;
    }

    return vs_skill_execute(skill, input);
}
