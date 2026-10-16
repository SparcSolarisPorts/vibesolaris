# Provider, protocol, URL, and authentication configuration

VibeSolaris 0.9.5 makes connection/authentication controls depend on the selected provider and protocol. API keys and protocol choices are remembered separately per provider. Providers that support both OpenAI-compatible and Anthropic Messages protocols normally retain a different URL for each protocol; the Custom provider intentionally shares one URL across both modes.

## OpenAI

Provider: `openai`  
Protocol: OpenAI-compatible  
Default URL: `https://api.openai.com/v1/chat/completions`

Authentication options:

1. **OpenAI API key** — bearer key entered through the GUI or `/key`.
2. **OAuth bearer** — obtained through the configurable native-client PKCE flow if official OAuth application details are issued for VibeSolaris.

The API URL is editable. OAuth is shown only for the OpenAI provider; it is not presented as an authentication option for unrelated providers.

If both credentials exist, a valid OAuth bearer is preferred for OpenAI. If an expired OAuth token cannot be refreshed and an API key is available, VibeSolaris falls back to the API key. See `OAUTH.md`.

## Claude

Provider: `claude`  
Protocol: Anthropic Messages  
Default URL: `https://api.anthropic.com/v1/messages`  
Authentication: Anthropic API key (`x-api-key`), or an Anthropic-issued OAuth session (`Authorization: Bearer`) when one is configured

The API URL is editable, so a compatible proxy/gateway or alternate officially supported Anthropic endpoint can be used without recompiling.

**Claude OAuth** is optional and separate from the OpenAI OAuth slot. It works only with a client ID that Anthropic issues to VibeSolaris, and only towards `https://api.anthropic.com`. Claude.ai and Claude Code subscription logins are not used. See `OAUTH.md` for setup and the compliance rules. If an OAuth session is not available, the API key path is used unchanged.

## Qwen / Alibaba Cloud Model Studio

Provider: `qwen`  
Protocols: **OpenAI-compatible or Anthropic Messages-compatible**  
Authentication: API key / Coding Plan / Token Plan credential appropriate to the selected URL

Coding Plan presets:

- OpenAI-compatible: `https://coding.dashscope.aliyuncs.com/v1`
- Anthropic-compatible: `https://coding.dashscope.aliyuncs.com/apps/anthropic`

Alibaba also publishes plan/region-specific endpoints. For example, Token Plan uses its own OpenAI- and Anthropic-compatible base URLs. The API key and base URL must belong to the same plan/region; do not mix Coding Plan, Token Plan, and pay-as-you-go credentials.

VibeSolaris remembers the Qwen OpenAI URL and Qwen Anthropic URL independently. Changing **Protocol** swaps to the corresponding saved URL. `/protocol openai` and `/protocol anthropic` provide the same control in the TUI.

## GLM and GLM Coding Plan

Providers: `glm`, `glm-coding`  
Protocols: **OpenAI-compatible or Anthropic Messages-compatible**  
Authentication: API key / Coding Plan credential

Presets:

- GLM API OpenAI-compatible: `https://open.bigmodel.cn/api/paas/v4/chat/completions`
- GLM Coding Plan OpenAI-compatible: `https://open.bigmodel.cn/api/coding/paas/v4`
- GLM Anthropic-compatible: `https://open.bigmodel.cn/api/anthropic`

Zhipu documents GLM use with Claude Code through `ANTHROPIC_BASE_URL`/`ANTHROPIC_AUTH_TOKEN`, so VibeSolaris exposes the Anthropic Messages protocol for GLM as well as OpenAI compatibility. As with Qwen, the two URLs are stored separately.

## Kimi

Provider: `kimi`  
Protocol: OpenAI-compatible  
Authentication: API key  
API URL: editable

## ERNIE / Qianfan

Provider: `ernie`  
Protocol: OpenAI-compatible preset  
Authentication: API key  
API URL: editable

## Gemini

Provider: `gemini`  
Protocol: Google `generateContent`  
Authentication: Gemini API key  
API URL: editable

## Custom

Provider: `custom`  
Protocols: OpenAI-compatible or Anthropic Messages-compatible  
Authentication: one shared API key for both protocols
URL: one shared custom base URL for both protocols

Use the GUI's Protocol/API URL/Model/API Key controls, or:

```text
/provider custom
/protocol openai|anthropic
/base URL
/model MODEL
/key KEY
```

For Anthropic-compatible non-Anthropic services, VibeSolaris sends a bearer credential by default plus the Anthropic protocol version header. Select `/anthropic-auth x-api-key` to use the `x-api-key` header instead, or `/anthropic-auth bearer` to restore bearer auth. The choice is saved per provider in encrypted configuration. Native Claude API keys always use `x-api-key`; Claude OAuth remains restricted to `https://api.anthropic.com` and is never sent to custom gateways. Custom URL and key settings are shared across OpenAI-compatible and Anthropic-compatible modes; use a gateway base URL that supports both request formats.

## URL handling

The GUI accepts either a protocol **base URL** or a full request URL:

- OpenAI-compatible base URLs have `/chat/completions` appended when it is not already present.
- Anthropic-compatible base URLs have `/v1/messages` appended when it is not already present.
- For Custom, a shared base ending in `/v1` maps to `/v1/chat/completions` for OpenAI-compatible requests and `/v1/messages` for Anthropic-compatible requests; this avoids a duplicated `/v1`.
- For Custom full endpoints, switching protocols replaces a terminal `/chat/completions` with `/v1/messages` (or the reverse), so a shared full endpoint does not receive both suffixes.
- Gemini uses its native model URL construction.

This makes URLs copied from provider setup pages usable without manually adding the final request path.

## Response streaming

OpenAI-compatible and Anthropic Messages-compatible requests use `stream: true` and consume Server-Sent Events incrementally. The GUI renders text fragments as they arrive; the TUI captures provider fragments internally while the spinner is active and renders exactly one complete authoritative answer after the agent loop, so tool, plan, question, and final markers cannot leak or duplicate. Gemini remains buffered because its `streamGenerateContent` framing is different. A gateway that does not provide SSE is retried through the existing buffered JSON path.

## API-key persistence

Provider keys, per-provider model IDs, protocol choices, and per-protocol URLs are autosaved into the active encrypted configuration store whenever settings change. VibeSolaris uses `/etc/vibesolaris` only when the current account can write it; otherwise it falls back to `~/.vibesolaris`, so normal users receive separate encrypted settings without root.

For example, Qwen can retain values conceptually equivalent to:

```text
protocol_qwen=anthropic
openai_url_qwen=https://coding.dashscope.aliyuncs.com/v1
anthropic_url_qwen=https://coding.dashscope.aliyuncs.com/apps/anthropic
```

The legacy `/saveconfig PATH` command still writes an explicit plain-text configuration file and should be used only when that is intentionally desired. See `SECURITY.md` for encrypted-storage details.

## Cache behaviour

VibeSolaris uses provider-specific cache controls only where the selected adapter supports them. See `CACHING.md`.
