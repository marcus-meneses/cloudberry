# Mouse handler

`mouse` is a privileged userland provider publishing `/@mouse`. Its C ABI is
`interfaces/mouse.h`. The build-owned catalog and boot provider list install/start
it alongside keyboard. There is no QEMU/physical-device decoding branch.

The handler discovers available USB HID interfaces through narrowly authorized
`/@io` operations, obtains each report descriptor, and keeps interfaces whose
Mouse application collections contain supported axes. Unsupported interfaces are
released. Existing claims are respected; unrelated interfaces on composite devices
remain available to their owners. Each accepted interface attachment has an
independent decoder, button state and device ID. Reconnection creates a new ID.

The descriptor decoder supports:

- Relative X/Y counts with packed, signed or unsigned fields up to 32 bits;
  absolute X/Y normalized to 0..65535, distinguished by event flags.
- Buttons 1..64, including variable bits and selector arrays; independent report
  IDs preserve button state belonging to other reports.
- Relative vertical Wheel and Consumer AC Pan horizontal scrolling.
- Standard Resolution Multiplier features: read the existing feature report,
  preserve unrelated bits, request its maximum multiplier, and apply it only when
  the write succeeds. Default/current feature values are retained on write failure.
  Wheel deltas divided by their resolution are detents; clients retain fractions.
- Report IDs, global PUSH/POP, usage lists/ranges and nested collections. Metadata
  designator/string items do not affect decoding. Fields outside Mouse collections
  are ignored while their report offsets are retained.

READ takes a versioned epoch/sequence cursor and returns copied event batches.
It blocks until events arrive or the caller's deadline expires; submitted reads
work with system wait-any without polling. Each reader has its own cursor and
only receives events at/after its authenticated grant time. History is dynamically
allocated and retained for five seconds, with explicit resynchronization after
expiry, overflow or memory-pressure loss. STATE is index-paginated, so the reply
frame does not impose a device-count limit. It reports connected interface
identities, buttons and positions. Relative positions are cumulative device
counts, not display pixels. The handler currently provides input, not a drawn
pointer acceleration or application click policy. Cursor policy now lives here: a black-outlined white arrow uses the trusted graphics overlay handle, which composites through display.

ADDED/REMOVED events identify attachment changes. Removal reports released buttons
before freeing decoder/class state. Malformed reports leave decoder state unchanged
and generate an error event. Stop closes every subscription/connection, cancels
owned receives, joins workers and releases class buffers. Rejected descriptor
records are pruned after topology changes rather than accumulating across unplug.

The independent HID kmodule's generic transport lives in `kmodules/shared/hid-reports.c`, not in this
handler. It owns USB claims, copied descriptors/features/reports, interrupt-IN
workers and acknowledged mailboxes. USB uses independent endpoint transfer state
and permanent bounce buffers, allowing sibling interfaces and EP0 to operate while
another interrupt-IN endpoint is idle. Disconnect/shutdown waits for all active
endpoints; uncertain DMA quiescence retains backing instead of freeing it.

Bounds are explicit: descriptors/reports/features fit the existing 4096-byte USB
transport limit; supported field widths are at most 32 bits and the public button
mask has 64 bits. Long/reserved HID items and alternative delimiter usage sets
are rejected safely. Vendor-specific commands, macros/RGB/firmware and Bluetooth
transport are outside this handler. Combined keyboard/mouse collections share one kernel HID capture source.
Report IDs and collection membership route copied reports, including fanout when
one report contains both classes. Class consumers acknowledge independently;
closing one leaves the other attached. `common/hid-report.c` is the descriptor
walker shared by kernel routing and both class decoders.

Host tests cover descriptor variants, feature preservation, malformed/OOM cases,
HID I/O ownership/lifecycle, and overlapping endpoint/control transfers. The
focused guest client is fixture-only, never a default installed application.
Physical-device/controller acceptance remains separate from emulated guest tests.
The decoder follows the [HID class specification](https://www.usb.org/hid) and
[HID Usage Tables](https://usb.org/document-library/hid-usage-tables-17).


Mouse consumers remain independent within the foreground login session. The
handler queries a copied keyboard-focus/session snapshot through restricted IO;
other sessions receive DENIED, closed/reaped callers REVOKED. Focus transitions
advance a replay floor; clients resynchronize instead of replaying events from a
previous session. No keyboard report or terminal event forwarding is introduced.

The appended `cursor_info` and `read_cursor` operations expose clamped pixel
coordinates and display bounds. `read_cursor` uses version 1.1 extended event
records with the cursor snapshot associated with each event; legacy `read`
records remain unchanged. Relative and absolute HID mice share this policy.
The pointer continues moving independently of an application's JS rendering loop.

`cursor_hide` and `cursor_show` are empty, synchronous operations. Hide is
idempotent and restricted to the exact keyboard-focused process, not just another
reader in the same session. Its owning process may show again even after losing
focus; another process cannot release its hide. Movement keeps updating coordinates
without rendering while hidden. Show reuses the existing bitmap and latest
coordinates. The mouse provider uses the existing IO owner-exit watch to restore
the arrow after a caller dies, and detects focus loss on device activity. Mouse
is added to the bound-provider IO watch allowlist; arbitrary IO callers remain
denied. Closing a JS mouse object restores any hide it owns.
