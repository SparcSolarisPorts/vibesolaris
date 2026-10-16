/* SPDX-License-Identifier: Unlicense */
#ifndef VIBESOLARIS_TUI_STREAM_H
#define VIBESOLARIS_TUI_STREAM_H

#include <stddef.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    int seen;
    int overflow;
} VSTUIStream;

typedef enum {
    VS_TUI_RENDER_FINAL = 0,
    VS_TUI_RENDER_EXACT = 1,
    VS_TUI_RENDER_SUFFIX = 2
} VSTUIRenderDecision;

void vs_tui_stream_init(VSTUIStream *stream);
void vs_tui_stream_reset(VSTUIStream *stream);
void vs_tui_stream_free(VSTUIStream *stream);
int vs_tui_stream_append(VSTUIStream *stream,const char *data,size_t len);
VSTUIRenderDecision vs_tui_stream_reconcile(const VSTUIStream *stream,
                                             const char *answer,
                                             size_t *suffix_offset);
const char *vs_tui_stream_select_answer(const VSTUIStream *stream,
                                        const char *answer,
                                        int cancelled);

#endif
