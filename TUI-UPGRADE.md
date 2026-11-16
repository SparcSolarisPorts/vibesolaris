# TUI upgrade (incremental)

This is an incremental upgrade, not a complete recreation of Claude Code.

Implemented:
- `❯` input prompt and compact model/mode/effort header.
- Shift+Tab cycles existing workflow modes while editing input.
- `/effort default|low|medium|high` controls the Anthropic `output_config.effort` request option. Defaults to omission. This is provider/model-dependent; a proxy that does not support the field can reject a request. The app does not silently claim support.
- Subagent summary output is indented using recorded parent depth.
- Existing slash completion, input history, Ctrl+J multiline input, and workflow approval remain enabled.
- **Full-screen TUI compositor** (`src/tui_compositor.c`): alternate-screen layout with scrollback buffer (512 lines), status bar (provider, model, mode, tokens), input bar with cursor navigation, and optional right sidebar showing concurrent sub-agents. Auto-detects alternate screen support; falls back to classic prompt mode over SSH when DECSET ?1049 is unavailable. Terminal resize handling via SIGWINCH.
- **TUI Theme system** (`src/tui_theme.c`): configurable color themes for full-screen mode with dark/light defaults, file-based theme loading/saving, and 16 color slots (bg, fg, status, prompt, sidebar, highlight, error, warning, success, dim, link, comment, keyword, string).
- **TUI Suggestions engine** (`src/tui_suggestions.c`): context-aware command and file suggestions with fuzzy matching, scoring (start-match bonus, word-boundary bonus, consecutive-match bonus, length penalty), and columnar rendering.
- **TUI Multiline Editor** (`src/tui_editor.c`): full-screen scrollback-aware editor with vi-style navigation (up/down/left/right/home/end), insert/backspace/newline, line joining on backspace, text wrapping at terminal width, and render support.
- **Agent coordinator** (`src/coordinator.c`): parallel sub-agent scheduling with pthreads. Each spawned agent runs in its own thread. Worktree isolation via `git worktree` (with fallback to file copy). Cancellation propagates to all children. Up to 8 concurrent sub-agents.
- **Parallel tool executor** (`src/tool_executor.c`): read-only tools (read, list, search, graph, web_search, web_fetch) can run concurrently in separate threads. Side-effecting tools (write, run, subagent, MCP) remain serial for safety. Mirrors Claude Code's StreamingToolExecutor pattern.
- **Skill system** (`src/skill.c`): loads SKILL.md files from directories, extracts prompts (with YAML frontmatter support), and executes skills in inline or fork mode. Supports `/skill` and `/skill-list` commands.
- **Persistent memory** (`src/memory.c`): cross-session memory store with JSONL persistence (`~/.vibesolaris/memory.jsonl`). Supports save, load, delete, search, and list operations. Entries have keys, values, tags, types, and access timestamps. LRU eviction when full (128 entries max).

## Phase 4 Advanced Features (Complete)

All Phase 4 features are now implemented and tested:
- **Checkpoints** (`/checkpoint create|rollback|list`): Git stash-based snapshots before edits, with auto-checkpointing.
- **AutoDream** (`/dream`): Memory consolidation that merges duplicates, prunes stale entries, normalizes dates.
- **Auto Mode** (`/auto-mode on|off|status|rules`): Action classifier — safe/risky/blocked tool call classification.
- **Agent Teams** (`/team create|add|list|shutdown`): Peer-to-peer agent messaging with shared scratch directories.
- **Plugins** (`/plugin install|list|uninstall`): Versioned plugin bundles with skills, commands, hooks, and MCP.
- **Code Review** (`/review`): Multi-agent code review with file discovery and severity-based findings.
- **Goal Management** (`/goal set|show|clear`): Persistent project goals injected into system prompt.
- **Auto-Onboarding** (`/init`): Repository analysis and CLAUDE.md generation.
- **Away Summary** (`/pause`): Session pause with elapsed time summary on resume.
- **Output Styles** (`/style compact|verbose|default`): Control output verbosity.
- **Keybindings** (`/keybindings`): Custom keybinding configuration from JSON.
- **PDF Reading**: PDF file reading via pdftotext.
- **CLI Flags**: `--json`, `--disable-slash-commands`, `--resume`, `--worktree PATH`.
- **Shell Passthrough** (`!` mode): Lines starting with `!` execute as shell commands directly.
- **Worktree** (`/worktree`, `/worktree-exit`): Enter/exit isolated worktree directories.
- **Computer Use**: Cross-platform screen capture and input (X11, Wayland, Cocoa/macOS).
- **Multi-Layer Settings** (`/config`, `/config layer`): Three-layer settings (user/project/local) with merge priority.
- **Hook Enhancements**: MessageDisplay hooks, prompt hooks, HTTP webhooks.
- **Project Index**: Symbol search, reference tracking, dependency analysis.
- **Full LSP**: JSON-RPC LSP client with diagnostics, go-to-definition, hover, rename.
- **Prompt Caching**: Provider-specific cache control for faster repeated turns.

## Still Not Implemented

- None — all planned features are implemented.

## Keyboard Shortcuts (TUI)

- **Ctrl+T**: Show tasks list (quick access to cross-session tasks)
- **Ctrl+R**: Roll back to previous checkpoint (undo last edit)
- **Shift+Tab**: Cycle workflow mode (auto/plan/manual)
- **Ctrl+A/E**: Move to start/end of line
- **Ctrl+U**: Clear input line
- **Ctrl+L**: Clear screen
- **Ctrl+W**: Delete previous word
- **Ctrl+J**: Insert newline in multiline input

## Recently Added

- **Mouse selections in full-screen mode**: X11 mouse tracking (CSI ?1000h) with SGR extended mode parsing; click coordinates captured for future scrollback interaction.
- **Effort indicator for all providers**: OpenAI-compatible endpoints now receive `output_config.effort` alongside Anthropic's native support.
- **Scrollback search**: `/search QUERY` searches the scrollback buffer case-insensitively; `/search-next` and `/search-prev` navigate matches; `/search-exit` leaves search mode. Status bar shows search state.
- **Enhanced status bar**: Shows cache status (`cache:on`) and search mode indicator with match count.

Build: `make clean && make vibesolaris` (POSIX C environment, OpenSSL/cURL/pthreads).
Tests run on Linux: `make test`.
