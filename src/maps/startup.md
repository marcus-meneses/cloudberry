# Startup memory ownership (x86-64, hub ABI 2.0)

Limine supplies four-level paging, its initial stack, a physical memory map,
HHDM, modules, framebuffer, physical RSDP and an MP response. Original kernel/modules, bootloader
memory, ACPI regions and framebuffer remain occupied. Only original map type 0
may supply new boot pages; no fixed physical address is assumed.

| Purpose | Virtual address | Ownership |
| --- | --- | --- |
| Kernel | `0xffffffff80000000` onward | Limine segments |
| Initial stack and responses | Limine addresses | Retained bootloader memory |
| Source module images | Limine HHDM addresses | Retained executable/modules memory |
| Loaded kmodules | Sequential from `0xffffffffa0000000` | Exact reserved pages; RX code and RW/NX data/BSS; guard page between images |
| Kmodule hub | `0xffffffffa1000000` | Four contiguous reserved pages (16384 bytes), RO/NX |
| Kernel mediation stubs | `0xffffffffa2000000` | Three contiguous reserved pages (12288 bytes), RX |
| Page tables and reservation chunks | HHDM + physical | Reserved usable pages |
| Managed stacks | `0xffffa00000000000` to `0xffffb00000000000` | mem-owned RW/NX body, unmapped guard at either end |
| Heap contexts and backing | HHDM + physical | Explicit reservations; backing grows on demand |
| Firmware/ECAM/MMIO window | `0xffffc00000000000` + physical | RO/NX/uncached reads; explicit MMIO mappings RW/NX/uncached; below 32 TiB |
| Framebuffer | Limine address | Temporary pre-flight output |

`memory.reservations` is a live hub table containing a reservation-table header,
not a copied free-memory map. Its header describes a linked list of page-backed
chunks containing physical base, byte count, purpose and an opaque owner value.
Its record count updates as boot allocates. Chunk pages reserve themselves;
more chunks are allocated as necessary. Kinds distinguish modules, hub, mediation,
page tables, heap contexts, heap backing and metadata. The header itself lives in
the kernel image, already occupied in the original map.

Allocation searches actual gaps between reservations. A large request cannot
silently consume smaller usable fragments; failed requests leave those fragments
available. Managed PAGES/STACK allocations can be released by exact owner/extent
only when their managed mapping count is zero. Release clears backing first,
then removes its ledger record; the free-gap search can reuse it. Permanent boot
allocations and original non-usable ranges remain excluded.
Limine's map is never modified. Hwenum's memory records explicitly carry
HWENUM_MEMORY_FIRMWARE_MAP and debug prints say firmware-usable; they describe
original firmware availability, not current free space.

The bootstrap heap shared by hardware inventory, MMIO inventory and scheduler starts with one backing page. Its context size/alignment comes
from the mem module's `mem.context` export. Configured mem surfaces can request
additional reserved pages through `memory.reserve`, add independent regions and
retry an allocation. No fixed 64 KiB heap or device-count ceiling remains.
Growth is still physically contiguous per added region and ultimately bounded by
available suitable RAM. Free returns blocks to the owning heap; it does not yet
release those backing pages to other owners.

Hub ABI2 stores only 48-byte method records (name, mediated entry, method version)
and 64-byte table references, sharing the contiguous hub allocation dynamically. Argument definitions
remain in headers. Registering exports moves the table-reference area within the
hub, so clients must reacquire record pointers after registration; callable entry
addresses and table backing addresses remain stable. Current production has 162
methods and five table references, occupying 8,168 bytes (49.85%) of the four-page
16 KiB hub. With five table references, up to 333 methods fit. Mediation backing
is sized from method capacity and occupies three pages (12 KiB); current stubs
use 5,184 bytes (42.19%). page_bytes describes the hub extent. Dynamic growth is not
implemented. Capacity checks fail cleanly rather than overrunning this extent.

Kmodules provide format2 named/versioned export manifests; the generic loader
validates their sizes, offsets, names, uniqueness and writable boundary, zeroes
BSS, maps sections and generates kernel-owned mediation entries. No module export
count/order or individual wrapper is hardcoded in the loader. The bootstrap policy
requires inthandle first, then configures mem, initializes scheduler and invokes
hwenum through names, then initializes mmio and publishes its region inventory. Clock initialization
follows, mapping HPET and calibrating/selecting the monotonic source. APIC
initialization owns controller masks/enable and spurious delivery; timer owns
its vector/calibration and one-shots. Event binds scheduler/timer dependencies.
No writable/executable leaf mapping is created, though HHDM writable aliases
remain; this is not protection against malicious kernel code. Mapping failures
halt boot; loading/unloading modules at runtime is not implemented.

Calls use the System V x86-64 ABI: six word arguments in RDI/RSI/RDX/RCX/R8/R9,
value in RAX and status in RDX (0 success, 1 failure). They use the caller stack.
The kernel gate validates the selected binding and calls the module's uniform
adapter. There is no process isolation or user-pointer validation.
Foreground mutable mechanisms are serialized by the ordered global/clock CPU locks. Thread
contexts execute independently and preempt only outside critical sections. IRQ
callbacks use preallocated notifications, acknowledge the APIC and may return
a running thread to its dispatcher. GS:0 identifies the current CPU.

Normal boot installs inthandle, draws the logo, creates the hardware snapshot,
prints hub entries and hardware descriptors, then transfers through smp.run to event.run on a managed BSP stack. The event
kmodule enables IRQs, dispatches ready callbacks, arms one-shot deadlines and
sleeps with HLT. Empty queues stop the timer; external device inputs stay masked.
Scheduler task records/copied payloads use that shared heap and remain owned
until results are reaped or their owning process is removed. Its run surface executes queued work on the caller
stack; it does not create threads. Separate threads/smp modules provide saved contexts
and AP startup, retaining the same ordinary kmodule method ABI.
Tests use separate kernels/media; no fixture module or fault injection is in the
production ISO. Source stacks/protocol responses remain live and cannot yet be
reclaimed.

References: [Limine protocol](https://github.com/limine-bootloader/limine-protocol/blob/trunk/PROTOCOL.md).

`hardware.map_mmio` installs writable device mappings only in reserved regions
or memory-map gaps, rejecting page-rounded overlap with RAM, firmware tables,
loader/kernel storage or framebuffer memory. It checks the UC PAT slot and uses
the physical-read window, upgrading existing UC read-only leaves as needed.
MMIO table creation is allocation-only. Clock initialization subsequently maps
HPET and enables its main counter without resetting it or programming HPET
comparators. APIC initialization maps local/IO APIC regions for controller
setup; x2APIC mode instead uses MSRs for the local controller. Device mappings
and page-table allocations persist until reboot.

Hardware inventory formatting is owned by hwenum.dump; main only looks up and
invokes the surface and has no dependency on hwenum descriptor definitions.

Splash validation/rendering and its packed logo live in splash.kmod, not in the
kernel image. Main passes the original Limine framebuffer response to splash.show
without interpreting it. Hub debug formatting is provided by hub.dump in hub
management. Boot protocol declarations/requests live in boot/limine.h/.c.

Managed stacks use independently allocated frames; their guards have no physical
backing. memory.map_owned/unmap_owned maintain per-reservation references and
invalidate local leaves and wait for remote TLB acknowledgements before release. The reservation table ABI is version 3;
entry.mappings counts CPU mapping references and DMA pins. SCRUB reservations
protect in-progress allocation/release while sanitization runs outside the lock. Empty page-table/ledger pages and
heap metadata backing remain permanent. See kmodules/shared/mem-pages.md for ownership,
release/rollback and alias limitations. Shootdown covers the shared address space; no process VM or PCID lifecycle exists.


SMP preparation allocates CPU-local GDT/TSS/IDT storage plus four guarded 32 KiB
stack bodies per CPU (dispatcher and three emergency stacks). Live CPU stack
ranges are exposed through smp.cpus; release rejects boot/emergency and active
thread stacks. Thread results become EXITED/reapable only after switching away.
The BSP initial Limine stack remains reserved after transfer; source images,
original paging, early static emergency stacks and heap backing are not reclaimed.


irq preallocates its vector-slot/notification storage from the boot heap. No IRQ
allocates a queue record. Clock validation uses preallocated SMP startup mailboxes;
accepted TSC mode allocates per-CPU previous-reading storage, retained for reuse.
Callback invocation releases its queue lock; private thread construction occurs
before the brief list-publication critical section. See kmodules/shared/profiling.md.

Stack maps/unmaps now use memory.map_batch/unmap_batch with at most 16 entries
per call and one acknowledged remote invalidation per batch. Construction and
teardown retain gap ownership while releasing the global lock between physical
operations and batches. Physical reuse still waits for invalidation completion.


`xhci.controllers` is a static registry header referencing dynamic controller/port
records outside the hub. Init publishes it empty; explicit controller open claims
PCI and creates owned DMA rings/contexts through existing facilities. Failed
quiescence retains pinned DMA; close only frees after halt/reset and IRQ drain.
See kmodules/shared/xhci.md for allocation sizes, generation and restart contracts.
