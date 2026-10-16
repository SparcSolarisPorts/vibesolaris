# Multiline soft-wrap redraw correction

The previous editor repainted ordinary input using a one-line `CR + erase line`
sequence, even after the input wrapped over many terminal rows. This left
stale prompts and duplicated text when editing long lines.

This update:
- Routes every edit, including ordinary character insertion, through one
  width-aware multiline renderer.
- Restores the cursor according to the cursor's wrapped row rather than
  the text's final row.
- Renders explicit newlines as CRLF in terminal raw mode.
- Keeps slash-command suggestions limited to the first unwrapped row.
- Redraws the complete input at its end before submitting the line.

Linux build and existing `make test` checks passed. This does **not** yet
provide a scrollable input viewport for text taller than the terminal or
runtime coverage on macOS/Solaris. Long input exceeding viewport height
requires an additional viewport/compositor implementation.
