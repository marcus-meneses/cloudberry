# AHCI SATA block backend

`sata.kmod` is a Limine-loaded kernel module implementing multiple explicitly selected
AHCI controllers and ATA disk ports. It uses the existing PCI inventory,
BAR mapping, exclusive ownership, DMA/IOMMU allocations, clock, preemptible
threads and deferred IRQ worker. It does not enumerate PCI again or implement a
filesystem. Intel and AMD use the same AHCI frontend/backend contract.

`init(hub,heap)` binds dependencies without claiming disks. `open(sata_open*,out block,
capacity,out cookie optional)` selects a hardware index and port, returning the common block ABI1.1.
Pass `sizeof(struct block_device)` for writes/flush and root identity; capacity zero
requests the legacy 48-byte read-only prefix. `info(cookie,out sata_info)` reports
geometry, state, reads/writes/barriers, interrupt/time-out counts and open stage;
cookie zero is accepted only with exactly one open instance. The optional fourth
open argument receives a cleanup cookie even if open fails.

Open performs BIOS/OS handoff when advertised, resets the exclusively owned HBA,
checks the selected implemented port and active link, allocates three tracked
pages for the command list/received FIS, command table and private data buffer,
and enables FIS reception before examining the reset signature. IDENTIFY DEVICE
requires plain ATA LBA48, FLUSH CACHE EXT and power-of-two 512–4096-byte logical
sectors. A successful flush qualifies the barrier before publishing writable
access. This initial backend requires a 64-bit DMA-capable HBA. Ports share one controller owner and MSI route, with separate command/FIS/data
pages and cookies. Opening a sibling never resets the HBA. Commands serialize
per controller; independent controllers keep separate queues and lifetimes.

`read/write(cookie,lba,sectors,buffer,capacity,0)` use READ/WRITE DMA EXT and one
non-NCQ command slot, with at most 4 KiB per call. Source data is copied into a
private owned DMA page before submission; caller memory is never handed to the
controller. `flush(cookie)` sends FLUSH CACHE EXT. No request is retried. A write
completion is not itself a durability barrier. Device errors, invalid completion
length, DMA-domain faults and timeouts poison the instance. An uncertain write
may already have changed disk contents.

MSI is programmed by `pci.msi`, through an owned `irq.route` message. AHCI cause
acknowledgement and waiter wakeup run in `sata.interrupt`, invoked by the deferred
IRQ worker. The hard ISR only records notifications. Ordinary command completion
blocks on IRQ/deadline rather than polling; startup/reset readiness checks have
one-second deadlines and sleep in one-millisecond steps. All blocking operations
require a CPU0 kernel thread, IF=1, no critical section and no IRQ-callback context.

`control(cookie,BLOCK_RETAIN/RELEASE)` pins the instance; `close(cookie)` refuses
retained or active instances. Cookie values never repeat. Timeout or failure
stops the affected port engine; failure to stop poisons all controller siblings
and attempts reset. Closing one port stops its engine, temporarily disables
bus mastering while freeing its DMA, and preserves siblings. Final close disables MSI and the software route, proves reset
completion, disables bus mastering, releases the route and tracked DMA, then
releases PCI ownership. If quiescence or cleanup fails, ownership and DMA stay
reachable for retry; clearing bus mastering alone is not proof of DMA drain.

The returned whole-device descriptor feeds `partition.find` using
`config.drivers.partition_guid`, then `ext4_fs.mount`. Partition bounds and
filesystem policy remain above SATA. No disk formatting, boot migration,
automatic disk claiming or executable loading is introduced. All current modules
remain Limine-loaded from the boot partition. Ext4 will host only selected future
modules.

Validation uses `make -C src test-block-boot BLOCK_BACKEND=sata`. It boots the
hybrid image from an AHCI disk itself, finds the configured GPT GUID, reads a known
fixture, performs journaled writes/append/rename/delete and readonly remount,
checks pins/bounds, suppresses notification delivery to force timeout, resets and
reopens with a fresh cookie, and independently checks GPT/ext4 after shutdown.
Only disposable images below `bin/tests` are attached.

`BLOCK_MULTI=1` validates two controllers and two ports on the first, closing and
reopening one port without disturbing its sibling, stale cookies and a timeout
while a different controller remains usable.

Remaining limits: no ATAPI, NCQ, hotplug,
32-bit-only DMA HBA, shared INTx, multiple MSI vectors or physical acceptance.
Readiness/flush deadlines need hardware qualification. MSI and MSI-X are device
capabilities, not CPU-vendor alternatives. Reference:
[AHCI 1.3.1 specification](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1-3-1.pdf).
