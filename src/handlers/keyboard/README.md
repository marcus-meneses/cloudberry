# Keyboard handler

`keyboard.elf` is the first privileged isolated resource provider. Bootstrap loads
its static native executable, JSON manifest and declared startup files from the unique
configured ext4 driver volume. It publishes **`/@keyboard`**, keyboard interface
1.0, defined in [keyboard.h](../../interfaces/keyboard.h). Foundational kmodules
remain Limine-loaded; the provider runs in ring 3 with its own private pages.
It acquires `/@memory` and `/@io` through the C system library. Interface headers
supply compile-time types; they grant no resource access or direct hub calls.

## Ownership and delivery

The HID kmodule, reached through the IO gateway, owns narrowly scoped USB interface claims and interrupt
transfers. Existing xHCI, USB core and USB-hub modules own transport, DMA safety,
root/downstream enumeration and hotplug. The provider owns HID report decoding,
pressed keys, Unicode layout interpretation, repeat and event history. No terminal
keymap or character interpretation is involved. Glyph rendering remains separate.

The provider uses generic HID report transport. The kernel selects report
protocol, retrieves the descriptor and owns each physical interface once.
`report.c` builds keyboard fields with the common descriptor walker, decodes
selector arrays and variable bitmaps (including NKRO), and retains pressed state
across report IDs. Modifiers, repeats and layout-dependent text remain here.
Malformed or rollover input resets keyboard state rather than retaining stuck keys.

Application collections determine routing: separate keyboard/mouse report IDs
reach their class provider; a single mixed report reaches both. Each decodes its
own fields using offsets across the full report. A mouse consumer can share the
interface without competing for a USB claim or changing the keyboard protocol.
The current keyboard provider selects one supported interface at a time.

Notifications and client calls arrive through the same blocking receive gate.
The provider acknowledges copied reports before decoding; kernel mailboxes are
independent per consumer. Receives remain armed while idle until input,
disconnect or cancellation. No application polls hardware. The final physical
consumer controls interface cleanup; closing one class leaves its sibling active.

## Operations and event contract

Base reflection exposes only the caller's granted operations and input scope.
Keyboard-specific operations are independently authorized:

- `read` returns a batch after an explicit provider epoch/sequence, waiting within
  the generic call deadline when no events are ready.
- `state` returns pressed physical HID usages and the current sequence anchor.

The first read uses epoch/sequence zero and starts at the capability's authenticated
grant timestamp, never exposing earlier history. Subsequent reads use the returned
epoch/next_sequence. Independent clients have explicit replayable cursors, without
destructive consumption. A stale epoch or evicted cursor returns INPUT_RESYNC
with a current anchor, preserving the original request and error trace. A client
can retrieve state if authorized, or clear local state and resume at that anchor.

Each 64-byte event contains sequence, boot-unique device ID, monotonic emission
time, kind/flags, physical HID usage and modifiers. TEXT carries Unicode scalars,
including U+0000, correlated with the originating physical event. IBM and ABNT2
JSON definitions live in `layouts/`; the handler reads its own `config.cnf`
(`layout=ibm` or `layout=abnt2`) and selects a bundled filename. Generic
`provider.cnf` lists the startup files; bootstrap copies them without interpreting
keyboard configuration. `boot.providers=keyboard` selects this bundle.
Dead keys emit a base scalar plus combining mark, rather than claiming full IME
or NFC composition. Repeat starts after 400 ms and continues at 33 ms intervals.
Layout files, not terminal code or hardcoded named layouts, define text mapping.

Disconnect/invalid reports clear held keys and emit reset/removal transitions.
Replacement USB devices get fresh identities. The current provider epoch remains
stable through device hotplug. Provider replacement/rebinding is separate work;
old capabilities are not yet automatically reconnected to a replacement process.

History grows through the memory handle and retains approximately five seconds
of events. Evicted cursors resynchronize explicitly. Allocation failure reuses an
existing buffer with a RESET/gap; failure to allocate initial storage terminates
the provider. Pending reads also use owned dynamic memory. Protocol usage tables
are fixed at 256 entries because the negotiated HID format uses eight-bit usages;
this is not a cap on files, handles or manifest resources.

## Authorization and shutdown

Trusted boot policy admits the manifest before execution and reserves `/@` names.
Provider access is scoped to keyboard I/O and its own memory. Read/state clients
need the `boot-input` scope. Broker-generated delivery context supplies stream
identity and grant time; client payloads cannot impersonate another admission.
This milestone has one boot input domain and one active keyboard per provider,
not installed-app/session routing or an interactive approval UI. Bundles are
trusted development image contents; signing/installation enforcement is pending.

Kernel decommission stops publication admission, rejects queued calls and notifies
the provider through a preallocated lifecycle mailbox. The provider finishes
admitted requests with replies, closes its I/O subscription, waits for the kernel
worker to quiesce and releases memory before exiting. Reap waits for actual thread
exit. Completed internal calls explicitly release their retained request records,
so regular clock/read/ack traffic does not accumulate an unbounded broker cache.
Forced process cancellation and automatic provider recovery are not implemented.

## Acceptance and limits

`test-keyboard` runs the production decoder and stream logic under UBSan: IBM/ABNT2,
modifiers, repeat, Unicode/NUL/combining text, arrows, rollover, malformed layouts,
grant boundaries, replay, deadlines, history eviction and resynchronization.
`test-process-boot` loads the actual bundle from disposable ext4 media. Separate
protected clients check authorized read/state, readonly reflection, denied I/O,
independent input delivery, structured arrows, USB unplug/replug, cleared pressed
state and quiescent decommission with process/stack cleanup. Fixture clients never
enter candidate images. One/four-CPU TCG acceptance is recorded in the process docs.

Descriptor-defined keyboard arrays/bitmaps, NKRO and shared-interface routing
are covered by `test-hid-demux`. Multiple keyboards, multi-session routing, LED
output, additional HID usages and physical USB qualification remain open. Unsupported formats fail explicitly; this implementation does not
claim general HID or USB input hardware support.

Packaged definitions now live in `/system/config/keyboard/{ibm,abnt2}.keymap`
(JSON format), with `config.cnf` in the same directory. Generic bootstrap aliases
preserve the handler’s logical startup names; there is no duplicated layout copy
in the handler resources.


## Capture liveness

The HID keyboard worker reports `CB_IO_ERROR/CB_PROVIDER_FAILED` before an
unexpected exit. The terminal notification is one-way; a dying worker never waits for a failed
provider to acknowledge it. The keyboard handler exits, withdrawing its publication; blocked callers receive provider failure
instead of waiting on an apparently live handle with no capture worker. Normal
shutdown is not reported as failure, and unsafe DMA ownership remains retained.
Terminal prints `keyboard input failed` if its read fails.

Copied HID report deliveries are acknowledged before decoding and serving
readers, permitting earlier capture rearming. Device-added negotiation still
finishes before acknowledgement. One spare pending-read node is retained and
reused; empty timed reads no longer map/unmap a private page on every request.
The cache is released on normal handler shutdown and never crosses processes.


`focus_claim`/`focus_release` (operations 3/4) arbitrate exclusive keyboard input
using the authenticated caller PID; apps cannot supply another owner's identity.
Claims return BUSY while another process owns focus; release is owner-only.
The handler watches owner lifetime through installation-bound IO owner_watch and
releases on owner death. That private IO method permits only installed graphics
and keyboard providers, not ordinary apps.

Optional 1.1 READ/BATCH frames append a focus generation to the original 32-byte
prefix. A generation change returns INPUT_RESYNC with the focus boundary and
FOCUS_SUSPENDED flag for other readers. Their resubmitted reads park, preserving
normal deadlines; after release another resync skips owner-era input before
resuming. The terminal uses this extension to pause its editor/cursor. Existing
1.0 frames remain supported; suspended legacy readers wait and resync after
release. Monitor acquires the handle and reads directly; stdin stays byte-only.

STATE also refuses non-owner callers while exclusive focus is held, preventing
pressed-key snapshots from bypassing non-echoed password input. Replay floors
on focus changes prevent other readers from retrieving owner-era key events.

The packaged default is `layout=abnt2`. IBM remains selectable with `layout=ibm`;
both definitions continue to be supplied under /system/config/keyboard.
