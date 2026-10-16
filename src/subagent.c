/* SPDX-License-Identifier: Unlicense */
/* Subagents: focused, isolated agent runs delegated by a parent agent.

   A subagent is a child VSContext created by copying the parent's provider
   and tool configuration, then clearing everything that owns heap memory or
   belongs to the parent's live session (history, trace, file cache, MCP
   processes, attachments, token counters).  The child runs the normal
   vs_agent_turn loop.  Its trace events are forwarded to the parent with a
   [sub#N dK] prefix so the TUI and GUI can show what delegated work is doing. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

/* Protect root lifecycle records and shared parent trace from concurrent workers. */
static pthread_mutex_t subagent_lock = PTHREAD_MUTEX_INITIALIZER;

static char *dupstr(const char *s)
{
    size_t n=s?strlen(s):0;char *p=(char*)malloc(n+1);
    if(!p)return NULL;
    if(s)memcpy(p,s,n);
    p[n]=0;return p;
}

static void forward_child_trace(void *userdata,int step,const char *kind,const char *detail)
{
    VSContext *child=(VSContext*)userdata;
    VSContext *parent;
    char *d;size_t n;
    (void)step;
    if(!child)return;
    parent=(VSContext*)child->subagent_parent;
    if(!parent)return;
    n=strlen(detail?detail:"")+64;
    d=(char*)malloc(n);
    if(!d){if(!parent->subagent_parent)pthread_mutex_lock(&subagent_lock);vs_trace(parent,kind,detail);if(!parent->subagent_parent)pthread_mutex_unlock(&subagent_lock);return;}
    snprintf(d,n,"[sub#%d d%d] %s",child->subagent_id,child->subagent_depth,detail?detail:"");
    if(!parent->subagent_parent)pthread_mutex_lock(&subagent_lock);
    vs_trace(parent,kind,d);
    if(!parent->subagent_parent)pthread_mutex_unlock(&subagent_lock);
    free(d);
}

/* Reset every field that owns memory or live parent state, so the child never
   frees or shares a pointer that belongs to the parent. */
static void isolate_child(VSContext *child)
{
    memset(child->history,0,sizeof(child->history));
    child->history_count=0;child->history_bytes=0;child->history_evicted=0;
    memset(child->agent_history,0,sizeof(child->agent_history));
    child->agent_history_count=0;child->agent_history_bytes=0;
    child->agent_checkpoint=NULL;child->agent_checkpoint_bytes=0;
    memset(child->trace,0,sizeof(child->trace));
    child->trace_count=0;child->trace_dropped=0;child->trace_bytes=0;
    memset(child->file_cache,0,sizeof(child->file_cache));
    child->file_cache_count=0;child->file_cache_next=0;
    child->file_cache_hits=0;child->file_cache_misses=0;
    memset(child->mcp_servers,0,sizeof(child->mcp_servers));
    child->mcp_server_count=0;
    memset(child->mcp_tools,0,sizeof(child->mcp_tools));
    child->mcp_tool_count=0;
    memset(child->attachments,0,sizeof(child->attachments));
    child->attachment_count=0;child->attachment_send_from=0;
    child->provider_cached_tokens=0;child->provider_cache_write_tokens=0;
    child->provider_input_tokens=0;child->provider_output_tokens=0;child->provider_total_tokens=0;
    child->conversation_input_tokens=0;child->conversation_output_tokens=0;
    child->conversation_total_tokens=0;child->conversation_usage_responses=0;
    /* The child must not rewrite the encrypted config; the parent owns persistence. */
    child->config_autosave=0;child->config_loading=0;
    child->cancel_requested=0;   /* cancellation is inherited through subagent_parent */
    memset(child->subagents,0,sizeof(child->subagents));child->subagent_record_count=0;
    child->subagent_root=NULL;child->subagent_record_id=0;
    child->trace_callback=forward_child_trace;
    child->trace_callback_data=child;
    child->stream_callback=NULL;
    child->stream_callback_data=NULL;
}

char *vs_subagent_request_prompt(const char *task)
{
    static const char lead[]=
        "Delegate the following task to a subagent. Emit one [[VS_TOOL subagent task=\"...\"]] directive "
        "whose task text is the full task below (escape double quotes as \\\"). When the subagent result "
        "comes back, report its findings concisely.\n\nTASK:\n";
    char *o;size_t n;
    n=strlen(lead)+strlen(task?task:"")+8;o=(char*)malloc(n);
    if(!o)return NULL;
    snprintf(o,n,"%s%s",lead,task?task:"");
    return o;
}

char *vs_subagent_run(VSContext *parent,const char *task)
{
    VSContext *child,*root;char *prompt,*ans,*out,detail[512];size_t n;int id,record_index=-1;VSSubagentRecord *record=NULL;
    if(!parent)return dupstr("ERROR: subagent has no parent agent");
    if(!task||!*task)return dupstr("ERROR: subagent requires task=\"...\"");
    if(vs_cancel_requested(parent))return dupstr("Stopped by user.");
    if(parent->subagent_depth>=VS_MAX_SUBAGENT_DEPTH)
        return dupstr("ERROR: subagent nesting limit reached; complete this part directly.");
    root=parent->subagent_root?(VSContext*)parent->subagent_root:parent;
    pthread_mutex_lock(&subagent_lock);
    if(root->subagent_serial>=VS_MAX_SUBAGENTS_PER_TURN){
        pthread_mutex_unlock(&subagent_lock);
        return dupstr("ERROR: subagent limit for this turn reached; continue the remaining work directly.");
    }
    pthread_mutex_unlock(&subagent_lock);

    child=(VSContext*)calloc(1,sizeof(VSContext));
    if(!child)return dupstr("ERROR: out of memory starting subagent");
    if(!parent->subagent_parent)pthread_mutex_lock(&subagent_lock);
    memcpy(child,parent,sizeof(VSContext));
    if(!parent->subagent_parent)pthread_mutex_unlock(&subagent_lock);
    isolate_child(child);

    pthread_mutex_lock(&subagent_lock);
    if(root->subagent_serial>=VS_MAX_SUBAGENTS_PER_TURN){pthread_mutex_unlock(&subagent_lock);free(child);return dupstr("ERROR: subagent limit for this turn reached");}
    if(parent!=root)parent->subagent_serial++;
    id=++root->subagent_serial;
    if(root->subagent_record_count<VS_MAX_SUBAGENT_RECORDS){
        record_index=root->subagent_record_count++;
        record=&root->subagents[record_index];
        memset(record,0,sizeof(*record));record->id=id;record->parent_id=parent->subagent_id;
        record->depth=parent->subagent_depth+1;record->status=0;record->started_at=(long)time(NULL);
        child->subagent_record_id=id;
        snprintf(record->task,sizeof(record->task),"%.255s",task);
        snprintf(record->summary,sizeof(record->summary),"running");
    }
    pthread_mutex_unlock(&subagent_lock);
    child->subagent_parent=parent;
    child->subagent_root=parent->subagent_root?parent->subagent_root:parent;
    child->subagent_depth=parent->subagent_depth+1;
    child->subagent_id=id;
    child->subagent_serial=0;
    child->subagent_root=root;

    n=strlen(task)+640;
    prompt=(char*)malloc(n);
    if(!prompt){pthread_mutex_lock(&subagent_lock);if(record){record->status=3;record->finished_at=(long)time(NULL);snprintf(record->summary,sizeof(record->summary),"unable to prepare task");}pthread_mutex_unlock(&subagent_lock);free(child);return dupstr("ERROR: out of memory preparing subagent");}
    snprintf(prompt,n,
        "SUBAGENT TASK. You are a focused subagent delegated by the parent agent. Complete only this task "
        "using the available tools. Coordinate through explicit handoffs: first establish a bounded "
        "scope, avoid changing files outside that scope, and inspect relevant files before editing. "
        "After editing, run available compile/tests or explain why verification was impossible. "
        "End with a structured handoff headed FILES CHANGED, CHECKS RUN, RESULTS, "
        "REMAINING RISKS. Do not claim success without evidence. "
        "Do not ask the user questions; if blocked, say what is blocked and stop.\n\nTASK:\n%s",task);

    snprintf(detail,sizeof(detail),"subagent %d started (depth %d): %.300s",id,child->subagent_depth,task);
    if(!parent->subagent_parent)pthread_mutex_lock(&subagent_lock);
    vs_trace(parent,"subagent-start",detail);
    if(!parent->subagent_parent)pthread_mutex_unlock(&subagent_lock);

    ans=vs_agent_turn(child,prompt);
    free(prompt);

    pthread_mutex_lock(&subagent_lock);
    if(record_index>=0&&root->subagent_record_count>record_index)record=&root->subagents[record_index];
    if(record){
        record->finished_at=(long)time(NULL);
        if(vs_cancel_requested(parent)|| (ans&&strstr(ans,"Stopped by user"))){record->status=2;snprintf(record->summary,sizeof(record->summary),"stopped");}
        else if(!ans||!strncmp(ans,"ERROR:",6)||strstr(ans,"limit reached")){record->status=3;snprintf(record->summary,sizeof(record->summary),"%.255s",ans?ans:"subagent returned no result");}
        else {record->status=1;snprintf(record->summary,sizeof(record->summary),"completed; see handoff");}
    }
    pthread_mutex_unlock(&subagent_lock);
    snprintf(detail,sizeof(detail),"subagent %d finished",id);
    if(!parent->subagent_parent)pthread_mutex_lock(&subagent_lock);
    vs_trace(parent,"subagent-done",detail);
    if(!parent->subagent_parent)pthread_mutex_unlock(&subagent_lock);

    /* The child owns its own history, trace, and file cache; release them. */
    vs_history_clear(child);
    vs_cache_clear(child);
    vs_trace_clear(child);
    free(child);

    if(!ans)ans=dupstr("(subagent produced no result)");
    n=strlen(ans)+96;
    out=(char*)malloc(n);
    if(!out){free(ans);return dupstr("ERROR: out of memory returning subagent result");}
    snprintf(out,n,"SUBAGENT_RESULT %d:\n%s",id,ans);
    free(ans);
    {
        char *bounded=vs_compact_text_limit(out,VS_SUBAGENT_RESULT_MAX,"subagent result compacted");
        if(bounded){free(out);out=bounded;}
    }
    return out;
}

/* Maps a trace event to a short activity label for the live indicator.
   Returns 1 and fills out when the event changes the phase, 0 otherwise.
   Events forwarded from a subagent (detail begins "[sub#") get a "Subagent: "
   prefix so the user can see delegated work distinctly. */
int vs_trace_phase(const char *kind,const char *detail,char *out,size_t cap)
{
    const char *base=NULL;int sub;
    if(!kind||!out||cap==0)return 0;
    if(!strcmp(kind,"model")||!strcmp(kind,"model-wait")||!strcmp(kind,"http-wait")||!strcmp(kind,"reasoning")||!strcmp(kind,"subagent-done"))base="Thinking";
    else if(!strcmp(kind,"tool-batch")||!strcmp(kind,"tool-health"))base="Running tools";
    else if(!strcmp(kind,"subagent-start"))base="Subagent starting";
    else if(!strcmp(kind,"compact"))base="Compacting context";
    else return 0;
    sub=detail&&!strncmp(detail,"[sub#",5);
    if(sub)snprintf(out,cap,"Subagent: %s",base);
    else snprintf(out,cap,"%s",base);
    return 1;
}
