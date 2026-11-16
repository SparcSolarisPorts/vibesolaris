/* SPDX-License-Identifier: Unlicense */
/* TUI Suggestions engine: context-aware command and file suggestions */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

void vs_tui_suggestions_init(VSTUISuggestion *suggestions, int *count)
{
    if (count) *count = 0;
    if (suggestions) memset(suggestions, 0, sizeof(VSTUISuggestion));
}

int vs_tui_suggestions_add(VSTUISuggestion *suggestions, int *count, const char *text, int type)
{
    if (!suggestions || !count || !text || *count >= VS_TUI_SUGGESTION_MAX) return -1;
    VSTUISuggestion *s = &suggestions[*count];
    strncpy(s->text, text, VS_TUI_SUGGESTION_TEXT_MAX - 1);
    s->text[VS_TUI_SUGGESTION_TEXT_MAX - 1] = 0;
    s->score = 0;
    s->type = type;
    (*count)++;
    return 0;
}

/* Fuzzy match: returns score (higher = better), 0 = no match */
int vs_tui_suggestions_fuzzy_match(const char *input, const char *candidate)
{
    if (!input || !candidate || !*input || !*candidate) return 0;

    int score = 0;
    const char *p = input;
    const char *q = candidate;
    int consecutive = 0;
    int start_match = 0;

    /* Check if candidate starts with input (highest priority) */
    if (strncasecmp(p, q, strlen(p)) == 0) {
        start_match = 1;
    }

    /* Fuzzy matching */
    int matched = 0;
    while (*p && *q) {
        if (tolower((unsigned char)*p) == tolower((unsigned char)*q)) {
            matched++;
            consecutive++;
            /* Bonus for consecutive matches */
            score += 2 + consecutive;
            /* Bonus for matching at word boundary */
            if (q == candidate || * (q - 1) == ' ' || *(q - 1) == '-' || *(q - 1) == '_') {
                score += 3;
            }
            p++;
        } else {
            consecutive = 0;
            q++;
        }
    }

    /* All input chars must match */
    if (*p != 0) return 0;

    /* Bonus for start match */
    if (start_match) score += 10;

    /* Penalty for longer candidates (prefer shorter matches) */
    score -= (int)strlen(candidate) / 2;

    return score > 0 ? score : 0;
}

int vs_tui_suggestions_match(VSTUISuggestion *suggestions, int count, const char *input,
                              VSTUISuggestion *results, int max_results)
{
    if (!suggestions || !input || !results || max_results <= 0) return 0;

    /* Score all suggestions */
    for (int i = 0; i < count; i++) {
        suggestions[i].score = vs_tui_suggestions_fuzzy_match(input, suggestions[i].text);
    }

    /* Sort by score (simple insertion sort) */
    for (int i = 1; i < count; i++) {
        VSTUISuggestion tmp = suggestions[i];
        int j = i - 1;
        while (j >= 0 && suggestions[j].score < tmp.score) {
            suggestions[j + 1] = suggestions[j];
            j--;
        }
        suggestions[j + 1] = tmp;
    }

    /* Copy top results */
    int n = 0;
    for (int i = 0; i < count && n < max_results; i++) {
        if (suggestions[i].score > 0) {
            results[n++] = suggestions[i];
        }
    }
    return n;
}

void vs_tui_suggestions_render(const VSTUISuggestion *results, int count, int width)
{
    if (!results || count <= 0 || width < 4) return;

    /* Find max width of suggestions */
    int max_w = 0;
    for (int i = 0; i < count; i++) {
        int w = (int)strlen(results[i].text);
        if (w > max_w) max_w = w;
    }
    if (max_w > width - 4) max_w = width - 4;

    /* Calculate columns */
    int cols = width / (max_w + 2);
    if (cols < 1) cols = 1;
    if (cols > 8) cols = 8;

    /* Render in columns */
    int rows = (count + cols - 1) / cols;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = c * rows + r;
            if (idx >= count) break;

            if (c > 0) {
                /* Padding between columns */
                printf("%*s", 2, "");
            }

            const char *text = results[idx].text;
            const char *color = "";
            const char *reset = "\033[0m";

            /* Color by type */
            switch (results[idx].type) {
                case 0: color = "\033[36m"; break; /* command: cyan */
                case 1: color = "\033[33m"; break; /* file: yellow */
                case 2: color = "\033[34m"; break; /* path: blue */
                case 3: color = "\033[35m"; break; /* tool: magenta */
                default: color = "\033[37m"; break;
            }

            /* Highlight first item */
            if (idx == 0) {
                printf("\033[1m%s%s.%s", color, text, reset);
            } else {
                printf("%s%s%s", color, text, reset);
            }
        }
        printf("\n");
    }
}
