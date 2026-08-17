# VibeSolaris agent instructions

- This repository must remain buildable on Solaris and illumos.
- Prefer C89/C90-compatible code where practical.
- Do not assume GNU-only userland commands unless you check for them.
- Detect OS and architecture before suggesting system-specific commands.
- Before editing a file, read it first.
- Keep destructive shell commands explicit and narrow.
