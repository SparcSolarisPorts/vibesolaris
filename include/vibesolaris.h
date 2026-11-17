/* SPDX-License-Identifier: Unlicense */
#ifndef VIBESOLARIS_H
#define VIBESOLARIS_H

#define VS_VERSION "0.12.0"

#include <stddef.h>
#include <signal.h>
#include <pthread.h>

#define VS_MAX_ATTACH 16
#define VS_MAX_PATH 1024
#define VS_MAX_TEXT (1024*1024)
#define VS_MAX_TOOL_ROUNDS 64
#define VS_MAX_MALFORMED_RECOVERIES 3 /* malformed-batch / missing-task_id re-emit attempts per agent turn */
#define VS_MAX_TASK_GATE_RECOVERIES 6 /* blocked/not-ready-task_id re-emit attempts; ordering slips are cheap to repair */
#define VS_MAX_NO_MARKER_RECOVERIES 3 /* prose-only responses (no VS_TOOL/VS_FINAL) before the turn stops instead of looping */
#define VS_MAX_PLAN_CONTINUES 16
#define VS_MIN_PLAN_EXPLORE_ROUNDS 2 /* read-only exploration rounds expected before an Auto-mode plan is adopted */
#define VS_MAX_PLAN_DEFERRALS 3      /* times an under-researched Auto-mode plan is sent back for verification */
#define VS_MAX_TOOL_RECOVERIES 12
#define VS_ACTIVE_TASK_MAX (16U*1024U)
#define VS_LONG_TASK_CHECKPOINT 8
#define VS_AGENT_COMPACT_ROUNDS 8
#define VS_MAX_AGENT_HISTORY 16
#define VS_AGENT_HISTORY_BUDGET (96U*1024U)
#define VS_AGENT_HISTORY_MESSAGE_MAX (24U*1024U)
#define VS_AGENT_CHECKPOINT_MAX (24U*1024U)
#define VS_AGENT_READ_RESULT_MAX (128U*1024U)
#define VS_AGENT_COMMAND_RESULT_MAX (96U*1024U)
#define VS_AGENT_TOOL_RESULT_MAX (192U*1024U)
#define VS_MAX_BATCH_TOOLS 24
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
#define VS_MAX_TRACE 512
#define VS_TRACE_EVENT_MAX (128U*1024U)
#define VS_MAX_TRACE_BYTES (8U*1024U*1024U)
#define VS_MAX_SUBAGENT_DEPTH 2
#define VS_MAX_SUBAGENTS_PER_TURN 6
#define VS_MAX_SUBAGENT_RECORDS 32

/* ---- Context window management ---- */
#define VS_CONTEXT_WINDOW_DEFAULT 1000000
#define VS_COMPACT_AT_PERCENT 75
#define VS_SUBAGENT_CACHE_MAX 64

/* ---- Auto-memory ---- */
#define VS_MAX_AUTO_MEMORY 32

/* ---- Checkpoints (git snapshots) ---- */
#define VS_MAX_CHECKPOINTS 16
#define VS_CHECKPOINT_ID_MAX 64
#define VS_CHECKPOINT_DESC_MAX 256
#define VS_CHECKPOINT_REF_MAX 256

/* ---- Auto Mode (action classifier) ---- */
#define VS_AUTO_MODE_SAFE 0
#define VS_AUTO_MODE_RISKY 1
#define VS_AUTO_MODE_BLOCKED 2
#define VS_MAX_AUTO_MODE_RULES 64
#define VS_AUTO_MODE_TOOL_MAX 64
#define VS_AUTO_MODE_PATTERN_MAX 256

typedef struct {
    char tool[VS_AUTO_MODE_TOOL_MAX];
    char pattern[VS_AUTO_MODE_PATTERN_MAX];
    int classification;  /* VS_AUTO_MODE_SAFE, RISKY, BLOCKED */
} VSAutoModeRule;

typedef struct {
    VSAutoModeRule rules[VS_MAX_AUTO_MODE_RULES];
    int rule_count;
    int enabled;
    char config_path[VS_MAX_PATH];
} VSAutoModeStore;

/* ---- Agent Teams (peer-to-peer) ---- */
#define VS_MAX_TEAMS 4
#define VS_MAX_TEAM_AGENTS 8
#define VS_TEAM_ID_MAX 64
#define VS_TEAM_AGENT_ID_MAX 64
#define VS_TEAM_ROLE_MAX 64

typedef struct {
    char id[VS_TEAM_AGENT_ID_MAX];
    char role[VS_TEAM_ROLE_MAX];
    int active;
} VSTeamAgent;

typedef struct {
    char id[VS_TEAM_ID_MAX];
    VSTeamAgent agents[VS_MAX_TEAM_AGENTS];
    int agent_count;
    char scratch_dir[VS_MAX_PATH];
    int active;
} VSTeam;

/* ---- Plugins ---- */
#define VS_MAX_PLUGINS 16
#define VS_PLUGIN_NAME_MAX 128
#define VS_PLUGIN_VERSION_MAX 64
#define VS_PLUGIN_DESC_MAX 512

typedef struct {
    char name[VS_PLUGIN_NAME_MAX];
    char version[VS_PLUGIN_VERSION_MAX];
    char description[VS_PLUGIN_DESC_MAX];
    char path[VS_MAX_PATH];
    long installed_at;
} VSPlugin;

/* ---- Code Review ---- */
#define VS_MAX_REVIEW_FINDINGS 64
#define VS_REVIEW_FILE_MAX 512
#define VS_REVIEW_DESC_MAX 1024

typedef struct {
    char file[VS_REVIEW_FILE_MAX];
    int line;
    int severity;  /* 0=info, 1=warning, 2=error */
    char category[32];
    char description[VS_REVIEW_DESC_MAX];
} VSCodeReviewFinding;

/* ---- Security Review (CWE-based vulnerability scanning) ---- */
#define VS_MAX_SECURITY_FINDINGS 128
#define VS_SECURITY_CWE_MAX 16
#define VS_SECURITY_DESC_MAX 512

typedef enum {
    VS_SEC_SEVERITY_LOW = 0,
    VS_SEC_SEVERITY_MEDIUM,
    VS_SEC_SEVERITY_HIGH,
    VS_SEC_SEVERITY_CRITICAL
} VSSecuritySeverity;

typedef struct {
    char file[VS_REVIEW_FILE_MAX];
    int line;
    VSSecuritySeverity severity;
    char cwe[VS_SECURITY_CWE_MAX];  /* e.g., "CWE-79" */
    char category[32];
    char description[VS_SECURITY_DESC_MAX];
} VSSecurityFinding;

/* ---- Keybindings ---- */
#define VS_MAX_KEYBINDINGS 16
#define VS_KEYBINDING_KEY_MAX 32
#define VS_KEYBINDING_CMD_MAX 256

typedef struct {
    char key[VS_KEYBINDING_KEY_MAX];
    char command[VS_KEYBINDING_CMD_MAX];
} VSKeybinding;

/* ---- Output styles ---- */
#define VS_OUTPUT_STYLE_DEFAULT 0
#define VS_OUTPUT_STYLE_COMPACT 1
#define VS_OUTPUT_STYLE_VERBOSE 2

/* ---- Computer Use backends ---- */
#define VS_COMPUTER_USE_NONE 0
#define VS_COMPUTER_USE_X11 1
#define VS_COMPUTER_USE_WAYLAND 2
#define VS_COMPUTER_USE_COCOA 3

typedef struct {
    char id[VS_CHECKPOINT_ID_MAX];
    long timestamp;
    char git_ref[VS_CHECKPOINT_REF_MAX];
    char description[VS_CHECKPOINT_DESC_MAX];
} VSCheckpointEntry;

typedef struct {
    VSCheckpointEntry entries[VS_MAX_CHECKPOINTS];
    int count;
    int is_git_repo;
    char dir[VS_MAX_PATH];
    char file[VS_MAX_PATH];
} VSCheckpointStore;

typedef struct {
    unsigned long task_hash;
    char *result;
    long timestamp;
} VSSubagentCacheEntry;

/* ---- Agent coordinator (parallel sub-agents) ---- */
#define VS_MAX_COORDINATOR_SLOTS 8
#define VS_COORDINATOR_WORKTREE_PREFIX ".vibesolaris-worktree"
#define VS_COORDINATOR_TASK_MAX 512
#define VS_COORDINATOR_RESULT_MAX (48U*1024U)

typedef enum {
    VS_AGENT_STATUS_IDLE = 0,
    VS_AGENT_STATUS_RUNNING,
    VS_AGENT_STATUS_COMPLETED,
    VS_AGENT_STATUS_FAILED,
    VS_AGENT_STATUS_CANCELLED
} VSAgentStatus;

typedef struct {
    int id;
    int parent_id;
    int depth;
    VSAgentStatus status;
    long started_at;
    long finished_at;
    char task[VS_COORDINATOR_TASK_MAX];
    char result[VS_COORDINATOR_RESULT_MAX];
    char worktree[VS_MAX_PATH];
    pthread_t thread;
    int thread_active;
} VSCoordinatorAgent;

typedef struct {
    VSCoordinatorAgent agents[VS_MAX_COORDINATOR_SLOTS];
    int agent_count;
    int next_id;
    pthread_mutex_t lock;
    volatile sig_atomic_t cancel_all;
} VSCoordinator;

/* ---- Full-screen TUI compositor ---- */
#define VS_TUI_SCROLLBACK_LINES 512
#define VS_TUI_LINE_MAX 1024
#define VS_TUI_SIDEBAR_WIDTH 32

typedef enum {
    VS_TUI_MODE_PROMPT = 0,    /* classic prompt mode (SSH-safe default) */
    VS_TUI_MODE_FULLSCREEN     /* alternate-screen compositor */
} VSTUIMode;

typedef struct {
    char lines[VS_TUI_SCROLLBACK_LINES][VS_TUI_LINE_MAX];
    int line_count;
    int scroll_offset;          /* lines from end visible in viewport */
    int term_rows;
    int term_cols;
    int sidebar_visible;
    int sidebar_width;
    VSTUIMode mode;
    int alt_screen_ok;          /* whether DECSET ?1049 works */
    /* streaming state */
    int streaming;
    char stream_buf[4096];
    int stream_pos;
    /* input state */
    char input_buf[4096];
    int input_len;
    int input_cursor;
    int input_history_pos;
    char input_history[32][4096];
    int input_history_count;
    /* mouse support */
    int mouse_enabled;          /* enable mouse tracking (CSI ?1000h) */
    int mouse_row;              /* last mouse click row (1-based) */
    int mouse_col;              /* last mouse click col (1-based) */
    /* scrollback search */
    int search_mode;            /* 1 = in search mode */
    char search_query[256];     /* current search query */
    int search_result_count;    /* number of search matches */
    int search_result_lines[64]; /* matched line indices */
    int search_current;         /* current match index */
} VSTUICompositor;

/* ---- Parallel tool executor ---- */
#define VS_MAX_PARALLEL_TOOLS 8

typedef struct {
    char directive[1024];
    int is_mcp;
    char *result;
    int done;
    int error;
} VSToolJob;

typedef struct {
    VSToolJob jobs[VS_MAX_PARALLEL_TOOLS];
    int job_count;
    int parallel_readonly;     /* allow read-only tools in parallel */
    int timeout_seconds;       /* per-tool timeout (0 = unlimited) */
} VSToolExecutor;

/* ---- Skill system ---- */
#define VS_MAX_SKILLS 16
#define VS_SKILL_NAME_MAX 64
#define VS_SKILL_PATH_MAX 512
#define VS_SKILL_PROMPT_MAX (32U*1024U)

typedef enum {
    VS_SKILL_MODE_INLINE = 0,
    VS_SKILL_MODE_FORK
} VSSkillMode;

typedef struct {
    char name[VS_SKILL_NAME_MAX];
    char path[VS_SKILL_PATH_MAX];
    char prompt[VS_SKILL_PROMPT_MAX];
    VSSkillMode mode;
    int enabled;
} VSSkill;

/* ---- Persistent memory ---- */
#define VS_MAX_MEMORY_ENTRIES 128
#define VS_MEMORY_KEY_MAX 256
#define VS_MEMORY_VALUE_MAX (8U*1024U)
#define VS_MEMORY_TAG_MAX 64

typedef enum {
    VS_MEMORY_TYPE_NOTE = 0,
    VS_MEMORY_TYPE_PREF,
    VS_MEMORY_TYPE_CONTEXT,
    VS_MEMORY_TYPE_SKILL_REF
} VSMemoryType;

typedef struct {
    char key[VS_MEMORY_KEY_MAX];
    char value[VS_MEMORY_VALUE_MAX];
    char tag[VS_MEMORY_TAG_MAX];
    VSMemoryType type;
    long created_at;
    long accessed_at;
    int access_count;
} VSMemoryEntry;

typedef struct {
    VSMemoryEntry entries[VS_MAX_MEMORY_ENTRIES];
    int entry_count;
    char store_path[VS_MAX_PATH];
} VSMemoryStore;

typedef struct {
    int merged;       /* duplicate entries merged */
    int pruned;       /* stale entries removed */
    int normalized;   /* dates normalized */
    int deduped_tags; /* tag duplicates merged */
    int total_before; /* entries before dream */
    int total_after;  /* entries after dream */
} VSDreamStats;

#define VS_MAX_PLAN_TASKS 16
#define VS_MAX_TASK_DEPS 4
#define VS_PLAN_TITLE_MAX 128
#define VS_TASK_TITLE_MAX 192

/* ---- TodoWrite: visible session task list ---- */
#define VS_MAX_TODO_ITEMS 32
#define VS_TODO_ITEM_MAX 256

typedef enum {
    VS_TODO_STATUS_PENDING = 0,
    VS_TODO_STATUS_IN_PROGRESS,
    VS_TODO_STATUS_COMPLETED,
    VS_TODO_STATUS_CANCELLED
} VSTodoStatus;

typedef struct {
    char text[VS_TODO_ITEM_MAX];
    VSTodoStatus status;
} VSTodoItem;

typedef struct {
    VSTodoItem items[VS_MAX_TODO_ITEMS];
    int item_count;
    int active;
} VSTodoList;

/* ---- Edit tool: targeted file modifications ---- */
#define VS_EDIT_DIFF_MAX (64U*1024U)

/* ---- Glob tool: pattern-based file discovery ---- */
#define VS_MAX_GLOB_RESULTS 512

/* ---- Grep tool: content search with context ---- */
#define VS_MAX_GREP_RESULTS 200
#define VS_GREP_LINE_MAX 2048

/* ---- Notebook tools: Jupyter .ipynb read/edit ---- */
#define VS_MAX_NOTEBOOK_CELLS 64
#define VS_NOTEBOOK_CELL_MAX (32U*1024U)

/* ---- Monitor tool: background process management ---- */
#define VS_MAX_MONITORS 8
#define VS_MONITOR_OUTPUT_MAX (64U*1024U)

/* ---- MultiEdit tool: multi-hunk file editing ---- */
#define VS_MAX_MULTI_EDIT_PAIRS 16

/* ---- LSP tool: Language Server Protocol ---- */
#define VS_LSP_MSG_MAX (32U*1024U)
#define VS_MAX_LSP_SERVERS 4
#define VS_LSP_REQUEST_TIMEOUT 10  /* seconds */

/* ---- Multi-layer settings system ---- */
#define VS_SETTINGS_KEY_MAX 128
#define VS_SETTINGS_VALUE_MAX 1024
#define VS_SETTINGS_MAX_KEYS 64

typedef enum {
    VS_SETTINGS_LAYER_USER = 0,
    VS_SETTINGS_LAYER_PROJECT,
    VS_SETTINGS_LAYER_LOCAL
} VSSettingsLayer;

typedef struct {
    char key[VS_SETTINGS_KEY_MAX];
    char value[VS_SETTINGS_VALUE_MAX];
    int layer;            /* VSSettingsLayer */
} VSSettingsEntry;

typedef struct {
    VSSettingsEntry entries[VS_SETTINGS_MAX_KEYS];
    int entry_count;
    char user_path[VS_MAX_PATH];
    char project_path[VS_MAX_PATH];
    char local_path[VS_MAX_PATH];
    char project_root[VS_MAX_PATH];
    int user_loaded;
    int project_loaded;
    int local_loaded;
} VSSettingsStore;

typedef struct {
    long pid;
    int in_fd;
    int out_fd;
    int initialized;
    int error;
    char root_path[VS_MAX_PATH];
    char server_cmd[512];
    unsigned long next_id;
    char io_buf[VS_LSP_MSG_MAX];
    size_t io_pos;
    size_t io_len;
} VSLSPServer;

/* ---- Permission rules: tool-level allow/deny ---- */
#define VS_MAX_PERMISSION_RULES 32
#define VS_PERMISSION_TOOL_MAX 64
#define VS_PERMISSION_PATH_MAX 256

typedef struct {
    char tool_pattern[VS_PERMISSION_TOOL_MAX];
    char path_pattern[VS_PERMISSION_PATH_MAX];
    int allow;              /* 1 = allow, 0 = deny */
    long added_at;
} VSPermissionRule;

typedef struct {
    VSPermissionRule rules[VS_MAX_PERMISSION_RULES];
    int rule_count;
} VSPermissionStore;

/* ---- Task management: cross-session tasks ---- */
#define VS_MAX_TASK_ENTRIES 64
#define VS_TASK_DESC_MAX (4U*1024U)

/* ---- Slash commands ---- */
#define VS_MAX_SLASH_COMMANDS 16
#define VS_MAX_SLASH_NAME 64
#define VS_MAX_SLASH_CMD 512
#define VS_MAX_SLASH_DESC 256
#define VS_MAX_SLASH_PATH VS_MAX_PATH

typedef struct {
    char name[VS_MAX_SLASH_NAME];
    char command[VS_MAX_SLASH_CMD];
    char description[VS_MAX_SLASH_DESC];
    long added_at;
} VSSlashCommand;

typedef struct {
    VSSlashCommand commands[VS_MAX_SLASH_COMMANDS];
    int command_count;
    char store_path[VS_MAX_SLASH_PATH];
} VSSlashStore;

/* ---- Hooks system ---- */
#define VS_MAX_HOOKS 16
#define VS_HOOK_MATCHER_MAX 256
#define VS_HOOK_CMD_MAX 512
#define VS_HOOK_PRE_TOOL_USE 0
#define VS_HOOK_POST_TOOL_USE 1
#define VS_HOOK_MESSAGE_DISPLAY 2
#define VS_HOOK_PROMPT 3
#define VS_HOOK_HTTP 4

typedef struct {
    char matcher[VS_HOOK_MATCHER_MAX];
    char command[VS_HOOK_CMD_MAX];
    int hook_type;        /* VS_HOOK_* */
    int is_pre;           /* 1 = PreToolUse, 0 = PostToolUse (legacy compat) */
    int async;            /* 1 = run asynchronously */
    char webhook_url[VS_MAX_PATH];  /* HTTP webhook URL */
    long added_at;
} VSHook;

typedef struct {
    VSHook hooks[VS_MAX_HOOKS];
    int hook_count;
    char config_path[VS_MAX_PATH];
} VSHooksStore;

/* ---- Phase 7: Reasoning engine ---- */
typedef struct {
    int enabled;          /* 1 = reasoning enabled */
    int depth;            /* 1=minimal, 2=standard, 3=deep */
} VSReasoningEngine;

/* ---- Phase 8: Loop (scheduled recurring prompts) ---- */
#define VS_MAX_LOOPS 16
#define VS_LOOP_PROMPT_MAX 1024

typedef struct {
    int id;
    int active;
    char prompt[VS_LOOP_PROMPT_MAX];
    int interval_seconds;
    long last_run;
    int run_count;
    int last_result;
} VSLoopEntry;

/* ---- Phase 8: Dynamic Workflows (JSON-based orchestration) ---- */
#define VS_WORKFLOW_ENGINE_MAX_STEPS 8
#define VS_WORKFLOW_ENGINE_MAX_PARALLEL 4
#define VS_WORKFLOW_ENGINE_NAME_MAX 128
#define VS_WORKFLOW_ENGINE_PROMPT_MAX 256
#define VS_WORKFLOW_ENGINE_RESULT_MAX 128
#define VS_WORKFLOW_ENGINE_CONDITION_MAX 64

typedef enum {
    VS_WORKFLOW_STEP_SEQUENTIAL = 0,
    VS_WORKFLOW_STEP_PARALLEL,
    VS_WORKFLOW_STEP_CONDITIONAL
} VSWorkflowStepType;

typedef enum {
    VS_WORKFLOW_STEP_PENDING = 0,
    VS_WORKFLOW_STEP_RUNNING,
    VS_WORKFLOW_STEP_SUCCESS,
    VS_WORKFLOW_STEP_FAILED,
    VS_WORKFLOW_STEP_SKIPPED
} VSWorkflowStepStatus;

typedef enum {
    VS_WF_ENGINE_IDLE = 0,
    VS_WF_ENGINE_RUNNING,
    VS_WF_ENGINE_COMPLETED,
    VS_WF_ENGINE_FAILED,
    VS_WF_ENGINE_CANCELLED
} VSWorkflowEngineStatus;

typedef struct {
    char prompt[VS_WORKFLOW_ENGINE_PROMPT_MAX];
    char result[VS_WORKFLOW_ENGINE_RESULT_MAX];
    VSWorkflowStepStatus status;
} VSWorkflowParallelTask;

typedef struct {
    int id;
    VSWorkflowStepType type;
    VSWorkflowStepStatus status;
    char prompt[VS_WORKFLOW_ENGINE_PROMPT_MAX];
    char result[VS_WORKFLOW_ENGINE_RESULT_MAX];
    char condition[VS_WORKFLOW_ENGINE_CONDITION_MAX];
    VSWorkflowParallelTask parallel_tasks[VS_WORKFLOW_ENGINE_MAX_PARALLEL];
    int parallel_task_count;
} VSWorkflowStep;

typedef struct {
    char name[VS_WORKFLOW_ENGINE_NAME_MAX];
    VSWorkflowEngineStatus status;
    VSWorkflowStep steps[VS_WORKFLOW_ENGINE_MAX_STEPS];
    int step_count;
    int current_step;
} VSWorkflowEngine;

/* ---- Phase 9: BTW (side comments) ---- */
#define VS_MAX_BTW_MESSAGES 8
#define VS_BTW_MESSAGE_MAX 512

typedef struct {
    char prompt[VS_BTW_MESSAGE_MAX];
    char response[VS_BTW_MESSAGE_MAX];
    long timestamp;
} VSBTWMessage;

typedef struct {
    VSBTWMessage messages[VS_MAX_BTW_MESSAGES];
    int message_count;
} VSBTWStore;

/* ---- Phase 9: Plan mode (propose without execute) ---- */
#define VS_PLAN_PREVIEW_MAX 1024

typedef struct {
    int enabled;              /* 1 = plan mode active */
    char preview[VS_PLAN_PREVIEW_MAX]; /* last plan preview output */
    int preview_len;
} VSPlanMode;

/* ---- Phase 9: Custom slash commands from markdown ---- */
#define VS_MAX_CUSTOM_SLASH 8
#define VS_CUSTOM_SLASH_NAME_MAX 64
#define VS_CUSTOM_SLASH_BODY_MAX 1024

typedef struct {
    char name[VS_CUSTOM_SLASH_NAME_MAX];
    char body[VS_CUSTOM_SLASH_BODY_MAX];
    char path[VS_MAX_PATH];
    long loaded_at;
} VSCustomSlashCommand;

typedef struct {
    VSCustomSlashCommand commands[VS_MAX_CUSTOM_SLASH];
    int command_count;
    char commands_dir[VS_MAX_PATH];
} VSCustomSlashStore;

/* ---- Phase 9: Markdown table rendering ---- */
#define VS_TABLE_MAX_COLS 8
#define VS_TABLE_MAX_ROWS 16
#define VS_TABLE_CELL_MAX 64

typedef struct {
    char cells[VS_TABLE_MAX_COLS][VS_TABLE_CELL_MAX];
    int col_count;
} VSTableRow;

typedef struct {
    VSTableRow rows[VS_TABLE_MAX_ROWS];
    int row_count;
    int col_count;
    int header_row; /* 1 = first row is header */
} VSTable;

/* ---- Phase 9: @-mention context resolution ---- */
#define VS_MAX_MENTIONS 4
#define VS_MENTION_TEXT_MAX 64
#define VS_MENTION_RESOLVED_MAX 256

typedef struct {
    char text[VS_MENTION_TEXT_MAX];
    char resolved[VS_MENTION_RESOLVED_MAX];
    int type; /* 0=file, 1=memory, 2=skill, 3=task */
} VSMention;

/* ---- Phase 9: Extended effort levels ---- */
#define VS_EFFORT_LOW 1
#define VS_EFFORT_MEDIUM 2
#define VS_EFFORT_HIGH 3
#define VS_EFFORT_XHIGH 4
#define VS_EFFORT_MAX 5
#define VS_EFFORT_ULTRACODE 6

/* ---- Phase 8: Rewind (conversation-level undo) ---- */
#define VS_REWIND_MARKER_MAX 64
#define VS_REWIND_MARKER_LABEL_MAX 128
#define VS_REWIND_UNDO_LOG_MAX 48
#define VS_REWIND_UNDO_DESC_MAX 256

typedef struct {
    char label[VS_REWIND_MARKER_LABEL_MAX];
    int history_count;    /* history count at marker time */
    long timestamp;
} VSRewindMarker;

typedef struct {
    int messages_removed;
    long timestamp;
    char description[VS_REWIND_UNDO_DESC_MAX];
} VSRewindUndoLog;

typedef struct {
    VSRewindMarker markers[VS_REWIND_MARKER_MAX];
    int marker_count;
    VSRewindUndoLog undo_log[VS_REWIND_UNDO_LOG_MAX];
    int undo_log_count;
    int undo_count;
    int last_history_count;
} VSRewindStore;

typedef enum {
    VS_TASK_PRIORITY_LOW = 0,
    VS_TASK_PRIORITY_MEDIUM,
    VS_TASK_PRIORITY_HIGH
} VSTaskPriority;

typedef enum {
    VS_TASK_STATUS_PENDING = 0,
    VS_TASK_STATUS_IN_PROGRESS,
    VS_TASK_STATUS_COMPLETED,
    VS_TASK_STATUS_CANCELLED
} VSTaskEntryStatus;

typedef struct {
    int id;
    char title[VS_TASK_TITLE_MAX];
    char description[VS_TASK_DESC_MAX];
    int status;             /* VSTaskEntryStatus */
    int priority;           /* VSTaskPriority */
    long created_at;
    long updated_at;
} VSTaskEntry;

typedef struct {
    VSTaskEntry tasks[VS_MAX_TASK_ENTRIES];
    int task_count;
    int next_id;
    char store_path[VS_MAX_PATH];
} VSTaskStore;

#define VS_NEED_USER_MAX_OPTIONS 8
#define VS_NEED_USER_TEXT_MAX 512
#define VS_NEED_USER_OPTION_MAX 128
#define VS_SUBAGENT_RESULT_MAX (48U*1024U)

/* Resource guards for long-running/large agent operations.  These are deliberately
   generous, but finite: a provider, command, or MCP server must not be able to
   grow the process until the desktop starts swapping or the GUI appears dead. */
#define VS_MAX_HTTP_RESPONSE (64U*1024U*1024U)
#define VS_MAX_REQUEST_BODY (96U*1024U*1024U)
#define VS_MAX_COMMAND_CAPTURE (1U*1024U*1024U)
#define VS_MAX_ATTACHMENT_TEXT (4U*1024U*1024U)
#define VS_FILE_CACHE_BLOB_MAX (2U*1024U*1024U)


#define VS_GRAPH_MAX_NODES 240
#define VS_GRAPH_MAX_EDGES 960
#define VS_GRAPH_LABEL_MAX 128
#define VS_GRAPH_PATH_MAX 512

typedef enum {
    VS_GRAPH_FILES = 1,
    VS_GRAPH_FIELDS = 2
} VSGraphMode;

typedef struct {
    char label[VS_GRAPH_LABEL_MAX];
    char path[VS_GRAPH_PATH_MAX];
    int kind;
    unsigned long weight;
} VSGraphNode;

typedef struct {
    unsigned short from;
    unsigned short to;
} VSGraphEdge;

typedef struct {
    VSGraphMode mode;
    char root[VS_MAX_PATH];
    VSGraphNode nodes[VS_GRAPH_MAX_NODES];
    VSGraphEdge edges[VS_GRAPH_MAX_EDGES];
    int node_count;
    int edge_count;
    int truncated;
} VSGraph;

#define VS_GLOBAL_CONFIG_DIR "/etc/vibesolaris"
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

/* Each OAuth credential slot is independent so that signing in with one
   provider can never overwrite the other's tokens. */
typedef enum {
    VS_OAUTH_OPENAI = 0,
    VS_OAUTH_CLAUDE = 1
} VSOAuthProvider;

typedef struct {
    int active;
    int listener_fd;
    int port;
    long started_at;
    VSOAuthProvider provider;
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
    char *detail;
    size_t bytes;
} VSTraceEvent;

typedef void (*VSTraceCallback)(void *userdata, int step, const char *kind, const char *detail);
typedef int (*VSHTTPChunkCallback)(void *userdata, const char *data, size_t len);

typedef struct {
    int id;
    int parent_id;
    int depth;
    int status; /* 0 running, 1 completed, 2 stopped, 3 failed/limited */
    long started_at;
    long finished_at;
    char task[256];
    char summary[256];
} VSSubagentRecord;

typedef enum {
    VS_MODE_AUTO = 0,
    VS_MODE_PLAN = 1,
    VS_MODE_MANUAL = 2
} VSExecutionMode;

typedef enum {
    VS_WORKFLOW_IDLE = 0,
    VS_WORKFLOW_PLANNING,
    VS_WORKFLOW_AWAITING_ACCEPT,
    VS_WORKFLOW_DOING,
    VS_WORKFLOW_AWAITING_ACTION,
    VS_WORKFLOW_PAUSED,
    VS_WORKFLOW_COMPLETED,
    VS_WORKFLOW_FAILED,
    VS_WORKFLOW_CANCELLED
} VSWorkflowPhase;

typedef enum {
    VS_TASK_PLANNED = 0,
    VS_TASK_BLOCKED,
    VS_TASK_READY,
    VS_TASK_RUNNING,
    VS_TASK_COMPLETED,
    VS_TASK_FAILED,
    VS_TASK_CANCELLED
} VSTaskStatus;

typedef struct {
    int id;
    int parent_id;
    int dependency_count;
    int dependencies[VS_MAX_TASK_DEPS];
    VSTaskStatus status;
    char title[VS_TASK_TITLE_MAX];
    char summary[256];
} VSTaskRecord;

typedef struct {
    VSExecutionMode mode;
    VSWorkflowPhase phase;
    unsigned long revision;
    unsigned long accepted_revision;
    int task_count;
    int current_task_id;
    int action_approved;
    char title[VS_PLAN_TITLE_MAX];
    char pending_request[4096];
    VSTaskRecord tasks[VS_MAX_PLAN_TASKS];
} VSWorkflow;

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

/* ---- Phase 8: Cross-Session Messaging (types before VSContext) ---- */
#define VS_MAX_SESSION_PEERS 8
#define VS_SESSION_PEER_ID_MAX 32
#define VS_SESSION_MESSAGE_MAX 512
#define VS_MAX_SESSION_MESSAGES 16

typedef struct {
    long pid;
    char id[VS_SESSION_PEER_ID_MAX];
    long last_seen;
    int active;
} VSSessionPeer;

typedef struct {
    long from_pid;
    long timestamp;
    char message[VS_SESSION_MESSAGE_MAX];
} VSSessionMessage;

typedef struct {
    VSSessionPeer peers[VS_MAX_SESSION_PEERS];
    int peer_count;
    VSSessionMessage inbox[VS_MAX_SESSION_MESSAGES];
    int inbox_count;
    long my_pid;
    char register_path[VS_MAX_PATH];
} VSCrossSessionStore;

typedef struct {
    char cwd[VS_MAX_PATH];
    char os_name[128];
    char os_release[128];
    char arch[128];
    char agent_md[VS_MAX_TEXT];
    char claude_md[VS_MAX_TEXT];
    VSAttachment attachments[VS_MAX_ATTACH];
    int attachment_count;
    VSProvider provider;
    int effort; /* 0 provider default, 1 low, 2 medium, 3 high */
    char provider_api_keys[VS_PROVIDER_SLOT_COUNT][512];
    char provider_models[VS_PROVIDER_SLOT_COUNT][128];
    char provider_openai_urls[VS_PROVIDER_SLOT_COUNT][512];
    char provider_anthropic_urls[VS_PROVIDER_SLOT_COUNT][512];
    int provider_protocols[VS_PROVIDER_SLOT_COUNT];
    int provider_anthropic_auth[VS_PROVIDER_SLOT_COUNT]; /* 0 bearer, 1 x-api-key */
    VSOAuthConfig oauth;
    VSOAuthConfig claude_oauth;

    int proxy_enabled;
    char proxy[1024];

    VSMcpServer mcp_servers[VS_MAX_MCP_SERVERS];
    int mcp_server_count;
    VSMcpTool mcp_tools[VS_MAX_MCP_TOOLS];
    int mcp_tool_count;

    VSTraceEvent trace[VS_MAX_TRACE];
    int trace_count;
    unsigned long trace_dropped;
    size_t trace_bytes;
    VSTraceCallback trace_callback;
    void *trace_callback_data;
    VSHTTPChunkCallback stream_callback;
    void *stream_callback_data;
    volatile sig_atomic_t cancel_requested;
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
    unsigned long conversation_cached_tokens;
    unsigned long conversation_cache_write_tokens;

    VSHistoryMessage history[VS_MAX_HISTORY];
    int history_count;
    size_t history_bytes;
    unsigned long history_evicted;

    /* Ephemeral context for the currently executing autonomous turn.  Unlike
       history[], this is discarded after the turn and periodically summarized
       so a long coding task does not grow the provider prompt every round. */
    VSHistoryMessage agent_history[VS_MAX_AGENT_HISTORY];
    int agent_history_count;
    size_t agent_history_bytes;
    char *agent_checkpoint;
    size_t agent_checkpoint_bytes;

    /* Attachments are one-shot within an autonomous turn.  The first request
       sends the user's attachments; later rounds send only newly loaded images. */
    int attachment_send_from;

    /* Subagents run in isolated child contexts (see subagent.c).  At top level
       subagent_parent is NULL.  Children forward cancellation and trace events
       to their parent; they never own the parent's MCP processes or history. */
    void *subagent_parent;
    int subagent_depth;
    int subagent_id;
    int subagent_record_id;
    int subagent_serial;
    void *subagent_root; /* top-level context owns per-turn lifecycle records */
    VSSubagentRecord subagents[VS_MAX_SUBAGENT_RECORDS];
    int subagent_record_count;
    VSWorkflow workflow;
    VSTodoList todo;
    VSPermissionStore permissions;
    VSTaskStore tasks;
    VSSlashStore slashes;
    VSHooksStore hooks;
    VSSkill skills[VS_MAX_SKILLS];
    int skill_count;
    VSMemoryStore memory;

    /* Phase 3: Final features */
    int bare_mode;              /* --bare: skip auto-discovery */
    int ax_mode;                /* --ax-screen-reader: accessibility mode */
    int image_display_enabled;  /* show images in TUI responses */
    char conversation_id[64];   /* unique session ID for save/restore */
    int auto_memory_count;      /* auto-captured memory entries this session */
    int computer_use_available; /* X11 display available for Computer Use */
    VSCheckpointStore checkpoints;
    int checkpoint_enabled;
    long last_escape_time;      /* for Escape x2 detection */

    /* Phase 4: Advanced features */
    VSAutoModeStore auto_mode;
    VSTeam teams[VS_MAX_TEAMS];
    int team_count;
    VSPlugin plugins[VS_MAX_PLUGINS];
    int plugin_count;
    char goal[VS_MAX_TEXT];
    long session_paused_at;     /* timestamp, 0 = not paused */
    int away_summary_enabled;
    int output_style;           /* VS_OUTPUT_STYLE_* */
    int json_output_mode;
    int slash_commands_disabled;
    int shell_passthrough_mode;
    VSKeybinding keybindings[VS_MAX_KEYBINDINGS];
    int keybinding_count;
    int worktree_active;
    char worktree_path[VS_MAX_PATH];
    int computer_use_backend;   /* VS_COMPUTER_USE_* */

    /* Phase 5: Advanced features */
    VSLSPServer lsp_servers[VS_MAX_LSP_SERVERS];
    int lsp_server_count;
    int prompt_caching_enabled;
    long provider_cache_read_tokens;
    VSSettingsStore settings;

    /* Phase 7: Reasoning quality and autonomy */
    VSReasoningEngine reasoning;

    /* Phase 8: Loop (scheduled recurring prompts) */
    VSLoopEntry loops[VS_MAX_LOOPS];
    int loop_count;
    int loop_next_id;
    pthread_t loop_thread;
    int loop_thread_active;

    /* Phase 8: Dynamic Workflows */
    VSWorkflowEngine workflow_engine;

    /* Phase 8: Cross-Session Messaging */
    VSCrossSessionStore cross_session;

    /* Phase 8: Rewind (conversation-level undo) */
    VSRewindStore rewind;

    /* Phase 9: BTW (side comments) */
    VSBTWStore btw;

    /* Phase 9: Plan mode */
    VSPlanMode plan_mode;

    /* Phase 9: Custom slash commands from markdown */
    VSCustomSlashStore custom_slash;

    /* Phase 9: @-mention context */
    VSMention mentions[VS_MAX_MENTIONS];
    int mention_count;

    /* TUI compositor reference (for search/mouse access from tui.c) */
    VSTUICompositor tui_composer;

    /* Context window management */
    int context_window_tokens;       /* model context window in tokens (0 = unknown) */
    unsigned long estimated_request_tokens; /* estimated tokens in next API request */
    int compaction_triggered;        /* was compaction triggered this turn? */
    long last_compaction_round;      /* round number of last compaction */

    /* Subagent result caching */
    VSSubagentCacheEntry subagent_cache[VS_SUBAGENT_CACHE_MAX];
    int subagent_cache_count;
    /* Ephemeral continuation captured when a plan pauses. Never serialized. */
    char pending_task[VS_ACTIVE_TASK_MAX];
    unsigned long pending_task_revision;
    int pending_task_valid;
    /* Set when the previous turn stopped because the model kept emitting
       prose without any executable marker; the next turn prepends a
       one-shot reminder that directives must come first. */
    int agent_stalled_on_narration;
    /* Set when the previous turn stopped after the malformed-directive
       recovery budget was exhausted; the next turn gets the same
       directive-first reminder. */
    int agent_stalled_on_malformed;
    int suppress_history_once;
    /* Ephemeral user-choice prompt returned by the agent. Never serialized. */
    int pending_question;
    unsigned long pending_question_revision;
    char question[VS_NEED_USER_TEXT_MAX];
    int question_option_count;
    char question_options[VS_NEED_USER_MAX_OPTIONS][VS_NEED_USER_OPTION_MAX];
} VSContext;

void vs_workflow_init(VSWorkflow *workflow);
/* Returns 0 when adopted, 1 when the proposal is identical to the accepted
   plan (state untouched, acceptance retained), -1 when invalid. */
int  vs_workflow_set_plan(VSWorkflow *workflow, const char *title, const VSTaskRecord *tasks, int count);
int  vs_workflow_accept(VSWorkflow *workflow);
void vs_workflow_reject(VSWorkflow *workflow);
int  vs_workflow_authorized(const VSWorkflow *workflow);
int  vs_workflow_needs_action_approval(const VSWorkflow *workflow);
int  vs_workflow_approve_action(VSWorkflow *workflow);
int  vs_workflow_task_ready(const VSWorkflow *workflow, int task_id);
int  vs_workflow_set_task_status(VSWorkflow *workflow, int task_id, VSTaskStatus status, const char *summary);
int  vs_workflow_consume_action_approval(VSWorkflow *workflow);
const char *vs_workflow_mode_name(VSExecutionMode mode);
int  vs_workflow_cycle_mode(VSWorkflow *workflow);
void vs_workflow_clear_pending_task(VSContext *ctx);
int  vs_workflow_take_pending_task(VSContext *ctx, char *out, size_t cap);
void vs_init(VSContext *ctx);
void vs_shutdown(VSContext *ctx);
void vs_detect_platform(VSContext *ctx);
int  vs_load_agent_md(VSContext *ctx);
int  vs_load_claude_md(VSContext *ctx);
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
char *vs_run_command_ctx(VSContext *ctx, const char *cmd, int *exit_code);
const char *vs_command_shell_name(void);
char *vs_chat(VSContext *ctx, const char *user_text);
char *vs_agent_turn(VSContext *ctx, const char *user_text);
char *vs_subagent_run(VSContext *parent, const char *task);
char *vs_subagent_request_prompt(const char *task);
int   vs_trace_phase(const char *kind, const char *detail, char *out, size_t cap);
char *vs_build_system_prompt(const VSContext *ctx);
char *vs_http_post(const char *url, const char *headers[], int nheaders, const char *body, long *status);
char *vs_http_get_ctx(VSContext *ctx, const char *url, long *status, size_t max_bytes);
char *vs_web_search(VSContext *ctx, const char *query, int max_results);
char *vs_web_fetch(VSContext *ctx, const char *url, size_t max_text);
int  vs_graph_build(VSContext *ctx, const char *root, VSGraphMode mode, VSGraph *out);
char *vs_graph_summary(const VSGraph *graph, int max_lines);
int  vs_graph_export_dot(const VSGraph *graph, const char *path);
char *vs_http_post_ctx(VSContext *ctx, const char *url, const char *headers[], int nheaders, const char *body, long *status);
int vs_http_post_stream_ctx(VSContext *ctx, const char *url, const char *headers[], int nheaders, const char *body, long *status, VSHTTPChunkCallback callback, void *userdata);
char *vs_http_post_capture_ctx(VSContext *ctx, const char *url, const char *headers[], int nheaders, const char *body, long *status, char *session_id, size_t session_cap);
char *vs_json_escape(const char *s);
char *vs_shell_quote(const char *s);
char *vs_compact_text_limit(const char *s, size_t limit, const char *reason);
int  vs_is_image_path(const char *path);
int  vs_open_url(const char *url);
unsigned long vs_hash_string(const char *s);
void vs_refresh_cache_key(VSContext *ctx);

void vs_trace(VSContext *ctx, const char *kind, const char *detail);
void vs_trace_clear(VSContext *ctx);
void vs_set_trace_callback(VSContext *ctx, VSTraceCallback callback, void *userdata);
void vs_set_stream_callback(VSContext *ctx, VSHTTPChunkCallback callback, void *userdata);
void vs_cancel_request(VSContext *ctx);
void vs_cancel_clear(VSContext *ctx);
int  vs_cancel_requested(const VSContext *ctx);
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
int  vs_oauth_ensure_access_token(VSContext *ctx, char *err, size_t err_cap);
void vs_oauth_logout(VSContext *ctx);
VSOAuthConfig *vs_oauth_config(VSContext *ctx, VSOAuthProvider which);
int  vs_oauth_is_configured_for(const VSContext *ctx, VSOAuthProvider which);
int  vs_oauth_is_signed_in_for(const VSContext *ctx, VSOAuthProvider which);
int  vs_oauth_begin_for(VSContext *ctx, VSOAuthProvider which, VSOAuthFlow *flow, char *url_out, size_t url_cap, char *err, size_t err_cap);
int  vs_oauth_login_blocking_for(VSContext *ctx, VSOAuthProvider which, char *msg, size_t msg_cap);
int  vs_oauth_refresh_for(VSContext *ctx, VSOAuthProvider which, char *err, size_t err_cap);
int  vs_oauth_ensure_access_token_for(VSContext *ctx, VSOAuthProvider which, char *err, size_t err_cap);
void vs_oauth_logout_for(VSContext *ctx, VSOAuthProvider which);
int  vs_claude_oauth_policy_check(const VSOAuthConfig *cfg, char *err, size_t err_cap);
int  vs_claude_oauth_bearer_url_ok(const char *url);
int  vs_oauth_save_profile(const VSContext *ctx);
int  vs_oauth_load_profile(VSContext *ctx);
const char *vs_oauth_profile_path(void);
void vs_sha256(const unsigned char *data, size_t len, unsigned char out[32]);

/* ---- Agent coordinator ---- */
void vs_coordinator_init(VSCoordinator *coord);
void vs_coordinator_shutdown(VSCoordinator *coord);
int  vs_coordinator_spawn(VSCoordinator *coord, VSContext *parent, const char *task, int depth);
void vs_coordinator_cancel_all(VSCoordinator *coord);
void vs_coordinator_wait_all(VSCoordinator *coord);
int  vs_coordinator_active_count(const VSCoordinator *coord);
VSCoordinatorAgent *vs_coordinator_agent_by_id(VSCoordinator *coord, int id);
char *vs_coordinator_worktree_path(VSCoordinator *coord, int agent_id, char *out, size_t cap);
int  vs_coordinator_worktree_create(VSCoordinator *coord, int agent_id, const char *project_root);
int  vs_coordinator_worktree_remove(const char *worktree);

/* ---- Full-screen TUI compositor ---- */
void vs_tui_compositor_init(VSTUICompositor *comp);
void vs_tui_compositor_shutdown(VSTUICompositor *comp);
int  vs_tui_compositor_enter(VSTUICompositor *comp);
void vs_tui_compositor_exit(VSTUICompositor *comp);
void vs_tui_compositor_resize(VSTUICompositor *comp);
void vs_tui_compositor_clear(VSTUICompositor *comp);
void vs_tui_compositor_add_line(VSTUICompositor *comp, const char *line);
void vs_tui_compositor_add_lines(VSTUICompositor *comp, const char *text);
void vs_tui_compositor_render(const VSTUICompositor *comp, const VSContext *ctx);
void vs_tui_compositor_render_status_bar(const VSTUICompositor *comp, const VSContext *ctx);
void vs_tui_compositor_render_sidebar(VSTUICompositor *comp, const VSCoordinator *coord);
void vs_tui_compositor_render_input(VSTUICompositor *comp);
void vs_tui_compositor_scroll_up(VSTUICompositor *comp, int lines);
void vs_tui_compositor_scroll_down(VSTUICompositor *comp, int lines);
void vs_tui_compositor_scroll_page_up(VSTUICompositor *comp);
void vs_tui_compositor_scroll_page_down(VSTUICompositor *comp);
void vs_tui_compositor_start_streaming(VSTUICompositor *comp);
void vs_tui_compositor_append_stream(VSTUICompositor *comp, const char *data, size_t len);
void vs_tui_compositor_end_streaming(VSTUICompositor *comp);
int  vs_tui_compositor_read_input(VSTUICompositor *comp, char *out, size_t cap);
int  vs_tui_supports_alt_screen(void);
/* Mouse support */
void vs_tui_compositor_enable_mouse(VSTUICompositor *comp);
void vs_tui_compositor_disable_mouse(VSTUICompositor *comp);
int  vs_tui_compositor_parse_mouse(unsigned char *seq, int *row, int *col, int *button);
/* Scrollback search */
int  vs_tui_compositor_search(VSTUICompositor *comp, const char *query);
void vs_tui_compositor_search_next(VSTUICompositor *comp);
void vs_tui_compositor_search_prev(VSTUICompositor *comp);
void vs_tui_compositor_search_exit(VSTUICompositor *comp);

/* ---- Parallel tool executor ---- */
void vs_tool_executor_init(VSToolExecutor *exec);
int  vs_tool_executor_parse_jobs(VSToolExecutor *exec, const char *answer);
void vs_tool_executor_run_parallel(VSToolExecutor *exec, VSContext *ctx);
char *vs_tool_executor_collect_results(VSToolExecutor *exec);
void vs_tool_executor_clear(VSToolExecutor *exec);
int  vs_tool_executor_is_readonly(const char *directive, int is_mcp);

/* ---- Skill system ---- */
int  vs_skill_load_dir(VSSkill *skills, int *count, const char *dir);
int  vs_skill_load_file(VSSkill *skill, const char *path);
VSSkill *vs_skill_find(VSSkill *skills, int count, const char *name);
char *vs_skill_execute(VSSkill *skill, const char *input);
void  vs_skill_free(VSSkill *skill);
int   vs_skill_list(VSSkill *skills, int count, char *out, size_t cap);

/* ---- Persistent memory ---- */
void vs_memory_init(VSMemoryStore *store, const char *path);
int  vs_memory_save(VSMemoryStore *store, const char *key, const char *value, const char *tag, VSMemoryType type);
int  vs_memory_delete(VSMemoryStore *store, const char *key);
VSMemoryEntry *vs_memory_get(VSMemoryStore *store, const char *key);
char *vs_memory_search(VSMemoryStore *store, const char *query, int max_results);
char *vs_memory_list(VSMemoryStore *store, int max_lines);
void  vs_memory_load(VSMemoryStore *store);
void  vs_memory_persist(VSMemoryStore *store);
void  vs_memory_clear(VSMemoryStore *store);

/* ---- TodoWrite: visible session task list ---- */
void vs_todo_init(VSTodoList *todo);
int  vs_todo_set_all(VSTodoList *todo, const char **items, int count);
int  vs_todo_add(VSTodoList *todo, const char *text);
int  vs_todo_update_status(VSTodoList *todo, int index, VSTodoStatus status);
int  vs_todo_clear(VSTodoList *todo);
char *vs_todo_render(const VSTodoList *todo);

/* ---- Edit tool: targeted file modifications ---- */
char *vs_edit_file(const char *path, const char *search, const char *replace, int *replacements);

/* ---- Glob tool: pattern-based file discovery ---- */
char *vs_glob(const char *path, const char *pattern, int *count, char results[][VS_MAX_PATH]);

/* ---- Grep tool: content search with context ---- */
char *vs_grep(const char *path, const char *pattern, int case_insensitive, int context_lines, int max_results);

/* ---- Notebook tools: Jupyter .ipynb read/edit ---- */
char *vs_notebook_read(const char *path);
char *vs_notebook_edit(const char *path, int cell_index, const char *new_source);

/* ---- Monitor tool: background process management ---- */
int  vs_monitor_start(const char *cmd);
char *vs_monitor_output(int monitor_id);
int  vs_monitor_kill(int monitor_id);
int  vs_monitor_kill_all(void);
int  vs_monitor_is_running(int monitor_id);

/* ---- MultiEdit tool: multi-hunk file editing ---- */
char *vs_multi_edit(const char *path, const char **searches, const char **replaces, int count, int *total_replacements);

/* ---- LSP tool: Language Server Protocol ---- */
VSLSPServer *vs_lsp_find_server(VSLSPServer *servers, int *count, const char *file_path);
int  vs_lsp_spawn_server(VSLSPServer *srv, const char *cmd, const char *root_path);
int  vs_lsp_initialize(VSLSPServer *srv);
char *vs_lsp_diagnostics(VSLSPServer *servers, int *count, const char *file_path);
char *vs_lsp_definition(VSLSPServer *servers, int *count, const char *file_path, int line, int col);
char *vs_lsp_diagnostics_compat(const char *file_path);
char *vs_lsp_definition_compat(const char *file_path, int line, int col);
char *vs_lsp_hover(VSLSPServer *servers, int *count, const char *file_path, int line, int col);
char *vs_lsp_rename(VSLSPServer *servers, int *count, const char *file_path, int line, int col, const char *new_name);
void vs_lsp_shutdown_all(VSLSPServer *servers, int *count);
void vs_lsp_notify_did_open(VSLSPServer *srv, const char *file_path, const char *content);
void vs_lsp_notify_did_change(VSLSPServer *srv, const char *file_path, const char *content);

/* ---- Multi-layer settings API ---- */
void vs_settings_init(VSSettingsStore *store, const char *project_root);
int  vs_settings_load_user(VSSettingsStore *store);
int  vs_settings_load_project(VSSettingsStore *store);
int  vs_settings_load_local(VSSettingsStore *store);
int  vs_settings_load_all(VSSettingsStore *store);
const char *vs_settings_get(VSSettingsStore *store, const char *key);
int  vs_settings_set(VSSettingsStore *store, const char *key, const char *value, VSSettingsLayer layer);
int  vs_settings_save_local(VSSettingsStore *store);
char *vs_settings_dump(VSSettingsStore *store);
int  vs_settings_detect_project_root(const char *start_path, char *out, size_t cap);
void vs_lsp_notify_did_close(VSLSPServer *srv, const char *file_path);

/* ---- Phase 7: Reasoning engine API ---- */
void vs_reasoning_init(VSReasoningEngine *eng);
char *vs_reasoning_analyze(const VSReasoningEngine *eng, const char *task);
char *vs_reasoning_decompose(const VSReasoningEngine *eng, const char *task);
int  vs_reasoning_confidence(const VSReasoningEngine *eng, const char *task, const char *action);
const char *vs_reasoning_confidence_label(int score);
char *vs_reasoning_self_correct(const VSReasoningEngine *eng, const char *task, const char *tool, const char *error);
char *vs_reasoning_suggest(const VSReasoningEngine *eng, const char *task, const char *last_action, const char *last_result);
int  vs_reasoning_should_analyze(const VSReasoningEngine *eng, const char *task);
 char *vs_reasoning_prompt_fragment(const VSReasoningEngine *eng);

/* ---- Phase 8: Loop (scheduled recurring prompts) ---- */
void vs_loop_init(VSContext *ctx);
int  vs_loop_add(VSContext *ctx, const char *prompt, int interval_seconds);
int  vs_loop_remove(VSContext *ctx, int id);
int  vs_loop_stop_all(VSContext *ctx);
char *vs_loop_list(VSContext *ctx, int max_lines);
void vs_loop_shutdown(VSContext *ctx);

/* ---- Phase 8: Dynamic Workflows (JSON-based orchestration) ---- */
void vs_workflow_engine_init(VSWorkflowEngine *engine);
int  vs_workflow_engine_parse(VSWorkflowEngine *engine, const char *json);
int  vs_workflow_engine_run(VSWorkflowEngine *engine, VSContext *ctx);
char *vs_workflow_engine_status(VSWorkflowEngine *engine, int max_lines);
void vs_workflow_engine_clear(VSWorkflowEngine *engine);

void vs_cross_session_init(VSCrossSessionStore *store);
int  vs_cross_session_discover(VSCrossSessionStore *store);
int  vs_cross_session_send(VSCrossSessionStore *store, long to_pid, const char *message);
int  vs_cross_session_receive(VSCrossSessionStore *store);
char *vs_cross_session_list(VSCrossSessionStore *store, int max_lines);
char *vs_cross_session_inbox(VSCrossSessionStore *store, int max_lines);
void vs_cross_session_shutdown(VSCrossSessionStore *store);

/* ---- Phase 8: Rewind (conversation-level undo) ---- */
void vs_rewind_init(VSRewindStore *store);
int  vs_rewind_mark(VSRewindStore *store, const char *label);
int  vs_rewind_undo(VSRewindStore *store, VSContext *ctx, int n);
int  vs_rewind_to_marker(VSRewindStore *store, VSContext *ctx, const char *label);
int  vs_rewind_checkpoint_rollback(VSRewindStore *store, VSCheckpointStore *cp_store);
char *vs_rewind_list(VSRewindStore *store, int max_lines);
void vs_rewind_clear(VSRewindStore *store);

/* ---- Permission rules: tool-level allow/deny ---- */
void vs_permission_init(VSPermissionStore *store);
int  vs_permission_add_rule(VSPermissionStore *store, const char *rule, int allow);
int  vs_permission_remove_rule(VSPermissionStore *store, int index);
int  vs_permission_check(VSPermissionStore *store, const char *tool, const char *path);
int  vs_permission_clear(VSPermissionStore *store);
char *vs_permission_list(VSPermissionStore *store, int max_lines);
void vs_permission_load(VSPermissionStore *store, const char *path);
int  vs_permission_persist(VSPermissionStore *store, const char *path);

/* ---- Task management: cross-session tasks ---- */
void vs_task_init(VSTaskStore *store, const char *path);
int  vs_task_create(VSTaskStore *store, const char *title, const char *description, int priority);
int  vs_task_update(VSTaskStore *store, int id, const char *status_str, const char *title, const char *description);
int  vs_task_delete(VSTaskStore *store, int id);
VSTaskEntry *vs_task_get(VSTaskStore *store, int id);
char *vs_task_list(VSTaskStore *store, int max_lines);
char *vs_task_search(VSTaskStore *store, const char *query, int status_filter, int max_results);
void vs_task_load(VSTaskStore *store);
int  vs_task_persist(VSTaskStore *store);
int  vs_task_clear(VSTaskStore *store);

/* ---- Slash commands ---- */
void vs_slash_init(VSSlashStore *store, const char *path);
int  vs_slash_register(VSSlashStore *store, const char *name, const char *command, const char *description);
int  vs_slash_remove(VSSlashStore *store, const char *name);
VSSlashCommand *vs_slash_find(VSSlashStore *store, const char *name);
char *vs_slash_execute(VSSlashStore *store, const char *name, const char *args);
char *vs_slash_list(VSSlashStore *store, int max_lines);
void vs_slash_load(VSSlashStore *store);
int  vs_slash_persist(VSSlashStore *store);
int  vs_slash_clear(VSSlashStore *store);

/* ---- Tool search ---- */
char *vs_tool_search(const char *query);

/* ---- Hooks system ---- */
void vs_hooks_init(VSHooksStore *store, const char *path);
int  vs_hooks_add(VSHooksStore *store, const char *matcher, const char *command, int pre);
int  vs_hooks_remove(VSHooksStore *store, int index);
int  vs_hooks_run_pre(VSHooksStore *store, const char *tool_name);
int  vs_hooks_run_post(VSHooksStore *store, const char *tool_name, const char *result);
char *vs_hooks_list(VSHooksStore *store, int max_lines);
void vs_hooks_load(VSHooksStore *store);
int  vs_hooks_persist(VSHooksStore *store);
int  vs_hooks_clear(VSHooksStore *store);
int  vs_hooks_run_message_display(VSHooksStore *store, const char *text);
int  vs_hooks_run_prompt(VSHooksStore *store, const char *prompt, char *modified, size_t cap);
int  vs_hooks_send_webhook(VSHooksStore *store, const char *event, const char *payload);

/* ---- Project index: symbol search and dependencies ---- */
void vs_project_index_init(const char *root);
int  vs_project_index_build(const char *root);
char *vs_project_index_search(const char *query);
char *vs_project_index_references(const char *symbol);
char *vs_project_index_dependencies(const char *file_path);
int  vs_project_index_is_valid(void);
void vs_project_index_clear(void);

/* ---- Skill invoke (extension) ---- */
char *vs_skill_invoke(VSSkill *skills, int *count, const char *name, const char *input);

/* ---- Worktree (coordinator extension) ---- */
int  vs_worktree_enter(const char *project_root, char *path_out, size_t cap);
int  vs_worktree_exit(const char *worktree);

/* ---- MCP resources (extension) ---- */
char *vs_mcp_list_resources(VSContext *ctx, const char *server);
char *vs_mcp_read_resource(VSContext *ctx, const char *server, const char *uri);

/* ---- Memory rules directory (extension) ---- */
int  vs_memory_load_rules_dir(VSMemoryStore *store, const char *dir);

/* ---- AutoDream: memory consolidation ---- */
VSDreamStats vs_memory_dream(VSMemoryStore *store);
char *vs_memory_dream_stats(const VSDreamStats *stats);

/* ---- Context window management ---- */
long vs_estimate_tokens(const char *text, int is_code);
long vs_estimate_request_tokens(const VSContext *ctx, const char *user_message);
int  vs_provider_context_window(const VSProvider *provider);
int  vs_should_compact(const VSContext *ctx);

/* ---- Subagent result file output ---- */
char *vs_subagent_result_to_file(const char *task, const char *result, long duration, int status);

/* ---- Subagent caching ---- */
char *vs_subagent_cache_get(VSContext *ctx, const char *task);
void  vs_subagent_cache_put(VSContext *ctx, const char *task, const char *result);
void  vs_subagent_cache_clear(VSContext *ctx);

/* ---- Auto-memory (continued) ---- */
void vs_memory_auto_capture(VSContext *ctx, const char *output, const char *tool_results);

/* ---- Conversation save/restore (continued) ---- */
int  vs_conversation_save(VSContext *ctx, const char *id);
int  vs_conversation_load(VSContext *ctx, const char *id);
char *vs_conversation_list(int max_lines);
int  vs_conversation_delete(const char *id);

/* ---- Image display (continued) ---- */
void vs_display_image(const char *base64_data, size_t base64_len, const char *description);

/* ---- Checkpoints (git snapshots) ---- */
void vs_checkpoint_init(VSCheckpointStore *store);
int  vs_checkpoint_create(VSCheckpointStore *store, const char *desc);
int  vs_checkpoint_rollback(VSCheckpointStore *store, const char *id);
char *vs_checkpoint_list(VSCheckpointStore *store, int max_lines);
int  vs_checkpoint_auto_before_edit(VSCheckpointStore *store);
int  vs_checkpoint_persist(VSCheckpointStore *store);
void vs_checkpoint_clear(VSCheckpointStore *store);

/* ---- Computer Use (X11) ---- */
int  vs_computer_use_init(void);
void vs_computer_use_shutdown(void);
char *vs_computer_use_screenshot(void);
char *vs_computer_use_click(int x, int y);
char *vs_computer_use_type(const char *text);
char *vs_computer_use_move(int x, int y);

/* ---- Auto Mode (action classifier) ---- */
void vs_auto_mode_init(VSAutoModeStore *store);
int  vs_auto_mode_classify(VSAutoModeStore *store, const char *tool, const char *args);
int  vs_auto_mode_should_block(VSAutoModeStore *store, int classification);
char *vs_auto_mode_list_rules(VSAutoModeStore *store, int max_lines);
void vs_auto_mode_load(VSAutoModeStore *store);
int  vs_auto_mode_persist(VSAutoModeStore *store);

/* ---- Agent Teams (peer-to-peer) ---- */
int  vs_team_init(VSTeam *team, const char *id);
int  vs_team_add_agent(VSTeam *team, const char *agent_id, const char *role);
char *vs_team_message(VSTeam *team, const char *from, const char *to, const char *msg);
char *vs_team_read_messages(VSTeam *team, const char *agent_id);
char *vs_team_broadcast(VSTeam *team, const char *from, const char *msg);
void  vs_team_shutdown(VSTeam *team);
char *vs_team_list(int team_count, VSTeam *teams, int max_lines);

/* ---- Plugins ---- */
int  vs_plugin_install(VSPlugin *plugins, int *count, const char *url_or_path);
char *vs_plugin_list(VSPlugin *plugins, int count, int max_lines);
int  vs_plugin_uninstall(VSPlugin *plugins, int *count, const char *name);
int  vs_plugin_update(VSPlugin *plugins, int *count, const char *name);

/* ---- Code Review ---- */
char *vs_code_review_start(const char *path, const char *scope, const char *focus);
char *vs_code_review_render(const VSCodeReviewFinding *findings, int count, int max_lines);

/* ---- Security Review (CWE-based vulnerability scanning) ---- */
int  vs_security_review_start(const char *path, const char *scope, VSSecurityFinding *findings, int max_findings);
char *vs_security_review_render(const VSSecurityFinding *findings, int count, int max_lines);
const char *vs_security_severity_label(VSSecuritySeverity sev);

/* ---- Goal management ---- */
int  vs_goal_save(const char *goal);
char *vs_goal_load(void);
int  vs_goal_clear(void);

/* ---- Init command (auto-onboarding) ---- */
char *vs_init_repo(const char *path);

/* ---- Keybindings ---- */
void vs_keybindings_load(VSKeybinding *bindings, int *count);
char *vs_keybindings_find(VSKeybinding *bindings, int count, const char *key);

/* ---- PDF reading ---- */
char *vs_read_pdf(const char *path, const char *pages);

/* ---- Computer Use backends ---- */
int  vs_computer_use_detect_backend(void);

/* ---- Wayland Computer Use (optional) ---- */
void agent_compact_force(VSContext *ctx, const char *task);
void vs_agent_context_release(VSContext *ctx);
#ifdef HAVE_WAYLAND
int  vs_wayland_init(void);
char *vs_wayland_screenshot(void);
char *vs_wayland_click(int x, int y);
char *vs_wayland_type(const char *text);
char *vs_wayland_move(int x, int y);

#endif

/* ---- Cocoa Computer Use (optional) ---- */
#ifdef HAVE_COCOA
int  vs_cocoa_init(void);
char *vs_cocoa_screenshot(void);
char *vs_cocoa_click(int x, int y);
char *vs_cocoa_type(const char *text);
char *vs_cocoa_move(int x, int y);
#endif

/* ---- TUI Theme system ---- */
#define VS_TUI_THEME_NAME_MAX 64
#define VS_TUI_THEME_MAX_COLORS 16

typedef struct {
    char name[VS_TUI_THEME_NAME_MAX];
    char colors[VS_TUI_THEME_MAX_COLORS][8]; /* ANSI color codes (e.g., "36m", "38;2;100;150;200m") */
    /* Color indices: 0=bg, 1=fg, 2=status_bg, 3=status_fg, 4=prompt, 5=sidebar_bg,
       6=sidebar_fg, 7=highlight, 8=error, 9=warning, 10=success, 11=dim,
       12=link, 13=comment, 14=keyword, 15=string */
    int is_dark; /* 1 = dark theme, 0 = light theme */
} VSTUITheme;

/* ---- TUI Suggestions engine ---- */
#define VS_TUI_SUGGESTION_MAX 16
#define VS_TUI_SUGGESTION_TEXT_MAX 256

typedef struct {
    char text[VS_TUI_SUGGESTION_TEXT_MAX];
    int score; /* relevance score, higher = better match */
    int type;  /* 0=command, 1=file, 2=path, 3=tool */
} VSTUISuggestion;

/* ---- TUI Theme functions ---- */
void vs_tui_theme_init(VSTUITheme *theme, const char *name);
void vs_tui_theme_load_default(VSTUITheme *theme, int dark);
int  vs_tui_theme_load_file(VSTUITheme *theme, const char *path);
int  vs_tui_theme_save_file(const VSTUITheme *theme, const char *path);
const char *vs_tui_theme_color(const VSTUITheme *theme, int color_index);
void vs_tui_theme_apply(const VSTUITheme *theme);

/* ---- TUI Suggestions functions ---- */
void vs_tui_suggestions_init(VSTUISuggestion *suggestions, int *count);
int  vs_tui_suggestions_add(VSTUISuggestion *suggestions, int *count, const char *text, int type);
int  vs_tui_suggestions_match(VSTUISuggestion *suggestions, int count, const char *input, VSTUISuggestion *results, int max_results);
int  vs_tui_suggestions_fuzzy_match(const char *input, const char *candidate);
void vs_tui_suggestions_render(const VSTUISuggestion *results, int count, int width);

/* ---- TUI slash-command completion (dropdown model; plain-ANSI safe) ----
 * The command table lives here so both the TUI and unit tests link the exact
 * same data.  Descriptions are resolved by longest matching prefix so
 * subcommands ("/effort low", "/cache clear", ...) inherit their family text. */
int         vs_tui_slash_command_count(void);
const char *vs_tui_slash_command_name(int index);
const char *vs_tui_slash_desc(const char *name);
int         vs_tui_slash_match(const char *buf, const char **names, const char **descs, int cap);

/* ---- TUI Editor (multiline) ---- */
#define VS_TUI_EDITOR_MAX_LINES 64
#define VS_TUI_EDITOR_LINE_MAX 512

typedef struct {
    char lines[VS_TUI_EDITOR_MAX_LINES][VS_TUI_EDITOR_LINE_MAX];
    int line_count;
    int cursor_line;
    int cursor_col;
    int scroll_offset;
    int mode; /* 0=insert, 1=normal (vi-style) */
} VSTUIEditor;

void vs_tui_editor_init(VSTUIEditor *ed);
void vs_tui_editor_insert_char(VSTUIEditor *ed, char ch);
void vs_tui_editor_backspace(VSTUIEditor *ed);
void vs_tui_editor_newline(VSTUIEditor *ed);
void vs_tui_editor_move_up(VSTUIEditor *ed);
void vs_tui_editor_move_down(VSTUIEditor *ed);
void vs_tui_editor_move_left(VSTUIEditor *ed);
void vs_tui_editor_move_right(VSTUIEditor *ed);
void vs_tui_editor_move_home(VSTUIEditor *ed);
void vs_tui_editor_move_end(VSTUIEditor *ed);
int  vs_tui_editor_get_text(const VSTUIEditor *ed, char *out, size_t cap);
void vs_tui_editor_render(const VSTUIEditor *ed, int row, int col, int width);
char *vs_tui_editor_wrap_line(const char *line, int width, char *out, size_t cap);

/* ---- Phase 9: BTW (side comments) ---- */
void vs_btw_init(VSBTWStore *store);
int  vs_btw_add(VSBTWStore *store, const char *prompt, const char *response);
char *vs_btw_list(VSBTWStore *store, int max_lines);
void vs_btw_clear(VSBTWStore *store);

/* ---- Phase 9: Plan mode ---- */
void vs_plan_mode_init(VSPlanMode *mode);
void vs_plan_mode_enable(VSPlanMode *mode);
void vs_plan_mode_disable(VSPlanMode *mode);
char *vs_plan_mode_preview(const VSPlanMode *mode);

/* ---- Phase 9: Custom slash commands from markdown ---- */
void vs_custom_slash_init(VSCustomSlashStore *store, const char *dir);
int  vs_custom_slash_load_dir(VSCustomSlashStore *store);
VSCustomSlashCommand *vs_custom_slash_find(VSCustomSlashStore *store, const char *name);
char *vs_custom_slash_execute(VSCustomSlashStore *store, const char *name, const char *args);
char *vs_custom_slash_list(VSCustomSlashStore *store, int max_lines);

/* ---- Phase 9: Markdown table rendering ---- */
int  vs_table_parse(const char *text, VSTable *table);
char *vs_table_render(const VSTable *table, int width);

/* ---- Phase 9: @-mention context resolution ---- */
int  vs_mention_parse(const char *text, VSMention *mentions, int max_mentions);
char *vs_mention_resolve(VSContext *ctx, const char *mention_text, int type);
char *vs_mention_inject_context(const VSMention *mentions, int count, const char *original);

/* ---- Phase 9: Extended effort levels ---- */
const char *vs_effort_label(int effort);
int  vs_effort_from_string(const char *s);

/* ---- Phase 9: PreCompact hooks ---- */
#define VS_HOOK_PRE_COMPACT 5
int  vs_hooks_run_pre_compact(VSHooksStore *store, const char *context_summary);

/* ---- Phase 9: Git branch automation ---- */
char *vs_git_branch_create(const char *name, const char *path);
char *vs_git_branch_list(const char *path, int max_lines);
char *vs_git_diff_staged(const char *path, int max_lines);
char *vs_git_commit_generate_message(const char *path);
char *vs_git_pr_description(const char *path, const char *branch, int max_lines);

#endif
