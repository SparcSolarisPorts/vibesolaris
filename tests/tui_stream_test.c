/* SPDX-License-Identifier: Unlicense */
#include "../src/tui_stream.h"
#include <stdio.h>
#include <string.h>

static int failures=0;
static void check(int ok,const char *name)
{
    if(ok)printf("ok: %s\n",name);
    else {printf("FAIL: %s\n",name);failures++;}
}

int main(void)
{
    VSTUIStream s;
    size_t off;
    vs_tui_stream_init(&s);

    check(vs_tui_stream_reconcile(&s,"answer",&off)==VS_TUI_RENDER_FINAL,
          "empty capture requires authoritative answer");
    check(!strcmp(vs_tui_stream_select_answer(&s,"answer",0),"answer") &&
          !strcmp(vs_tui_stream_select_answer(&s,"",0),"(no response)"),
          "authoritative and empty answers resolve consistently");
    check(vs_tui_stream_append(&s,"answer",6)==0 &&
          vs_tui_stream_reconcile(&s,"answer",&off)==VS_TUI_RENDER_EXACT,
          "exact streamed answer is not duplicated");
    vs_tui_stream_reset(&s);
    check(vs_tui_stream_append(&s,"ans",3)==0 &&
          vs_tui_stream_append(&s,"wer",3)==0 &&
          !strcmp(vs_tui_stream_select_answer(&s,"",0),"answer"),
          "empty authoritative answer falls back to bounded capture");
    vs_tui_stream_reset(&s);
    check(vs_tui_stream_append(&s,"part",4)==0 &&
          vs_tui_stream_reconcile(&s,"partial",&off)==VS_TUI_RENDER_SUFFIX && off==4,
          "authoritative suffix follows streamed prefix");
    vs_tui_stream_reset(&s);
    check(vs_tui_stream_append(&s,"directive",9)==0 &&
          vs_tui_stream_reconcile(&s,"clean final",&off)==VS_TUI_RENDER_FINAL,
          "divergent intermediate round does not suppress final");
    vs_tui_stream_reset(&s);
    check(vs_tui_stream_reconcile(&s,"",&off)==VS_TUI_RENDER_FINAL,
          "empty authoritative answer is rendered by caller");
    vs_tui_stream_reset(&s);
    {
        char large[128U*1024U+2];
        memset(large,'x',sizeof(large));
        check(vs_tui_stream_append(&s,large,sizeof(large))<0 && s.overflow,
              "oversized capture becomes non-authoritative");
        check(vs_tui_stream_reconcile(&s,"complete",&off)==VS_TUI_RENDER_FINAL,
              "overflow requires complete authoritative answer");
        check(!strcmp(vs_tui_stream_select_answer(&s,"",0),"(no response)"),
              "overflow never becomes an answer fallback");
        check(!strcmp(vs_tui_stream_select_answer(&s,"Stopped by user.",1),"Stopped by user."),
              "cancellation takes precedence over captured text");
    }
    vs_tui_stream_free(&s);
    if(failures){printf("%d failure(s)\n",failures);return 1;}
    puts("all TUI stream tests passed");
    return 0;
}
