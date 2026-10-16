# USB core

`usb.kmod` supplies root and downstream USB enumeration and interface ownership above the
[xHCI transport](xhci.md). It is a trusted kernel module using the hub C ABI;
it is not an application-facing handle or a HID/storage class driver.

## Startup and events

`usb.init(hub,heap)` resolves versioned mem/clock/threads/IRQ/xHCI dependencies,
collects xHCI PCI hardware references from `hwenum.hardware` and publishes the
owned `usb.devices` registry. Initialization does not claim controllers.
Normal boot starts the hub-class manager and calls `usb.start()` after foundational initialization; it creates one
preemptible CPU0 worker per discovered controller. Controller mechanics, DMA,
IOMMU domains and interrupt routing stay in xHCI and its existing hub dependencies.

Each worker opens its controller, subscribes with `xhci.watch`, scans root ports
and blocks between events. Deferred xHCI port changes/failures grant wake credits;
there is no periodic USB polling thread. Subscribe-before-snapshot plus credited
thread wakes closes the check/block race. Debounce (100 ms) and reset recovery
(10 ms) use timed blocking; enumeration/control I/O waits on actual transfer IRQs.
Different controllers have separate workers; class operations serialize per device.
Topology changes serialize per controller using blocked waiters, including
postorder subtree removal. xHCI configuration contention also uses wake-driven
waiters; concurrent hub enumeration does not turn a busy command ring into a
spurious device failure.

For each new attachment, core verifies the port generation, resets the port,
creates an addressed host slot, reads the first eight device-descriptor bytes,
validates/updates EP0 packet size and reads the full device descriptor. It retrieves
and validates all configuration descriptors, then selects the first configuration
with alternate zero active. xHCI assigns the bus address; core never sends a second
SET_ADDRESS. Device/configuration strings are represented by their descriptor
indices; automatic string retrieval and configuration-selection policy are deferred.

Parsing checks descriptor lengths/bounds, total size, interface/alternate
uniqueness, alternate-zero presence, endpoint counts/address collisions and
speed-specific packet/interval/burst rules. High-speed interrupt transactions and
SuperSpeed endpoint companions are translated into the existing host contract.
Unknown class descriptors are retained in raw configuration bytes. Malformed
input and allocation failure produce separate errors and release partial parsing
allocations. Unsupported endpoints remain identifiable and cannot be claimed.

## Owned devices and lifetime

`usb.devices` is a static 40-byte header with a live linked list of dynamic
128-byte public device records. Private device allocations include the record,
state, host token and a separate 4 KiB bounce buffer. Configuration bytes,
interfaces/endpoints and configured-endpoint flags are dynamically allocated.
There is no fixed device/interface count beyond USB descriptor/hardware fields
and available memory. One connector record is retained/reused per observed physical
topology path; disconnect/restart does not append an unbounded history.

The common identity contains a USB-tagged ID, generation and parent identity: PCI for root devices, USB for downstream devices.
`root_port`, downstream `port` and `depth` preserve topology independently of IDs.
Every attachment receives a fresh ID/generation. An old reference cannot operate
on a replacement device. States distinguish ENUMERATING, CONFIGURED, DETACHED,
FAILED and UNSUPPORTED, with an explicit error. Snapshot methods copy metadata;
raw table traversal requires CPU-lock synchronization. Descriptor backing remains
private and callers never retain pointers into it.

Composite devices have independent interface owners. The common device owner
field stays zero; inspect `usb.interface` for the actual owner of each interface.
Endpoint ownership derives from the claimed alternate-zero interface. Class
selection and application permissions are future layers, not inferred from USB
class codes by this module.

## Surfaces

Payloads are defined in [usb.h](usb.h).

- `init(hub,heap)`, `start()`, `stop()` and `info(out)` manage workers/inventory.
  `worker` is the registered thread entry, not a client operation.
- `watch(enable)` subscribes/unsubscribes the current BSP thread to coalesced
  device/worker-state wakes. Subscribe before snapshot, then block and rescan;
  `info.changes`/`table.changes` count transitions. Unsubscribe before thread teardown.
- `device(ref,out)` returns identity/status metadata. A detached record can still
  be inspected until rebinding replaces its reference.
- `configuration(ref,index,buffer,capacity,out_size)` copies the complete raw
  configuration. Insufficient capacity reports required size without copying.
- `interface(ref,ordinal,out)` returns active-configuration interface/alternate
  metadata; `endpoint(ref,interface,address,out)` reports alternate-zero endpoints.
- `claim(ref,interface,owner)` requires a nonzero owner and supported endpoints,
  and configures host endpoints before publishing ownership. Repeating the same
  claim is idempotent. A different owner fails. Already configured endpoints can
  be reused after release; partial claim failures retain known host configuration.
- `release(ref,interface,owner)` refuses overlapping operations or another owner.
- `control(io,out_result)` permits interface/endpoint-scoped class/vendor requests
  and safe standard reads/endpoint clear-halt operations. Address, configuration
  and alternate-setting mutations remain core-owned. These are trusted kernel
  ownership checks, not a userland security boundary.
- `transfer(io,out_result)` submits bulk/interrupt endpoint traffic for a claimed
  interface. Both I/O methods synchronously block on transport completion and
  return the actual byte count/result. They require a preemptible CPU0 thread,
  IF enabled, outside CPU critical sections and deferred IRQ callbacks.

I/O uses a permanent core-owned bounce buffer rather than retaining the caller's
memory in xHCI. OUT is copied before submission; IN is copied only after successful
completion/reap. Timeout cancels/reaps before reuse. Cancellation/quiescence failure
retains transport/core storage and marks the device failed; no caller pointer
escapes return. Recoverable cancellation/stall does not automatically disable the
whole device. Default I/O deadline is one second; callers may supply an absolute
clock deadline. Overlap on the same device is refused rather than queued.

Additional trusted class-driver surfaces:

- `hub_configure(config)` requires a claimed class-9 interface and configures the
  host hub port count, multi-TT and TT think-time fields.
- `downstream(request)` requires that hub owner and a generation-qualified parent,
  port and connection generation. It attaches/enumerates a child or invalidates
  and removes its entire subtree, deepest slots first. Accepted enumeration can
  produce a failed child record; a malformed child does not kill its healthy hub.
- `cancel(io)` cancels a pending request only within the claimed interface/endpoint.
  The blocked caller consumes the terminal result; cancellation does not reap it.

## Shutdown and restart

`stop()` requests shutdown and wakes workers; use `info.running` to observe
completion and call `stop()` again to reap exited worker stacks. Workers revoke
new I/O, cancel outstanding transfers, wait for active callers, disable slots,
unsubscribe, and close the controller before releasing DMA/PCI ownership.
An intermediate Stopped event cannot be reaped while shutdown cancellation is
still completing. Uncertain shutdown leaves ownership/storage retained and a
reported controller failure; it does not pretend resources are free.

A subsequent `start()` after clean shutdown reopens controllers and enumerates
again even if host port generations restart at one. Connector records are reused,
while attachment IDs are fresh. This is controlled bus restart, not hot executable
unload. The class-driver owner must still account for loss of its interface.

## Current boundaries and validation

[usb-hub](usb-hub.md) supplies external hub class policy; xHCI carries route and
transaction-translator context. Alternate-setting selection,
isochronous endpoints, SuperSpeed bulk streams, suspend/resume, controller PCI
hot removal and image unloading remain deferred. Individual submissions and whole
configuration descriptors are limited by xHCI's current 4 KiB transfer capacity;
larger configurations fail explicitly rather than being truncated. USB3 descriptor
normalization is modeled; real SuperSpeed transfers and physical quirks remain
unqualified. HID and USB mass-storage/SCSI policy are separate future modules.
No boot filesystem or persistent disk access is introduced by enumeration.

`make -C src test-usb` exercises production core code with a modeled host under
UBSan: parsing, malformed/OOM rollback, packet/interval normalization, enumeration,
interface claims/control scoping, detach/rebind, stale references, cleanup and
restart reuse. `test-xhci` also covers the watcher/close ownership contract.
`make -C src test-usb-boot` uses isolated images/disposable media and QMP
keyboard unplug/replug. It validates real keyboard/mouse/boot-storage descriptors,
class GetMaxLUN control, ownership refusal, pending interrupt-I/O shutdown and
controller restart with no growing connector history. One/four-CPU TCG tests
include both emulated IOMMU backends; native/physical USB-core acceptance is pending.
Candidate images contain neither test probes nor injected input.

References: [USB 2.0 specification](https://www.usb.org/document-library/usb-20-specification),
[xHCI requirements](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf).

External hubs are covered by `test-usb-hub` and `test-usb-hub-boot`; see
[hub class driver](usb-hub.md) for topology, teardown and qualification limits.
