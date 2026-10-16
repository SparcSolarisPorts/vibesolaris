# SSH-safe bounded terminal composer

This revision replaces unbounded editor repaint with a fixed six-row (or smaller,
for short terminals) viewport. Text beyond the visible area remains in the input
buffer and cursor movement scrolls the viewport. Bracketed paste and the 256 KiB
input capacity are retained. No alternate screen, mouse tracking, or graphical
clipboard API is required for text entry over SSH.

Known limits: the slash suggestion popup is currently suppressed in the bounded
composer (Tab completion remains functional); terminal resize while an active
prompt is open is not yet handled; complex grapheme clusters/terminal double-width
Unicode cells can affect column counts. Clipboard images on remote hosts still
need a file attachment or image transfer workflow, because SSH cannot pass the
local graphical clipboard by itself. The source archive as supplied did not
contain the tests directory required by Makefile's test target.
