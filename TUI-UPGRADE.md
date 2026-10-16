# TUI upgrade (incremental)

This is an incremental upgrade, not a complete recreation of Claude Code.

Implemented:
- `❯` input prompt and compact model/mode/effort header.
- Shift+Tab cycles existing workflow modes while editing input.
- `/effort default|low|medium|high` controls the Anthropic `output_config.effort` request option. Defaults to omission. This is provider/model-dependent; a proxy that does not support the field can reject a request. The app does not silently claim support.
- Subagent summary output is indented using recorded parent depth.
- Existing slash completion, input history, Ctrl+J multiline input, and workflow approval remain enabled.

Not yet implemented:
- Full-screen scrollback-aware editor, correctly reflowed multiline editing at terminal width, mouse selections.
- Full Claude Code command suggestion ranking and file/path suggestions.
- Live expandable subagent tree/sidebar and concurrent agents.
- A unified per-request effort indicator and capability negotiation for OpenAI/Gemini-compatible endpoints.
- Claude Code parity in tools, reasoning quality, autonomy, permissions, or UX.

Build: `make clean && make vibesolaris` (POSIX C environment, OpenSSL/cURL/pthreads).
Tests run on Linux: `make test-workflow`.
