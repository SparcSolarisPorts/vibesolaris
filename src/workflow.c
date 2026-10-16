/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <string.h>

static int task_index(const VSTaskRecord *tasks, int count, int id)
{
    int i;
    for (i=0;i<count;i++) if (tasks[i].id==id) return i;
    return -1;
}

static int dependency_cycle_visit(const VSTaskRecord *tasks, int count, int index, unsigned char *state)
{
    int i,dep;
    if (state[index]==1) return 1;
    if (state[index]==2) return 0;
    state[index]=1;
    for (i=0;i<tasks[index].dependency_count;i++) {
        dep=task_index(tasks,count,tasks[index].dependencies[i]);
        if (dep<0 || dependency_cycle_visit(tasks,count,dep,state)) return 1;
    }
    state[index]=2;
    return 0;
}

static int plan_has_cycles(const VSTaskRecord *tasks, int count)
{
    unsigned char state[VS_MAX_PLAN_TASKS];
    int i,parent,steps;
    memset(state,0,sizeof(state));
    for (i=0;i<count;i++) if (!state[i]&&dependency_cycle_visit(tasks,count,i,state)) return 1;
    for (i=0;i<count;i++) {
        parent=tasks[i].parent_id;
        for (steps=0;parent&&steps<=count;steps++) {
            int p=task_index(tasks,count,parent);
            if (p<0) return 1;
            if (p==i || steps==count) return 1;
            parent=tasks[p].parent_id;
        }
    }
    return 0;
}

void vs_workflow_init(VSWorkflow *w)
{
    if (!w) return;
    memset(w, 0, sizeof(*w));
    /* Plan mode preserves the explicit-review behavior by default.  Auto is
       opt-in through the mode control and accepts valid plans automatically. */
    w->mode = VS_MODE_PLAN;
    w->phase = VS_WORKFLOW_IDLE;
}

int vs_workflow_set_plan(VSWorkflow *w, const char *title, const VSTaskRecord *tasks, int count)
{
    int i,j,found;
    if (!w || !title || !*title || !tasks || count < 1 || count > VS_MAX_PLAN_TASKS) return -1;
    for (i=0;i<count;i++) {
        if (tasks[i].id <= 0 || !tasks[i].title[0] || tasks[i].dependency_count < 0 || tasks[i].dependency_count > VS_MAX_TASK_DEPS) return -1;
        for (j=0;j<i;j++) if (tasks[j].id == tasks[i].id) return -1;
        if (tasks[i].parent_id) {
            found=0;for(j=0;j<count;j++)if(tasks[j].id==tasks[i].parent_id)found=1;
            if(!found||tasks[i].parent_id==tasks[i].id)return -1;
        }
        for(j=0;j<tasks[i].dependency_count;j++) {
            int k;found=0;
            if(tasks[i].dependencies[j]==tasks[i].id)return -1;
            for(k=0;k<count;k++)if(tasks[k].id==tasks[i].dependencies[j])found=1;
            if(!found)return -1;
        }
    }
    /* Plans are untrusted input: reject dependency and parent cycles. */
    if (plan_has_cycles(tasks,count)) return -1;
    memset(w->tasks,0,sizeof(w->tasks));
    memcpy(w->tasks,tasks,(size_t)count*sizeof(tasks[0]));
    w->task_count=count;
    strncpy(w->title,title,sizeof(w->title)-1);w->title[sizeof(w->title)-1]=0;
    w->revision++;
    if(!w->revision)w->revision=1;
    w->accepted_revision=0;
    w->current_task_id=0;
    w->action_approved=0;
    w->phase=VS_WORKFLOW_AWAITING_ACCEPT;
    return 0;
}

int vs_workflow_accept(VSWorkflow *w)
{
    if(!w||w->phase!=VS_WORKFLOW_AWAITING_ACCEPT||w->task_count<=0||!w->revision)return -1;
    w->accepted_revision=w->revision;
    w->action_approved=0;
    w->phase=VS_WORKFLOW_DOING;
    return 0;
}

void vs_workflow_reject(VSWorkflow *w)
{
    if(!w)return;
    w->accepted_revision=0;
    w->current_task_id=0;
    w->pending_request[0]=0;
    w->phase=VS_WORKFLOW_PLANNING;
}

const char *vs_workflow_mode_name(VSExecutionMode mode)
{
    if(mode==VS_MODE_AUTO)return "Auto";
    if(mode==VS_MODE_PLAN)return "Plan";
    return "Manual";
}

int vs_workflow_cycle_mode(VSWorkflow *w)
{
    if(!w)return -1;
    if(w->mode==VS_MODE_AUTO)w->mode=VS_MODE_PLAN;
    else w->mode=VS_MODE_AUTO;
    w->action_approved=0;
    w->pending_request[0]=0;
    if(w->phase==VS_WORKFLOW_AWAITING_ACTION)w->phase=VS_WORKFLOW_DOING;
    return (int)w->mode;
}

void vs_workflow_clear_pending_task(VSContext *ctx)
{
    if(!ctx)return;
    ctx->pending_task[0]=0;
    ctx->pending_task_revision=0;
    ctx->pending_task_valid=0;
}

int vs_workflow_take_pending_task(VSContext *ctx, char *out, size_t cap)
{
    if(!ctx||!out||cap==0||!ctx->pending_task_valid||
       ctx->pending_task_revision!=ctx->workflow.revision||
       ctx->workflow.phase!=VS_WORKFLOW_DOING)return -1;
    strncpy(out,ctx->pending_task,cap-1);out[cap-1]=0;
    vs_workflow_clear_pending_task(ctx);
    return out[0]?0:-1;
}

int vs_workflow_authorized(const VSWorkflow *w)
{
    return w && (w->phase==VS_WORKFLOW_DOING||w->phase==VS_WORKFLOW_AWAITING_ACTION) && w->revision!=0 && w->accepted_revision==w->revision;
}

int vs_workflow_needs_action_approval(const VSWorkflow *w)
{
    return vs_workflow_authorized(w) && w->mode==VS_MODE_MANUAL && !w->action_approved;
}

int vs_workflow_approve_action(VSWorkflow *w)
{
    if(!vs_workflow_authorized(w)||w->mode!=VS_MODE_MANUAL||w->phase!=VS_WORKFLOW_AWAITING_ACTION)return -1;
    w->phase=VS_WORKFLOW_DOING;
    w->action_approved=1;
    return 0;
}

int vs_workflow_task_ready(const VSWorkflow *w,int id)
{
    int i,j,found;
    if(!w)return 0;
    for(i=0;i<w->task_count;i++)if(w->tasks[i].id==id){
        if(w->tasks[i].status==VS_TASK_CANCELLED||w->tasks[i].status==VS_TASK_FAILED||w->tasks[i].status==VS_TASK_BLOCKED||w->tasks[i].status==VS_TASK_COMPLETED)return 0;
        for(j=0;j<w->tasks[i].dependency_count;j++){
            found=0;for(int k=0;k<w->task_count;k++)if(w->tasks[k].id==w->tasks[i].dependencies[j]){found=w->tasks[k].status==VS_TASK_COMPLETED;break;}
            if(!found)return 0;
        }
        return 1;
    }
    return 0;
}

int vs_workflow_set_task_status(VSWorkflow *w,int id,VSTaskStatus status,const char *summary)
{
    int i;
    if(!w||status<VS_TASK_PLANNED||status>VS_TASK_CANCELLED)return -1;
    for(i=0;i<w->task_count;i++)if(w->tasks[i].id==id){
        w->tasks[i].status=status;
        if(summary){strncpy(w->tasks[i].summary,summary,sizeof(w->tasks[i].summary)-1);w->tasks[i].summary[sizeof(w->tasks[i].summary)-1]=0;}
        w->current_task_id=id;
        return 0;
    }
    return -1;
}

int vs_workflow_consume_action_approval(VSWorkflow *w)
{
    if(!w||w->mode!=VS_MODE_MANUAL||!w->action_approved)return -1;
    w->action_approved=0;
    w->pending_request[0]=0;
    return 0;
}
