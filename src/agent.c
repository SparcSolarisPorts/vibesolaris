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
    static const char *k[]={" add "," make "," fix "," edit "," update "," modify "," implement "," build "," create "," write "," change "," run "," test "," debug "," compile "," install "," remove "," replace "," refactor "," convert ",0};
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
static int should_auto_continue(const char *original,const char *answer){
    if(!execution_intent(original)||!prospective_language(answer))return 0;
    if(ascii_contains_ci(answer,"if you want")||ascii_contains_ci(answer,"if you'd like"))return 0;
    return 1;
}
static char *strip_marker(const char *s,const char *marker){
    const char *p;char *o;size_t a,b;if(!s)return dupstr("");p=strstr(s,marker);if(!p)return dupstr(s);a=(size_t)(p-s);b=strlen(p+strlen(marker));o=(char*)malloc(a+b+1);if(!o)return 0;memcpy(o,s,a);memcpy(o+a,p+strlen(marker),b+1);return o;
}

static char *continuation_prompt(const char *task,const char *result,int planning_only){
    const char *tail="Continue executing the ACTIVE_TASK autonomously. The local command runner remains available even after a command returns a non-zero status, times out, or prints an error; inspect the result, correct the problem, and run another command when useful. Do not stop merely to describe what you will do next and do not ask the user to send another prompt just to continue. Use VS_TOOL or VS_MCP directives as needed. Ask the user only if genuinely blocked, using [[VS_NEED_USER question=\"YOUR QUESTION\"]]. When the requested task is actually complete, emit [[VS_FINAL]] followed by the concise result.";
    const char *lead=planning_only?"AUTONOMY_CONTINUE: the previous response described future work without executing it. Execute the next concrete step now.":"TOOL_RESULT:";
    size_t n=strlen(task?task:"")+strlen(result?result:"")+strlen(lead)+strlen(tail)+160;
    char *o=(char*)malloc(n);
    if(!o)return NULL;
    if(planning_only)snprintf(o,n,"ACTIVE_TASK (persistent anchor; do not ask the user to restate it):\n%s\n\n%s\n\n%s",task?task:"",lead,tail);
    else snprintf(o,n,"ACTIVE_TASK (persistent anchor; do not ask the user to restate it):\n%s\n\n%s\n%s\n\n%s",task?task:"",lead,result?result:"",tail);
    return o;
}

char *vs_agent_turn(VSContext *c,const char *user){
    char *prompt=dupstr(user),*ans=0,*original=dupstr(user),*task_anchor=0;int round,plan_continues=0,max_rounds=VS_MAX_TOOL_ROUNDS;
    char step[512];
    {const char *mr=getenv("VIBESOLARIS_MAX_AGENT_ROUNDS");if(mr&&*mr){char *ep=0;long v=strtol(mr,&ep,10);if(ep&&*ep==0&&v>=8&&v<=256)max_rounds=(int)v;}}
    if(!prompt||!original){free(prompt);free(original);return dupstr("out of memory");}
    task_anchor=vs_compact_text_limit(original,VS_ACTIVE_TASK_MAX,"active task anchor compacted");
    if(!task_anchor)task_anchor=dupstr(original);
    if(!task_anchor){free(prompt);free(original);return dupstr("out of memory");}
    vs_trace_clear(c);
    vs_trace(c,"agent","starting agent turn");
    if(c->mcp_server_count>0){int n=vs_mcp_refresh_all(c,0);snprintf(step,sizeof(step),"MCP catalogue ready: %d tool(s)",n<0?0:n);vs_trace(c,"mcp",step);}
    for(round=0;round<max_rounds;round++){
        char *tool,*mcp,*result=0,*next;
        if(round>0 && (round%VS_LONG_TASK_CHECKPOINT)==0){snprintf(step,sizeof(step),"long-task checkpoint: %d rounds completed; local command and MCP tools remain available",round);vs_trace(c,"checkpoint",step);}
        snprintf(step,sizeof(step),"model round %d",round+1);vs_trace(c,"model",step);
        free(ans);ans=vs_chat(c,prompt);if(!ans){free(prompt);free(original);free(task_anchor);return dupstr("provider error");}
        vs_trace(c,"model-result","model response received");

        /* Explicit lifecycle markers are optional but make the cross-provider
           text protocol deterministic when a model supports the instruction. */
        if(strstr(ans,"[[VS_NEED_USER")){
            char *q=attr(strstr(ans,"[[VS_NEED_USER"),"question");
            vs_trace(c,"agent","paused because the model genuinely needs user input");
            if(q&&*q){free(ans);ans=q;}else free(q);
            vs_history_add(c,"user",prompt);vs_history_add(c,"assistant",ans);
            break;
        }
        if(strstr(ans,"[[VS_FINAL]]")){
            char *clean=strip_marker(ans,"[[VS_FINAL]]");
            if(clean){free(ans);ans=clean;}
            vs_history_add(c,"user",prompt);vs_history_add(c,"assistant",ans);
            vs_trace(c,"agent","completed with explicit final marker");
            break;
        }

        tool=strstr(ans,"[[VS_TOOL ");
        mcp=strstr(ans,"[[VS_MCP ");
        if(!tool && !mcp){
            if(plan_continues<VS_MAX_PLAN_CONTINUES && should_auto_continue(original,ans)){
                plan_continues++;
                snprintf(step,sizeof(step),"planning-only response detected; automatically continuing (%d/%d)",plan_continues,VS_MAX_PLAN_CONTINUES);vs_trace(c,"auto-continue",step);
                vs_history_add(c,"user",prompt);vs_history_add(c,"assistant",ans);
                next=continuation_prompt(task_anchor,NULL,1);
                free(prompt);prompt=next;if(!prompt){vs_trace(c,"memory-error","could not allocate autonomy continuation prompt");break;}
                continue;
            }
            vs_history_add(c,"user",prompt);vs_history_add(c,"assistant",ans);vs_trace(c,"agent","completed without further tool calls");break;
        }

        plan_continues=0;
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
            if(!raw)raw=dupstr("ERROR: the host could not start the command process. This is a per-command failure; the command tool itself remains available for another attempt.");
            if(raw){size_t z=strlen(raw)+160;result=(char*)malloc(z);if(result)snprintf(result,z,"COMMAND_EXIT_STATUS: %d\nCOMMAND_TOOL_AVAILABLE: yes\nOUTPUT:\n%s",st,raw);free(raw);}
            if(!result)result=dupstr("ERROR: command result allocation failed; command tool remains available");
            if(st==124)snprintf(step,sizeof(step),"command timed out (exit 124); command runner remains available");
            else snprintf(step,sizeof(step),"command exit %d; command runner remains available",st);
            vs_trace(c,"tool-result",step);free(cmd);
        } else if(!strncmp(tool+10,"write ",6)){
            char *p=attr(tool,"path"),*ct=attr(tool,"content");int rc=-1;if(ct)unesc(ct);snprintf(step,sizeof(step),"write file %s",p?p:"?");vs_trace(c,"tool-write",step);if(p&&ct)rc=vs_write_file(p,ct);if(rc==0&&p)vs_cache_invalidate(c,p);result=dupstr(rc==0?"OK: file written":"ERROR: write failed");free(p);free(ct);
        } else result=dupstr("ERROR: unknown tool");

        if(result){
            char *bounded=vs_compact_text_limit(result,VS_MAX_TOOL_RESULT,"tool result truncated for model context");
            if(bounded){if(strlen(bounded)<strlen(result))vs_trace(c,"limit","tool/MCP result was compacted to keep the agent stable");free(result);result=bounded;}
            snprintf(step,sizeof(step),"%.470s",result);vs_trace(c,"tool-output",step);
        }
        vs_history_add(c,"user",prompt);vs_history_add(c,"assistant",ans);
        next=continuation_prompt(task_anchor,result,0);
        if(!next){free(result);vs_trace(c,"memory-error","could not allocate next tool round prompt");break;}
        free(result);free(prompt);prompt=next;
    }
    if(round>=max_rounds){
        snprintf(step,sizeof(step),"maximum autonomous agent rounds reached (%d); local command execution is not disabled and remains available on the next turn",max_rounds);vs_trace(c,"limit",step);
        if(ans && (strstr(ans,"[[VS_TOOL ")||strstr(ans,"[[VS_MCP "))){free(ans);ans=dupstr("The single-turn autonomous safety ceiling was reached while work was still in progress. Local command execution is still available. For unusually large jobs, raise VIBESOLARIS_MAX_AGENT_ROUNDS (up to 256) and continue from the existing files and conversation state.");}
    }
    free(prompt);free(original);free(task_anchor);return ans;
}

