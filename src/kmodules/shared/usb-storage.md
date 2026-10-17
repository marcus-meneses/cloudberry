# USB mass storage kmodule

`usb-storage` is a Limine-loaded, shared Intel/AMD kernel module. Its exported
prefix is `usb_storage`. It uses versioned hub calls to USB core, memory, threads,
clock and CPU synchronization; it contains no xHCI, interrupt or DMA implementation.
Initialization publishes the owned live `usb_storage.devices` list header but
claims no hardware. A caller selects a generation-qualified USB device, interface
and LUN explicitly with `usb_storage.open(request, out_block, capacity)`. Pass
`sizeof(struct block_device)` for ABI1.1; capacity zero returns the original
48-byte read-only descriptor without touching its caller's trailing memory.

Supported interfaces are SCSI-transparent Bulk-Only Transport (class/subclass/
protocol 08/06/50). The module claims one interface and its bulk IN/OUT endpoints,
queries optional Get Max LUN, reads INQUIRY, establishes readiness and discovers
capacity using READ CAPACITY(10), falling back to (16). Direct-access and CD/DVD
media can be read; direct-access media advertise writes only after MODE SENSE
confirms write protection is off and SYNCHRONIZE CACHE succeeds. CD/DVD stays
read-only. One LUN can be open per interface; UAS, concurrent LUN
multiplexing and automatic driver selection are deferred.

Reads use READ(10) for its address range and READ(16) beyond it. Each command
validates CBW/CSW sizes, signature, tag, residue and status. It uses a private
4 KiB staging buffer, copying into the caller buffer only after complete success.
Writes use WRITE(10)/(16), copy the input into private staging and validate the
status wrapper before reporting completion. Flush uses SYNCHRONIZE CACHE(10);
a failed write/barrier poisons the instance because the on-disk outcome can be
unknown. There are no automatic write retries.

The common contract is [block.h](block.h): logical sector addressing, geometry,
transfer limits and explicit lifetime retention. The block read surface returns
completed bytes, with a zero-byte failure leaving the destination unchanged.
Sector sizes 512/1024/2048/4096 are supported. A larger request is split by the
consumer at the advertised transfer limit rather than imposing a file-size limit.

Command failures retrieve REQUEST SENSE; `usb_storage.info(cookie, out)` reports
sense key/ASC/ASCQ and counters. UNIT ATTENTION is consumed at most three times
only during initial readiness, before any block descriptor is published. After
publication it poisons the instance, so a changed medium cannot silently inherit
filesystem references. Transport errors, malformed wrappers and disconnect also
fail closed. There are no automatic read retries or BOT reset recovery; poisoned
transport may require USB core stop/start/rebind before reopening. Healthy
close/reopen is tested. USB core owns cancellation and retained DMA on uncertainty.

`config.storage.timeout_ms` bounds each BOT command, shared across its wrapper,
data and status phases. No module lock spans USB waits. Calls require a BSP thread,
interrupts enabled, outside IRQ and CPU-critical context. A per-instance busy
flag refuses overlap; separate devices can have independent instances. Lifetime
retain/release permits partitions and filesystems to pin a parent. Close refuses
busy/retained objects, releases the interface and frees the owned record. Detached
references fail; monotonically assigned cookies are not reused.

On failed open, `out_block` is normally zero. If cleanup cannot release a still-live
claim, it contains a cookie for the retained failed record: inspect and retry close.
The failed descriptor is not valid for block I/O. This keeps uncertain ownership
reachable rather than leaking or freeing it blindly. Table traversal requires the
CPU lock; backing is owned until successful close. Records include a 4 KiB buffer.

Validation: focused UBSan BOT/error/bounds/lifetime tests and isolated QEMU
USB→GPT→ext4 reads. Four-CPU AMD-IOMMU validation includes actual unplug/replug,
failed reads through a mounted filesystem, retained-parent teardown and a fresh
attachment. Native/physical USB storage remains unqualified.

Protocol reference: [USB-IF Bulk-Only Transport 1.0](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf).
