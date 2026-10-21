# Text provider

`text.elf` publishes `/@text`, using `/@graphics`, which uses `/@display`.
It derives columns and rows from actual pixel dimensions with 8×16 cells:
1280×800 yields 160×50. No fixed terminal column count is required.

Operations report dimensions/cursor, clear, write a UTF-8 byte sequence, or draw
an absolute positioned cell, or set the sequential cursor within the grid. UTF-8 decoding and control characters (newline, CR,
backspace) belong here. Invalid sequences become replacement characters. The
terminal encodes keyboard text events as UTF-8 and does not interpret a codepage.
The graphics font currently renders printable ASCII and substitutes other glyphs.

Sequential output wraps and destructively scrolls; scrolling uses an overlap-safe
graphics blit and clears the bottom row. Positioned cells leave the sequential
cursor unchanged. The retained cell grid is allocated through private memory.
This is one shared boot console, without terminal escape sequences, independent
windows or user sessions. Text services do not launch apps or parse commands.

Writes collect at most 256 positioned glyphs per graphics request and flush at
request completion, batch capacity, or before scrolling. Cell operations still
complete presentation before replying. Graphics owns the font and validates a
whole glyph batch before drawing/presenting once. This removes a synchronous
graphics/display round trip per character within a multi-character text write.


Isolated one/four-CPU console acceptance uses this provider for keyboard echo,
stdout/stderr, cwd prompts and error output at the actual 160×50 boot grid.

The appended `cursor` operation takes `cb_text_cursor {column,row}`. It validates
both coordinates before changing state, resets any incomplete UTF-8 sequence and
does not draw or scroll. Existing operation numbers and CELL semantics are
unchanged. This primitive supports application-owned editing; text contains no
command history, filesystem completion or terminal key bindings.
