# Tool inspection and streaming usage fix

- Inspections now get up to two explicit read-only tool continuation attempts when a model responds without using local tools. The system prompt reinforces that the local C host exposes VS_TOOL read/list/search even in Plan mode.
- Anthropic streaming usage is assembled from SSE message_start and message_delta and counted once per successful streamed response. Cache read/write input contributes to overall input counts as in non-streaming requests.
- This change does not implement native Anthropic tool_use; VibeSolaris uses text directives.
- Compilation validated on Linux; online provider and macOS SSH runtime behavior require testing with actual API responses.
