# Text provider

`text.elf` publishes `/@text`, acquiring `/@graphics`, private `/@memory`, and
console authorization through `/@io`. Graphics supplies font metrics and rasterizes
Unicode with Ubuntu Mono. The current 16-pixel face has 8×17 cells: the requested 1024×768 startup mode gives
128×45. No terminal font, framebuffer pointer or hardcoded grid is required.

```text
main graphics canvas (surface 0)
└── text viewport (clipping container, no bitmap allocation)
    ├── reusable row bitmap
    ├── reusable row bitmap
    └── … one bitmap per visible row
```

Each physical row owns a bitmap and retained character/foreground/background
cells. The logical rows form a ring. A newline at the bottom moves the child
surfaces up by one cell height, repositions the recycled top-row bitmap at the
bottom, and clears that bitmap only. It never fills the main canvas to scroll.
Graphics can translate the already composed opaque viewport using display.copy;
the underlying line bitmaps retain their pixels and generations.

A WRITE parses UTF-8/control characters in order, presenting at each newline
and automatic line wrap before processing the following line. Trailing text
without a newline is presented when the request ends. A newline that only moves
the logical cursor through unchanged rows needs no display transfer. Changed
cells within each line still use deferred glyph batches; scrolling reuses the
row bitmaps and clears only the recycled row. Explicit CLEAR resets all retained
rows and their bitmaps; it is separate from newline handling. This deliberately
preserves intermediate line-by-line screen states instead of coalescing a whole
stream chunk into its final viewport, accepting additional presentation work.

The graphics hierarchy clips descendants, checks ownership, and selects the
currently authorized drawing process's tree at presentation. The terminal's
cached view can therefore be hidden while a graphical app is active and restored
when text output resumes. This remains one shared console, without a desktop,
terminal escape sequences or a secure window manager.

WRITE handles UTF-8, newline, CR and backspace; malformed input becomes replacement
characters. Sequential writes wrap and destructively scroll. Positioned CELL
preserves its colors and leaves the sequential cursor unchanged. CURSOR validates
coordinates and resets incomplete UTF-8 input. Text never parses commands or
launches applications. Permission checks precede mutations as before.

Failed commits retain dirty cells and cached-row updates. An accepted positional
scroll is not replayed when repairing a later failure. Requested cells are
explicitly repainted even if another drawing client changed the main canvas.
Owned row surfaces are released on provider shutdown/owner exit.

Focused host tests execute this production model against retained mock bitmaps,
capturing each newline frame and asserting there are no main-canvas FILL/BLIT
calls during scrolling. Checks also cover trailing partial lines, CRLF, blank
lines, automatic wrap, split UTF-8 and failure before the next line is parsed. Native graphics
tests verify one display copy plus bottom-strip repair, unchanged retained bitmap
generations, clipping, cycle/foreign-owner rejection and parent-detachment safety.
