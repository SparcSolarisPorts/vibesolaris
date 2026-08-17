# VibeSolaris caching

VibeSolaris uses three independent caching layers.

## 1. Provider prompt/token caching

Caching is enabled by default.

- **OpenAI:** requests use a stable `prompt_cache_key`. GPT-5.6 and later also receive an explicit cache breakpoint after the stable system/`AGENT.MD` prefix plus the normal moving implicit breakpoint for growing conversations. Earlier OpenAI models still use OpenAI's automatic prefix caching.
- **Claude:** requests enable Anthropic automatic prompt caching with `cache_control: {"type":"ephemeral"}` and also mark the stable system prefix. The default provider cache lifetime is 5 minutes.
- **Gemini:** VibeSolaris preserves a stable, append-only prompt prefix so Gemini's implicit context caching can hit when the selected model/account supports it.
- **GLM, GLM Coding Plan, Kimi, Qwen, ERNIE and custom OpenAI-compatible endpoints:** VibeSolaris preserves the same stable-prefix layout but does not send vendor-specific cache fields unless their public API contract is known to accept them. This avoids breaking compatibility.

The TUI command `/cache status` shows the last provider-reported cache read/write token counts when the provider returns such fields.

## 2. Local attachment and file cache

Text files and base64-encoded image attachments are cached in memory. Entries are keyed by path plus file size and modification time and retain a lightweight content hash. Repeated reads return a copy of the cached data instead of rereading/re-encoding the file.

Agent writes invalidate the affected path immediately. The cache holds up to 24 entries and evicts older slots when full.

Use:

    /cache status
    /cache clear
    /cache off
    /cache on

## 3. Append-only conversation context with compaction

Conversation and tool-call history is retained as real user/assistant messages rather than rebuilding one giant changing prompt. This is important because provider KV caches rely on exact prompt-prefix matches.

Each tool round appends the prior user request and assistant tool directive before sending the tool result. The next provider request can therefore reuse the exact previous prefix.

History is bounded to 48 messages and approximately 192 KiB. Very large individual messages are locally compacted to their beginning and end. When the total budget is exceeded, the oldest user/assistant pair is evicted. This bounds token growth without continually rewriting the recent prefix.

Use:

    /history status
    /history clear

The GUI displays cache state and the most recent provider cached-token count in the sidebar. Ctrl/Meta+T toggles caching and Ctrl/Meta+Y clears conversation history.

## Important distinction

The local file cache saves disk I/O and repeated base64 work. It does **not** by itself reduce provider token billing. Provider-side prompt/context caching is what can reduce cached-input cost and latency. History compaction limits how much context is sent at all.
