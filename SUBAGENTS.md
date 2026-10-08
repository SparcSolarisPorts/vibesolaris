# Subagents

A subagent is a focused, isolated agent run started by the main agent (or by you) to handle one self-contained task. It uses the same provider, the same tools, and the same autonomous loop as the main agent, but with its own working memory, file cache, and activity trace. When it finishes, its report comes back to the parent as a tool result.

## Using subagents

- **Let the agent delegate.** The agent can emit a directive such as `[[VS_TOOL subagent task="Summarise the build system in src/"]]`. The task text must be complete, because the subagent cannot see the conversation.
- **Delegate explicitly.** In the TUI or GUI, type `/subagent TASK`. The agent is asked to hand that task to a subagent and report the result.

## Limits

- Nesting: the main agent can start a subagent, and that subagent can start one more level. Deeper requests are refused with an explanatory result.
- Count: at most 6 subagents per parent turn.
- Output: a subagent's report is bounded to 48 KB before it returns to the parent.
- Cancellation: stopping the parent (Ctrl-C in the TUI, Stop in the GUI) also stops every running subagent.

## What is isolated

A subagent gets its own conversation history, working memory, file cache, attachments, and activity trace. It shares the provider and API settings. It never owns the parent's MCP server processes and never writes the encrypted configuration, so it cannot change saved settings or tokens.

## Seeing what subagents are doing

Subagent activity appears in the activity trace with a `[sub#N dK]` prefix, where `N` is the subagent number in this turn and `K` is its depth. The live indicator shows the current phase, for example `Subagent: Thinking` or `Subagent: Running tools`, with elapsed seconds.

## Testing

`make test-subagent` runs the real delegation loop against a scripted provider. It checks the round trip, the nesting and count limits, cancellation inheritance, trace forwarding, and the indicator labels.
