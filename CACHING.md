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

## 3. Persistent conversation history + ephemeral agent working context

Normal completed turns are retained as real user/assistant messages and remain bounded to 48 messages / approximately 192 KiB. Very large individual messages are locally compacted and the oldest user/assistant pair is evicted when necessary.

Autonomous tool work is different in 0.11.0. Intermediate tool cycles are kept in a separate ephemeral working history rather than being appended permanently. The recent working set is bounded to roughly 96 KiB, with individual intermediate messages bounded to 24 KiB.

Every eight autonomous rounds by default, or earlier when the working set reaches about 75% of its budget, VibeSolaris issues an internal compaction request to the selected model. The result is a dense working-state checkpoint capped at 24 KiB. The raw intermediate exchanges are then dropped. The checkpoint is carried forward with the active task and latest tool result so the model retains important files, errors, decisions, test results and remaining work without repeatedly receiving every old compiler log and protocol reminder.

Use `VIBESOLARIS_AGENT_COMPACT_ROUNDS=N` to set an interval from 4 through 64. Set it to `0` to disable semantic checkpoints. A checkpoint costs one additional provider request, but long agent runs avoid the much larger cumulative cost of repeatedly resending a continually growing transcript.

At completion, only the original user request and final answer are committed to persistent conversation history. This also improves future provider prompt-cache locality because old autonomous scratch work is not carried into unrelated later turns.

Attachments are similarly one-shot during an autonomous turn: the first model round receives the user's attachments, and later rounds receive only newly loaded images. Exact file contents can be requested again with the native `read` tool when needed.

Use:

    /history status
    /history clear

The GUI displays cache state and the most recent provider cached-token count in the sidebar. Ctrl/Meta+T toggles caching and Ctrl/Meta+Y clears conversation history.

## Important distinction

The local file cache saves disk I/O and repeated base64 work. It does **not** by itself reduce provider token billing. Provider-side prompt/context caching can reduce cached-input cost and latency, while autonomous working-context compaction and one-shot attachments reduce how much content needs to be sent at all.
