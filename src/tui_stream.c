/* SPDX-License-Identifier: Unlicense */
#include "tui_stream.h"
#include <stdlib.h>
#include <string.h>

#define VS_TUI_STREAM_MAX (128U*1024U)

void vs_tui_stream_init(VSTUIStream *stream)
{
    if(!stream)return;
    memset(stream,0,sizeof(*stream));
}

void vs_tui_stream_reset(VSTUIStream *stream)
{
    if(!stream)return;
    stream->len=0;
    stream->seen=0;
    stream->overflow=0;
    if(stream->data)stream->data[0]=0;
}

void vs_tui_stream_free(VSTUIStream *stream)
{
    if(!stream)return;
    free(stream->data);
    memset(stream,0,sizeof(*stream));
}

int vs_tui_stream_append(VSTUIStream *stream,const char *data,size_t len)
{
    size_t need,cap;
    char *p;
    if(!stream||!data||!len)return 0;
    stream->seen=1;
    if(stream->overflow)return -1;
    if(len>VS_TUI_STREAM_MAX-stream->len-1){stream->overflow=1;return -1;}
    need=stream->len+len+1;
    if(need>stream->cap){
        cap=stream->cap?stream->cap:4096U;
        while(cap<need){
            if(cap>=VS_TUI_STREAM_MAX){cap=VS_TUI_STREAM_MAX;break;}
            cap*=2;
        }
        if(cap<need){stream->overflow=1;return -1;}
        p=(char*)realloc(stream->data,cap);
        if(!p){stream->overflow=1;return -1;}
        stream->data=p;stream->cap=cap;
    }
    memcpy(stream->data+stream->len,data,len);
    stream->len+=len;stream->data[stream->len]=0;
    return 0;
}

VSTUIRenderDecision vs_tui_stream_reconcile(const VSTUIStream *stream,
                                             const char *answer,
                                             size_t *suffix_offset)
{
    size_t n;
    if(suffix_offset)*suffix_offset=0;
    if(!answer||!*answer)return VS_TUI_RENDER_FINAL;
    if(!stream||!stream->seen||stream->overflow||!stream->data)return VS_TUI_RENDER_FINAL;
    n=strlen(answer);
    if(n==stream->len&&!memcmp(answer,stream->data,n))return VS_TUI_RENDER_EXACT;
    if(n>stream->len&&!memcmp(answer,stream->data,stream->len)){
        if(suffix_offset)*suffix_offset=stream->len;
        return VS_TUI_RENDER_SUFFIX;
    }
    return VS_TUI_RENDER_FINAL;
}

const char *vs_tui_stream_select_answer(const VSTUIStream *stream,
                                        const char *answer,
                                        int cancelled)
{
    if(cancelled)return answer&&*answer?answer:"Stopped by user.";
    if(answer&&*answer)return answer;
    if(stream&&stream->seen&&!stream->overflow&&stream->data&&stream->len)
        return stream->data;
    return "(no response)";
}
