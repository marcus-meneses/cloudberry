# Software graphics provider (graphics-vga)

`graphics-vga.elf` publishes `/@graphics` and acquires `/@display`, private `/@memory`,
and narrowly bound `/@io` owner-exit and console-authorization operations. Rendering is a
software backend, separate from display hardware. The wire contract is
`interfaces/graphics.h`; the independently host-tested raster core is `raster.c`.

| Operations | Behavior |
| --- | --- |
| info | Display dimensions, live off-screen surface count and pixel storage bytes |
| fill / glyph | Opaque RGB screen fill and default TrueType character-cell rendering; immediate presentation |
| glyphs | Nonempty positioned glyph array; validate the whole batch, draw in order, present once |
| font_info / text_batch | Report measured face metrics; render positioned glyphs at 4–128 pixels with optional deferred presentation |
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
it elsewhere. Deferred identity-transform text batches retain up to 64 separate regions; overflow and general drawing fall back to the authoritative bounding rectangle. Distant caret/status changes therefore do not resend the intervening screen.

Presentation flattens canvas alpha over black into canonical opaque RGB.
New drawing operations update memory; compose/blit into surface 0 and present
explicitly. The legacy cell/fill operations present immediately for text. Pixel
coordinates/dimensions are limited to signed 16-bit raster bounds; affine linear
coefficients are bounded to ±16 in 16.16 to keep integer arithmetic checked.
Wire payloads remain bounded; large bitmaps can be uploaded in tiles. All drawing
is preemptible userland work. FreeType 2.15.0 rasterizes the bundled Ubuntu Mono TrueType face with grayscale
antialiasing, Unicode lookup and an evicting 4 MiB glyph-coverage cache. The default
16-pixel face reports 8×17 cells, rather than a hardcoded text geometry. Font
interpretation lives here, not in the terminal. Complex shaping, image-file decoders, accelerated graphics,
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
unsupported copies fall back to pixels. Visible generic overlays are repaired at their live and translated source positions after a copy, preserving the accelerated scroll path without cursor ghosts. Failed copies are never retried as copies:
the destination becomes pending pixel damage, preserving authoritative backing
pixels even if a backend partially performed an overlapping copy. Transforms,
alpha composition and multiple deferred changes retain the general fallback.

Screen access now follows kernel-owned keyboard focus and session membership.
The frontend checks the authenticated caller before mutating screen state;
background sessions cannot bypass this by acquiring a lower rendering layer.
See [session console policy](../sessions/README.md#foreground-console-boundary).
Already authorized operations may finish during a focus transition. This remains
one console, without a secure compositor or secure-attention authentication UI.

Screen pixels and damage remain shared, but transform/clip/composition state is
private to the authenticated drawing process. Switching callers restores that
caller's state (identity/full clip/SOURCE by default), including the text renderer.
Contexts register owner-exit cleanup and do not leak state into the next app.

The additional `/@graphics-overlay` handle is reserved for trusted boot providers;
ordinary installations cannot acquire or publish it. It offers generic ARGB layer
set/move/hide operations and display dimensions. The mouse provider owns pointer
shape/position policy. Layers composite over current backing pixels, never save
and restore stale backgrounds, and repair only their old/new regions. Owner exit
removes its layer. Cursor updates do not force an application to present its frame.
Display remains a generic fill/blit/copy backend with no cursor-specific operation.

TrueType direct rendering advances the surface generation by actual changed
pixels (saturating), just like other raster paths. Repeated identical glyphs
leave it unchanged; deferred text batches can therefore retain sparse damage.

## Retained surface hierarchy

CREATE also accepts `cb_graphics_create_ex`. `CB_SURFACE_CONTAINER` allocates only
geometry/ownership metadata; a normal surface allocates its ARGB bitmap. Containers
cannot receive pixel drawing operations. INFO includes composed-frame/work buffers
in pixel storage totals; containers add no pixel storage.

`surface_place` takes an array of `{surface,parent,x,y,flags,reserved}` records.
Parent 0 is the main canvas; every nonzero parent/child belongs to the authenticated
caller. Positions are parent-relative and descendants are clipped to all ancestor
bounds. Visible siblings compose in attachment order. Moves within a parent retain
that order. Complete transactions are validated for duplicate children, ownership,
coordinate bounds and cycles before mutation; changed edges detach first so valid
batch reparentings cannot create transient cycles. Traversal is iterative.

`surface_scroll` moves a parent's direct children without changing their bitmap
pixels. Metadata changes are deferred until PRESENT. A completely covered opaque
container with nonoverlapping leaf children and no intersecting unrelated surfaces
can use display.copy and a RAM frame-cache translation. Exposed strips and changed
nodes are then composed. Overlaps, transparency, incomplete coverage and complex
nesting use the ordinary software composition path. Cursor ghosts/live pointer
areas are repaired through the independent overlay plane. Failed copies retain
repair damage and are never replayed as non-idempotent copies.

The authorized main-canvas presenter selects its own visible root tree; another
app's tree is hidden rather than left over the new app. Direct canvas drawing is
the background beneath that caller's attached surfaces. Drawing transforms remain
local raster state; placement adds translation and ancestor rectangle clipping.
BLIT copies a surface's bitmap pixels, not an implicit flattened descendant tree.

The provider retains the last composed image plus a reusable work buffer. Changed
pixel bounds are compared before display transfer. A single-cell edit does not
upload the entire cached line; unchanged cached lines need no rasterization.
The public bitmap damage observation remains intact, separately from per-operation
composition damage. Main-canvas SURFACE_INFO includes pending hierarchy damage and
its saturating layout generation. Presentation failures force conservative repair
on retry, including pixels already updated in the RAM cache. Mouse-only updates
use the last presented composition and do not flush an app's pending layout.

Releasing a parent detaches its immediate children without destroying them. Their
IDs remain owner-local and can be reattached; unattached trees are invisible. Each
surface keeps its owner-exit watch, and process exit reclaims all owned bitmaps.
This is a generic software surface compositor, not a desktop/window manager or a
native GPU implementation. Repositioning saves rasterization; the framebuffer
backend still has to update changed pixels. Complete new pages also pay per-surface
update overhead, so retained hierarchies do not guarantee faster output in every case.

Surface placement/reparenting, positional scrolling and content updates use
pixel-difference checking before display transfer. Moving cached surfaces still
avoids glyph rasterization; unchanged composed pixels need no transfer. No-op
placements remain no-ops. The opaque viewport copy fast path still applies,
including clipping, ownership and coverage validation. Failed transfers retain
forced redraws on retry: the RAM cache may already contain pixels that never
reached the display. The earlier layout comparison bypass has been removed.


This implementation is installed as `/system/handlers/graphics-vga`, publishing
`/@graphics` as before. Its `vga` suffix denotes the software/framebuffer pair;
it is not a legacy VGA-register driver. Startup selects it with `display-vga`
when no qualified accelerated pair is available. The [OpenGL definition](../graphics-opengl/README.md)
is staged separately and currently excluded from the installed catalog.


The public decoder/receive loop now lives in `../graphics-common/provider.inc`.
`graphics-vga.c` is its software-backend entry wrapper. Existing raster/font,
scene/damage and overlay helpers stay here as shared source libraries; the
experimental OpenGL provider reuses the public semantics with GPU presentation.
The software pixel-comparison, copy-repair and newline presentation behavior is
unchanged and remains the normal boot path.
