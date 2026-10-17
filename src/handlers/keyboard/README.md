# First handler: keyboard

The first loaded driver/handler will publish **`/@keyboard`**, keyboard interface
1.0, defined in [keyboard.h](../../interfaces/keyboard.h). This folder currently
holds its design, not an implemented USB HID driver or boot-selected executable.
Foundational kernel modules still load first; the handler will execute as a
privileged isolated process rather than exporting a kmodule-hub callback to apps.

## Responsibility and dependencies

The handler claims only keyboard-class interfaces through its granted OS I/O
handle. Existing xHCI, USB enumeration, interface ownership, DMA/interrupt safety
and hotplug remain in their current owners. The handler decodes HID reports,
tracks physical state and emits structured events. The kernel's deferred IRQ
path wakes the USB operation; completion wakes this process. A blocked client
read sleeps until events, removal, revocation or deadline, without app polling.
No PS/2 support, hard-ISR allocation or direct device/MMIO access is introduced.

Bootstrap grants memory allocation and scoped keyboard HID I/O before this
provider starts. Loading it from ext4 follows the configured driver-volume GUID,
not enumeration order. Its native executable and JSON policy manifest are future
artifacts, distinct from the current trusted format3 kmodule image/driver manifest.
It publishes its interface only after successful initialization. A client receiving
the handle sees no USB/controller implementation details.

## Operations and event contract

Base reflection is filtered by the caller's grant. Keyboard-specific operations:

- `read`: synchronously return an event batch after an explicit epoch/sequence,
  waiting within the generic call deadline when no events are ready.
- `state`: return the currently pressed physical HID usages and a sequence anchor
  for resynchronization, limited to the caller's authorized input source.

The first read uses epoch/sequence zero and starts at the capability's grant point,
never exposing earlier events. Later requests use the returned provider epoch
and next_sequence. Reads have no implicit destructive cursor advance: an explicit
cursor makes a repeated request replayable while its events are retained. A stale
epoch/evicted sequence produces INPUT_RESYNC and a current anchor, never a silent
gap or invented successful delivery. State plus its last_sequence lets clients
rebuild held-key state, then continue reading. A read-only grant without state
permission can clear local state on RESET and resume from the supplied anchor.

Each 64-byte event carries a sequence, boot-unique device ID, monotonic provider
emission timestamp, kind, flags, physical HID usage page/usage and modifier state.
Key down/up/repeat remain physical events. TEXT events carry Unicode scalar
values, including U+0000; composition can produce multiple TEXT events correlated
with the originating physical sequence. Terminal code does not decode scancodes,
ASCII or keyboard layouts. Layout definitions/configuration belong to the input
policy/handler; glyph/codepage rendering belongs to text/graphics. No embedded
hardcoded terminal keymap is part of this contract.

Modifier bits 0..7 correspond to HID keyboard usages E0..E7 (left/right control,
shift, alt and GUI). Repeat/TEXT timestamps describe emission, not a claimed
hardware keypress timestamp. Device IDs are never reused during a boot. Disconnect
and recovery emit explicit reset/removal transitions so clients cannot retain
stuck keys. Recovery changes provider_epoch and requires resynchronization; old
logical handles reconnect, while old physical device/cursor identities do not.

Event storage is provider-owned and allocated through the memory handle. ISR
paths never grow it. Retention pressure must report a gap/reset or resource
failure, not silently drop input. Batch capacity derives from reply capacity and
max_events; event counts do not define a global keyboard/history limit. Slow
consumers have independent cursors. State replies contain the header followed by
`count` 16-byte pressed-key records; read replies contain the batch header followed
by `count` event records. Reserved fields/flags are zero in version 1.0.

## Authorization and first acceptance

Sequences/cursors belong to the admitted input stream, not a global stream of
all users' keystrokes. Unrelated hidden events do not create visible numbering
gaps or reveal other-session activity. Stream identity and effective source scope
come from the broker's authenticated delivery context, not the read payload.

Acquiring this name does not grant unrestricted keylogging. Kernel input routing
binds the grant to an authorized session/input source; observing background or
other-session input requires explicit additional policy. The provider cannot
authenticate a process from caller-supplied IDs. Read/state permissions are
independent, and revocation prevents queued/new reads after admitted work finishes.

The first implementation should support the QEMU USB keyboard's negotiated HID
format, validate report/descriptor lengths and release its interface cleanly.
Unsupported formats must fail explicitly. Focused acceptance then covers press/
release, simultaneous/modifier keys, structured arrows, Unicode text, blocking
read wakeup, independent clients, denied reflection/operations, small reply
buffers, disconnect/reconnect, provider restart/reset and overflow resync. Use
disposable test media and synthetic input; no driver fixture enters candidate boot.
