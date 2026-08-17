# MCP support

VibeSolaris 0.9.5 includes an MCP client in the shared C agent core.

## Transports

Two transports are supported:

- **stdio** for local MCP servers. VibeSolaris launches the configured command as a subprocess and exchanges newline-delimited JSON-RPC over its standard input/output. The GUI/TUI also accept `stdin` as a friendly alias for `stdio`.
- **Streamable HTTP** for remote MCP endpoints. VibeSolaris POSTs JSON-RPC to the configured endpoint and accepts JSON or SSE response bodies.

The implementation prefers the current stateless MCP `2026-07-28` request format and, for Streamable HTTP, emits `MCP-Protocol-Version`, `Mcp-Method`, and `Mcp-Name` where required. If a server rejects that format, VibeSolaris automatically falls back to the `2025-11-25` initialize/initialized lifecycle; for legacy Streamable HTTP it also preserves and returns `Mcp-Session-Id`. Remote definitions may include a bearer token; the token is stored only inside the active encrypted configuration.

## Configuration

TUI examples:

```text
/mcp add-stdio files python3 /opt/mcp/files_server.py
/mcp add-stdin git /usr/local/bin/git-mcp --repo /work/project
/mcp add-http search https://mcp.example.com/mcp TOKEN
/mcp list
/mcp refresh
/mcp tools
/mcp remove search
```

The Xlib GUI has an **MCP** item in the left sidebar. Its compact entry format is:

```text
NAME|stdio|COMMAND
NAME|stdin|COMMAND
NAME|http|URL|BEARER_TOKEN
```

The bearer token is optional.

MCP server definitions are automatically encrypted with provider/model credentials. VibeSolaris uses `/etc/vibesolaris/config.enc` when writable and otherwise automatically uses the current user’s `~/.vibesolaris/config.enc`.

## Agent use

At the beginning of an agent turn, VibeSolaris discovers configured MCP tools using `tools/list`. The stable tool name, description, and `inputSchema` are added to the model's system context. A model can request a call using the host directive:

```text
[[VS_MCP server="search" tool="web_search" args="{\"query\":\"Solaris 11.4\"}"]]
```

VibeSolaris sends `tools/call`, returns the JSON-RPC result to the model, and allows the agent to continue for the same bounded tool-round limit used by local file/command tools.

## Activity trace

VibeSolaris records an execution trace for every agent turn, including:

- agent turn start/completion
- each model round and selected provider/model/protocol
- MCP server startup and tool discovery
- every MCP method and tool name invoked
- MCP arguments and returned result (bounded in the display trace)
- local `read`, `run`, and `write` tool requests
- command exit status and tool output

The GUI shows recent trace entries in the sidebar and inserts the complete trace as a selectable/copyable message before the final assistant answer. The TUI prints the trace before the final answer and exposes `/trace`.

The trace intentionally records observable execution operations; it does not expose or fabricate hidden model chain-of-thought.

## Compatibility note

The native implementation prefers current stateless MCP (`2026-07-28`) but also negotiates the previous `2025-11-25` initialize/initialized lifecycle for stdio and Streamable HTTP servers, including legacy HTTP session IDs. It does not implement the deprecated `2024-11-05` HTTP+SSE transport.
