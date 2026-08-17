/* SPDX-License-Identifier: Unlicense */
#ifndef VIBESOLARIS_H
#define VIBESOLARIS_H

#define VS_VERSION "0.10.2"

#include <stddef.h>

#define VS_MAX_ATTACH 16
#define VS_MAX_PATH 1024
#define VS_MAX_TEXT (1024*1024)
#define VS_MAX_TOOL_ROUNDS 16
#define VS_MAX_PLAN_CONTINUES 4
#define VS_MAX_HISTORY 48
#define VS_HISTORY_BUDGET (192*1024)
#define VS_HISTORY_MESSAGE_MAX (32*1024)
#define VS_MAX_FILE_CACHE 24
#define VS_PROVIDER_SLOT_COUNT 10
#define VS_OAUTH_TOKEN_MAX 4096
#define VS_OAUTH_URL_MAX 1024
#define VS_MAX_MCP_SERVERS 12
#define VS_MAX_MCP_TOOLS 64
#define VS_MCP_SCHEMA_MAX 4096
#define VS_MAX_TRACE 128

/* Resource guards for long-running/large agent operations.  These are deliberately
   generous, but finite: a provider, command, or MCP server must not be able to
   grow the process until the desktop starts swapping or the GUI appears dead. */
#define VS_MAX_HTTP_RESPONSE (64U*1024U*1024U)
#define VS_MAX_REQUEST_BODY (96U*1024U*1024U)
#define VS_MAX_COMMAND_CAPTURE (4U*1024U*1024U)
#define VS_MAX_TOOL_RESULT (2U*1024U*1024U)
#define VS_MAX_ATTACHMENT_TEXT (4U*1024U*1024U)
#define VS_FILE_CACHE_BLOB_MAX (2U*1024U*1024U)

#define VS_GLOBAL_CONFIG_DIR "/etc/vibesolaris"
#define VS_GLOBAL_CONFIG_FILE "/etc/vibesolaris/config.enc"
#define VS_GLOBAL_KEY_FILE "/etc/vibesolaris/master.key"
#define VS_USER_CONFIG_SUBDIR ".vibesolaris"

typedef enum {
    VS_PROVIDER_OPENAI,
    VS_PROVIDER_GLM,
    VS_PROVIDER_GLM_CODING,
    VS_PROVIDER_KIMI,
    VS_PROVIDER_CLAUDE,
    VS_PROVIDER_QWEN,
    VS_PROVIDER_ERNIE,
    VS_PROVIDER_GEMINI,
    VS_PROVIDER_DEEPSEEK,
    VS_PROVIDER_CUSTOM
} VSProviderKind;

typedef enum {
    VS_PROTOCOL_OPENAI,
    VS_PROTOCOL_ANTHROPIC,
    VS_PROTOCOL_GEMINI
} VSProtocolKind;

typedef struct {
    char path[VS_MAX_PATH];
    int is_image;
} VSAttachment;

typedef struct {
    VSProviderKind kind;
    VSProtocolKind protocol;
    char name[64];
    char api_key[512];
    char base_url[512];
    char model[128];
} VSProvider;


typedef struct {
    char client_id[512];
    char authorize_url[VS_OAUTH_URL_MAX];
    char token_url[VS_OAUTH_URL_MAX];
    char scopes[512];
    char redirect_uri[512];
    char access_token[VS_OAUTH_TOKEN_MAX];
    char refresh_token[VS_OAUTH_TOKEN_MAX];
    char token_type[64];
    long expires_at;
} VSOAuthConfig;

typedef struct {
    int active;
    int listener_fd;
    int port;
    long started_at;
    char state[96];
    char verifier[160];
    char redirect_uri[512];
    char callback_path[256];
} VSOAuthFlow;


typedef enum {
    VS_MCP_STDIO,
    VS_MCP_HTTP
} VSMcpTransport;

typedef struct {
    char server[64];
    char name[128];
    char description[512];
    char input_schema[VS_MCP_SCHEMA_MAX];
} VSMcpTool;

typedef struct {
    char name[64];
    VSMcpTransport transport;
    char target[VS_MAX_PATH * 2];
    char auth[512];
    int enabled;
    int tools_loaded;
    int legacy;
    int initialized;
    char protocol_version[32];
    char session_id[256];
    long pid;
    int in_fd;
    int out_fd;
    unsigned long next_id;
    char io_buf[8192];
    size_t io_pos;
    size_t io_len;
} VSMcpServer;

typedef struct {
    char kind[24];
    char detail[512];
} VSTraceEvent;

typedef void (*VSTraceCallback)(void *userdata, int step, const char *kind, const char *detail);

typedef struct {
    char role[16];
    char *content;
    size_t bytes;
} VSHistoryMessage;

typedef struct {
    char path[VS_MAX_PATH];
    long mtime;
    long size;
    int is_image;
    unsigned long hash;
    char *text;
    char *base64;
    size_t base64_len;
    unsigned long hits;
} VSFileCacheEntry;

typedef struct {
    char cwd[VS_MAX_PATH];
    char os_name[128];
    char os_release[128];
    char arch[128];
    char agent_md[VS_MAX_TEXT];
    VSAttachment attachments[VS_MAX_ATTACH];
    int attachment_count;
    VSProvider provider;
    char provider_api_keys[VS_PROVIDER_SLOT_COUNT][512];
    char provider_models[VS_PROVIDER_SLOT_COUNT][128];
    char provider_openai_urls[VS_PROVIDER_SLOT_COUNT][512];
    char provider_anthropic_urls[VS_PROVIDER_SLOT_COUNT][512];
    int provider_protocols[VS_PROVIDER_SLOT_COUNT];
    VSOAuthConfig oauth;

    int proxy_enabled;
    char proxy[1024];

    VSMcpServer mcp_servers[VS_MAX_MCP_SERVERS];
    int mcp_server_count;
    VSMcpTool mcp_tools[VS_MAX_MCP_TOOLS];
    int mcp_tool_count;

    VSTraceEvent trace[VS_MAX_TRACE];
    int trace_count;
    unsigned long trace_dropped;
    VSTraceCallback trace_callback;
    void *trace_callback_data;
    int config_autosave;
    int config_loading;

    int cache_enabled;
    char cache_key[128];
    VSFileCacheEntry file_cache[VS_MAX_FILE_CACHE];
    int file_cache_count;
    int file_cache_next;
    unsigned long file_cache_hits;
    unsigned long file_cache_misses;
    long provider_cached_tokens;
    long provider_cache_write_tokens;
    long provider_input_tokens;
    long provider_output_tokens;
    long provider_total_tokens;
    unsigned long conversation_input_tokens;
    unsigned long conversation_output_tokens;
    unsigned long conversation_total_tokens;
    unsigned long conversation_usage_responses;

    VSHistoryMessage history[VS_MAX_HISTORY];
    int history_count;
    size_t history_bytes;
    unsigned long history_evicted;
} VSContext;

void vs_init(VSContext *ctx);
void vs_shutdown(VSContext *ctx);
void vs_detect_platform(VSContext *ctx);
int  vs_load_agent_md(VSContext *ctx);
int  vs_load_config(VSContext *ctx, const char *path);
int  vs_save_config(const VSContext *ctx, const char *path);
char *vs_config_serialize(const VSContext *ctx);
int  vs_config_apply_text(VSContext *ctx, const char *text);
int  vs_secure_config_dir(char *out, size_t cap);
int  vs_secure_config_path(char *out, size_t cap);
int  vs_secure_config_is_per_user(void);
int  vs_global_config_exists(void);
int  vs_global_config_load(VSContext *ctx, char *err, size_t err_cap);
int  vs_global_config_save(const VSContext *ctx, char *err, size_t err_cap);
int  vs_set_provider(VSContext *ctx, const char *name);
int  vs_set_protocol(VSContext *ctx, const char *name);
void vs_set_api_key(VSContext *ctx, const char *key);
void vs_set_model(VSContext *ctx, const char *model);
int  vs_persist_settings(VSContext *ctx);
void vs_set_base_url(VSContext *ctx, const char *url);
int  vs_set_proxy(VSContext *ctx, const char *value);
void vs_proxy_redacted(const VSContext *ctx, char *out, size_t cap);
const char *vs_protocol_name(VSProtocolKind protocol);
int  vs_provider_supports_protocol(VSProviderKind kind, VSProtocolKind protocol);
int  vs_attach(VSContext *ctx, const char *path);
void vs_clear_attachments(VSContext *ctx);
char *vs_read_file(const char *path);
char *vs_cached_read_file(VSContext *ctx, const char *path);
char *vs_cached_base64_file(VSContext *ctx, const char *path, size_t *out_len);
void vs_cache_invalidate(VSContext *ctx, const char *path);
void vs_cache_clear(VSContext *ctx);
void vs_history_add(VSContext *ctx, const char *role, const char *content);
void vs_history_clear(VSContext *ctx);
int  vs_write_file(const char *path, const char *text);
char *vs_run_command(const char *cmd, int *exit_code);
char *vs_chat(VSContext *ctx, const char *user_text);
char *vs_agent_turn(VSContext *ctx, const char *user_text);
char *vs_build_system_prompt(const VSContext *ctx);
char *vs_http_post(const char *url, const char *headers[], int nheaders, const char *body, long *status);
char *vs_http_post_capture(const char *url, const char *headers[], int nheaders, const char *body, long *status, char *session_id, size_t session_cap);
char *vs_http_post_ctx(VSContext *ctx, const char *url, const char *headers[], int nheaders, const char *body, long *status);
char *vs_http_post_capture_ctx(VSContext *ctx, const char *url, const char *headers[], int nheaders, const char *body, long *status, char *session_id, size_t session_cap);
char *vs_json_escape(const char *s);
char *vs_base64_file(const char *path, size_t *out_len);
char *vs_shell_quote(const char *s);
char *vs_compact_text_limit(const char *s, size_t limit, const char *reason);
int  vs_is_image_path(const char *path);
int  vs_open_url(const char *url);
unsigned long vs_hash_string(const char *s);
void vs_refresh_cache_key(VSContext *ctx);

void vs_trace(VSContext *ctx, const char *kind, const char *detail);
void vs_trace_clear(VSContext *ctx);
void vs_set_trace_callback(VSContext *ctx, VSTraceCallback callback, void *userdata);
void vs_usage_clear(VSContext *ctx);
int  vs_mcp_add_stdio(VSContext *ctx, const char *name, const char *command);
int  vs_mcp_add_http(VSContext *ctx, const char *name, const char *url, const char *bearer);
int  vs_mcp_remove(VSContext *ctx, const char *name);
int  vs_mcp_refresh_all(VSContext *ctx, int force);
int  vs_mcp_refresh_server(VSContext *ctx, int index);
char *vs_mcp_call(VSContext *ctx, const char *server, const char *tool, const char *args_json);
char *vs_mcp_prompt_fragment(const VSContext *ctx);
void vs_mcp_shutdown(VSContext *ctx);

void vs_oauth_defaults(VSContext *ctx);
int  vs_oauth_is_configured(const VSContext *ctx);
int  vs_oauth_is_signed_in(const VSContext *ctx);
int  vs_oauth_begin(VSContext *ctx, VSOAuthFlow *flow, char *url_out, size_t url_cap, char *err, size_t err_cap);
int  vs_oauth_poll(VSContext *ctx, VSOAuthFlow *flow, char *msg, size_t msg_cap);
void vs_oauth_cancel(VSOAuthFlow *flow);
int  vs_oauth_login_blocking(VSContext *ctx, char *msg, size_t msg_cap);
int  vs_oauth_refresh(VSContext *ctx, char *err, size_t err_cap);
int  vs_oauth_ensure_access_token(VSContext *ctx, char *err, size_t err_cap);
void vs_oauth_logout(VSContext *ctx);
int  vs_oauth_save_profile(const VSContext *ctx);
int  vs_oauth_load_profile(VSContext *ctx);
const char *vs_oauth_profile_path(void);
void vs_sha256(const unsigned char *data, size_t len, unsigned char out[32]);

#endif
