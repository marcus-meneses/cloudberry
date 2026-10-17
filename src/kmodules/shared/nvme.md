# NVMe block backend

`nvme.kmod` implements one explicitly selected PCIe NVMe controller and one explicitly selected
active NVM namespace. It exposes kernel surfaces, not an application filesystem
or disk handle. Boot only initializes its dependencies; it does not automatically
claim, read, format or migrate either persistent disk.

`init(hub)` caches surfaces. `open(hardware_index,out nvme_info,namespace_id=0)` claims the PCI
endpoint, sizes/maps BARs, disables the controller and waits for RDY=0 before
allocating six tracked DMA pages. It creates 16-entry admin and I/O queue pairs,
identifies the controller/namespace, configures MSI-X entry zero, and creates an
interrupt-enabled I/O completion queue. Command/completion layouts are checked
at compile time. Zero selects the first active namespace; a nonzero ID selects exactly that active namespace, with no fallback. The selected namespace must have plain 512–4096-byte blocks
without metadata or protection information. Unsupported layouts fail closed.

`read(lba,blocks,destination)` requires a CPU0 preemptible thread, interrupts
enabled and no critical section or deferred IRQ callback context. Concurrent
open/read/close operations are rejected by an atomic ownership guard. Requests
may span arbitrary validated block ranges; transfers are chunked through a
private page-aligned 4 KiB DMA buffer. No PRP list is needed for these chunks.
PCI pins/maps every DMA buffer through the selected IOMMU backend.
Queue publication and completion consumption use x86 ordering fences; completion
phase, command ID, queue ID, head bounds, status and recorded DMA-domain faults
are checked before copying.

The reader blocks until the IRQ worker consumes its completion and wakes it, or
an independent thread deadline expires. There is no completion polling loop.
The initial I/O deadline is 500 ms per chunk. Controller readiness uses CAP.TO
with a 500 ms minimum and a finite poll bound. Admin startup commands poll with
voluntary thread yields when called from a thread; this is separate from the
interrupt-driven data path.

A timeout/controller/completion error makes the instance unavailable and attempts
a reset. No automatic retry occurs. Successful earlier chunks may already be in
the destination; failure does not promise an untouched whole buffer. `close()`
masks device and software routes, disables the controller, observes RDY=0, turns
off bus mastering, and then releases the IRQ route and DMA allocation. Failed
quiescence or an in-flight callback retains ownership/storage for retry; uncertain
DMA memory is never recycled. A closed failed instance can be opened afresh.

`info(out)` reports geometry, completed chunks, notifications, timeouts, route ID,
online/failure state and open stage (0 complete; 2 capability checks, 3 disable,
4 DMA allocation, 5 enable, 6–8 identify, 9 route, 10 MSI-X, 11–12 I/O queues).
`block(out,capacity)` exports the currently open namespace as the common block
ABI1.1. Full capacity is `sizeof(struct block_device)`; zero requests the original
read-only 48-byte prefix. `block_read/write(cookie,lba,count,buffer,capacity,0)`
accept at most 4 KiB and use NVM Read/Write commands through the existing queue,
private DMA page and MSI-X completion worker. `flush(cookie)` issues NVM Flush.
A write completion alone need not be persistent; flush is the ordering/durability
barrier. Sources are copied before submission and never retained. Errors poison
the instance without retry; completed earlier chunks are reported as a prefix.
`control(cookie,BLOCK_RETAIN/RELEASE)` prevents close while mounted or sliced.
Cookie values never repeat, and old descriptors fail after reopen. The legacy
`read(lba,blocks,destination)` remains compatible. No discard, format or arbitrary
admin-command surface exists.

The whole-device descriptor feeds `partition.find` with the configured GPT GUID,
then `ext4_fs.mount`. This reuses the same partition/filesystem implementations
as USB and SATA. Boot does not automatically claim persistent disks or select a
volume. All current modules remain Limine-loaded; only selected future modules
will be read from the ext4 driver directory.

The isolated one/four-CPU test reads known patterns and the last sector from a
read-only disposable 1 GiB image, wraps both queue indexes repeatedly, checks
range/owner rejection and actual MSI-X counters, suppresses completion delivery
to force timeout/reset, then closes/reopens and reads again. Native hardware,
broader IOMMU firmware layouts, simultaneous controllers/namespaces, queue parallelism and general
controller recovery remain future work.

Register/command references: [NVM Express specifications](https://nvmexpress.org/specifications/),
[controller registers](https://learn.microsoft.com/en-us/windows/win32/api/nvme/ns-nvme-nvme_controller_registers),
[command layout and PRPs](https://learn.microsoft.com/en-us/windows/win32/api/nvme/ns-nvme-nvme_command).

The focused `make -C src test-block-boot BLOCK_BACKEND=nvme` boots the hybrid
image from NVMe itself, exercises GPT GUID selection and journaled ext4 file
mutations/remount, checks actual MSI-X delivery, suppresses completion to force
timeout/reset, validates stale cookies and explicit namespace selection, and
runs independent GPT/ext4 checks afterward. Test media are disposable below
`bin/tests`; developer disk images and candidate VM runs are excluded. Write
and Flush opcodes follow the [NVM command set specification](https://nvmexpress.org/wp-content/uploads/NVM-Express-NVM-Command-Set-Specification-1.0c-2022.10.03-Ratified.pdf).
