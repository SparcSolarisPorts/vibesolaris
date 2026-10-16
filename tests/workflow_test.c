/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)

int main(void)
{
    VSWorkflow w;
    VSTaskRecord t[3];
    memset(t,0,sizeof(t));
    t[0].id=1;strcpy(t[0].title,"Inspect the project");
    t[1].id=2;t[1].parent_id=1;strcpy(t[1].title,"Review the agent loop");
    t[2].id=3;t[2].parent_id=1;t[2].dependency_count=1;t[2].dependencies[0]=2;strcpy(t[2].title,"Add the approval gate");
    vs_workflow_init(&w);
    CHECK(w.mode==VS_MODE_PLAN&&w.phase==VS_WORKFLOW_IDLE);
    CHECK(!strcmp(vs_workflow_mode_name(VS_MODE_AUTO),"Auto"));
    CHECK(vs_workflow_cycle_mode(&w)==VS_MODE_AUTO&&w.mode==VS_MODE_AUTO);
    CHECK(vs_workflow_cycle_mode(&w)==VS_MODE_PLAN&&w.mode==VS_MODE_PLAN);
    CHECK(vs_workflow_set_plan(&w,"Implement safe workflow",t,3)==0);
    CHECK(w.phase==VS_WORKFLOW_AWAITING_ACCEPT&&!vs_workflow_authorized(&w));
    CHECK(vs_workflow_accept(&w)==0&&vs_workflow_authorized(&w));
    CHECK(vs_workflow_task_ready(&w,1));
    CHECK(vs_workflow_task_ready(&w,2)); /* parent is a visual grouping, dependencies gate execution */
    CHECK(vs_workflow_set_task_status(&w,2,VS_TASK_COMPLETED,"child done")==0);
    CHECK(!vs_workflow_task_ready(&w,2));
    CHECK(vs_workflow_task_ready(&w,3));
    CHECK(vs_workflow_set_task_status(&w,3,VS_TASK_BLOCKED,"waiting")==0);
    CHECK(!vs_workflow_task_ready(&w,3));
    t[2].dependencies[0]=99;
    CHECK(vs_workflow_set_plan(&w,"Invalid dependency",t,3)!=0);
    CHECK(vs_workflow_authorized(&w)); /* invalid proposal must not overwrite accepted plan */
    t[2].dependencies[0]=2;
    CHECK(vs_workflow_set_plan(&w,"Revised plan",t,3)==0);
    CHECK(!vs_workflow_authorized(&w)&&w.phase==VS_WORKFLOW_AWAITING_ACCEPT);
    CHECK(vs_workflow_accept(&w)==0&&vs_workflow_authorized(&w));
    w.mode=VS_MODE_MANUAL;w.phase=VS_WORKFLOW_AWAITING_ACTION;
    CHECK(vs_workflow_approve_action(&w)==0&&vs_workflow_authorized(&w));
    CHECK(vs_workflow_consume_action_approval(&w)==0&&!w.action_approved&&w.pending_request[0]==0);
    vs_workflow_reject(&w);
    CHECK(!vs_workflow_authorized(&w)&&w.phase==VS_WORKFLOW_PLANNING);
    t[2].dependencies[0]=3;
    CHECK(vs_workflow_set_plan(&w,"Cycle",t,3)!=0);
    t[0].dependency_count=1;t[0].dependencies[0]=2;
    t[1].dependency_count=1;t[1].dependencies[0]=3;
    t[2].dependency_count=1;t[2].dependencies[0]=1;
    CHECK(vs_workflow_set_plan(&w,"Long dependency cycle",t,3)!=0);
    t[0].dependency_count=0;t[1].dependency_count=0;t[2].dependency_count=0;t[1].parent_id=3;t[2].parent_id=2;
    CHECK(vs_workflow_set_plan(&w,"Parent cycle",t,3)!=0);
    puts("workflow tests passed");
    return 0;
}
