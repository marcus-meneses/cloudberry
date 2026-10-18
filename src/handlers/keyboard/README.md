# Keyboard handler

`keyboard.elf` is the first privileged isolated resource provider. Bootstrap loads
its static native executable, JSON manifest and declared startup files from the unique
configured ext4 driver volume. It publishes **`/@keyboard`**, keyboard interface
1.0, defined in [keyboard.h](../../interfaces/keyboard.h). Foundational kmodules
remain Limine-loaded; the provider runs in ring 3 with its own private pages.
It acquires `/@memory` and `/@io` through the C system library. Interface headers
supply compile-time types; they grant no resource access or direct hub calls.

## Ownership and delivery

The kernel I/O manager owns narrowly scoped USB interface claims and interrupt
transfers. Existing xHCI, USB core and USB-hub modules own transport, DMA safety,
root/downstream enumeration and hotplug. The provider owns HID report decoding,
pressed keys, Unicode layout interpretation, repeat and event history. No terminal
keymap or character interpretation is involved. Glyph rendering remains separate.

The first implementation negotiates the HID boot-keyboard protocol and SET_IDLE(0),
then decodes its eight-byte reports: modifier bits plus six ordinary usages.
[USB HID 1.11](https://www.usb.org/sites/default/files/documents/hid1_11.pdf)
defines this format. `/@io` accepts only a boot-keyboard interface and reports
attach, bytes, removal or transport failure. The provider receives these kernel
notifications and client calls through the same blocking gate. It acknowledges
each notification before another USB read. Client reads and the kernel worker
sleep on wakes/deadlines; no application polls hardware. USB retains its existing
one-second transaction health deadline and safe cancellation/rearm behavior.

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

General report-descriptor interpretation/NKRO, multiple keyboards, multi-session
routing, LED output, native KVM input acceptance and physical USB qualification
remain open. Unsupported formats fail explicitly; this implementation does not
claim general HID or USB input hardware support.
