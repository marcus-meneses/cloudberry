# xHCI controller transport

`xhci.kmod` is shared across Intel/AMD platforms. The kernel loads it with the
other boot images and calls `xhci.init(hub,heap)` after PCI/IOMMU initialization.
Initialization caches versioned hub dependencies and publishes `xhci.controllers`;
it does not open a controller or access the developer's persistent disks.
USB core, hub/HID class drivers and application-facing handlers remain deferred.

The module uses existing PCI ownership/BAR/MSI-X surfaces, PCI-managed DMA/IOMMU
mapping, mem allocation, irq routing/deferred notifications, clock deadlines and
thread block/wake/yield. It does not duplicate those mechanisms. Mapped device
register access and xHCI ring/context formats belong to this driver.

## Owned registry and lifetime

`xhci.controllers` references a 32-byte static registry header with a live linked
list of dynamic controller records. `count` counts discovered/opened controller
records, not a fixed array extent. Each controller allocation is 480 bytes,
including its 96-byte public record, with a 104-byte port record per advertised
root port and 288-byte private slot record per advertised slot plus slot zero.
These records are allocated on explicit open, not during generic PCI enumeration.
A closed controller's stable record is reused on reopen with a fresh ID/generation;
port-array pointers must be reacquired. No unbounded per-restart history is kept.
Registry consumers are trusted kernel code; raw traversal requires CPU-lock
synchronization. `xhci.info` and `xhci.port` provide synchronized snapshots.

Controller IDs, device tokens and transfer IDs are monotonically assigned;
exhaustion fails instead of wrapping. A port's generation changes on connection
changes, including CSC with unchanged final connection state. Device binding
requires the current generation. Disconnect invalidates submission through the
old token even if another device immediately appears on the same connector.

Controller states are OPENING, ONLINE, QUIESCING, FAILED and CLOSED. Each instance
serializes its synchronous mutations with a busy flag; IRQ completion continues
while commands wait. Blocking operations require a preemptible CPU0 thread,
IF enabled, no kernel critical section and no deferred IRQ callback context.
Multiple controllers have independent state; routes currently target CPU0.

## Hardware startup

Open validates PCI class 0c:03:30, claims the endpoint and its BAR, and requires
MSI-X. Capability traversal checks bounds and forward progression, negotiates
optional firmware ownership and records USB2/USB3 supported-protocol ranges,
slot types and advertised speed-ID words. Ports with missing/overlapping protocol
coverage fail closed. Firmware refusing ownership is not forcibly reset.

Startup waits for CNR, halts/resets the controller and requires 4KiB page support.
It allocates a four-page base arena: DCBAA, command ring, event ring and ERST.
Scratchpad arrays/buffers follow the controller's advertised requirement.
32-bit-only controllers reject DMA above their addressability limit.
All buffers are mapped before enabling bus mastering. Bus mastering is enabled
while halted before programming ERSTBA, because that write may fetch its DMA
segment descriptor immediately. Startup programs owned MSI-X routing, powers
PPC root ports if necessary, enables the controller/interrupter and proves a
No-op command completed through actual DMA and deferred IRQ delivery.

The generic irq ISR coalesces a preallocated notification and acknowledges the
APIC. `xhci.interrupt` runs in deferred foreground context, acknowledges xHCI
causes and drains at most 256 event entries under the CPU lock, without allocating
or calling clients. Ring-full/host errors and unexpected events fail the instance.
The driver uses release/acquire publication and MMIO ordering for DMA rings.

## Host-controller surfaces

Payloads are in [usb-host.h](usb-host.h), separate from hardware TRBs and contexts.
The generic payload contract is ready for a future USB core; no USB policy module
is implemented here.

- `open(hardware_index,out info)` returns a controller ID; `close(id)` is retryable.
- `info(id,out)` and `port(id,one_based_port,out)` return snapshots.
- `reset_port(id,port)` uses USB2 PR or USB3 WPR and checks connection/enabled state.
- `device(id,host_device_request,out token)` enables and addresses one root-port
  slot, with the caller-supplied initial EP0 maximum packet size.
- `endpoint(id,host_endpoint_request)` configures bulk/interrupt endpoints. Address
  zero with HOST_CONTROL evaluates EP0 packet size without exposing raw contexts.
  The caller supplies xHCI-normalized interval/burst/ESIT values; future USB core
  will derive them from descriptors and protocol speeds.
- `transfer(id,host_transfer_request)` returns an asynchronous request ID.
  Control transfers use setup/data/status stages; bulk/interrupt use normal TRBs.
  One request/result per endpoint is retained, with concurrent endpoints allowed.
- `wait(id,request_id,out,absolute_deadline_ns)` blocks until completion or deadline.
  Wake credits prevent a check/block race. One waiter is supported per request.
  A timeout leaves the request owned; the caller must complete or cancel it.
- `result(id,request_id,out,reap)` snapshots/reaps completion. IN bytes are copied
  to the retained caller buffer only on successful reap; its lifetime must extend
  through reap. Short control data is retained until the status stage completes.
- `cancel(id,device,endpoint)` stops/resets the endpoint and installs the next
  dequeue before permitting reuse. `remove(id,device)` disables the slot and
  invalidates its token. Completion errors require endpoint recovery before reuse.

Transfers are bounded to 4KiB per submission, with a private 4KiB bounce buffer
and 4KiB/256-entry transfer ring per allocated endpoint (152-byte metadata).
No client buffer is handed to hardware. Slot output/input contexts occupy two
pages. Rings/contexts remain pinned for reuse until controller close; hot-unplug
alone never frees potentially referenced DMA. DMA descriptors consume 48 bytes
per allocation, in addition to PCI/IOMMU ownership metadata.

Close refuses unreaped results/waiters, masks device/generic interrupts, proves
halt/reset, disables bus mastering and releases the IRQ only after queued/running
callbacks drain. Only then does it unmap/free DMA and release PCI ownership.
Uncertain halt/reset, firmware ownership or invalidation retains resources and
fails closed. This supports controlled controller restart, not executable-image
unloading or uninterrupted service across a controller reset. Reset affects all
USB devices below that controller. No driver manager is introduced yet.

## Validation and limits

`make -C src test-xhci` exercises production controller code with modeled
MMIO/DMA/IRQ adapters under UBSan: command/transfer/event cycle wrap, short reads,
wake waiting/timeouts, cancellation, detach/stale tokens, bounded command failure,
restart reuse, malformed protocol ranges, firmware refusal and retained DMA on
failed halt. The model checks bus mastering precedes ERSTBA fetch.

`make -C src test-xhci-boot` uses a separate test kernel/module and disposable
media, with QMP over pipes to unplug/replug a test keyboard. Raw test-only USB
requests exercise slot/address, EP0 evaluation, 100 device-descriptor transfers,
interrupt-endpoint cancellation, stale-token denial and close/reopen. The same
probe passes four-CPU TCG with both emulated Intel/AMD IOMMUs. Tests do not enter
candidate media or consume persistent disks. Controller startup uses no polling
worker; command waits yield and transfer waits block on notifications.

External hubs/TT routing, isochronous endpoints, streams, suspend/resume,
PCI-controller hot removal and MSI/INTx fallback remain unsupported. USB3
transport is represented by protocol/port support; real USB3 transfers, physical
controller quirks, native KVM and physical hardware acceptance remain unqualified.

Reference: [xHCI 1.2b requirements specification](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf).
