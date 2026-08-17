/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dupstr(const char *s){size_t n=s?strlen(s):0;char *p=(char*)malloc(n+1);if(!p)return 0;if(s)memcpy(p,s,n);p[n]=0;return p;}
static char *attr(const char *s,const char *name){char pat[64],*p,*e,*o;size_t n;snprintf(pat,sizeof(pat),"%s=\"",name);p=strstr((char*)s,pat);if(!p)return 0;p+=strlen(pat);e=p;while(*e && !(*e=='"' && (e==p || e[-1]!='\\')))e++;n=(size_t)(e-p);o=(char*)malloc(n+1);if(!o)return 0;memcpy(o,p,n);o[n]=0;return o;}
static void unesc(char *s){char *r=s,*w=s;while(*r){if(*r=='\\'&&r[1]){r++;if(*r=='n')*w++='\n';else if(*r=='t')*w++='\t';else if(*r=='r')*w++='\r';else *w++=*r;r++;}else *w++=*r++;}*w=0;}


/* Models occasionally narrate a plan instead of executing it.  Keep this
   deliberately small and ASCII-only for old C/Solaris toolchains. */
static int ascii_contains_ci(const char *s,const char *needle){
    size_t i,n;if(!s||!needle||!*needle)return 0;n=strlen(needle);
    for(;*s;s++){
        for(i=0;i<n;i++){
            unsigned char a=(unsigned char)s[i],b=(unsigned char)needle[i];
            if(!a)return 0;
            if(a>='A'&&a<='Z')a=(unsigned char)(a-'A'+'a');
            if(b>='A'&&b<='Z')b=(unsigned char)(b-'A'+'a');
            if(a!=b)break;
        }
        if(i==n)return 1;
    }
    return 0;
}
static int execution_intent(const char *s){
    static const char *k[]={" add "," make "," fix "," edit "," update "," modify "," implement "," build "," create "," write "," change "," run "," test "," debug "," compile "," install "," remove "," replace "," refactor "," convert "," do all "," do that "," do it "," go ahead "," proceed "," continue "," keep going "," carry on ",0};
    char *q;size_t n;int i;if(!s)return 0;n=strlen(s);q=(char*)malloc(n+3);if(!q)return 0;q[0]=' ';memcpy(q+1,s,n);q[n+1]=' ';q[n+2]=0;
    for(i=0;k[i];i++)if(ascii_contains_ci(q,k[i])){free(q);return 1;}
    free(q);return 0;
}
static int prospective_language(const char *s){
    static const char *k[]={"i'll ","i will ","i am going to ","i'm going to ","going to inspect","going to check","going to edit","going to update","going to fix","going to run","let me inspect","let me check","let me edit","let me update","let me fix","next i will","next, i will","first i will","first, i will","i will now","will now inspect","will now edit","will now run",0};int i;
    if(!s)return 0;
    for(i=0;k[i];i++)if(ascii_contains_ci(s,k[i]))return 1;
    return 0;
}
static int false_tool_unavailable_claim(const char *s){
    static const char *k[]={
        "no tool_result","no tool result","tool_result channel","tool result channel",
        "vs_tool result channel","restore the vs_tool","restore vs_tool",
        "cannot run commands","can't run commands","unable to run commands",
        "cannot execute commands","can't execute commands","unable to execute commands",
        "can no longer run commands","command execution is unavailable",
        "command tool is unavailable","local command tool is unavailable",
        "command runner is unavailable","tooling is unavailable",
        "command directives are currently returning no",
        "not returning any tool_result","not returning tool_result",
        "not returning any tool result","not returning tool result",
        "re-enable the workspace tool bridge","reenable the workspace tool bridge",
        "re-enable the tool bridge","reenable the tool bridge",
        "workspace tool bridge","tool bridge is broken","tool bridge unavailable",
        "tool bridge stopped","tool bridge has stopped","bridge stopped returning",
        "tool stopped returning","tools stopped returning","command results stopped",
        "no results after","results are not coming back","results are no longer",
        "please restore the tool bridge","provide the command results so i can continue",0
    };int i,toolish,blocked;if(!s)return 0;for(i=0;k[i];i++)if(ascii_contains_ci(s,k[i]))return 1;
    toolish=ascii_contains_ci(s,"tool")||ascii_contains_ci(s,"command")||ascii_contains_ci(s,"vs_tool")||ascii_contains_ci(s,"tool_result")||ascii_contains_ci(s,"workspace bridge");
    blocked=ascii_contains_ci(s,"not returning")||ascii_contains_ci(s,"stopped returning")||ascii_contains_ci(s,"no result")||ascii_contains_ci(s,"no results")||ascii_contains_ci(s,"unavailable")||ascii_contains_ci(s,"cannot continue")||ascii_contains_ci(s,"can't continue")||ascii_contains_ci(s,"unable to continue")||ascii_contains_ci(s,"restore")||ascii_contains_ci(s,"re-enable")||ascii_contains_ci(s,"reenable")||ascii_contains_ci(s,"broken");
    return toolish&&blocked;
}
static int should_auto_continue(const char *original,const char *answer){
    if(!execution_intent(original)||!prospective_language(answer))return 0;
    if(ascii_contains_ci(answer,"if you want")||ascii_contains_ci(answer,"if you'd like"))return 0;
    return 1;
}
static char *strip_marker(const char *s,const char *marker){
    const char *p;char *o;size_t a,b;if(!s)return dupstr("");p=strstr(s,marker);if(!p)return dupstr(s);a=(size_t)(p-s);b=strlen(p+strlen(marker));o=(char*)malloc(a+b+1);if(!o)return 0;memcpy(o,s,a);memcpy(o+a,p+strlen(marker),b+1);return o;
}

static char *continuation_prompt(const char *task,const char *result,int planning_only){
    const char *tail="Continue executing the ACTIVE_TASK autonomously. TOOL_RESULT is ordinary text delivered in this continuation message; there is no separate hidden tool-result transport/channel that the user must restore. If COMMAND_RUNNER_STATUS is operational, the local command runner is confirmed working even when COMMAND_EXIT_STATUS is non-zero, the shell reports a syntax error, a compiler fails, or a command times out. Inspect the result, correct the command/problem, and run another command when useful. Do not stop merely to describe what you will do next and do not ask the user to send another prompt just to continue. Use VS_TOOL or VS_MCP directives as needed. Ask the user only if genuinely blocked by task information, credentials, or permission, using [[VS_NEED_USER question=\"YOUR QUESTION\"]]. When the requested task is actually complete, emit [[VS_FINAL]] followed by the concise result. Never combine VS_FINAL with VS_TOOL or VS_MCP in one response; pending executable directives always run first.";
    const char *lead=planning_only?"AUTONOMY_CONTINUE: the previous response did not complete the execution protocol. Do not return prose-only status. Either execute the next concrete step with VS_TOOL/VS_MCP, emit VS_NEED_USER for a genuine blocker, or emit VS_FINAL only if the requested work is actually complete.":"TOOL_RESULT (delivered successfully by the host):";
    size_t n=strlen(task?task:"")+strlen(result?result:"")+strlen(lead)+strlen(tail)+192;
    char *o=(char*)malloc(n);
    if(!o)return NULL;
    if(planning_only)snprintf(o,n,"ACTIVE_TASK (persistent anchor; do not ask the user to restate it):\n%s\n\n%s\n\n%s",task?task:"",lead,tail);
    else snprintf(o,n,"ACTIVE_TASK (persistent anchor; do not ask the user to restate it):\n%s\n\n%s\n%s\n\n%s",task?task:"",lead,result?result:"",tail);
    return o;
}

static char *tool_channel_recovery_prompt(const char *task,const char *last_result,const char *claim,int actual_tool_seen){
    const char *tail="The host independently verified that the local command runner is operational. Do not ask the user to restore VS_TOOL, TOOL_RESULT, or a workspace/tool bridge. If you want to run a command, you MUST actually emit a directive such as [[VS_TOOL run cmd=\"pwd && uname -a\"]] on its own line and wait for the host result. Never claim that a command produced no result when you did not emit a VS_TOOL directive. A shell/compiler failure is a normal result to inspect and correct. Continue the ACTIVE_TASK now using VS_TOOL or VS_MCP as needed. Ask the user only for genuinely missing task information, credentials, destructive-action permission, or a decision that cannot safely be inferred. Emit [[VS_FINAL]] only after the requested work is actually complete, and never in the same response as VS_TOOL or VS_MCP.";
    const char *seen=actual_tool_seen?"A real VS_TOOL/VS_MCP request had previously been executed in this turn.":"IMPORTANT: no VS_TOOL or VS_MCP directive was present in the model response that made this claim. The model therefore did not actually attempt the command it says failed.";
    size_t n=strlen(task?task:"")+strlen(last_result?last_result:"")+strlen(claim?claim:"")+strlen(tail)+strlen(seen)+512;
    char *o=(char*)malloc(n);
    if(!o)return NULL;
    snprintf(o,n,"ACTIVE_TASK (persistent anchor):\n%s\n\nTOOL_CHANNEL_RECOVERY:\nThe prior model response incorrectly claimed that command/tool results were unavailable. The host independently ran a local command-runner health probe and it succeeded.\n%s\n\nHOST_HEALTH_PROBE_RESULT:\nCOMMAND_RUNNER_STATUS: operational\nTOOL_RESULT_DELIVERED: yes\nCOMMAND_TOOL_AVAILABLE: yes\n\nLAST_DELIVERED_TOOL_RESULT:\n%s\n\nPREVIOUS_MODEL_CLAIM (do not repeat it):\n%s\n\n%s",task?task:"",seen,last_result?last_result:"(no earlier model-issued tool result in this turn)",claim?claim:"",tail);
    return o;
}


char *vs_agent_turn(VSContext *c,const char *user){
    char *prompt=dupstr(user),*ans=0,*original=dupstr(user),*task_anchor=0,*last_tool_result=0;int round,plan_continues=0,tool_recoveries=0,command_runner_confirmed=0,actual_tool_seen=0,execution_mode=0,max_rounds=VS_MAX_TOOL_ROUNDS;
    char step[512];
    {const char *mr=getenv("VIBESOLARIS_MAX_AGENT_ROUNDS");if(mr&&*mr){char *ep=0;long v=strtol(mr,&ep,10);if(ep&&*ep==0&&v>=8&&v<=256)max_rounds=(int)v;}}
    if(!prompt||!original){free(prompt);free(original);return dupstr("out of memory");}
    task_anchor=vs_compact_text_limit(original,VS_ACTIVE_TASK_MAX,"active task anchor compacted");
    if(!task_anchor)task_anchor=dupstr(original);
    if(!task_anchor){free(prompt);free(original);return dupstr("out of memory");}
    execution_mode=execution_intent(original);
    vs_trace_clear(c);
    vs_trace(c,"agent","starting agent turn");
    if(c->mcp_server_count>0){int n=vs_mcp_refresh_all(c,0);snprintf(step,sizeof(step),"MCP catalogue ready: %d tool(s)",n<0?0:n);vs_trace(c,"mcp",step);}
    for(round=0;round<max_rounds;round++){
        char *tool,*mcp,*result=0,*next;
        if(round>0 && (round%VS_LONG_TASK_CHECKPOINT)==0){snprintf(step,sizeof(step),"long-task checkpoint: %d rounds completed; local command and MCP tools remain available",round);vs_trace(c,"checkpoint",step);}
        snprintf(step,sizeof(step),"model round %d",round+1);vs_trace(c,"model",step);
        free(ans);ans=vs_chat(c,prompt);if(!ans){free(prompt);free(original);free(task_anchor);free(last_tool_result);return dupstr("provider error");}
        vs_trace(c,"model-result","model response received");

        /* Parse executable directives BEFORE lifecycle markers.  Some models
           embed a VS_TOOL/VS_MCP directive in prose and also append VS_FINAL.
           A pending tool action always takes precedence: VS_FINAL is only valid
           after a response contains no executable directive. */
        tool=strstr(ans,"[[VS_TOOL ");
        mcp=strstr(ans,"[[VS_MCP ");
        if((tool||mcp) && strstr(ans,"[[VS_FINAL]]")){
            char *clean=strip_marker(ans,"[[VS_FINAL]]");
            vs_trace(c,"protocol","deferred premature VS_FINAL because an executable directive is pending");
            if(clean){free(ans);ans=clean;tool=strstr(ans,"[[VS_TOOL ");mcp=strstr(ans,"[[VS_MCP ");}
        }

        /* A model may hallucinate a broken tool bridge without ever emitting a
           VS_TOOL directive.  Only run the independent health-probe path when
           there is NO executable directive waiting in this same response.  If
           there is a directive, execute it and let its real result speak. */
        if(!tool && !mcp && (execution_mode||actual_tool_seen) && false_tool_unavailable_claim(ans) && tool_recoveries<VS_MAX_TOOL_RECOVERIES){
            char *probe=0,*recover=0;int pst=-1;
            tool_recoveries++;
            snprintf(step,sizeof(step),"model claimed tool channel unavailable%s; probing command runner (%d/%d)",actual_tool_seen?" after tool use":" without issuing a tool directive",tool_recoveries,VS_MAX_TOOL_RECOVERIES);vs_trace(c,"tool-health",step);
            probe=vs_run_command("printf 'VIBESOLARIS_COMMAND_RUNNER_OK\\n'",&pst);
            if(probe && pst==0 && strstr(probe,"VIBESOLARIS_COMMAND_RUNNER_OK")){
                command_runner_confirmed=1;
                vs_trace(c,"tool-health","command runner health probe passed; false blocker suppressed and task will continue");
                vs_history_add(c,"user",prompt);vs_history_add(c,"assistant",ans);
                recover=tool_channel_recovery_prompt(task_anchor,last_tool_result,ans,actual_tool_seen);
                free(probe);free(prompt);prompt=recover;
                if(!prompt){vs_trace(c,"memory-error","could not allocate tool-channel recovery prompt");break;}
                continue;
            }
            if(probe)free(probe);
            command_runner_confirmed=0;
            vs_trace(c,"tool-health","command runner health probe failed; treating tool availability as a genuine host problem");
        }

        /* Explicit lifecycle markers are optional but make the cross-provider
           text protocol deterministic when a model supports the instruction.
           They are honoured only when there is no pending executable directive. */
        if(!tool && !mcp && strstr(ans,"[[VS_NEED_USER")){
            char *q=attr(strstr(ans,"[[VS_NEED_USER"),"question");
            vs_trace(c,"agent","paused because the model genuinely needs user input");
            if(q&&*q){free(ans);ans=q;}else free(q);
            vs_history_add(c,"user",prompt);vs_history_add(c,"assistant",ans);
            break;
        }
        if(!tool && !mcp && strstr(ans,"[[VS_FINAL]]")){
            char *clean=strip_marker(ans,"[[VS_FINAL]]");
            if(clean){free(ans);ans=clean;}
            vs_history_add(c,"user",prompt);vs_history_add(c,"assistant",ans);
            vs_trace(c,"agent","completed with explicit final marker");
            break;
        }
        if(!tool && !mcp){
            /* Once the user asked for execution, or once this turn has actually
               used a tool, prose alone is NOT a completion signal.  Requiring
               VS_FINAL/VS_NEED_USER removes a whole class of model-specific
               "tool bridge stopped" hallucinations and premature status exits. */
            if(execution_mode||actual_tool_seen){
                plan_continues++;
                snprintf(step,sizeof(step),"execution turn returned no directive/final marker; automatically continuing (%d, bounded by overall %d-round ceiling)",plan_continues,max_rounds);vs_trace(c,"auto-continue",step);
                vs_history_add(c,"user",prompt);vs_history_add(c,"assistant",ans);
                next=continuation_prompt(task_anchor,last_tool_result,1);
                free(prompt);prompt=next;if(!prompt){vs_trace(c,"memory-error","could not allocate autonomy continuation prompt");break;}
                continue;
            }
            if(plan_continues<VS_MAX_PLAN_CONTINUES && should_auto_continue(original,ans)){
                plan_continues++;
                snprintf(step,sizeof(step),"planning-only response detected; automatically continuing (%d/%d)",plan_continues,VS_MAX_PLAN_CONTINUES);vs_trace(c,"auto-continue",step);
                vs_history_add(c,"user",prompt);vs_history_add(c,"assistant",ans);
                next=continuation_prompt(task_anchor,NULL,1);
                free(prompt);prompt=next;if(!prompt){vs_trace(c,"memory-error","could not allocate autonomy continuation prompt");break;}
                continue;
            }
            vs_history_add(c,"user",prompt);vs_history_add(c,"assistant",ans);vs_trace(c,"agent","completed informational turn without tool work");break;
        }

        plan_continues=0;
        actual_tool_seen=1;
        execution_mode=1;
        if(mcp && (!tool || mcp<tool)){
            char *server=attr(mcp,"server"),*name=attr(mcp,"tool"),*args=attr(mcp,"args");
            if(args)unesc(args);
            snprintf(step,sizeof(step),"MCP %s.%s",server?server:"?",name?name:"?");vs_trace(c,"mcp-call",step);
            result=(server&&name)?vs_mcp_call(c,server,name,args&&*args?args:"{}"):dupstr("ERROR: malformed VS_MCP directive");
            free(server);free(name);free(args);
        } else if(!strncmp(tool+10,"image ",6)){
            char *p=attr(tool,"path");int rc=-1;
            snprintf(step,sizeof(step),"load image %s",p?p:"?");vs_trace(c,"tool-image",step);
            if(p&&vs_is_image_path(p))rc=vs_attach(c,p);
            if(rc==0){size_t z=strlen(p?p:"")+96;result=(char*)malloc(z);if(result)snprintf(result,z,"OK: image loaded as visual input for the next model round: %s",p?p:"");}
            if(!result)result=dupstr((p&& !vs_is_image_path(p))?"ERROR: image tool requires PNG, JPEG, GIF, or WebP":"ERROR: unable to load image file");
            free(p);
        } else if(!strncmp(tool+10,"read ",5)){
            char *p=attr(tool,"path");snprintf(step,sizeof(step),"read file %s",p?p:"?");vs_trace(c,"tool-read",step);result=p?vs_cached_read_file(c,p):0;if(!result)result=dupstr("ERROR: unable to read file");free(p);
        } else if(!strncmp(tool+10,"run ",4)){
            char *cmd=attr(tool,"cmd"),*raw=0;int st=0;
            snprintf(step,sizeof(step),"run command %.430s",cmd?cmd:"?");vs_trace(c,"tool-run",step);
            raw=cmd?vs_run_command(cmd,&st):0;
            if(raw){
                size_t z=strlen(raw)+640;
                command_runner_confirmed=1;
                result=(char*)malloc(z);
                if(result)snprintf(result,z,"TOOL_RESULT_DELIVERED: yes\nCOMMAND_RUNNER_STATUS: operational\nCOMMAND_SHELL: %s\nCOMMAND_EXIT_STATUS: %d\nCOMMAND_TOOL_AVAILABLE: yes\nRECOVERY_RULE: A non-zero status, syntax error, compiler error, or timeout is a normal command result. Correct it and continue; do not ask the user to restore VS_TOOL/TOOL_RESULT.\nOUTPUT:\n%s",vs_command_shell_name(),st,raw);
                free(raw);
            }else{
                command_runner_confirmed=0;
                result=dupstr("TOOL_RESULT_DELIVERED: yes\nCOMMAND_RUNNER_STATUS: host_error\nCOMMAND_EXIT_STATUS: -1\nCOMMAND_TOOL_AVAILABLE: uncertain\nERROR: the host could not start or communicate with the command process.");
            }
            if(!result)result=dupstr("ERROR: command result allocation failed");
            if(st==124)snprintf(step,sizeof(step),"command timed out (exit 124); command runner remains operational");
            else if(command_runner_confirmed)snprintf(step,sizeof(step),"command exit %d; command runner confirmed operational",st);
            else snprintf(step,sizeof(step),"command runner host error");
            vs_trace(c,"tool-result",step);free(cmd);
        } else if(!strncmp(tool+10,"write ",6)){
            char *p=attr(tool,"path"),*ct=attr(tool,"content");int rc=-1;if(ct)unesc(ct);snprintf(step,sizeof(step),"write file %s",p?p:"?");vs_trace(c,"tool-write",step);if(p&&ct)rc=vs_write_file(p,ct);if(rc==0&&p)vs_cache_invalidate(c,p);result=dupstr(rc==0?"OK: file written":"ERROR: write failed");free(p);free(ct);
        } else result=dupstr("ERROR: unknown tool");

        if(result){
            char *bounded=vs_compact_text_limit(result,VS_MAX_TOOL_RESULT,"tool result truncated for model context");
            if(bounded){if(strlen(bounded)<strlen(result))vs_trace(c,"limit","tool/MCP result was compacted to keep the agent stable");free(result);result=bounded;}
            snprintf(step,sizeof(step),"%.470s",result);vs_trace(c,"tool-output",step);
        }
        free(last_tool_result);last_tool_result=result?dupstr(result):0;
        vs_history_add(c,"user",prompt);vs_history_add(c,"assistant",ans);
        next=continuation_prompt(task_anchor,result,0);
        if(!next){free(result);vs_trace(c,"memory-error","could not allocate next tool round prompt");break;}
        free(result);free(prompt);prompt=next;
    }
    if(round>=max_rounds){
        snprintf(step,sizeof(step),"maximum autonomous agent rounds reached (%d); local command execution is not disabled and remains available on the next turn",max_rounds);vs_trace(c,"limit",step);
        if(execution_mode || (ans && (strstr(ans,"[[VS_TOOL ")||strstr(ans,"[[VS_MCP ")))){free(ans);ans=dupstr("The single-turn autonomous safety ceiling was reached while work was still in progress. Local command execution is still available; the task was not marked complete. For unusually large jobs, raise VIBESOLARIS_MAX_AGENT_ROUNDS (up to 256) and continue from the existing files and conversation state.");}
    }
    free(prompt);free(original);free(task_anchor);free(last_tool_result);return ans;
}

