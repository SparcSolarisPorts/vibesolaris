/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Keybindings ---- */

void vs_keybindings_load(VSKeybinding *bindings, int *count)
{
    const char *home = getenv("HOME");
    char path[VS_MAX_PATH];
    FILE *f = NULL;

    if (!bindings || !count) return;
    *count = 0;
    memset(bindings, 0, sizeof(VSKeybinding) * VS_MAX_KEYBINDINGS);

    if (!home) return;
    snprintf(path, sizeof(path), "%s/.vibesolaris/keybindings.json", home);
    f = fopen(path, "r");
    if (!f) return;

    /* Simple JSON parser: look for {"key":"...","command":"..."} entries */
    char line[1024];
    char key[VS_KEYBINDING_KEY_MAX];
    char cmd[VS_KEYBINDING_CMD_MAX];

    while (fgets(line, sizeof(line), f)) {
        key[0] = 0;
        cmd[0] = 0;

        /* Extract "key" value */
        char *kp = strstr(line, "\"key\"");
        if (kp) {
            kp = strchr(kp + 5, ':');
            if (kp) {
                kp = strchr(kp + 1, '"');
                if (kp) {
                    char *end = strchr(kp + 1, '"');
                    if (end) {
                        size_t n = (size_t)(end - kp - 1);
                        if (n >= sizeof(key)) n = sizeof(key) - 1;
                        memcpy(key, kp + 1, n);
                        key[n] = 0;
                    }
                }
            }
        }

        /* Extract "command" value */
        char *cp = strstr(line, "\"command\"");
        if (cp) {
            cp = strchr(cp + 9, ':');
            if (cp) {
                cp = strchr(cp + 1, '"');
                if (cp) {
                    char *end = strchr(cp + 1, '"');
                    if (end) {
                        size_t n = (size_t)(end - cp - 1);
                        if (n >= sizeof(cmd)) n = sizeof(cmd) - 1;
                        memcpy(cmd, cp + 1, n);
                        cmd[n] = 0;
                    }
                }
            }
        }

        if (key[0] && cmd[0] && *count < VS_MAX_KEYBINDINGS) {
            strncpy(bindings[*count].key, key, sizeof(bindings[*count].key) - 1);
            strncpy(bindings[*count].command, cmd, sizeof(bindings[*count].command) - 1);
            (*count)++;
        }
    }

    fclose(f);
}

char *vs_keybindings_find(VSKeybinding *bindings, int count, const char *key)
{
    int i;
    if (!bindings || !key || count <= 0) return NULL;
    for (i = 0; i < count; i++) {
        if (!strcmp(bindings[i].key, key)) {
            return strdup(bindings[i].command);
        }
    }
    return NULL;
}

/* ---- PDF reading ---- */

char *vs_read_pdf(const char *path, const char *pages)
{
    char cmd[VS_MAX_PATH + 128];
    char *result;
    int status;

    if (!path) return NULL;

    /* Use pdftotext if available; fall back to error */
    if (pages && pages[0]) {
        snprintf(cmd, sizeof(cmd), "pdftotext -f %s -l %s %s - 2>/dev/null", pages, pages, path);
    } else {
        snprintf(cmd, sizeof(cmd), "pdftotext %s - 2>/dev/null", path);
    }

    result = vs_run_command(cmd, &status);
    if (status != 0 || !result || !result[0]) {
        free(result);
        /* Try alternative: pdfinfo for metadata */
        snprintf(cmd, sizeof(cmd), "pdfinfo %s 2>/dev/null", path);
        result = vs_run_command(cmd, &status);
        if (status != 0) {
            free(result);
            return strdup("ERROR: pdftotext not available or PDF could not be read. Install poppler-utils.");
        }
    }
    return result;
}
