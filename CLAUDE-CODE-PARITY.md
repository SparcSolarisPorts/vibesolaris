# Claude Code Parity Architecture

This document describes how VibeSolaris maps Claude Code's agent system and TUI architecture into portable C, while remaining SSH-safe and dependency-free.

## Claude Code Architecture (Reference)

Claude Code (TypeScript/Bun/React/Ink) has these key components:

| Component | Claude Code | VibeSolaris equivalent |
|---|---|---|
| **TUI framework** | React + Ink (full-screen) | `src/tui_compositor.c` (ANSI/VT100 compositor) |
| **Agent loop** | `query.ts` (785 KB main loop) | `src/agent.c` (`vs_agent_turn`) |
| **Tool system** | 40+ tools in `src/tools/` | `[[VS_TOOL ...]]` directives in system prompt |
| **Edit tool** | `Edit` (search/replace) | `src/edit.c` (`vs_edit_file`) |
| **TodoWrite** | `TodoWrite` (task checklist) | `src/todo.c` (`VSTodoList`) |
| **Tool executor** | `StreamingToolExecutor` (parallel) | `src/tool_executor.c` (parallel read-only) |
| **Sub-agents** | `coordinator/` (parallel, worktree) | `src/coordinator.c` (parallel, worktree) |
| **Skills** | `skills/` (SKILL.md per skill) | `src/skill.c` (SKILL.md loader) |
| **Memory** | `memdir/` (persistent JSONL) | `src/memory.c` (JSONL store) |
| **Task management** | `tasks/` (dependency graph) | `VSWorkflow` + `VSTaskRecord` + `src/task.c` |
| **Permission rules** | `allowedTools`/`deny` | `src/permission.c` (allow/deny rules) |
| **Glob** | Glob pattern search | `src/glob.c` (recursive **/*/? patterns) |
| **Grep** | ripgrep-style search | `src/grep.c` (content search with context) |
| **Notebook** | Notebook read/edit | `src/notebook.c` (.ipynb parsing) |
| **Monitor** | Background process mgmt | `src/monitor.c` (fork+pipe, kill) |
| **MultiEdit** | Multi-hunk edits | `src/multi_edit.c` (atomic multi-edit) |
| **LSP** | Language server protocol | `src/lsp.c` (diagnostics, definition) |
| **MCP** | MCP SDK integration | `src/mcp.c` (stdio + HTTP + resources) |
| **SlashCommand** | Custom slash commands | `src/slash.c` (JSONL store) |
| **ToolSearch** | Search available tools | `src/tool_search.c` (registry search) |
| **Hooks** | PreToolUse/PostToolUse/MessageDisplay | `src/hooks.c` (command-based, prompt hooks, HTTP webhooks) |
| **Settings** | Three-layer .claude/ hierarchy | `src/settings.c` (user/project/local merge) |
| **Project Index** | Codebase symbol index | `src/project_index.c` (symbol search, dependencies) |
| **Context compaction** | `autoCompact()` | `agent_compact()` in `src/agent.c` |

## What Was Implemented

### 1. Full-screen TUI Compositor (`src/tui_compositor.c`)

Mirrors Claude Code's Ink-based layout:

```
+--------------------------------------------------+
|  Scrollback buffer (512 lines, scrollable)       |  | Agents (sidebar)
|  Agent output with streaming text                |  |  #1 [RUN] Read src/
|                                                  |  |  #2 [DON] Search ...
|                                                  |  |
|                                                  |  |
+--------------------------------------------------+
| > user input with cursor navigation              |
| provider/model [mode] effort:high tokens:1024/512|
+--------------------------------------------------+
```

- **Alternate screen** (DECSET ?1049) for full-screen mode
- **SSH-safe fallback**: auto-detects terminal capability; falls back to classic prompt mode when alternate screen is unavailable
- **Scrollback**: 512-line buffer with page up/down navigation
- **Status bar**: provider, model, mode, effort, token counts
- **Sidebar**: shows concurrent sub-agents with status (RUN/DON/ERR/STP)
- **Streaming**: real-time token display during model responses
- **Terminal resize**: handled via SIGWINCH

### 2. Agent Coordinator (`src/coordinator.c`)

Mirrors Claude Code's `coordinator/` for parallel sub-agents:

- **Parallel execution**: up to 8 sub-agents run concurrently via pthreads
- **Worktree isolation**: each agent can get an isolated filesystem via `git worktree` (fallback: file copy)
- **Cancellation**: Ctrl-C propagates to all running sub-agents
- **Result aggregation**: collects results when all threads complete
- **Thread safety**: mutex-protected agent state

### 3. Parallel Tool Executor (`src/tool_executor.c`)

Mirrors Claude Code's `StreamingToolExecutor`:

- **Read-only tools** (read, list, search, graph, web_search, web_fetch) run in parallel threads
- **Side-effecting tools** (write, run, subagent, MCP) remain serial for safety
- **Per-tool timeout** support
- **Result collection** with bounded output

### 4. Skill System (`src/skill.c`)

Mirrors Claude Code's `skills/`:

- Loads `SKILL.md` from directories (e.g., `~/.vibesolaris/skills/commit/SKILL.md`)
- YAML frontmatter parsing for metadata (mode: inline/fork)
- **Inline mode**: skill prompt injected into model context
- **Fork mode**: skill runs as a separate agent turn
- Up to 16 skills loaded

### 5. Persistent Memory (`src/memory.c`)

Mirrors Claude Code's `memdir/`:

- JSONL storage at `~/.vibesolaris/memory.jsonl`
- **Entry types**: note, preference, context, skill reference
- **Search**: case-insensitive across key, value, and tag
- **LRU eviction**: 128 entries max, least recently used evicted first
- **Persistence**: automatic save/load across sessions

### 6. Edit Tool (`src/edit.c`)

Mirrors Claude Code's `Edit` tool for targeted file modifications:

- **Search/replace**: exact string matching, replaces all occurrences
- **Safer than write**: preserves surrounding context, explicit diff
- **Cache invalidation**: automatically invalidates file cache after edit
- **Size limit**: 64 KB files for safety
- **Usage**: `[[VS_TOOL edit path="FILE" search="EXACT_TEXT" replace="NEW_TEXT"]]`

### 7. TodoWrite (`src/todo.c`)

Mirrors Claude Code's `TodoWrite` tool for visible session task tracking:

- **Task checklist**: up to 32 items with status tracking
- **Statuses**: pending `[ ]`, in_progress `[*]`, completed `[x]`, cancelled `[-]`
- **Operations**: set all items, add single item, update status, render list
- **Usage**: `[[VS_TOOL todo items="Item1|Item2"]]` or `[[VS_TOOL todo index="1" status="completed"]]`
- **Read-only**: does not require plan approval, runs during discovery

### 8. Glob Tool (`src/glob.c`)

Mirrors Claude Code's Glob tool for pattern-based file discovery:

- **Patterns**: `**` (recursive), `*` (any chars except /), `?` (single char)
- **Recursive walk**: traverses directories matching patterns
- **512 result limit**: prevents runaway searches
- **Usage**: `[[VS_TOOL glob path="." pattern="**/*.c"]]`

### 9. Grep Tool (`src/grep.c`)

Mirrors Claude Code's Grep tool for content search:

- **Context lines**: shows surrounding lines around matches
- **Case sensitivity**: optional case-insensitive mode
- **Binary file skip**: detects and skips binary files
- **File/line reporting**: `file:line: content` format
- **200 result limit**: prevents runaway searches
- **Usage**: `[[VS_TOOL grep path="." pattern="fn_main" context="3" case_insensitive="0"]]`

### 10. Notebook Tools (`src/notebook.c`)

Mirrors Claude Code's Notebook read/edit for Jupyter .ipynb files:

- **Read**: parses JSON .ipynb format, extracts cells with type/source
- **Edit**: replaces cell source by index
- **64 cell limit**: prevents large notebook parsing
- **Usage**: `[[VS_TOOL notebook_read path="notebook.ipynb"]]`, `[[VS_TOOL notebook_edit path="notebook.ipynb" cell="0" source="new code"]]`

### 11. Monitor Tool (`src/monitor.c`)

Mirrors Claude Code's Monitor tool for background process management:

- **Fork + pipe**: captures stdout/stderr of background processes
- **Bounded output buffer**: 64 KB per monitor
- **Kill/KillAll**: terminate individual or all monitors
- **8 max monitors**: prevents resource exhaustion
- **Usage**: `[[VS_TOOL monitor cmd="npm start"]]`, `[[VS_TOOL monitor_output monitor_id="1"]]`, `[[VS_TOOL monitor_kill monitor_id="1"]]`

### 12. MultiEdit Tool (`src/multi_edit.c`)

Mirrors Claude Code's MultiEdit for multi-hunk file editing:

- **Atomic apply**: all search/replace pairs applied in one pass
- **16 pair limit**: prevents runaway edits
- **Usage**: `[[VS_TOOL multi_edit path="file.c" edits="search1|replace1;search2|replace2"]]`

### 13. LSP Tool (`src/lsp.c`)

Minimal Language Server Protocol integration:

- **Diagnostics**: requests textDocument/diagnostic for a file
- **Go-to-definition**: requests textDocument/definition for a position
- **Env-var configured**: server command set via environment variable
- **Usage**: `[[VS_TOOL lsp command="diagnostics" path="file.c"]]`

### 14. Permission Rules (`src/permission.c`)

Mirrors Claude Code's `allowedTools`/`deny` system:

- **Allow/deny rules**: per-tool or tool+path pattern matching
- **Wildcard support**: `*` matches all tools
- **Pattern matching**: `*` for any chars, prefix match for plain patterns
- **Persistence**: load/save rules from file
- **32 rule limit**: prevents rule explosion
- **TUI commands**: `/permission list`, `/permission allow TOOL`, `/permission deny TOOL`

### 15. Task Management (`src/task.c`)

Cross-session task management distinct from TodoWrite:

- **JSONL storage**: `~/.vibesolaris/tasks.jsonl`
- **Fields**: id, title, description, status, priority, timestamps
- **Search**: case-insensitive across title and description
- **Status filter**: filter by pending/in_progress/completed/cancelled
- **64 entry limit**: prevents runaway growth
- **TUI commands**: `/tasks list`, `/tasks search QUERY`, `/tasks create TITLE`

### 16. TaskGet, TaskStop, TaskOutput (`src/agent.c`)

Completes the Task toolset:

- **TaskGet**: retrieve a single task by ID with full details
- **TaskStop**: cancel a background task
- **TaskOutput**: read accumulated output from a task
- **Usage**: `[[VS_TOOL task_get id="1"]]`, `[[VS_TOOL task_stop id="1"]]`, `[[VS_TOOL task_output id="1"]]`

### 17. ExitPlanMode (`src/agent.c`)

Explicitly exit plan mode and enter execution mode:

- **Usage**: `[[VS_TOOL exit_plan_mode]]`
- Transitions workflow from plan mode to auto mode

### 18. SlashCommand (`src/slash.c`)

Custom slash command registration and execution:

- **Registration**: `vs_slash_register(name, command, description)`
- **JSONL storage**: `~/.vibesolaris/slashes.jsonl`
- **16 command limit**
- **Usage**: `[[VS_TOOL slash_command name="fix" args="..."]]`

### 19. ToolSearch (`src/tool_search.c`)

Search available tools by name or description:

- **Registry**: all 40+ tools with descriptions
- **Case-insensitive search** across name and description
- **Usage**: `[[VS_TOOL tool_search query="edit file"]]`

### 20. Skill Tool (`src/skill.c`)

Invoke a loaded skill by name as a tool call:

- **vs_skill_invoke()**: finds and executes a skill by name
- **Usage**: `[[VS_TOOL skill name="commit" input="..."]]`

### 21. EnterWorktree/ExitWorktree (`src/coordinator.c`)

Manage git worktree isolation for the current agent session:

- **vs_worktree_enter()**: creates a git worktree or file-copy fallback
- **vs_worktree_exit()**: removes the worktree
- **Usage**: `[[VS_TOOL enter_worktree path="."]]`, `[[VS_TOOL exit_worktree]]`

### 22. AskUserQuestion (`src/agent.c`)

Structured user question tool:

- **Options support**: pipe-separated options for multiple choice
- **Free-form input**: text answer when no options provided
- **Usage**: `[[VS_TOOL ask_user_question question="..." options="opt1|opt2"]]`

### 23. ListMcpResources/ReadMcpResource (`src/mcp.c`)

MCP server resource listing and reading:

- **resources/list**: list available resources from an MCP server
- **resources/read**: read a specific resource by URI
- **Usage**: `[[VS_TOOL list_mcp_resources server="name"]]`, `[[VS_TOOL read_mcp_resource server="name" uri="..."]]`

### 24. Hooks System (`src/hooks.c`)

PreToolUse/PostToolUse hooks:

- **Command-based hooks**: shell commands triggered before/after tool execution
- **Pattern matching**: pipe-separated tool name matching (e.g., `Edit|Write`)
- **JSON config**: `~/.vibesolaris/hooks.json`
- **16 hook limit**

### 25. Memory Rules Directory (`src/memory.c`)

Load memory rules from `~/.vibesolaris/memory/` directory:

- **vs_memory_load_rules_dir()**: scans for `.md` files
- **Auto-loading**: called during `vs_init()`
- **Context type**: loaded as `VS_MEMORY_TYPE_CONTEXT` entries

### 26. AutoDream — Memory Consolidation (`src/memory.c`)

Periodic memory consolidation that merges duplicates and prunes stale entries:

- **vs_memory_dream()**: merges duplicate entries, normalizes dates, prunes stale items
- **VSDreamStats**: returns stats on entries merged, pruned, and normalized
- **Auto-dream**: triggered at session start via `VIBESOLARIS_AUTO_DREAM` env var
- **TUI commands**: `/dream` (manual), `/dream off` (disable)

### 27. Auto Mode — Action Classifier (`src/auto_mode.c`)

Classifies tool calls as safe/risky/blocked before execution:

- **Default rules**: read/list/glob/grep = safe, write/edit/bash = risky, sudo/rm -rf = blocked
- **64-rule limit**, JSON config at `~/.vibesolaris/auto_mode.json`
- **TUI commands**: `/auto-mode on|off|status|rules`
- **Agent directive**: `[[VS_TOOL auto_mode action="on|off|status|add|rules"]]`

### 28. Agent Teams — Peer-to-Peer (`src/agent_team.c`)

File-based peer-to-peer agent messaging with shared scratch directories:

- **Max 8 agents per team, 4 teams**
- **JSONL message storage**: `~/.vibesolaris/teams/<team_id>/messages/`
- **TUI commands**: `/team create NAME`, `/team add TEAM ROLE`, `/team list`, `/team shutdown TEAM`
- **Agent directives**: `team_message`, `team_read`, `team_broadcast`

### 29. Plugins — Bundle System (`src/plugin.c`)

Install/list/uninstall versioned plugin bundles:

- **Plugin structure**: `plugin.json` + `skills/` + `commands/` + `hooks/` + `mcp/`
- **Max 16 plugins**, storage at `~/.vibesolaris/plugins/`
- **TUI commands**: `/plugin install URL`, `/plugin list`, `/plugin uninstall NAME`
- **Agent directive**: `[[VS_TOOL plugin action="install|list|uninstall"]]`

### 30. Code Review (`src/code_review.c`)

Multi-agent code review with file discovery:

- **File discovery**: scans target path for source files
- **Findings**: reports issues with severity levels
- **TUI command**: `/review [path] [scope] [focus]`
- **Agent directive**: `[[VS_TOOL code_review path="." scope="all" focus="security"]]`

### 31. Init — Auto-Onboarding (`src/init_cmd.c`)

Analyze a repo and generate CLAUDE.md for auto-onboarding:

- **Repo analysis**: scans structure, identifies language, frameworks, and patterns
- **CLAUDE.md generation**: creates project context document
- **TUI command**: `/init [path]`
- **Agent directive**: `[[VS_TOOL init path="."]]`

### 32. Goal Management (`src/util.c`)

Persistent goal setting for long-running tasks:

- **vs_goal_save/load/clear()**: persist goals to `~/.vibesolaris/goal.txt`
- **System prompt injection**: goal included in every agent turn
- **TUI commands**: `/goal set TEXT`, `/goal show`, `/goal clear`

### 33. Checkpoints — Git Snapshots (`src/checkpoint.c`)

Git stash-based checkpointing with LRU eviction:

- **16 checkpoint max** with automatic LRU eviction
- **Auto-checkpoint**: before write/edit/multi_edit/notebook_edit
- **TUI commands**: `/checkpoint create`, `/checkpoint rollback`, `/checkpoint list`
- **JSONL persistence**: `~/.vibesolaris/checkpoints/`

### 34. Keybindings & PDF (`src/keybindings.c`)

Custom keybindings loader and PDF reading:

- **vs_keybindings_load()**: parses `~/.vibesolaris/keybindings.json`
- **vs_read_pdf()**: reads PDFs via `pdftotext` (poppler-utils)
- **TUI command**: `/keybindings`

### 35. Computer Use — Cross-Platform

Screen capture and input across X11, Wayland, and macOS:

- **X11**: `src/computer_use.c` — X11 library (screenshot, click, type, move)
- **Wayland**: `src/computer_use_wayland.c` — external commands (grim, slurp, hyprctl, swaymsg)
- **Cocoa/macOS**: `src/computer_use_cocoa.c` — external commands (screencapture, osascript)
- **Backend detection**: `vs_computer_use_detect_backend()` auto-selects available backend
- **Conditional compilation**: `#ifdef HAVE_WAYLAND`, `#ifdef HAVE_COCOA`

### 36. CLI Flags & Shell Passthrough

Advanced CLI and TUI features:

- **--json**: JSON output mode for scripting
- **--disable-slash-commands**: pass `/` commands to agent
- **--resume**: resume previous conversation context
- **--worktree PATH**: enter worktree directory at startup
- **! mode**: shell passthrough — `! command` executes directly
- **TUI commands**: `/worktree [path]`, `/worktree-exit`, `/pause`, `/style compact|verbose|default`

## SSH Compatibility

The TUI compositor is designed to work through SSH:

1. **Auto-detection**: checks `SSH_TTY`, `TERM`, and terminal capabilities
2. **Fallback**: when alternate screen is unavailable, uses the existing prompt-based TUI
3. **No X11 dependency**: pure ANSI/VT100 escape sequences
4. **No mouse required**: keyboard-only navigation
5. **Bounded composer**: the existing 6-row input composer works over SSH

## Execution Flow

```
User Input
    |
    v
tui_compositor_read_input()  [full-screen or prompt mode]
    |
    v
vs_agent_turn()  [main agent loop in agent.c]
    |
    +---> parse_plan_marker()  [plan approval gate]
    |
    +---> preflight_batch()  [validate directives]
    |
    +---> vs_tool_executor_run_parallel()  [parallel read-only tools]
    |         |
    |         +---> pthread for each read-only tool
    |         +---> wait for all threads
    |         +---> collect results
    |
    +---> execute_batch()  [serial side-effecting tools]
    |
    +---> vs_coordinator_spawn()  [parallel sub-agents]
    |         |
    |         +---> worktree isolation (optional)
    |         +---> pthread per sub-agent
    |         +---> cancellation propagation
    |
    +---> agent_compact()  [context compaction every N rounds]
    |
    v
Response streamed to tui_compositor_append_stream()
    |
    v
tui_compositor_render()  [full-screen redraw]
```

## Differences from Claude Code

| Aspect | Claude Code | VibeSolaris |
|---|---|---|
| Language | TypeScript/Bun | Portable C (C99) |
| TUI | React/Ink | ANSI escape sequences |
| Dependencies | Node.js, ripgrep | cURL, OpenSSL, pthreads |
| Runtime | ~500 MB (Node + deps) | ~5 MB binary |
| SSH | Requires local terminal | Works through SSH |
| Parallel tools | Full parallel | Read-only parallel, serial side-effects |
| Worktree | git worktree | git worktree + file copy fallback |
| Skills | Full skill system | SKILL.md loader (inline/fork) |
| Memory | memdir with AI ranking | JSONL store with LRU |
| Platforms | macOS, Linux | Solaris, illumos, Linux, BSD, macOS |

## Build

```sh
make clean && make vibesolaris
```

Requires: C compiler, make, libcurl, OpenSSL/libcrypto, pthreads.

## Testing

```sh
make test              # all 1173 tests (34 suites)
make test-subagent     # sub-agent delegation tests
make test-settings     # multi-layer settings tests
make test-hooks-extended # hook enhancement tests
make test-project-index  # project index tests
make test-rewind       # conversation-level undo tests
make test-security-review # security scan tests
make test-loop         # scheduled recurring prompts tests
make test-workflow-engine # JSON workflow orchestration tests
make test-cross-session  # cross-instance messaging tests
make test-phase9       # Phase 9 additional features tests
```

## Phase 5: Advanced Features Beyond Parity

Phase 5 adds features that go beyond Claude Code parity:

- **Full LSP** (`src/lsp.c`): JSON-RPC LSP client with server spawning, diagnostics, go-to-definition, hover, rename
- **Multi-layer Settings** (`src/settings.c`): Three-layer settings (user/project/local) with merge priority
- **Hook Enhancements** (`src/hooks.c`): MessageDisplay hooks, prompt hooks, HTTP webhooks
- **Project Index** (`src/project_index.c`): Symbol search, reference tracking, dependency analysis
- **Additional TUI Commands**: `/config`, `/config layer`, `/cost`, `/clear`, `/export`, `/doctor`, `/bashes`, `/hooks`, `/rename`, `/reason`
- **Prompt Caching**: Provider-specific cache control annotations for faster repeated turns
- **Reasoning Engine**: Chain-of-thought analysis, task decomposition, confidence scoring, self-correction from tool failures, proactive suggestions

## Phase 7: Reasoning Quality & Autonomy

Phase 7 implements the final gap for Claude Code parity: reasoning quality and autonomous decision-making:

- **Reasoning engine** (`src/reasoning.c`, 496 lines): Chain-of-thought analysis, task decomposition, confidence scoring, self-correction from tool failures, proactive next-step suggestions
- **System prompt integration** (`src/provider.c`): Reasoning guidelines injected into system prompt based on depth level (1=minimal, 2=standard, 3=deep)
- **TUI commands** (`src/tui.c`): `/reason QUERY` for manual analysis, `/reason on|off` to toggle, `/reason depth [1-3]` to set depth
- **Configurable via env vars**: `VIBESOLARIS_REASONING_DEPTH` (1-3), `VIBESOLARIS_REASONING_DISABLE` (1/0)
- **83 new tests** across reasoning engine functions

## Phase 6: TUI Polish & Final Features

Phase 6 completes the remaining TUI features for full Claude Code parity:

- **Effort indicator for all providers** (`src/provider.c`): OpenAI-compatible endpoints now receive `output_config.effort` alongside Anthropic's native support
- **Mouse selection in full-screen mode** (`src/tui_compositor.c`): X11 mouse tracking with SGR extended mode parsing
- **Scrollback search** (`src/tui_compositor.c`): `/search QUERY` with case-insensitive search, `/search-next`, `/search-prev`, `/search-exit`
- **Enhanced status bar**: Shows cache status, LSP server count, and search mode indicator with match count
- **Keyboard shortcuts** (`src/tui.c`): Ctrl+T (tasks), Ctrl+R (checkpoint rollback), Shift+Tab (cycle mode)
- **TUI Theme system** (`src/tui_theme.c`): Configurable color themes with dark/light defaults
- **TUI Suggestions engine** (`src/tui_suggestions.c`): Context-aware command and file suggestions with fuzzy matching
- **TUI Multiline Editor** (`src/tui_editor.c`): Full-screen vi-style editor with navigation and text wrapping

## Phase 8: Missing Features & GUI

Phase 8 implements features Claude Code has that VibeSolaris was missing:

- **`/rewind`** (`src/rewind.c`): Conversation-level undo with markers, history truncation, and checkpoint rollback
- **`/security-review`** (`src/code_review.c`): CWE-based vulnerability scanning (hardcoded secrets, SQL injection, buffer overflows, XSS, command injection, path traversal, weak crypto)
- **`/loop`** (`src/loop.c`): Scheduled recurring prompt execution via background pthread (60s minimum interval)
- **Dynamic workflows** (`src/workflow_engine.c`): JSON-based workflow engine for scriptable subagent orchestration (sequential, parallel, conditional steps)
- **Cross-session messaging** (`src/cross_session.c`): Instance discovery and messaging via shared filesystem (~/.vibesolaris/sessions/)
- **GUI build** (`src/gui.c`): Desktop app builds alongside TUI, 585 KB binary

## Phase 9: Additional Claude Code Features

Phase 9 adds 12 more Claude Code features for near-complete parity:

- **`/btw`** (`src/phase9.c`): Side comments without polluting main thread
- **`/model`** (`src/phase9.c`): Mid-session model switching
- **`/plan`** (`src/phase9.c`): Plan mode — propose without executing
- **Custom slash commands** (`src/phase9.c`): Load from markdown files with YAML frontmatter
- **`/branch`** (`src/phase9.c`): Git branch automation
- **`/diff`** (`src/phase9.c`): Staged changes preview
- **Extended effort levels** (`src/phase9.c`): xhigh, max, ultracode
- **`/commit`** (`src/phase9.c`): Smart commit message generation
- **`/pr`** (`src/phase9.c`): PR description generation
- **PreCompact hooks** (`src/phase9.c`): VS_HOOK_PRE_COMPACT
- **Markdown table rendering** (`src/phase9.c`): Parse and render aligned tables
- **`@-mention` context** (`src/phase9.c`): Resolve @file, @memory, @skill, @task references

## Remaining Gaps vs Claude Code

Features that require external infrastructure beyond portable C:

| Feature | Why it's missing |
|---|---|
| Remote Control / Web UI | Requires web server + frontend |
| Dispatch API | Requires REST API server + job queue |
| Channels (Telegram, Discord) | Requires external API integration |
| Cloud Sessions | Requires cloud VM infrastructure |
| IDE Integrations (VS Code, JetBrains) | Requires native plugin development |
| Voice Mode | Requires audio processing libraries |
| Cowork (desktop agent) | Requires full GUI application |
| `/schedule` (persistent tasks) | Could be added — persistent cron-like tasks |
| Plugin sharing marketplace | Could be added — mod distribution system |
| Cost estimation per provider | Could be added — pricing calculator |
