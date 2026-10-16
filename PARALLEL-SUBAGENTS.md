# Parallel delegated subagents

When a model response contains two or more `[[VS_TOOL subagent ...]]` directives and **every** directive in that batch is a subagent, VibeSolaris executes them concurrently using POSIX threads. Workers have isolated VSContext history, caches, and token accounting. Workers join before the parent receives their handoffs, which are returned in directive order. Lifecycle record allocation and forwarded trace events are protected with a mutex. Nested agent delegation remains supported, with the existing depth and per-turn limits.

Non-subagent and mixed batches retain sequential semantics, to avoid reordering command execution and filesystem writes. Parallel agents can still edit the same working tree and conflict with one another: use explicit non-overlapping file ownership in tasks or direct them to read-only review. This release does NOT implement filesystem locks, worktrees, merge conflict resolution, concurrency caps independent of total per-turn limits, or full cancellation of blocking provider calls.

Test coverage: `make test-subagent` verifies actual overlap of sibling workers, completion of both, lifecycle status, nested delegation, and cancellation. `make test` runs the standard regression suite. This work was built and tested on Linux, not yet Solaris or macOS.
