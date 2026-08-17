# Provider, protocol, URL, and authentication configuration

VibeSolaris 0.9.5 makes connection/authentication controls depend on the selected provider and protocol. API keys, protocol choice, and API URLs are remembered separately per provider; providers that support both OpenAI-compatible and Anthropic Messages protocols also retain a different URL for each protocol.

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
Authentication: Anthropic API key (`x-api-key`)

The API URL is editable, so a compatible proxy/gateway or alternate officially supported Anthropic endpoint can be used without recompiling.

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
Authentication: API key  

Use the GUI's Protocol/API URL/Model/API Key controls, or:

```text
/provider custom
/protocol openai|anthropic
/base URL
/model MODEL
/key KEY
```

For Anthropic-compatible non-Anthropic services, VibeSolaris sends a bearer credential plus the Anthropic protocol version header. Native Claude uses `x-api-key`.

## URL handling

The GUI accepts either a protocol **base URL** or a full request URL:

- OpenAI-compatible base URLs have `/chat/completions` appended when it is not already present.
- Anthropic-compatible base URLs have `/v1/messages` appended when it is not already present.
- Gemini uses its native model URL construction.

This makes URLs copied from provider setup pages usable without manually adding the final request path.

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
