# Graphics provider

`graphics.elf` publishes `/@graphics` and acquires `/@display`, private `/@memory`,
and a narrowly bound `/@io` owner-exit notification operation. Rendering is a
software backend, separate from display hardware. The wire contract is
`interfaces/graphics.h`; the independently host-tested raster core is `raster.c`.

| Operations | Behavior |
| --- | --- |
| info | Display dimensions, live off-screen surface count and pixel storage bytes |
| fill / glyph | Opaque RGB screen fill and built-in 8×16 character-cell rendering; immediate presentation |
| glyphs | Nonempty positioned glyph array; validate the whole batch, draw in order, present once |
| surface_create / surface_release / surface_info | Dynamically allocated off-screen ARGB surfaces, owner-local IDs, dimensions and modification state |
| state | Signed 16.16 affine transform, device-space rectangular clip, source replacement or source-over composition |
| line / rectangle / ellipse / arc | Clipped lines and outlined/filled shapes; circles are equal-axis ellipses, arcs use degrees 16.16 |
| bezier / path | Cubic curves; move/line/cubic/close paths with multiple contours and even-odd filling |
| bitmap / blit | Raw straight-alpha ARGB bitmaps, surface regions, global opacity, transformed nearest-neighbor rendering and overlap-safe self-blits |
| present | Flush dirty regions of surface 0 through bounded display blits |

Surface 0 is the shared display-sized canvas. Other IDs are usable only by their
creator, authenticated through broker admission context. Creating a surface also
registers a preallocated OS owner-exit notification: explicit release or owner
exit frees its pixels and descriptor. The provider releases all surfaces on its
own cooperative shutdown. A surface cannot expose or free another owner's pixels.
These are opaque object IDs within a granted graphics capability, not pointers.

Every actual pixel change expands a dirty bounding rectangle and advances a
saturating 64-bit generation. Identical writes leave both unchanged. State changes
alone do not modify pixels. `surface_info` observes damage without consuming it.
Presentation batches as many dirty rows as fit each bounded region blit, so small
glyph damage fits one transfer. `present` clears damage only after every display chunk succeeds; failed transfers
retain the whole region for retry. Off-screen damage is not cleared by compositing
it elsewhere. This first implementation coalesces damage into one rectangle.

Presentation flattens canvas alpha over black into canonical opaque RGB.
New drawing operations update memory; compose/blit into surface 0 and present
explicitly. The legacy cell/fill operations present immediately for text. Pixel
coordinates/dimensions are limited to signed 16-bit raster bounds; affine linear
coefficients are bounded to ±16 in 16.16 to keep integer arithmetic checked.
Wire payloads remain bounded; large bitmaps can be uploaded in tiles. All drawing
is preemptible userland work. The initial bitmap font covers printable ASCII;
other Unicode codepoints render a replacement glyph. Font interpretation lives
here, not in the terminal. Antialiasing, image-file decoders, accelerated graphics,
window management and a native GPU driver are outside this milestone.

Identity-transform, opaque SOURCE self-blits copy in place in overlap-safe order,
preserving clipping and damage tracking. Scrolling therefore does not allocate,
map, unmap and free a full-screen snapshot on every line. Alpha composition and
transformed copies retain the general snapshot path.


Validation: host UBSan tests cover clipping/guards, affine bitmaps, alpha and
compound path holes, as well as unchanged-pixel damage. Isolated one/four-CPU TCG
console tests exercise the public operations, foreign-owner denial, owner-exit
reclamation and visible dirty-region presentation. Tests add graphics-check only
to their copied fixture image; the normal production catalog has no test app.
Native KVM console acceptance remains separate from earlier native handle results.

Solid-color presentation checks the final RGB pixels in the damaged region and
uses the existing display `fill` operation when they are uniform. This avoids
chunking a full-screen clear through blit RPCs. Mixed regions still use blits;
backing pixels, clipping/composition and damage-on-failure semantics are preserved.

Rectangle fills with an identity transform use clipped row loops rather than
polygon edge testing. Self-blits also use clipped, overlap-safe row loops; damage
is aggregated per changed row and generation still counts changed pixels exactly.
An opaque legacy fill whose damage is known to be covered by its rectangle/clip
issues display.fill directly, without rescanning all pixels for uniformity.

A clean screen self-blit can queue one coordinate-only display.copy for the next
presentation. It preserves deferred BLIT semantics. Subsequent changes, such as
the bottom strip exposed by text scrolling, retain their own damage. PRESENT
applies the queued copy first and sends only that later damage. SURFACE_INFO
includes deferred-copy damage. A covering clear supersedes the queued copy;
unsupported copies fall back to pixels. Failed copies are never retried as copies:
the destination becomes pending pixel damage, preserving authoritative backing
pixels even if a backend partially performed an overlapping copy. Transforms,
alpha composition and multiple deferred changes retain the general fallback.
