/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *slot_names[VS_PROVIDER_SLOT_COUNT] = {
    "openai", "glm", "glm_coding", "kimi", "claude",
    "qwen", "ernie", "gemini", "deepseek", "custom"
};

static void copy_value(char *dst,size_t cap,const char *src){size_t n;if(!dst||cap==0)return;if(!src)src="";n=strlen(src);if(n>=cap)n=cap-1;memmove(dst,src,n);dst[n]=0;}
static int valid_kind(VSProviderKind k){return (int)k>=0&&(int)k<VS_PROVIDER_SLOT_COUNT;}
static int slot_from_key_name(const char *name){int i;for(i=0;i<VS_PROVIDER_SLOT_COUNT;i++)if(!strcmp(name,slot_names[i]))return i;return -1;}

const char *vs_protocol_name(VSProtocolKind p){if(p==VS_PROTOCOL_ANTHROPIC)return "anthropic";if(p==VS_PROTOCOL_GEMINI)return "gemini";return "openai";}
int vs_provider_supports_protocol(VSProviderKind k,VSProtocolKind p){if(k==VS_PROVIDER_GEMINI)return p==VS_PROTOCOL_GEMINI;if(k==VS_PROVIDER_CLAUDE)return p==VS_PROTOCOL_ANTHROPIC;if(k==VS_PROVIDER_QWEN||k==VS_PROVIDER_GLM||k==VS_PROVIDER_GLM_CODING||k==VS_PROVIDER_DEEPSEEK||k==VS_PROVIDER_CUSTOM)return p==VS_PROTOCOL_OPENAI||p==VS_PROTOCOL_ANTHROPIC;return p==VS_PROTOCOL_OPENAI;}

static const char *default_url(VSProviderKind k,VSProtocolKind p){
    if(p==VS_PROTOCOL_ANTHROPIC){if(k==VS_PROVIDER_CLAUDE)return "https://api.anthropic.com/v1/messages";if(k==VS_PROVIDER_QWEN)return "https://coding.dashscope.aliyuncs.com/apps/anthropic";if(k==VS_PROVIDER_GLM||k==VS_PROVIDER_GLM_CODING)return "https://open.bigmodel.cn/api/anthropic";if(k==VS_PROVIDER_DEEPSEEK)return "https://api.deepseek.com/anthropic";return "";}
    if(p==VS_PROTOCOL_GEMINI)return "https://generativelanguage.googleapis.com/v1beta/models";
    switch(k){case VS_PROVIDER_OPENAI:return "https://api.openai.com/v1/chat/completions";case VS_PROVIDER_GLM:return "https://open.bigmodel.cn/api/paas/v4/chat/completions";case VS_PROVIDER_GLM_CODING:return "https://open.bigmodel.cn/api/coding/paas/v4";case VS_PROVIDER_KIMI:return "https://api.moonshot.cn/v1/chat/completions";case VS_PROVIDER_QWEN:return "https://coding.dashscope.aliyuncs.com/v1";case VS_PROVIDER_ERNIE:return "https://qianfan.baidubce.com/v2/chat/completions";case VS_PROVIDER_DEEPSEEK:return "https://api.deepseek.com";default:return "";}
}
static const char *default_model(VSProviderKind k){switch(k){case VS_PROVIDER_OPENAI:return "gpt-5.4";case VS_PROVIDER_GLM:return "glm-5.3";case VS_PROVIDER_GLM_CODING:return "glm-5.2";case VS_PROVIDER_KIMI:return "kimi-k3";case VS_PROVIDER_CLAUDE:return "claude-sonnet-5";case VS_PROVIDER_QWEN:return "qwen3.7-plus";case VS_PROVIDER_ERNIE:return "ernie-4.5-turbo-128k";case VS_PROVIDER_GEMINI:return "gemini-3.6-flash";case VS_PROVIDER_DEEPSEEK:return "deepseek-v4-pro";default:return "";}}
static VSProtocolKind default_protocol(VSProviderKind k){if(k==VS_PROVIDER_CLAUDE)return VS_PROTOCOL_ANTHROPIC;if(k==VS_PROVIDER_GEMINI)return VS_PROTOCOL_GEMINI;return VS_PROTOCOL_OPENAI;}

static void save_active(VSContext *c){int s;if(!valid_kind(c->provider.kind))return;s=(int)c->provider.kind;copy_value(c->provider_api_keys[s],sizeof(c->provider_api_keys[s]),c->provider.api_key);copy_value(c->provider_models[s],sizeof(c->provider_models[s]),c->provider.model);if(c->provider.protocol==VS_PROTOCOL_ANTHROPIC)copy_value(c->provider_anthropic_urls[s],sizeof(c->provider_anthropic_urls[s]),c->provider.base_url);else if(c->provider.protocol==VS_PROTOCOL_OPENAI)copy_value(c->provider_openai_urls[s],sizeof(c->provider_openai_urls[s]),c->provider.base_url);c->provider_protocols[s]=(int)c->provider.protocol;}
static void load_active_url(VSContext *c){int s;const char *v="";if(!valid_kind(c->provider.kind))return;s=(int)c->provider.kind;if(c->provider.protocol==VS_PROTOCOL_ANTHROPIC)v=c->provider_anthropic_urls[s];else if(c->provider.protocol==VS_PROTOCOL_OPENAI)v=c->provider_openai_urls[s];copy_value(c->provider.base_url,sizeof(c->provider.base_url),v[0]?v:default_url(c->provider.kind,c->provider.protocol));}

int vs_persist_settings(VSContext *c){char err[256];if(!c||!c->config_autosave||c->config_loading)return 0;save_active(c);err[0]=0;if(vs_global_config_save(c,err,sizeof(err))!=0){vs_trace(c,"config-error",err[0]?err:"encrypted config autosave failed");return -1;}return 0;}

void vs_set_api_key(VSContext *c,const char *key){const char *v=key?key:"";copy_value(c->provider.api_key,sizeof(c->provider.api_key),v);if(valid_kind(c->provider.kind))copy_value(c->provider_api_keys[(int)c->provider.kind],sizeof(c->provider_api_keys[0]),v);(void)vs_persist_settings(c);}
void vs_set_model(VSContext *c,const char *model){copy_value(c->provider.model,sizeof(c->provider.model),model);if(valid_kind(c->provider.kind))copy_value(c->provider_models[(int)c->provider.kind],sizeof(c->provider_models[0]),c->provider.model);vs_refresh_cache_key(c);(void)vs_persist_settings(c);}
void vs_set_base_url(VSContext *c,const char *url){copy_value(c->provider.base_url,sizeof(c->provider.base_url),url);save_active(c);(void)vs_persist_settings(c);}

int vs_set_protocol(VSContext *c,const char *name){VSProtocolKind p;int s;if(!name)return -1;if(!strcmp(name,"anthropic")||!strcmp(name,"claude"))p=VS_PROTOCOL_ANTHROPIC;else if(!strcmp(name,"gemini"))p=VS_PROTOCOL_GEMINI;else if(!strcmp(name,"openai"))p=VS_PROTOCOL_OPENAI;else return -1;if(!vs_provider_supports_protocol(c->provider.kind,p))return -1;save_active(c);c->provider.protocol=p;if(valid_kind(c->provider.kind)){s=(int)c->provider.kind;c->provider_protocols[s]=(int)p;}load_active_url(c);vs_refresh_cache_key(c);(void)vs_persist_settings(c);return 0;}

int vs_set_provider(VSContext *c,const char *name){const char *n=name?name:"openai";VSProviderKind k;VSProtocolKind p;int s;if(!strcmp(n,"chatgpt"))n="openai";if(c->provider.name[0])save_active(c);
    if(!strcmp(n,"openai"))k=VS_PROVIDER_OPENAI;else if(!strcmp(n,"glm"))k=VS_PROVIDER_GLM;else if(!strcmp(n,"glm-coding"))k=VS_PROVIDER_GLM_CODING;else if(!strcmp(n,"kimi"))k=VS_PROVIDER_KIMI;else if(!strcmp(n,"claude"))k=VS_PROVIDER_CLAUDE;else if(!strcmp(n,"qwen"))k=VS_PROVIDER_QWEN;else if(!strcmp(n,"ernie"))k=VS_PROVIDER_ERNIE;else if(!strcmp(n,"gemini"))k=VS_PROVIDER_GEMINI;else if(!strcmp(n,"deepseek"))k=VS_PROVIDER_DEEPSEEK;else k=VS_PROVIDER_CUSTOM;
    memset(&c->provider,0,sizeof(c->provider));c->provider.kind=k;copy_value(c->provider.name,sizeof(c->provider.name),n);s=(int)k;p=(VSProtocolKind)c->provider_protocols[s];if(!vs_provider_supports_protocol(k,p))p=default_protocol(k);c->provider.protocol=p;c->provider_protocols[s]=(int)p;if(!c->provider_models[s][0])copy_value(c->provider_models[s],sizeof(c->provider_models[s]),default_model(k));copy_value(c->provider.model,sizeof(c->provider.model),c->provider_models[s]);load_active_url(c);copy_value(c->provider.api_key,sizeof(c->provider.api_key),c->provider_api_keys[s]);vs_refresh_cache_key(c);(void)vs_persist_settings(c);return 0;
}

static void mcp_runtime_init(VSMcpServer *s){s->pid=0;s->in_fd=-1;s->out_fd=-1;s->next_id=1;s->io_pos=s->io_len=0;s->tools_loaded=0;s->legacy=0;s->initialized=0;s->protocol_version[0]=0;s->session_id[0]=0;}
static void set_mcp_field(VSContext *c,const char *k,const char *v){int idx;const char *p=k+4;char *e;idx=(int)strtol(p,&e,10);if(idx<0||idx>=VS_MAX_MCP_SERVERS||e==p||*e!='_')return;if(idx>=c->mcp_server_count)c->mcp_server_count=idx+1;if(!strcmp(e+1,"name"))copy_value(c->mcp_servers[idx].name,sizeof(c->mcp_servers[idx].name),v);else if(!strcmp(e+1,"transport"))c->mcp_servers[idx].transport=!strcmp(v,"http")?VS_MCP_HTTP:VS_MCP_STDIO;else if(!strcmp(e+1,"target"))copy_value(c->mcp_servers[idx].target,sizeof(c->mcp_servers[idx].target),v);else if(!strcmp(e+1,"auth"))copy_value(c->mcp_servers[idx].auth,sizeof(c->mcp_servers[idx].auth),v);else if(!strcmp(e+1,"enabled"))c->mcp_servers[idx].enabled=atoi(v)?1:0;}

static void setkv(VSContext *c,const char *k,const char *v){int s,p,i;if(!strcmp(k,"provider"))vs_set_provider(c,v);else if(!strcmp(k,"protocol"))vs_set_protocol(c,v);else if(!strcmp(k,"api_key"))vs_set_api_key(c,v);else if(!strncmp(k,"api_key_",8)){s=slot_from_key_name(k+8);if(s>=0)copy_value(c->provider_api_keys[s],sizeof(c->provider_api_keys[s]),v);}else if(!strncmp(k,"model_",6)){s=slot_from_key_name(k+6);if(s>=0)copy_value(c->provider_models[s],sizeof(c->provider_models[s]),v);}else if(!strncmp(k,"openai_url_",11)){s=slot_from_key_name(k+11);if(s>=0)copy_value(c->provider_openai_urls[s],sizeof(c->provider_openai_urls[s]),v);}else if(!strncmp(k,"anthropic_url_",14)){s=slot_from_key_name(k+14);if(s>=0)copy_value(c->provider_anthropic_urls[s],sizeof(c->provider_anthropic_urls[s]),v);}else if(!strncmp(k,"protocol_",9)){s=slot_from_key_name(k+9);if(s>=0){if(!strcmp(v,"anthropic"))p=VS_PROTOCOL_ANTHROPIC;else if(!strcmp(v,"gemini"))p=VS_PROTOCOL_GEMINI;else p=VS_PROTOCOL_OPENAI;c->provider_protocols[s]=p;}}else if(!strcmp(k,"base_url"))copy_value(c->provider.base_url,sizeof(c->provider.base_url),v);else if(!strcmp(k,"model"))copy_value(c->provider.model,sizeof(c->provider.model),v);else if(!strcmp(k,"cache"))c->cache_enabled=atoi(v)?1:0;else if(!strcmp(k,"oauth_client_id"))copy_value(c->oauth.client_id,sizeof(c->oauth.client_id),v);else if(!strcmp(k,"oauth_authorize_url"))copy_value(c->oauth.authorize_url,sizeof(c->oauth.authorize_url),v);else if(!strcmp(k,"oauth_token_url"))copy_value(c->oauth.token_url,sizeof(c->oauth.token_url),v);else if(!strcmp(k,"oauth_scopes"))copy_value(c->oauth.scopes,sizeof(c->oauth.scopes),v);else if(!strcmp(k,"oauth_redirect_uri"))copy_value(c->oauth.redirect_uri,sizeof(c->oauth.redirect_uri),v);else if(!strcmp(k,"oauth_access_token"))copy_value(c->oauth.access_token,sizeof(c->oauth.access_token),v);else if(!strcmp(k,"oauth_refresh_token"))copy_value(c->oauth.refresh_token,sizeof(c->oauth.refresh_token),v);else if(!strcmp(k,"oauth_token_type"))copy_value(c->oauth.token_type,sizeof(c->oauth.token_type),v);else if(!strcmp(k,"oauth_expires_at"))c->oauth.expires_at=strtol(v,NULL,10);else if(!strcmp(k,"mcp_count")){i=atoi(v);if(i<0)i=0;if(i>VS_MAX_MCP_SERVERS)i=VS_MAX_MCP_SERVERS;c->mcp_server_count=i;for(s=0;s<i;s++)mcp_runtime_init(&c->mcp_servers[s]);}else if(!strncmp(k,"mcp_",4))set_mcp_field(c,k,v);}

static void finish_load(VSContext *c){int s,i;if(valid_kind(c->provider.kind)){s=(int)c->provider.kind;if(vs_provider_supports_protocol(c->provider.kind,(VSProtocolKind)c->provider_protocols[s]))c->provider.protocol=(VSProtocolKind)c->provider_protocols[s];load_active_url(c);copy_value(c->provider.api_key,sizeof(c->provider.api_key),c->provider_api_keys[s]);if(c->provider_models[s][0])copy_value(c->provider.model,sizeof(c->provider.model),c->provider_models[s]);else copy_value(c->provider.model,sizeof(c->provider.model),default_model(c->provider.kind));}for(i=0;i<c->mcp_server_count;i++){mcp_runtime_init(&c->mcp_servers[i]);}c->mcp_tool_count=0;vs_refresh_cache_key(c);}

int vs_config_apply_text(VSContext *c,const char *text)
{
    char *copy,*line,*next,*eq,*e;
    char active_provider[64],active_protocol[32],active_key[512],active_model[128],active_base[512];
    size_t n;
    int have_provider=0,have_protocol=0,have_key=0,have_model=0,have_base=0;
    if(!c||!text)return -1;
    active_provider[0]=active_protocol[0]=active_key[0]=active_model[0]=active_base[0]=0;
    n=strlen(text);copy=(char*)malloc(n+1);if(!copy)return -1;memcpy(copy,text,n+1);
    c->config_loading++;

    /* First pass: load every provider-specific slot before choosing the active provider.
       The five legacy/current active keys are remembered and applied afterwards. */
    line=copy;
    while(line&&*line){
        next=strchr(line,'\n');if(next){*next=0;next++;}
        if(line[0]!='#'&&line[0]!=0&&line[0]!='\r'){
            eq=strchr(line,'=');
            if(eq){
                *eq=0;e=eq+1;e[strcspn(e,"\r")]=0;
                if(!strcmp(line,"provider")){copy_value(active_provider,sizeof(active_provider),e);have_provider=1;}
                else if(!strcmp(line,"protocol")){copy_value(active_protocol,sizeof(active_protocol),e);have_protocol=1;}
                else if(!strcmp(line,"api_key")){copy_value(active_key,sizeof(active_key),e);have_key=1;}
                else if(!strcmp(line,"model")){copy_value(active_model,sizeof(active_model),e);have_model=1;}
                else if(!strcmp(line,"base_url")){copy_value(active_base,sizeof(active_base),e);have_base=1;}
                else setkv(c,line,e);
            }
        }
        line=next;
    }

    /* Second pass: restore the exact last active provider and its exact last model.
       This makes startup independent of serialisation order and keeps GUI/TUI identical. */
    if(have_provider&&active_provider[0])vs_set_provider(c,active_provider);
    if(have_protocol&&active_protocol[0])(void)vs_set_protocol(c,active_protocol);
    if(have_base){copy_value(c->provider.base_url,sizeof(c->provider.base_url),active_base);save_active(c);}
    if(have_key){copy_value(c->provider.api_key,sizeof(c->provider.api_key),active_key);if(valid_kind(c->provider.kind))copy_value(c->provider_api_keys[(int)c->provider.kind],sizeof(c->provider_api_keys[0]),active_key);}
    if(have_model&&active_model[0]){copy_value(c->provider.model,sizeof(c->provider.model),active_model);if(valid_kind(c->provider.kind))copy_value(c->provider_models[(int)c->provider.kind],sizeof(c->provider_models[0]),active_model);}

    c->config_loading--;
    free(copy);
    finish_load(c);
    return 0;
}
int vs_load_config(VSContext *c,const char *path){char *t=vs_read_file(path);int rc;if(!t)return -1;rc=vs_config_apply_text(c,t);free(t);return rc;}

static int append_line(char **buf,size_t *len,size_t *cap,const char *key,const char *value){size_t need,kl,vl;char *n;if(!value)value="";kl=strlen(key);vl=strlen(value);need=kl+vl+2;if(*len+need+1>*cap){size_t nc=*cap?*cap:4096;while(*len+need+1>nc)nc*=2;n=(char*)realloc(*buf,nc);if(!n)return -1;*buf=n;*cap=nc;}memcpy(*buf+*len,key,kl);*len+=kl;(*buf)[(*len)++]='=';memcpy(*buf+*len,value,vl);*len+=vl;(*buf)[(*len)++]='\n';(*buf)[*len]=0;return 0;}
char *vs_config_serialize(const VSContext *cc){VSContext *c=(VSContext*)cc;char *buf=NULL;size_t len=0,cap=0;int i;char key[128],val[64];if(!c)return NULL;save_active(c);if(append_line(&buf,&len,&cap,"provider",c->provider.name)||append_line(&buf,&len,&cap,"protocol",vs_protocol_name(c->provider.protocol))||append_line(&buf,&len,&cap,"base_url",c->provider.base_url)||append_line(&buf,&len,&cap,"model",c->provider.model))goto fail;snprintf(val,sizeof(val),"%d",c->cache_enabled?1:0);if(append_line(&buf,&len,&cap,"cache",val)||append_line(&buf,&len,&cap,"oauth_client_id",c->oauth.client_id)||append_line(&buf,&len,&cap,"oauth_authorize_url",c->oauth.authorize_url)||append_line(&buf,&len,&cap,"oauth_token_url",c->oauth.token_url)||append_line(&buf,&len,&cap,"oauth_scopes",c->oauth.scopes)||append_line(&buf,&len,&cap,"oauth_redirect_uri",c->oauth.redirect_uri)||append_line(&buf,&len,&cap,"oauth_access_token",c->oauth.access_token)||append_line(&buf,&len,&cap,"oauth_refresh_token",c->oauth.refresh_token)||append_line(&buf,&len,&cap,"oauth_token_type",c->oauth.token_type))goto fail;snprintf(val,sizeof(val),"%ld",c->oauth.expires_at);if(append_line(&buf,&len,&cap,"oauth_expires_at",val))goto fail;
    for(i=0;i<VS_PROVIDER_SLOT_COUNT;i++){snprintf(key,sizeof(key),"api_key_%s",slot_names[i]);if(append_line(&buf,&len,&cap,key,c->provider_api_keys[i]))goto fail;snprintf(key,sizeof(key),"model_%s",slot_names[i]);if(append_line(&buf,&len,&cap,key,c->provider_models[i]))goto fail;snprintf(key,sizeof(key),"protocol_%s",slot_names[i]);if(append_line(&buf,&len,&cap,key,vs_protocol_name((VSProtocolKind)c->provider_protocols[i])))goto fail;snprintf(key,sizeof(key),"openai_url_%s",slot_names[i]);if(append_line(&buf,&len,&cap,key,c->provider_openai_urls[i]))goto fail;snprintf(key,sizeof(key),"anthropic_url_%s",slot_names[i]);if(append_line(&buf,&len,&cap,key,c->provider_anthropic_urls[i]))goto fail;}
    snprintf(val,sizeof(val),"%d",c->mcp_server_count);if(append_line(&buf,&len,&cap,"mcp_count",val))goto fail;for(i=0;i<c->mcp_server_count;i++){snprintf(key,sizeof(key),"mcp_%d_name",i);if(append_line(&buf,&len,&cap,key,c->mcp_servers[i].name))goto fail;snprintf(key,sizeof(key),"mcp_%d_transport",i);if(append_line(&buf,&len,&cap,key,c->mcp_servers[i].transport==VS_MCP_HTTP?"http":"stdio"))goto fail;snprintf(key,sizeof(key),"mcp_%d_target",i);if(append_line(&buf,&len,&cap,key,c->mcp_servers[i].target))goto fail;snprintf(key,sizeof(key),"mcp_%d_auth",i);if(append_line(&buf,&len,&cap,key,c->mcp_servers[i].auth))goto fail;snprintf(key,sizeof(key),"mcp_%d_enabled",i);snprintf(val,sizeof(val),"%d",c->mcp_servers[i].enabled?1:0);if(append_line(&buf,&len,&cap,key,val))goto fail;}return buf;fail:if(buf)free(buf);return NULL;}
int vs_save_config(const VSContext *c,const char *path){FILE *f;char *t;size_t n;t=vs_config_serialize(c);if(!t)return -1;f=fopen(path,"w");if(!f){free(t);return -1;}n=strlen(t);if(n&&fwrite(t,1,n,f)!=n){fclose(f);free(t);return -1;}fclose(f);free(t);return 0;}
