# Cowork Comparison

This document compares Anthropic's Claude Cowork with VibeSolaris-GUI to clarify
their different target audiences, capabilities, and design philosophies.

## What is Claude Cowork?

Claude Cowork is Anthropic's **agentic desktop assistant for non-technical knowledge
workers**, announced in early 2026. It runs inside the Claude desktop app and
targets operations, research, finance, and legal teams.

### Key characteristics

- **Target audience**: Non-developers who work with documents, spreadsheets, and
  presentations
- **Interface**: GUI-only, no terminal, no commands to memorize
- **Sandboxing**: Runs code and shell actions inside a **local virtual machine**
  (Apple Virtualization framework on macOS, Host Compute Service on Windows)
- **Core job**: File organization, document drafting, data extraction, spreadsheet
  formulas, research synthesis
- **Connectors**: Integrations with Asana, Notion, browser workflows via
  Claude-in-Chrome
- **Setup**: None beyond installing the desktop app
- **Audit trail**: Minimal — no Compliance API, no audit logs, conversations stay
  local

## What is VibeSolaris-GUI?

VibeSolaris-GUI is a **desktop wrapper around the same agent engine as the VibeSolaris
TUI**, built for developers who prefer a visual interface over a terminal.

### Key characteristics

- **Target audience**: Developers who want a visual interface instead of a terminal
- **Interface**: X11-based desktop app with sidebar, message panel, composer, and
  settings modals
- **Sandboxing**: None — runs directly on the host, same as the TUI
- **Core job**: Code generation, refactoring, debugging, git operations, MCP tool
  execution — the full VibeSolaris agent loop
- **Connectors**: MCP (Model Context Protocol) servers, LSP servers, hooks
- **Setup**: Compiled from source, requires X11
- **Features**: Message history, task tracking, provider settings, OAuth
  configuration, MCP management, slash commands

## Side-by-Side Comparison

| Aspect              | Claude Cowork                     | VibeSolaris-GUI               |
|---------------------|-----------------------------------|-------------------------------|
| Audience            | Non-technical knowledge workers   | Developers                    |
| Sandboxing          | Local VM (isolated execution)     | None (direct host access)     |
| Primary domain      | Documents, files, spreadsheets    | Codebases, git, CI pipelines  |
| Connectors          | Asana, Notion, browser            | MCP servers, LSP, hooks       |
| Execution model     | VM-isolated agent                 | Direct host agent             |
| Audit trail         | None (local only)                 | Session logs, hooks           |
| Platform            | macOS, Windows (native)           | Any X11 platform              |
| Dependencies        | Proprietary desktop app           | Portable C, X11 library       |
| Size                | Part of Claude desktop app        | ~585 KB standalone binary     |
| TUI alternative     | No                                | Yes — full TUI parity         |
| Open source         | No                                | Yes                           |

## What VibeSolaris-GUI is Missing vs Cowork

1. **Local VM sandboxing** — Cowork runs agent actions in a VM for safety. Adding
   `unshare`/`bubblewrap` sandboxing on Linux is possible; a full VM is out of
   scope for portable C.
2. **Document-centric workflows** — Cowork specializes in file organization,
   document drafting, and spreadsheet manipulation. Our GUI is code-centric.
3. **SaaS connectors** — Cowork integrates with Asana, Notion, and browser
   automation. We have MCP, which is more general-purpose but requires manual
   server configuration.
4. **Polished UX** — Cowork is a commercial product with professional UI design.
   Our GUI is functional but rough around the edges.
5. **Scheduled cloud tasks** — Cowork can run scheduled tasks in the cloud. We have
   `/loop` for local recurring prompts.

## What VibeSolaris-GUI Has That Cowork Doesn't

- **Full code agent capabilities** — multi-file refactors, parallel subagents, LSP
  integration, git workflows
- **MCP ecosystem** — extensible tool integration via Model Context Protocol
- **Portable C** — runs on Solaris, Linux, BSD, macOS; no proprietary runtime
- **TUI parity** — all GUI features also work in the terminal via SSH
- **Open source** — full transparency, no vendor lock-in
- **Hooks system** — pre/post tool hooks, HTTP webhooks, PreCompact hooks
- **Cross-session messaging** — instances can discover and message each other
- **Workflow engine** — JSON-based scriptable subagent orchestration
- **Conversation rewind** — `/rewind` for undoing conversation history
- **Security review** — `/security-review` for CWE-based vulnerability scanning
- **Plan mode** — `/plan` to propose changes without executing them
- **Custom slash commands** — user-defined commands from markdown files
- **1173+ unit tests** — comprehensive test coverage across 34 test suites

## Bottom Line

Cowork is essentially "Claude Code for non-developers" — same agentic engine,
different target audience and UX. VibeSolaris-GUI is "VibeSolaris with a visual
interface" — same developer-focused agent, just with a GUI instead of a terminal.

They solve different problems: Cowork automates knowledge work for business users;
VibeSolaris-GUI provides a desktop alternative to the terminal for developers who
already use VibeSolaris.
