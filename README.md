# VibeSolaris 0.9.5

VibeSolaris is a lightweight local AI coding client intended to run on old and new Unix systems without requiring Electron, Qt, GTK, Java, Node.js, or a browser engine.

It ships two programs over the same agent core:

- `vibesolaris` — terminal interface with ANSI/VT100 colour coding.
- `vibesolaris-gui` — lightweight pure-Xlib chat interface with file/image attachments, clipboard support, text selection, and X11 drag-and-drop.

The shared agent core can inspect files, edit files, run local commands, use `AGENT.MD`, call MCP servers, maintain conversation context, and expose an activity trace showing every observable model/tool/MCP step it takes.

Supported targets include Solaris 8 through Solaris 11.4, illumos, Linux (including SPARC/SPARC64), FreeBSD, OpenBSD, NetBSD, DragonFly BSD, and Darwin/XNU/macOS on Intel, Apple Silicon/ARM64, and historical PowerPC where the required libraries and a working compiler are available.

Project documentation and user-facing text use **British English** (`colour`, `behaviour`, `authorisation`, `licence`). Standard protocol names, HTTP headers such as `Authorization`, environment conventions such as `NO_COLOR`, and old command-line aliases are left unchanged where compatibility requires them.

## Contents

1. [Quick start](#quick-start)
2. [Dependencies](#dependencies)
3. [Building](#building)
4. [First-time configuration](#first-time-configuration)
5. [Providers and models](#providers-and-models)
6. [Using the GUI](#using-the-gui)
7. [Using the TUI](#using-the-tui)
8. [Files, commands, and AGENT.MD](#files-commands-and-agentmd)
9. [MCP](#mcp)
10. [Activity trace](#activity-trace)
11. [Caching](#caching)
12. [Encrypted configuration](#encrypted-configuration)
13. [OAuth](#oauth)
14. [Build/package output](#buildpackage-output)
15. [Troubleshooting](#troubleshooting)
16. [Security](#security)
17. [Licence](#licence)

## Quick start

VibeSolaris is easiest to use if you treat the directory you launch it from as the project workspace. The agent can read and edit files and run commands from that working directory, so start it from the repository or folder you actually want it to work on.

A first session normally looks like this:

1. Install the development dependencies for your operating system.
2. Build VibeSolaris with `./build.sh`.
3. `cd` into the project you want the agent to work on, or launch VibeSolaris from the VibeSolaris directory while testing it.
4. Choose a provider, model, protocol, API URL, and credential.
5. Add an `AGENT.MD` to the project if you want permanent project-specific instructions.
6. Ask a normal-language coding question. VibeSolaris will show an activity trace whenever it reads files, runs commands, or calls MCP tools.

On a Linux desktop with the development dependencies installed:

```sh
./build.sh
./vibesolaris-gui
```

For the terminal version:

```sh
./vibesolaris
```

The first time you configure a provider, VibeSolaris automatically saves the selected provider, model, protocol, API URL, credentials, cache settings, OAuth data, and MCP definitions into an encrypted configuration store.

For an ordinary user this normally becomes:

```text
~/.vibesolaris/config.enc
~/.vibesolaris/master.key
```

You do **not** normally need root access or `setup-system-config.sh`.

A basic TUI session looks like this:

```text
/provider qwen
/model YOUR_MODEL_ID
/key YOUR_API_KEY

Explain this project and tell me what file I should inspect first.
```

The GUI exposes the same settings in the left sidebar.

## Dependencies

Required for both front ends:

- C compiler: GCC preferred; Clang is supported on Linux/BSD/Darwin; Sun/Oracle Studio `cc` remains a portability target.
- `make`.
- libcurl development headers and library.
- OpenSSL/LibreSSL-compatible libcrypto development headers and library.

Additional GUI dependency:

- X11 development headers and `libX11`.

`pkg-config`/`pkgconf` is optional but used automatically when present.

### Fedora / RHEL / Rocky / Alma

```sh
sudo dnf install gcc make pkgconf-pkg-config libcurl-devel libX11-devel openssl-devel
```

### Debian / Ubuntu

```sh
sudo apt-get install build-essential pkg-config libcurl4-openssl-dev libx11-dev libssl-dev
```

### openSUSE / SLES

```sh
sudo zypper install gcc make pkg-config libcurl-devel libX11-devel libopenssl-devel
```

### Arch Linux

```sh
sudo pacman -S --needed base-devel pkgconf curl libx11 openssl
```

### FreeBSD / DragonFly

Typical third-party dependencies are:

```sh
pkg install curl libX11 pkgconf
```

The base system normally provides a compiler, `make`, and an OpenSSL-compatible crypto library, depending on the release.

### OpenBSD

Install curl and ensure the X11 development files from the xbase sets are present if you want the GUI:

```sh
pkg_add curl
```

### NetBSD

With pkgsrc/pkgin, a typical setup is:

```sh
pkgin install curl libX11 pkg-config
```

### Darwin / macOS / XNU

The TUI is a normal POSIX terminal program. The GUI remains pure Xlib, so macOS uses XQuartz rather than Cocoa. Current Intel and Apple Silicon systems can use Xcode Command Line Tools plus Homebrew/MacPorts dependencies; historical PowerPC Darwin/macOS can use a matching GCC/Clang-era compiler and third-party curl/OpenSSL/X11 libraries.

A current Homebrew setup is typically:

```sh
xcode-select --install
brew install pkg-config curl openssl@3
```

Install XQuartz if you want `vibesolaris-gui`, or use:

```sh
VS_NO_GUI=1 ./build.sh
```

The build script recognises `arm64`/`aarch64`, `x86_64`/`amd64`, 32-bit Intel, `ppc`/`powerpc`, and `ppc64`/`powerpc64` Darwin machine names.

### Solaris / illumos

You need:

- GCC or Sun/Oracle Studio C compiler.
- a sufficiently modern libcurl and TLS stack for current HTTPS AI endpoints.
- OpenSSL-compatible libcrypto.
- X11 headers/libraries for `vibesolaris-gui`.

On very old Solaris installations, obtaining a modern TLS-capable libcurl is usually the larger compatibility problem than compiling the C source itself.

## Building

The normal build is:

```sh
./build.sh
```

To force a compiler:

```sh
VS_CC=gcc ./build.sh
```

To build only the terminal client and avoid the X11 dependency:

```sh
VS_NO_GUI=1 ./build.sh
```

You can also invoke `make` directly after dependencies are available:

```sh
make clean
make
```

`build.sh` is recommended because it performs dependency probes, identifies the host OS/architecture, and creates the appropriate package/archive output.

### Solaris cross-ISA builds

A Solaris x86 machine cannot create a usable SPARC binary merely by changing an architecture name, and vice versa. Building the opposite ISA requires a real cross compiler plus the matching target headers/libraries/sysroot.

Example:

```sh
VS_SPARC_CC=/opt/cross/bin/sparc-sun-solaris2.11-gcc \
VS_SPARC_CFLAGS='--sysroot=/opt/sysroots/solaris-sparc' \
VS_SPARC_LDFLAGS='--sysroot=/opt/sysroots/solaris-sparc' \
./build.sh
```

See `PORTABILITY.md` for more detail.

## First-time configuration

There are five settings that matter for most providers:

1. **Provider** — OpenAI, Claude, Qwen, GLM, Kimi, ERNIE, Gemini, DeepSeek, or Custom.
2. **Protocol** — OpenAI-compatible, Anthropic-compatible, or Gemini native, depending on provider.
3. **Model** — the provider model ID you want to use.
4. **API URL** — provider endpoint/base URL.
5. **Credential** — API key/plan credential, or configured OAuth where applicable.

VibeSolaris remembers these **per provider**. Qwen can therefore have its own model/key/URLs, Claude another key/model, DeepSeek another model/key, and so on. Providers supporting both OpenAI and Anthropic compatibility retain a separate URL for each protocol.

### GUI setup

1. Start `./vibesolaris-gui`.
2. Choose a provider in the sidebar.
3. Choose or type the model ID.
4. Choose the protocol if that provider supports more than one.
5. Check/edit the API URL.
6. Open the authentication/API-key control and enter the credential for that provider.
7. Start a new chat message.

Changes are autosaved to the encrypted configuration store when possible.

#
### Per-prompt activity trace in the GUI

Every GUI prompt has its own **Activity** disclosure row between the prompt and the final answer. It is collapsed by default so normal chat stays compact. The row summarises the number of recorded steps, model calls, local tools, MCP calls, and errors. Click the row to expand the complete execution trace; click it again to collapse it. Expanded trace text can be selected and copied like other conversation text.

The trace is an execution log (model requests, local tools, commands, file operations, MCP discovery/calls/results), not private model chain-of-thought. The TUI streams each observable activity event immediately while the agent is working and retains the completed trace for `/trace`.

## TUI setup

Example:

```text
/provider deepseek
/protocol openai
/model YOUR_MODEL_ID
/base https://your-provider-endpoint.example/v1
/key YOUR_API_KEY
```

Check the encrypted storage location with:

```text
/globalconfig status
```

## Providers and models

Built-in provider names are:

```text
openai
claude
gemini
glm
glm-coding
kimi
qwen
ernie
deepseek
custom
```

Model IDs and provider URLs are editable deliberately. Providers change catalogues, regional endpoints, plan-specific endpoints, and model names over time, so VibeSolaris does not require recompilation merely to use a new model ID.

### OpenAI

```text
/provider openai
/model MODEL_ID
/key API_KEY
```

The URL is editable with `/base URL` or through the GUI.

VibeSolaris also contains a generic OAuth 2.0 Authorisation Code + PKCE client. It does **not** invent or reverse-engineer ChatGPT OAuth credentials. Use it only if legitimate OAuth application credentials/endpoints have been issued for your application. See `OAUTH.md`.

### Claude

Claude uses the Anthropic Messages adapter:

```text
/provider claude
/model MODEL_ID
/key API_KEY
```

The API URL remains editable for supported gateways or provider endpoints.

### Qwen

Qwen supports both adapters in VibeSolaris:

```text
/provider qwen
/protocol openai
```

or:

```text
/provider qwen
/protocol anthropic
```

Each protocol has its own remembered URL. This is useful when Coding Plan, Token Plan, regional, or compatible endpoints differ.

### GLM / GLM Coding

```text
/provider glm
```

or:

```text
/provider glm-coding
```

Both can use OpenAI-compatible or Anthropic-compatible routing in VibeSolaris:

```text
/protocol openai
/protocol anthropic
```

### DeepSeek

```text
/provider deepseek
/protocol openai
/model MODEL_ID
/key API_KEY
```

DeepSeek can also be switched to the Anthropic-compatible adapter when the selected endpoint supports it.

### Gemini

```text
/provider gemini
/model MODEL_ID
/key API_KEY
```

Gemini uses its native adapter rather than pretending to be OpenAI-compatible.

### Custom endpoint

Use this for an OpenAI-compatible or Anthropic-compatible server not represented by a preset:

```text
/provider custom
/protocol openai
/base https://server.example/v1
/model my-model
/key my-key
```

or:

```text
/provider custom
/protocol anthropic
/base https://server.example
/model my-model
/key my-key
```

See `PROVIDERS.md` for the provider/protocol behaviour in more detail.

## Using the GUI

Start it with:

```sh
./vibesolaris-gui
```

The GUI is intentionally pure Xlib. It does not load a web browser engine and does not require GTK/Qt/Electron.

### Chat composer

- Enter — send.
- Shift+Enter — insert a newline.
- Ctrl/Meta+A — select all in the active text field/composer.
- Ctrl/Meta+C — copy selected text to the X11 clipboard.
- Ctrl/Meta+V — paste from the X11 clipboard.
- Ctrl/Meta+X — cut selected editable text.
- Middle mouse button — paste the X11 PRIMARY selection.
- Ctrl/Meta+L — clear composer.
- Ctrl/Meta+N — new chat.

Mouse dragging selects text in the composer and chat messages. Selection can be copied into other X11 applications.

### File and image attachments

You can attach files in three ways:

1. Click the `+` attachment button.
2. Use Ctrl/Meta+O.
3. Drag files from an XDND-capable X11/XWayland file manager and drop them onto the VibeSolaris window.

Attachment chips appear in the composer area. Click a chip to remove it before sending.

The current build supports up to 16 attachments per request.

### GUI connection controls

The sidebar lets you change:

- provider
- model
- protocol where applicable
- API/base URL
- authentication/API key
- cache state
- encrypted configuration
- MCP servers

The authentication controls change with provider/protocol instead of showing one unrelated login method for everything.

### Activity trace in the GUI

Every GUI agent turn includes a collapsed **Activity** disclosure row before the final model answer. Click it to expand events such as:

```text
[agent] starting agent turn
[model-request] provider=qwen protocol=openai model=...
[mcp-request] filesystem -> tools/list
[mcp-call] filesystem.read_file ...
[tool] run command: make
[command] command exited 0
[model] model round 2
[agent] completed without further tool calls
```

This is an observable execution trace. It is not hidden chain-of-thought.

## Using the TUI

Start it with:

```sh
./vibesolaris
```

The TUI now uses ANSI/VT100 colour automatically when stdout is a terminal and `TERM` is usable.

Typical colours are:

- cyan — prompt/model requests and important interface labels
- blue — connection/status sections
- green — successful actions/results and answer headings
- yellow — local tools/commands/warnings
- magenta — MCP and activity-trace sections
- red — errors/failures
- dim text — secondary information

When output is redirected or piped, colours are disabled automatically.

Disable colours explicitly:

```sh
./vibesolaris --no-colour
```

Force colours:

```sh
./vibesolaris --colour
```

The conventional `NO_COLOR` environment variable is honoured in automatic mode:

```sh
NO_COLOR=1 ./vibesolaris
```

You can also change it while running:

```text
/colour auto
/colour on
/colour off
/colour status
```

For compatibility with older VibeSolaris releases, `/color`, `--color`, `--no-color`, and `VIBESOLARIS_COLOR` are still accepted as aliases. New documentation and examples use the British spellings `/colour`, `--colour`, `--no-colour`, and `VIBESOLARIS_COLOUR`.

### Core TUI commands

Show help:

```text
/help
```

Provider/model setup:

```text
/provider NAME
/protocol openai|anthropic
/model MODEL
/base URL
/key KEY
```

Attachments and local tools:

```text
/attach PATH
/clearattach
/read PATH
/run COMMAND
```

Cache/history:

```text
/cache status
/cache on
/cache off
/cache clear
/history status
/history clear
```

Encrypted config:

```text
/globalconfig status
/globalconfig load
/globalconfig save
```

Activity and token usage:

```text
/trace
/usage
```

Exit:

```text
/quit
```

## Files, commands, and AGENT.MD

VibeSolaris is an agent, not only a chat client.

At startup it detects the operating system, OS release, CPU architecture, current working directory, and available project instructions. That information is added to the model context so an agent can distinguish, for example, Solaris/SPARC from Linux/x86-64.

### AGENT.MD

Place an `AGENT.MD` file in the project where you launch VibeSolaris. It can contain instructions such as:

```markdown
# Project instructions

- This project targets Solaris 11.4 SPARC and x86-64.
- Use portable C89/C90-style constructs where practical.
- Do not introduce GTK, Qt, Electron, Node, or Java dependencies.
- Run `make` after modifying C sources.
- Keep command execution visible in the activity trace.
```

An example is provided at:

```text
examples/AGENT.MD
```

### Local tools

The agent can request host operations including:

- reading a file
- writing/editing a file
- running a shell command

VibeSolaris returns the operation result to the model and can continue for a bounded number of tool rounds.

Run the program as an ordinary user in the project directory you intend it to modify.

## MCP

VibeSolaris supports MCP servers over:

- local `stdio` (`stdin` is accepted as an alias in the UI)
- remote Streamable HTTP

### Local MCP server

```text
/mcp add-stdio files python3 /opt/mcp/files_server.py
```

or:

```text
/mcp add-stdin files python3 /opt/mcp/files_server.py
```

### Remote MCP server

Without authentication:

```text
/mcp add-http search https://mcp.example.com/mcp
```

With a bearer token:

```text
/mcp add-http search https://mcp.example.com/mcp YOUR_TOKEN
```

Inspect servers/tools:

```text
/mcp list
/mcp refresh
/mcp tools
```

Remove one:

```text
/mcp remove search
```

MCP definitions and remote bearer tokens are included in the encrypted autosaved configuration.

See `MCP.md` for transport/protocol details.

## Activity trace

VibeSolaris records an execution trace for each agent turn. It includes observable actions such as:

- agent turn start/completion
- model round number
- provider/model/protocol used
- local file read/write requests
- shell commands and exit status
- MCP discovery
- MCP server/tool calls
- bounded tool/MCP results

The TUI prints each trace event live as it occurs, so model rounds, local tools, commands, and MCP calls are visible while the turn is still running. `/trace` shows the completed trace again afterwards.

The GUI displays a collapsed per-prompt Activity disclosure row; clicking it reveals selectable/copyable trace text. Recent activity is also surfaced in the interface.

The trace does not reveal or fabricate hidden model reasoning.

### Conversation token usage

Both front ends keep a running total of **provider-reported token usage for the current conversation**. The TUI prints the total after every answer and exposes it with `/usage`; the GUI shows total, input, and output tokens in the sidebar. Starting a new chat or using `/history clear` resets the conversation counters.

OpenAI-compatible providers normally report `prompt_tokens`, `completion_tokens`, and `total_tokens`; Anthropic-compatible providers report input/output usage with cache fields; Gemini reports its usage metadata. VibeSolaris normalises those values into input/output/total counters. If a provider does not return usage metadata, the interfaces say that token usage has not been reported rather than estimating it.

## Caching

Caching is enabled by default.

VibeSolaris has three related mechanisms:

1. provider prompt/context caching where supported by the selected adapter/provider
2. local unchanged-file/image caching
3. bounded append-only conversation history with compaction

Check TUI statistics with:

```text
/cache status
```

Clear local cache/statistics:

```text
/cache clear
```

See `CACHING.md` for details.

## Encrypted configuration

VibeSolaris automatically chooses a configuration directory in this order:

1. `VIBESOLARIS_GLOBAL_CONFIG_DIR` if explicitly set.
2. `/etc/vibesolaris` if the current user can write it.
3. `~/.vibesolaris` otherwise.

For normal desktop use the third case is expected, which means every Unix account receives separate encrypted settings.

Typical files:

```text
~/.vibesolaris/config.enc
~/.vibesolaris/master.key
```

Default permissions are:

```text
~/.vibesolaris             0700
config.enc                  0600
master.key                  0600
```

The encrypted config includes provider selection, per-provider model/API key, per-protocol URL, cache settings, OAuth configuration/session values, and MCP server definitions/authentication.

Encryption uses AES-256-CBC plus encrypt-then-MAC HMAC-SHA256 with independently derived keys and a random IV for each save. This protects the configuration from casual disclosure and detects ciphertext modification, but a user/root attacker who can read both `config.enc` and `master.key` can still decrypt it.

For externally managed key material, set a 64-hex-character key:

```sh
export VIBESOLARIS_MASTER_KEY=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
```

Check the active location:

```text
/globalconfig status
```

The optional `setup-system-config.sh` exists only for administrators who intentionally want an account to use `/etc/vibesolaris`; it is not required for normal per-user operation.

See `SECURITY.md`.

## OAuth

VibeSolaris contains a generic native-app OAuth 2.0 Authorisation Code + PKCE implementation with:

- S256 PKCE
- random state
- state validation
- loopback callback listener
- token exchange
- refresh-token handling
- logout

It does **not** rely on the Codex command/package and it does not collect a ChatGPT password or scrape browser cookies.

The OAuth settings are configurable because VibeSolaris must not invent private or undocumented provider credentials/endpoints. Configure Client ID, authorisation URL, token URL, scopes, and redirect URI only when legitimate application values are available.

TUI commands:

```text
/oauth status
/oauth client CLIENT_ID
/oauth authorize URL
/oauth token URL
/oauth scopes SCOPE_LIST
/oauth redirect http://127.0.0.1:14555/callback
/oauth save
/oauth login
/oauth logout
```

`/login` is an alias for `/oauth login`.

See `OAUTH.md`.

## Build/package output

`build.sh` writes output under `dist/`.

Linux examples:

```text
dist/vibesolaris-0.9.5-linux-x86_64.tar.gz
dist/vibesolaris-0.9.5-linux-sparc64.tar.gz
```

BSD and Darwin builds produce OS-labelled tar bundles, for example:

```text
dist/vibesolaris-0.9.5-freebsd-x86_64.tar.gz
dist/vibesolaris-0.9.5-darwin-arm64.tar.gz
dist/vibesolaris-0.9.5-darwin-ppc.tar.gz
```

On Solaris, `build.sh` uses SVR4 `pkgmk`/`pkgtrans` when available and otherwise creates a tar archive.

The installation tree uses `/usr/local/bin` for the binaries and `/usr/local/share/vibesolaris` for documentation/examples.

## Troubleshooting

### `curl/curl.h: No such file or directory`

The curl command may be installed while the development headers are not.

Fedora/RHEL-family:

```sh
sudo dnf install libcurl-devel
```

Debian/Ubuntu:

```sh
sudo apt-get install libcurl4-openssl-dev
```

### `X11/Xlib.h` missing

Install X11 development headers, or build TUI-only:

```sh
VS_NO_GUI=1 ./build.sh
```

### OpenSSL headers/libcrypto missing

Install the OpenSSL development package (`openssl-devel`, `libssl-dev`, or the platform equivalent).

### Solaris `make` behaves differently from GNU make

The supplied Makefile uses explicit object targets and avoids GNU-only `?=`/pattern-rule assumptions specifically for classic Solaris `make` compatibility. Prefer the bundled Makefile rather than replacing it with a GNU-specific one.

### Source files are reported as being “in the future”

That means the system clock and archive timestamps differ. It is normally harmless, but correcting VM/NTP time will remove the warning.

### Colours appear as escape sequences

Use:

```sh
./vibesolaris --no-colour
```

or:

```sh
NO_COLOR=1 ./vibesolaris
```

### Settings are not remembered

Run:

```text
/globalconfig status
```

For a normal user the path should usually be under `~/.vibesolaris`. Confirm that the home directory is writable and that the process has permission to create that directory.

### GUI clipboard paste does not work

VibeSolaris uses standard X11 `CLIPBOARD` and PRIMARY selections. On Wayland, run it through an XWayland-capable session. Ctrl/Meta+V uses CLIPBOARD; middle-click uses PRIMARY.

### Drag-and-drop does not work

The file manager must expose X11/XWayland XDND `text/uri-list`. You can always use the `+` attachment button or Ctrl/Meta+O instead.

## Security

VibeSolaris is deliberately capable of modifying your computer. It can run shell commands and write files when the selected model requests those tools.

Recommended practice:

- run it as an unprivileged user
- launch it inside the project directory you intend the agent to access
- inspect the activity trace
- avoid storing unrelated secrets in files accessible to the agent
- use narrowly scoped provider/MCP credentials where possible
- review `SECURITY.md`

The encrypted config protects data at rest from casual reading but does not protect it from root, the same logged-in user, process compromise, or malware with equivalent permissions.

## Licence

VibeSolaris is released under **The Unlicense**.

The project is dedicated to the public domain to the extent permitted by law. You may copy, modify, publish, use, compile, sell, or distribute it for any purpose, subject to the warranty disclaimer in `LICENSE`.

See the full text in [`LICENSE`](LICENSE).

### Shared last-used provider and model

The GUI and TUI use the **same encrypted configuration**. The active provider and the last model used for every provider are saved automatically. If you select `qwen` and `qwen3.8-max` in the GUI, close it, and then start the TUI, the TUI starts on that same provider/model. Changing it in the TUI works the same way in reverse.

The normal per-user location is `~/.vibesolaris/config.enc` when `/etc/vibesolaris` is not writable. Use `/globalconfig status` in the TUI to see the exact file in use. The active provider/model are restored **after** all per-provider settings are loaded, so older configuration-file ordering cannot reset the model to a built-in default.
