/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dupstr(const char *s){size_t n=s?strlen(s):0;char *p=(char*)malloc(n+1);if(!p)return 0;if(s)memcpy(p,s,n);p[n]=0;return p;}
static char *attr(const char *s,const char *name){char pat[64],*p,*e,*o;size_t n;snprintf(pat,sizeof(pat),"%s=\"",name);p=strstr((char*)s,pat);if(!p)return 0;p+=strlen(pat);e=p;while(*e && !(*e=='"' && (e==p || e[-1]!='\\')))e++;n=(size_t)(e-p);o=(char*)malloc(n+1);if(!o)return 0;memcpy(o,p,n);o[n]=0;return o;}
static void unesc(char *s){char *r=s,*w=s;while(*r){if(*r=='\\'&&r[1]){r++;if(*r=='n')*w++='\n';else if(*r=='t')*w++='\t';else if(*r=='r')*w++='\r';else *w++=*r;r++;}else *w++=*r++;}*w=0;}

char *vs_agent_turn(VSContext *c,const char *user){
    char *prompt=dupstr(user),*ans=0;int round;
    char step[512];
    if(!prompt)return dupstr("out of memory");
    vs_trace_clear(c);
    vs_trace(c,"agent","starting agent turn");
    if(c->mcp_server_count>0){int n=vs_mcp_refresh_all(c,0);snprintf(step,sizeof(step),"MCP catalogue ready: %d tool(s)",n<0?0:n);vs_trace(c,"mcp",step);}
    for(round=0;round<VS_MAX_TOOL_ROUNDS;round++){
        char *tool,*mcp,*result=0,*next;size_t n;
        snprintf(step,sizeof(step),"model round %d",round+1);vs_trace(c,"model",step);
        free(ans);ans=vs_chat(c,prompt);if(!ans){free(prompt);return dupstr("provider error");}
        vs_trace(c,"model-result","model response received");
        tool=strstr(ans,"[[VS_TOOL ");
        mcp=strstr(ans,"[[VS_MCP ");
        if(!tool && !mcp){vs_history_add(c,"user",prompt);vs_history_add(c,"assistant",ans);vs_trace(c,"agent","completed without further tool calls");break;}

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
            char *cmd=attr(tool,"cmd");int st=0;snprintf(step,sizeof(step),"run command %.430s",cmd?cmd:"?");vs_trace(c,"tool-run",step);result=cmd?vs_run_command(cmd,&st):0;if(!result)result=dupstr("ERROR: command failed");snprintf(step,sizeof(step),"command exit %d",st);vs_trace(c,"tool-result",step);free(cmd);
        } else if(!strncmp(tool+10,"write ",6)){
            char *p=attr(tool,"path"),*ct=attr(tool,"content");int rc=-1;if(ct)unesc(ct);snprintf(step,sizeof(step),"write file %s",p?p:"?");vs_trace(c,"tool-write",step);if(p&&ct)rc=vs_write_file(p,ct);if(rc==0&&p)vs_cache_invalidate(c,p);result=dupstr(rc==0?"OK: file written":"ERROR: write failed");free(p);free(ct);
        } else result=dupstr("ERROR: unknown tool");

        if(result){
            char *bounded=vs_compact_text_limit(result,VS_MAX_TOOL_RESULT,"tool result truncated for model context");
            if(bounded){if(strlen(bounded)<strlen(result))vs_trace(c,"limit","tool/MCP result was compacted to keep the agent stable");free(result);result=bounded;}
            snprintf(step,sizeof(step),"%.470s",result);vs_trace(c,"tool-output",step);
        }
        vs_history_add(c,"user",prompt);vs_history_add(c,"assistant",ans);
        n=strlen(result?result:"")+160;next=(char*)malloc(n);if(!next){free(result);vs_trace(c,"memory-error","could not allocate next tool round prompt");break;}
        snprintf(next,n,"TOOL_RESULT:\n%s\n\nContinue from the previous response. If done, answer normally without a VS_TOOL or VS_MCP directive.",result?result:"");
        free(result);free(prompt);prompt=next;
    }
    free(prompt);return ans;
}
