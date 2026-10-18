# Scoped application I/O

`io` is the kernel I/O manager bridge used through `/@io`. It is not the keyboard
handler: it never decodes keys, layouts or text and never publishes `/@keyboard`.
The public contract is `interfaces/io.h`; its first scope is keyboard-class USB
I/O for an explicitly admitted privileged process. No PCI/MMIO addresses, DMA
buffers, arbitrary USB setup packets or device-ownership cookies cross to userland.

`keyboard_subscribe` creates one process-owned subscription and a pinned BSP
kernel I/O worker. It claims only a configured HID boot-keyboard interface
(class 3, subclass 1, protocol 1) with a supported interrupt-IN endpoint large
enough for its negotiated eight-byte reports. Root/downstream discovery and
hotplug still belong to USB/xHCI/USB-hub. The worker watches topology changes,
claims one keyboard at a time and reports attach, bytes, removal or failure.
Additional formats, simultaneous keyboards and I/O-provider migration are not
claimed by this initial bridge.

An attach notification lets the handler explicitly select boot protocol through
`keyboard_protocol`. That operation transports the narrowly validated SET_PROTOCOL
request and SET_IDLE(0); decoding remains in the handler. The handler acknowledges each consumed
notification before the worker submits another read. Preallocated broker mailboxes
apply backpressure without allocating or overwriting queued reports. Kernel-event
delivery flags cannot be supplied by ordinary clients. The provider receives
notifications and admitted client calls through the same sleeping receive gate.

USB retains its existing one-second transaction health deadline; an idle interrupt
endpoint is safely cancelled/rearmed on expiration. This periodic transport health
work is not application polling. Normal input uses IRQ completion and thread wakes.
Shutdown marks the subscription stopped, requests cancellation, wakes its worker,
then waits for actual thread exit before freeing interface/stack/mailbox state.
A cancellation racing submission is bounded by that existing deadline. Uncertain
hardware quiescence retains ownership and makes cleanup retryable, never unsafe.

`keyboard_close` closes an owned subscription. Process exit initiates cleanup;
reap retries until the kernel I/O worker is quiescent. Existing USB transfer bounce
buffers ensure caller/process storage is not retained by hardware. `now_ns` returns
the OS monotonic clock. Manifests grant each I/O operation independently: permission
to read the clock does not grant keyboard subscription or protocol changes.

Hub surfaces are `io.init`, `io.invoke` (kernel-owned invocation record),
`io.worker`, `io.info` (owned subscription/worker snapshot) and `io.exit`; these are not application handles or public syscalls.

The keyboard integration test exercises actual eight-byte reports and USB topology
notifications, a replacement device identity, cleared pressed state, worker close
and process reap. Native KVM/physical HID acceptance remains open.
