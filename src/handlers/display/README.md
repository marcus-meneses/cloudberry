# Display provider

`display.elf` publishes `/@display` in privileged userland. Its stable C contract
is `interfaces/display.h`: actual pixel dimensions, opaque RGB rectangle fill,
and bounded region blits with canonical `0x00RRGGBB` pixels. Rectangles and exact
payload lengths are checked before drawing. No physical address, pitch or
framebuffer pointer is returned to an application.

The first backend uses Limine's mapped framebuffer. Kernel bootstrap passes its
metadata to the I/O mechanism after splash validation; only the installation-bound
display provider can invoke framebuffer operations through `/@io`. Copying pixels
and converting channel masks are kernel I/O mechanisms, not graphics policy.
A future native display driver can replace this provider/backend while retaining
`/@display`; text and graphics do not depend on Limine or hardware pixel layout.
Splash remains the temporary pre-flight renderer, before these providers start.

The initial four-CPU isolated console acceptance passed, including visible output
and real xHCI keyboard input. Native GPU drivers and accelerated presentation
remain deferred. This is one boot display, without window/session policy.

The optional appended `copy` operation takes cb_display_copy: destination x/y,
source x/y and width/height. Both rectangles must fit the display. It copies raw
backend pixel representations in overlap-safe order; no framebuffer pointer is
exposed. Its privileged IO counterpart `display_copy` is appended without
renumbering existing operations and remains installation-bound to display.
The Limine backend respects framebuffer pitch and channel layout. A replacement
backend may return UNAVAILABLE, allowing graphics to present backing pixels.

The initial framebuffer backend keeps a lazy private RAM shadow in native pixel
format. Every successful fill/blit updates it; region copies read RAM, update
overlap-safe rows and write only changed row spans to framebuffer memory. A
full initial paint seeds it without framebuffer reads. If copying is requested
before such a paint, one readback initializes it. The shadow is retained for the
resident I/O backend's lifetime, uses mem.pages_alloc through the hub and costs
width×height×4 bytes rounded to pages (3.90625 MiB at 1280×800). Failure to allocate
it leaves fills/blits working and reports copy unavailable for graphics fallback.

Screen access now follows kernel-owned keyboard focus and session membership.
The frontend checks the authenticated caller before mutating screen state;
background sessions cannot bypass this by acquiring a lower rendering layer.
See [session console policy](../sessions/README.md#foreground-console-boundary).
Already authorized operations may finish during a focus transition. This remains
one console, without a secure compositor or secure-attention authentication UI.
