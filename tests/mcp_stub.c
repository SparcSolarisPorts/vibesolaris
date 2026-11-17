/* SPDX-License-Identifier: Unlicense */
/* Stub for MCP resource functions used by test-agent targets.
 * The real implementations are in src/mcp.c which requires util.c,
 * causing duplicate symbol errors when linked with AGENT_SRCS. */
#include "vibesolaris.h"
#include <stdlib.h>
#include <string.h>

static char *mdup(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

__attribute__((weak)) char *vs_mcp_list_resources(VSContext *ctx, const char *server)
{
    (void)ctx;
    (void)server;
    return mdup("{\"error\":\"MCP stub\"}");
}

__attribute__((weak)) char *vs_mcp_read_resource(VSContext *ctx, const char *server, const char *uri)
{
    (void)ctx;
    (void)server;
    (void)uri;
    return mdup("{\"error\":\"MCP stub\"}");
}

/* ---- Context window management stubs (only for tests that don't link provider.c) ---- */
__attribute__((weak)) long vs_estimate_tokens(const char *text, int is_code)
{
    (void)is_code;
    if (!text) return 0;
    return (long)(strlen(text) / 3.5);
}

__attribute__((weak)) long vs_estimate_request_tokens(const VSContext *ctx, const char *user_message)
{
    (void)user_message;
    if (!ctx) return 0;
    return (long)(ctx->agent_history_bytes / 3.5);
}

__attribute__((weak)) int vs_provider_context_window(const VSProvider *provider)
{
    (void)provider;
    return VS_CONTEXT_WINDOW_DEFAULT;
}

__attribute__((weak)) int vs_should_compact(const VSContext *ctx)
{
    (void)ctx;
    return 0;
}

/* ---- Computer Use stubs ---- */
__attribute__((weak)) int vs_computer_use_init(void)
{
    return 0;
}

__attribute__((weak)) char *vs_computer_use_screenshot(void)
{
    return mdup("Computer Use not available in test environment");
}

__attribute__((weak)) char *vs_computer_use_click(int x, int y)
{
    (void)x; (void)y;
    return mdup("Computer Use not available in test environment");
}

__attribute__((weak)) char *vs_computer_use_type(const char *text)
{
    (void)text;
    return mdup("Computer Use not available in test environment");
}

__attribute__((weak)) char *vs_computer_use_move(int x, int y)
{
    (void)x; (void)y;
    return mdup("Computer Use not available in test environment");
}

__attribute__((weak)) void vs_computer_use_shutdown(void)
{
}

/* ---- Checkpoint stubs ---- */
__attribute__((weak)) void vs_checkpoint_init(VSCheckpointStore *store)
{
    if (store) memset(store, 0, sizeof(*store));
}
__attribute__((weak)) int vs_checkpoint_create(VSCheckpointStore *store, const char *desc)
{
    (void)store; (void)desc; return -1;
}
__attribute__((weak)) int vs_checkpoint_rollback(VSCheckpointStore *store, const char *id)
{
    (void)store; (void)id; return -1;
}
__attribute__((weak)) char *vs_checkpoint_list(VSCheckpointStore *store, int max_lines)
{
    (void)store; (void)max_lines; return mdup("Checkpoints not available in test environment");
}
__attribute__((weak)) int vs_checkpoint_auto_before_edit(VSCheckpointStore *store)
{
    (void)store; return 0;
}
__attribute__((weak)) int vs_checkpoint_persist(VSCheckpointStore *store)
{
    (void)store; return 0;
}
__attribute__((weak)) void vs_checkpoint_clear(VSCheckpointStore *store)
{
    (void)store;
}

/* ---- AutoDream stubs ---- */
__attribute__((weak)) VSDreamStats vs_memory_dream(VSMemoryStore *store)
{
    (void)store;
    VSDreamStats s;
    memset(&s, 0, sizeof(s));
    return s;
}
__attribute__((weak)) char *vs_memory_dream_stats(const VSDreamStats *stats)
{
    (void)stats;
    return mdup("AutoDream not available in test environment");
}

/* ---- Auto-memory stub ---- */
__attribute__((weak)) void vs_memory_auto_capture(VSContext *ctx, const char *output, const char *tool_results)
{
    (void)ctx; (void)output; (void)tool_results;
}

/* ---- Conversation save/restore stubs ---- */
__attribute__((weak)) int vs_conversation_save(VSContext *ctx, const char *id)
{
    (void)ctx; (void)id;
    return -1;
}

__attribute__((weak)) int vs_conversation_load(VSContext *ctx, const char *id)
{
    (void)ctx; (void)id;
    return -1;
}

__attribute__((weak)) char *vs_conversation_list(int max_lines)
{
    (void)max_lines;
    return mdup("(no saved conversations)");
}

__attribute__((weak)) int vs_conversation_delete(const char *id)
{
    (void)id;
    return -1;
}

/* ---- Image display stub ---- */
__attribute__((weak)) void vs_display_image(const char *base64_data, size_t base64_len, const char *description)
{
    (void)base64_data; (void)base64_len; (void)description;
}

/* ---- CLAUDE.md stub ---- */
__attribute__((weak)) int vs_load_claude_md(VSContext *ctx)
{
    (void)ctx;
    return -1;
}

/* ---- Goal management stubs ---- */
__attribute__((weak)) int vs_goal_save(const char *goal)
{
    (void)goal; return -1;
}
__attribute__((weak)) char *vs_goal_load(void)
{
    return NULL;
}
__attribute__((weak)) int vs_goal_clear(void)
{
    return -1;
}

/* ---- vs_read_file stub (used by init_cmd.c) ---- */
__attribute__((weak)) char *vs_read_file(const char *path)
{
    (void)path;
    return NULL;
}

/* ---- Auto Mode stubs ---- */
__attribute__((weak)) void vs_auto_mode_init(VSAutoModeStore *store)
{
    if (store) memset(store, 0, sizeof(*store));
}
__attribute__((weak)) int vs_auto_mode_classify(VSAutoModeStore *store, const char *tool, const char *args)
{
    (void)store; (void)tool; (void)args; return VS_AUTO_MODE_SAFE;
}
__attribute__((weak)) int vs_auto_mode_should_block(VSAutoModeStore *store, int classification)
{
    (void)store; (void)classification; return 0;
}
__attribute__((weak)) char *vs_auto_mode_list_rules(VSAutoModeStore *store, int max_lines)
{
    (void)store; (void)max_lines; return mdup("Auto Mode not available in test environment");
}
__attribute__((weak)) void vs_auto_mode_load(VSAutoModeStore *store)
{
    (void)store;
}
__attribute__((weak)) int vs_auto_mode_persist(VSAutoModeStore *store)
{
    (void)store; return -1;
}

/* ---- Agent Team stubs ---- */
__attribute__((weak)) int vs_team_init(VSTeam *team, const char *id)
{
    (void)team; (void)id; return -1;
}
__attribute__((weak)) int vs_team_add_agent(VSTeam *team, const char *agent_id, const char *role)
{
    (void)team; (void)agent_id; (void)role; return -1;
}
__attribute__((weak)) char *vs_team_message(VSTeam *team, const char *from, const char *to, const char *msg)
{
    (void)team; (void)from; (void)to; (void)msg; return mdup("Team not available in test environment");
}
__attribute__((weak)) char *vs_team_read_messages(VSTeam *team, const char *agent_id)
{
    (void)team; (void)agent_id; return mdup("No messages");
}
__attribute__((weak)) char *vs_team_broadcast(VSTeam *team, const char *from, const char *msg)
{
    (void)team; (void)from; (void)msg; return mdup("Team not available in test environment");
}
__attribute__((weak)) void vs_team_shutdown(VSTeam *team)
{
    (void)team;
}
__attribute__((weak)) char *vs_team_list(int team_count, VSTeam *teams, int max_lines)
{
    (void)team_count; (void)teams; (void)max_lines; return mdup("No active teams");
}

/* ---- Plugin stubs ---- */
__attribute__((weak)) int vs_plugin_install(VSPlugin *plugins, int *count, const char *url_or_path)
{
    (void)plugins; (void)count; (void)url_or_path; return -1;
}
__attribute__((weak)) char *vs_plugin_list(VSPlugin *plugins, int count, int max_lines)
{
    (void)plugins; (void)count; (void)max_lines; return mdup("No plugins installed");
}
__attribute__((weak)) int vs_plugin_uninstall(VSPlugin *plugins, int *count, const char *name)
{
    (void)plugins; (void)count; (void)name; return -1;
}
__attribute__((weak)) int vs_plugin_update(VSPlugin *plugins, int *count, const char *name)
{
    (void)plugins; (void)count; (void)name; return -1;
}

/* ---- Code Review stubs ---- */
__attribute__((weak)) char *vs_code_review_start(const char *path, const char *scope, const char *focus)
{
    (void)path; (void)scope; (void)focus; return mdup("Code review not available in test environment");
}
__attribute__((weak)) char *vs_code_review_render(const VSCodeReviewFinding *findings, int count, int max_lines)
{
    (void)findings; (void)count; (void)max_lines; return mdup("No findings");
}

/* ---- Init command stub ---- */
__attribute__((weak)) char *vs_init_repo(const char *path)
{
    (void)path; return mdup("Init not available in test environment");
}

/* ---- Keybindings stubs ---- */
__attribute__((weak)) void vs_keybindings_load(VSKeybinding *bindings, int *count)
{
    (void)bindings; (void)count;
}
__attribute__((weak)) char *vs_keybindings_find(VSKeybinding *bindings, int count, const char *key)
{
    (void)bindings; (void)count; (void)key; return NULL;
}

/* ---- PDF reading stub ---- */
__attribute__((weak)) char *vs_read_pdf(const char *path, const char *pages)
{
    (void)path; (void)pages; return mdup("PDF reading not available in test environment");
}

/* ---- Computer Use backend detection stub ---- */
__attribute__((weak)) int vs_computer_use_detect_backend(void)
{
    return VS_COMPUTER_USE_NONE;
}

/* ---- Settings stubs ---- */
__attribute__((weak)) void vs_settings_init(VSSettingsStore *store, const char *project_root)
{
    (void)store; (void)project_root;
}
__attribute__((weak)) int vs_settings_load_user(VSSettingsStore *store)
{
    (void)store; return -1;
}
__attribute__((weak)) int vs_settings_load_project(VSSettingsStore *store)
{
    (void)store; return -1;
}
__attribute__((weak)) int vs_settings_load_local(VSSettingsStore *store)
{
    (void)store; return -1;
}
__attribute__((weak)) int vs_settings_load_all(VSSettingsStore *store)
{
    (void)store; return 0;
}
__attribute__((weak)) const char *vs_settings_get(VSSettingsStore *store, const char *key)
{
    (void)store; (void)key; return NULL;
}
__attribute__((weak)) int vs_settings_set(VSSettingsStore *store, const char *key, const char *value, VSSettingsLayer layer)
{
    (void)store; (void)key; (void)value; (void)layer; return -1;
}
__attribute__((weak)) int vs_settings_save_local(VSSettingsStore *store)
{
    (void)store; return -1;
}
__attribute__((weak)) char *vs_settings_dump(VSSettingsStore *store)
{
    (void)store; return NULL;
}
__attribute__((weak)) int vs_settings_detect_project_root(const char *start_path, char *out, size_t cap)
{
    (void)start_path; (void)out; (void)cap; return -1;
}

/* ---- Hook enhancement stubs ---- */
__attribute__((weak)) int vs_hooks_run_message_display(VSHooksStore *store, const char *text)
{
    (void)store; (void)text; return 0;
}
__attribute__((weak)) int vs_hooks_run_prompt(VSHooksStore *store, const char *prompt, char *modified, size_t cap)
{
    (void)store; (void)prompt; (void)modified; (void)cap; return 0;
}
__attribute__((weak)) int vs_hooks_send_webhook(VSHooksStore *store, const char *event, const char *payload)
{
    (void)store; (void)event; (void)payload; return 0;
}

/* ---- Project index stubs ---- */
__attribute__((weak)) void vs_project_index_init(const char *root)
{
    (void)root;
}
__attribute__((weak)) int vs_project_index_build(const char *root)
{
    (void)root; return 0;
}
__attribute__((weak)) char *vs_project_index_search(const char *query)
{
    (void)query; return NULL;
}
__attribute__((weak)) char *vs_project_index_references(const char *symbol)
{
    (void)symbol; return NULL;
}
__attribute__((weak)) char *vs_project_index_dependencies(const char *file_path)
{
    (void)file_path; return NULL;
}
__attribute__((weak)) int vs_project_index_is_valid(void)
{
    return 0;
}
__attribute__((weak)) void vs_project_index_clear(void)
{
}

/* ---- Wayland Computer Use stubs ---- */
__attribute__((weak)) int vs_wayland_init(void)
{
    return -1;
}
__attribute__((weak)) char *vs_wayland_screenshot(void)
{
    return mdup("Wayland screenshot not available in test environment");
}
__attribute__((weak)) char *vs_wayland_click(int x, int y)
{
    (void)x; (void)y; return mdup("Wayland click not available in test environment");
}
__attribute__((weak)) char *vs_wayland_type(const char *text)
{
    (void)text; return mdup("Wayland type not available in test environment");
}
__attribute__((weak)) char *vs_wayland_move(int x, int y)
{
    (void)x; (void)y; return mdup("Wayland move not available in test environment");
}

/* ---- Cocoa Computer Use stubs ---- */
__attribute__((weak)) int vs_cocoa_init(void)
{
    return -1;
}
__attribute__((weak)) char *vs_cocoa_screenshot(void)
{
    return mdup("Cocoa screenshot not available in test environment");
}
__attribute__((weak)) char *vs_cocoa_click(int x, int y)
{
    (void)x; (void)y; return mdup("Cocoa click not available in test environment");
}
__attribute__((weak)) char *vs_cocoa_type(const char *text)
{
    (void)text; return mdup("Cocoa type not available in test environment");
}
__attribute__((weak)) char *vs_cocoa_move(int x, int y)
{
    (void)x; (void)y; return mdup("Cocoa move not available in test environment");
}

/* ---- TUI Theme stubs ---- */
__attribute__((weak)) void vs_tui_theme_init(VSTUITheme *theme, const char *name)
{
    (void)theme; (void)name;
}
__attribute__((weak)) void vs_tui_theme_load_default(VSTUITheme *theme, int dark)
{
    (void)theme; (void)dark;
}
__attribute__((weak)) int vs_tui_theme_load_file(VSTUITheme *theme, const char *path)
{
    (void)theme; (void)path; return -1;
}
__attribute__((weak)) int vs_tui_theme_save_file(const VSTUITheme *theme, const char *path)
{
    (void)theme; (void)path; return -1;
}
__attribute__((weak)) const char *vs_tui_theme_color(const VSTUITheme *theme, int color_index)
{
    (void)theme; (void)color_index; return "0m";
}
__attribute__((weak)) void vs_tui_theme_apply(const VSTUITheme *theme)
{
    (void)theme;
}

/* ---- TUI Suggestions stubs ---- */
__attribute__((weak)) void vs_tui_suggestions_init(VSTUISuggestion *suggestions, int *count)
{
    (void)suggestions; (void)count;
}
__attribute__((weak)) int vs_tui_suggestions_add(VSTUISuggestion *suggestions, int *count, const char *text, int type)
{
    (void)suggestions; (void)count; (void)text; (void)type; return -1;
}
__attribute__((weak)) int vs_tui_suggestions_match(VSTUISuggestion *suggestions, int count, const char *input, VSTUISuggestion *results, int max_results)
{
    (void)suggestions; (void)count; (void)input; (void)results; (void)max_results; return 0;
}
__attribute__((weak)) int vs_tui_suggestions_fuzzy_match(const char *input, const char *candidate)
{
    (void)input; (void)candidate; return 0;
}
__attribute__((weak)) void vs_tui_suggestions_render(const VSTUISuggestion *results, int count, int width)
{
    (void)results; (void)count; (void)width;
}

/* ---- TUI Editor stubs ---- */
__attribute__((weak)) void vs_tui_editor_init(VSTUIEditor *ed)
{
    (void)ed;
}
__attribute__((weak)) void vs_tui_editor_insert_char(VSTUIEditor *ed, char ch)
{
    (void)ed; (void)ch;
}
__attribute__((weak)) void vs_tui_editor_backspace(VSTUIEditor *ed)
{
    (void)ed;
}
__attribute__((weak)) void vs_tui_editor_newline(VSTUIEditor *ed)
{
    (void)ed;
}
__attribute__((weak)) void vs_tui_editor_move_up(VSTUIEditor *ed)
{
    (void)ed;
}
__attribute__((weak)) void vs_tui_editor_move_down(VSTUIEditor *ed)
{
    (void)ed;
}
__attribute__((weak)) void vs_tui_editor_move_left(VSTUIEditor *ed)
{
    (void)ed;
}
__attribute__((weak)) void vs_tui_editor_move_right(VSTUIEditor *ed)
{
    (void)ed;
}
__attribute__((weak)) void vs_tui_editor_move_home(VSTUIEditor *ed)
{
    (void)ed;
}
__attribute__((weak)) void vs_tui_editor_move_end(VSTUIEditor *ed)
{
    (void)ed;
}
__attribute__((weak)) int vs_tui_editor_get_text(const VSTUIEditor *ed, char *out, size_t cap)
{
    (void)ed; (void)out; (void)cap; return 0;
}
__attribute__((weak)) void vs_tui_editor_render(const VSTUIEditor *ed, int row, int col, int width)
{
    (void)ed; (void)row; (void)col; (void)width;
}
__attribute__((weak)) char *vs_tui_editor_wrap_line(const char *line, int width, char *out, size_t cap)
{
    (void)line; (void)width; (void)out; (void)cap; return out;
}

/* ---- Phase 7: Reasoning engine stubs ---- */
__attribute__((weak)) void vs_reasoning_init(VSReasoningEngine *eng)
{
    (void)eng;
}
__attribute__((weak)) char *vs_reasoning_analyze(const VSReasoningEngine *eng, const char *task)
{
    (void)eng; (void)task; return mdup("reasoning analysis stub");
}
__attribute__((weak)) char *vs_reasoning_decompose(const VSReasoningEngine *eng, const char *task)
{
    (void)eng; (void)task; return mdup("task decomposition stub");
}
__attribute__((weak)) int vs_reasoning_confidence(const VSReasoningEngine *eng, const char *task, const char *action)
{
    (void)eng; (void)task; (void)action; return 50;
}
__attribute__((weak)) const char *vs_reasoning_confidence_label(int score)
{
    (void)score; return "medium";
}
__attribute__((weak)) char *vs_reasoning_self_correct(const VSReasoningEngine *eng, const char *task, const char *tool, const char *error)
{
    (void)eng; (void)task; (void)tool; (void)error; return mdup("self-correction stub");
}
__attribute__((weak)) char *vs_reasoning_suggest(const VSReasoningEngine *eng, const char *task, const char *last_action, const char *last_result)
{
    (void)eng; (void)task; (void)last_action; (void)last_result; return mdup("suggestion stub");
}
__attribute__((weak)) int vs_reasoning_should_analyze(const VSReasoningEngine *eng, const char *task)
{
    (void)eng; (void)task; return 0;
}
__attribute__((weak)) char *vs_reasoning_prompt_fragment(const VSReasoningEngine *eng)
{
    (void)eng; return mdup("reasoning prompt stub");
}

/* ---- Phase 8: Rewind stubs ---- */
__attribute__((weak)) void vs_rewind_init(VSRewindStore *store)
{
    if (store) memset(store, 0, sizeof(*store));
}
__attribute__((weak)) int vs_rewind_mark(VSRewindStore *store, const char *label)
{
    (void)store; (void)label; return -1;
}
__attribute__((weak)) int vs_rewind_undo(VSRewindStore *store, VSContext *ctx, int n)
{
    (void)store; (void)ctx; (void)n; return 0;
}
__attribute__((weak)) int vs_rewind_to_marker(VSRewindStore *store, VSContext *ctx, const char *label)
{
    (void)store; (void)ctx; (void)label; return -1;
}
__attribute__((weak)) int vs_rewind_checkpoint_rollback(VSRewindStore *store, VSCheckpointStore *cp_store)
{
    (void)store; (void)cp_store; return -1;
}
__attribute__((weak)) char *vs_rewind_list(VSRewindStore *store, int max_lines)
{
    (void)store; (void)max_lines; return mdup("Rewind not available in test environment");
}
__attribute__((weak)) void vs_rewind_clear(VSRewindStore *store)
{
    (void)store;
}

/* ---- Phase 8: Security Review stubs ---- */
__attribute__((weak)) int vs_security_review_start(const char *path, const char *scope,
                                                    VSSecurityFinding *findings, int max_findings)
{
    (void)path; (void)scope; (void)findings; (void)max_findings; return 0;
}
__attribute__((weak)) char *vs_security_review_render(const VSSecurityFinding *findings, int count, int max_lines)
{
    (void)findings; (void)count; (void)max_lines; return mdup("Security review not available in test environment");
}
__attribute__((weak)) const char *vs_security_severity_label(VSSecuritySeverity sev)
{
    (void)sev; return "UNKNOWN";
}

/* ---- vs_chat stub (needed by loop.c tests) ---- */
__attribute__((weak)) char *vs_chat(VSContext *ctx, const char *user_text)
{
    (void)ctx; (void)user_text; return mdup("loop result stub");
}
__attribute__((weak)) void vs_history_add(VSContext *ctx, const char *role, const char *content)
{
    (void)ctx; (void)role; (void)content;
}

/* ---- Phase 8: Loop stubs ---- */
__attribute__((weak)) void vs_loop_init(VSContext *ctx)
{
    if (ctx) {
        memset(ctx->loops, 0, sizeof(ctx->loops));
        ctx->loop_count = 0;
        ctx->loop_thread_active = 0;
    }
}
__attribute__((weak)) int vs_loop_add(VSContext *ctx, const char *prompt, int interval_seconds)
{
    (void)ctx; (void)prompt; (void)interval_seconds; return -1;
}
__attribute__((weak)) int vs_loop_remove(VSContext *ctx, int id)
{
    (void)ctx; (void)id; return -1;
}
__attribute__((weak)) int vs_loop_stop_all(VSContext *ctx)
{
    (void)ctx; return -1;
}
__attribute__((weak)) char *vs_loop_list(VSContext *ctx, int max_lines)
{
    (void)ctx; (void)max_lines; return mdup("No active loops");
}
__attribute__((weak)) void vs_loop_shutdown(VSContext *ctx)
{
    (void)ctx;
}

/* ---- Phase 8: Workflow Engine stubs ---- */
__attribute__((weak)) void vs_workflow_engine_init(VSWorkflowEngine *engine)
{
    if (engine) memset(engine, 0, sizeof(*engine));
}
__attribute__((weak)) int vs_workflow_engine_parse(VSWorkflowEngine *engine, const char *json)
{
    (void)engine; (void)json; return 0;
}
__attribute__((weak)) int vs_workflow_engine_run(VSWorkflowEngine *engine, VSContext *ctx)
{
    (void)engine; (void)ctx; return -1;
}
__attribute__((weak)) char *vs_workflow_engine_status(VSWorkflowEngine *engine, int max_lines)
{
    (void)engine; (void)max_lines; return mdup("No workflow");
}
__attribute__((weak)) void vs_workflow_engine_clear(VSWorkflowEngine *engine)
{
    (void)engine;
}

/* ---- Phase 8: Cross-Session stubs ---- */
__attribute__((weak)) void vs_cross_session_init(VSCrossSessionStore *store)
{
    if (store) memset(store, 0, sizeof(*store));
}
__attribute__((weak)) int vs_cross_session_discover(VSCrossSessionStore *store)
{
    (void)store; return 0;
}
__attribute__((weak)) int vs_cross_session_send(VSCrossSessionStore *store, long to_pid, const char *message)
{
    (void)store; (void)to_pid; (void)message; return -1;
}
__attribute__((weak)) int vs_cross_session_receive(VSCrossSessionStore *store)
{
    (void)store; return 0;
}
__attribute__((weak)) char *vs_cross_session_list(VSCrossSessionStore *store, int max_lines)
{
    (void)store; (void)max_lines; return mdup("No sessions");
}
__attribute__((weak)) char *vs_cross_session_inbox(VSCrossSessionStore *store, int max_lines)
{
    (void)store; (void)max_lines; return mdup("No messages");
}
__attribute__((weak)) void vs_cross_session_shutdown(VSCrossSessionStore *store)
{
    (void)store;
}

/* ---- Phase 9: BTW ---- */
__attribute__((weak)) void vs_btw_init(VSBTWStore *store) { (void)store; }
__attribute__((weak)) int vs_btw_add(VSBTWStore *store, const char *prompt, const char *response)
{ (void)store; (void)prompt; (void)response; return -1; }
__attribute__((weak)) char *vs_btw_list(VSBTWStore *store, int max_lines)
{ (void)store; (void)max_lines; return mdup("No BTW messages"); }
__attribute__((weak)) void vs_btw_clear(VSBTWStore *store) { (void)store; }

/* ---- Phase 9: Plan mode ---- */
__attribute__((weak)) void vs_plan_mode_init(VSPlanMode *mode) { (void)mode; }
__attribute__((weak)) void vs_plan_mode_enable(VSPlanMode *mode) { (void)mode; }
__attribute__((weak)) void vs_plan_mode_disable(VSPlanMode *mode) { (void)mode; }
__attribute__((weak)) char *vs_plan_mode_preview(const VSPlanMode *mode)
{ (void)mode; return mdup("No preview"); }

/* ---- Phase 9: Custom slash commands ---- */
__attribute__((weak)) void vs_custom_slash_init(VSCustomSlashStore *store, const char *dir)
{ (void)store; (void)dir; }
__attribute__((weak)) int vs_custom_slash_load_dir(VSCustomSlashStore *store)
{ (void)store; return 0; }
__attribute__((weak)) VSCustomSlashCommand *vs_custom_slash_find(VSCustomSlashStore *store, const char *name)
{ (void)store; (void)name; return NULL; }
__attribute__((weak)) char *vs_custom_slash_execute(VSCustomSlashStore *store, const char *name, const char *args)
{ (void)store; (void)name; (void)args; return mdup("Not found"); }
__attribute__((weak)) char *vs_custom_slash_list(VSCustomSlashStore *store, int max_lines)
{ (void)store; (void)max_lines; return mdup("No custom commands"); }

/* ---- Phase 9: Markdown table ---- */
__attribute__((weak)) int vs_table_parse(const char *text, VSTable *table)
{ (void)text; (void)table; return 0; }
__attribute__((weak)) char *vs_table_render(const VSTable *table, int width)
{ (void)table; (void)width; return mdup(""); }

/* ---- Phase 9: @-mention ---- */
__attribute__((weak)) int vs_mention_parse(const char *text, VSMention *mentions, int max_mentions)
{ (void)text; (void)mentions; (void)max_mentions; return 0; }
__attribute__((weak)) char *vs_mention_resolve(VSContext *ctx, const char *mention_text, int type)
{ (void)ctx; (void)mention_text; (void)type; return mdup(""); }
__attribute__((weak)) char *vs_mention_inject_context(const VSMention *mentions, int count, const char *original)
{ (void)mentions; (void)count; (void)original; return mdup(""); }

/* ---- Dependencies used by phase9.c in test builds ---- */
__attribute__((weak)) VSMemoryEntry *vs_memory_get(VSMemoryStore *store, const char *key)
{ (void)store; (void)key; return NULL; }
__attribute__((weak)) char *vs_http_post(const char *url, const char *headers[], int nheaders, const char *body, long *status)
{ (void)url; (void)headers; (void)nheaders; (void)body; (void)status; return NULL; }
__attribute__((weak)) VSSkill *vs_skill_find(VSSkill *skills, int count, const char *name)
{ (void)skills; (void)count; (void)name; return NULL; }
__attribute__((weak)) char *vs_run_command(const char *cmd, int *exit_code)
{ (void)cmd; (void)exit_code; return mdup(""); }

/* ---- Phase 9: Extended effort ---- */
__attribute__((weak)) const char *vs_effort_label(int effort)
{ (void)effort; return "default"; }
__attribute__((weak)) int vs_effort_from_string(const char *s)
{ (void)s; return 0; }

/* ---- Phase 9: PreCompact hooks ---- */
__attribute__((weak)) int vs_hooks_run_pre_compact(VSHooksStore *store, const char *context_summary)
{ (void)store; (void)context_summary; return 0; }

/* ---- Phase 9: Git branch automation ---- */
__attribute__((weak)) char *vs_git_branch_create(const char *name, const char *path)
{ (void)name; (void)path; return mdup("Branch created"); }
__attribute__((weak)) char *vs_git_branch_list(const char *path, int max_lines)
{ (void)path; (void)max_lines; return mdup("No branches"); }
__attribute__((weak)) char *vs_git_diff_staged(const char *path, int max_lines)
{ (void)path; (void)max_lines; return mdup("No staged changes"); }
__attribute__((weak)) char *vs_git_commit_generate_message(const char *path)
{ (void)path; return mdup("Auto-generated commit"); }
__attribute__((weak)) char *vs_git_pr_description(const char *path, const char *branch, int max_lines)
{ (void)path; (void)branch; (void)max_lines; return mdup("PR description"); }

