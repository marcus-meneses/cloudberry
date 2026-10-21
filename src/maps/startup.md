# Startup memory ownership (x86-64, hub ABI 3.0)


## Startup graph

The graph follows the production bootstrap. Indented branches show facilities
made available at that phase; device discovery and workers can continue
asynchronously after their initialization calls return.

```text
UEFI firmware
└── Limine (boot media)
    ├── kernel + cloudberry.cnf + foundational kmodule images
    └── kmain: validate handoff, reserve usable boot pages, publish hub
        ├── inthandle: essential fault handling
        ├── splash: validate UEFI framebuffer and draw centered logo
        ├── select Intel/AMD CPU backend; load remaining boot modules
        ├── initialize kernel facilities
            ├── mem / address spaces / config
            ├── scheduler / hwenum / MMIO / clock / APIC / xstate
            ├── SMP preparation / timer / threads / process / handles / event
            ├── start APs; initialize IRQ / IOMMU / PCI / ACPI / platform
            ├── NVMe / xHCI / USB / USB hubs / USB storage
            ├── partition / ext4-fs / iso9660-fs / SATA
            ├── bootstrap (prepare storage and provider startup facilities)
            └── IO gateway initializes independent backends through hub
                ├── applications: catalog admission and process lifecycle
                ├── HID: shared interface ownership and class report routing
                ├── display: temporary framebuffer backend
                └── filesystem: discover registered format backends
        └── start USB workers; enter managed SMP event loops
            ├── IRQ delivery → deferred device work → sleeping-thread wakes
            ├── bootstrap storage worker
            │   ├── wait for initial USB/hub discovery
            │   ├── find configured partition GUID over USB / NVMe / SATA
            │   ├── mount known ext4 driver volume; check boot origin
            │   └── load selected future modules from /system/kmodules/drivers
            └── provider startup worker (waits for volume readiness)
                ├── adopt ext4 at /; mount verified ISO9660 boot filesystem at /boot; admit catalog
                ├── bind provider PIDs to kernel backends before execution
                ├── launch configured providers in readiness order
                │   ├── keyboard → /@io → HID → shared USB interface
                │   ├── mouse    → /@io → HID → same or separate interface
                │   ├── display  → /@io → display kmodule
                │   ├── graphics → /@display
                │   ├── text     → /@graphics
                │   ├── filesystem → /@io → filesystem → format backend
                │   ├── identity → /@filesystem → protected binary account database
                │   ├── sessions → /@io → process membership and owner notifications
                │   └── applications → /@io → applications kmodule
                └── applications launches initial ordinary terminal
                    ├── /@keyboard and /@text
                    ├── /@filesystem and /@applications
                    └── stdin / stdout / stderr stream handles
```

Kmodules use hub surfaces; resource handlers run in isolated processes and expose application handles.
The keyboard and mouse handlers decode their own classes. A shared interface
has one kernel capture worker; report IDs and application-collection membership
route copied reports to the appropriate handlers, including both for mixed reports.


Limine supplies four-level paging, its initial stack, a physical memory map,
HHDM, modules, framebuffer, physical RSDP and an MP response. Original kernel/modules, bootloader
memory, ACPI regions and framebuffer remain occupied. Only original map type 0
may supply new boot pages; no fixed physical address is assumed.

| Purpose | Virtual address | Ownership |
| --- | --- | --- |
| Kernel | `0xffffffff80000000` onward | Limine segments |
| Initial stack and responses | Limine addresses | Retained bootloader memory |
| Source module images | Limine HHDM addresses | Retained executable/modules memory |
| Loaded kmodules | Sequential within 1 GiB from `0xffff920000000000` | Exact reserved pages; RX code and RW/NX data/BSS; guard page between images |
| Hub header | `0xffffffffa1000000` | One retained page, RO/NX |
| Hub methods | `0xffff900000000000` | Page-backed on demand, RO/NX |
| Hub table references | `0xffff900001000000` | Page-backed on demand, RO/NX |
| Hub providers | `0xffff900002000000` | Page-backed on demand, RO/NX |
| Private invocation metadata | `0xffff900003000000` | Read-only primary mapping; kernel writes through private alias |
| Kernel mediation stubs | `0xffff900004000000` | Page-backed on demand, RX |
| Private array/stub aliases | Each primary array address + `0x10000000000` | Same physical pages, RW/NX; kernel construction only |
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

Hub ABI3 separates stable method, table-reference and provider arrays from the
one-page header. Individual physical pages are mapped as those arrays, private
invocation metadata and RX mediation stubs grow. Each has a disjoint 16 MiB
virtual reservation; this consumes no corresponding physical RAM upfront. Records
are 56/72/96 bytes for methods/tables/providers, 16 bytes for invocation metadata,
and 32 bytes per stub. The old shared 16 KiB capacity and static metadata arrays
are gone. Header counters report mapped backing and fully committed slot capacity.
Current production adds process/handle/I/O methods, keyboard boot configuration
and bootstrap application-state references. Boot header counters give the actual
method/table counts; there is one provider record per loaded kmodule plus the kernel.
The isolated keyboard provider has its own process/capability records, not a hub
provider record. Growth does not move
published pointers. Read-only records, RX stubs and private RW/NX aliases remain
mapped until reboot, even after cooperative provider retirement.

Publication has a nonblocking writer token. Complete records precede release-
publication of counts; readers acquire those counts. Allocation/mapping failure
can retain construction pages but exposes no partial batch. Ledger/map critical
sections cover one page at a time, not bulk image copying or SMP invalidation.
Owner identities, pinned tables, queued/active caller leases and the safe retirement
boundary are defined in [the hub contract](../kernel/hub.md).

Kmodules provide format3 named/versioned export manifests; the generic loader
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
halt boot. Explicitly selected future format3 modules can load from the selected
ext4 driver volume at runtime, with hidden init/start, versioned dependency leases
and cooperative retirement. All existing modules remain Limine-loaded. Executable
mapping reclamation and published-table withdrawal remain future work; retired
images are retained rather than reused unsafely. See [module loading](../kmodules/shared/module-load.md).

Calls use the System V x86-64 ABI: six word arguments in RDI/RSI/RDX/RCX/R8/R9,
value in RAX and status in RDX (0 success, 1 failure). They use the caller stack.
The kernel gate validates the selected binding and calls the module's uniform
adapter. Kmodules share kernel trust. The separate process/handle gates validate
user mappings and copy payloads across isolated address spaces.
Foreground mutable mechanisms are serialized by the ordered global/clock CPU locks. Thread
contexts execute independently and preempt only outside critical sections. IRQ
callbacks use preallocated notifications, acknowledge the APIC and may return
a running thread to its dispatcher. GS:0 identifies the current CPU.

Normal boot installs inthandle, draws the logo, creates the hardware snapshot,
prints hub entries and hardware descriptors, then transfers through smp.run to event.run on a managed BSP stack. The event
kmodule enables IRQs, dispatches ready callbacks, arms one-shot deadlines and
sleeps with HLT. Empty queues stop the timer; device owners enable their owned routes and deferred
notifications wake sleeping workers.
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
and page-table allocations persist until reboot. `hardware.firmware_map` separately
permits ACPI reclaimable/NVS in the same UC window, while continuing to reject
usable RAM, loader/kernel storage and framebuffers; it does not assume an HHDM
mapping exists. [ACPI/platform inventories](../kmodules/shared/acpi.md) own stable
heap-backed records and generation-qualified PCI companion references.

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
protect in-progress allocation/release while sanitization runs outside the lock. Shared kernel page-table/ledger pages and
heap metadata backing remain permanent. See kmodules/shared/mem-pages.md for ownership,
release/rollback and alias limitations. Private lower-half roots and shared supervisor-only upper mappings now coexist;
see [mem spaces](../kmodules/shared/mem-spaces.md). Bootstrap captures the master
root and seeds absent upper PDPT slots before AP startup (at most 1 MiB shared
boot backing). Kernel mappers always edit that root; process leaves are U/S,
owned and W^X. Space-aware shootdowns synchronize with thread root selection.
Private roots/tables can be reclaimed after active CPUs and thread leases drain;
Protected processes enter ring 3 through private TSS entry stacks; checked copies
mediate handle frames through owned physical aliases. Process heaps and readonly
startup packets are owned lower-half mappings. PCID remains deferred.


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

`usb.devices` adds a live 40-byte registry header and dynamic 128-byte public
connector records, with private enumeration state and separately allocated
configuration/interface/endpoint data. One connector record is reused per observed
topology path. Each observed device has a private 4 KiB I/O bounce buffer; no caller
buffer is retained across USB surface return. Each controller has a preemptible
32 KiB worker stack. Core host/device metadata persists for safe restart; transport
DMA is released only after verified controller shutdown. See [USB core](../kmodules/shared/usb.md).

`usb.hubs` adds a static 32-byte registry header and dynamic 56-byte public hub
records, owned by usb-hub. Private connector records persist for reuse. Each
active hub dynamically allocates port state (16 bytes per advertised port) and a
32 KiB guarded worker stack; the manager has a separate 32 KiB guarded stack.
Port arrays are freed on rebind and exited worker stacks are reaped. Interrupt
bitmaps are bounded to 32 bytes by the USB descriptor's 255-port field. No USB
class allocation occurs in the IRQ handler.

Boot configuration now supplies eight `config.<key>` descriptor references,
including the real appended drivers-partition GUID. The parser still permits
omission of that optional schema item, but image building requires it.
Each descriptor is 24 bytes and points into config-owned heap storage. The eight
schema slots occupy 448 bytes, plus copied directory/module/provider strings
and allocator metadata. `filesystem.writable` is the added boolean descriptor.
Limine reports the source file's address/size; parsed pointers do not depend on
that source buffer. Config initializes after the boot heap, before hardware/APs.
`hub.publish_tables(refs,count)` validates a whole batch before publication under
the nonblocking hub writer token. See [config](../kmodules/shared/config.md) for types and lifetime.

USB storage publishes `usb_storage.devices`, a live 16-byte list header. Each
explicit open owns a record and 4 KiB staging buffer; each GPT slice owns 8 KiB
of scratch/cache plus its descriptors; each ext4 mount owns 12 KiB of buffers
plus geometry/state. These allocations are dynamic and reclaimed on successful
close/unmount. Parent descriptor retention prevents premature teardown.

The private ext4 allocation/journal engine is linked into the boot-loaded ext4-fs
image; it adds no hub tables. Ordinary mounts dynamically allocate a block cache
and per-device/journal state, with dynamically owned mount/device registry nodes.
File-write transactions are bounded at 64 KiB. Failed dirty transactions retain
owned state and parent descriptors until reboot; normal unmount reclaims them.

`bootstrap.applications` publishes a stable 24-byte aggregate header. Private
40-byte process startup records plus copied bundle/readiness strings are released
only after process reap. Per-process snapshots copy 16 bytes and expose no owned
record pointers. Generic startup packets have a 24-byte header and 16-byte file
prefixes, followed by inline names/data. Their backing becomes readonly/NX owned
user pages and lives until process reap; temporary kernel assembly buffers are
freed after loading. The bootstrap module does not interpret handler configuration.


Application runtime adds an immutable copied OS installation catalog owned by the
applications kmodule, selected at first trusted provider launch. Each managed child has
one dynamically allocated I/O descriptor, preallocated completion/owner-exit
mailboxes and a kernel wait worker with an owned stack until completion is
consumed. Reaping execution frees the child mappings/stacks and worker; retaining
an exit result keeps only the descriptor/owner-exit notification until release.
The privileged userland `/@applications` provider separately owns dynamic child
and deferred-wait records through its private `/@memory` handle. Its readonly
startup catalog and initial-launch data are copied by the existing process loader.
Neither catalog pointers nor kernel process records cross into caller messages.


## Console runtime

Bootstrap supplies the validated Limine framebuffer metadata to the I/O mechanism.
Only the catalog-bound display provider can invoke framebuffer writes; userland
gets copied dimensions/pixels, never its mapping. Graphics allocates a display-sized
ARGB backbuffer plus dynamic owner-local off-screen surfaces through private memory.
Each surface has affine/clip/composition state, generation and dirty bounds.
Owner-exit mailboxes reclaim abandoned surfaces. Text allocates a dimensions-derived
cell grid; the terminal separately owns command parsing and stream routing.

Pipes allocate 16 KiB buffers on demand; endpoint capabilities and wait registrations
are separate. Launch binds new child-local directional capabilities before start.
Ordinary process CPUs are selected by the kernel, while hardware providers retain
required BSP affinity. This does not add thread migration. File streams retain
copied paths/offsets and join BSP workers for ext4 operations; same-mount callers
wait by cookie without retaining a volume pointer while blocked.

The filesystem manager separately allocates its backend registry and mount
records/path strings. It pins registered backend query/invocation owners until
reboot. A mount retains its backend cookie and tracks active calls and open stream
references; open stream objects retain mount IDs rather than ext4 cookies.
Bootstrap adopts its existing ext4 mount at `/`; the boot owner retains unmount
responsibility. Display shadow and HID transfer state belong to their own modules.

Identity startup loads existing binary records; only the image builder provisions
them. Applications starts root with explicit kernel-owned credentials. Ordinary
launch inherits the caller's credentials before start. The installed identity
provider may replace a verified caller's identity through its restricted IO
surface; runtime filesystem operations enforce that identity alongside manifest
and installation policy. No account or directory migration runs at boot.

The Limine filesystem stores kernel/config/modules at its root; public paths are
/boot/kernel.elf, /boot/cloudberry.cnf and /boot/kmodules. The host source boot/
directory remains source/configuration, not an ext4 completion directory. ext4
contains only an empty /boot mountpoint and /system/kmodules/{drivers,helpers}.
Boot mounting requires verified parent-disk identity; FAT ESP mounting is separate.

Explicit account/home management runs after startup through `/@identity`.
`accounts login` authenticates the launcher, begins `/@sessions`, resolves the
existing home and launches terminal/app with inherited UID/GIDs and SID. The
keyboard owns a dynamic focus stack; direct-input children restore their parent.
Session records live in the sessions provider; process membership/ancestry are
kernel-owned values, not borrowed pointers. Logout requests cooperative shutdown.

`init-dev-disk` clones the complete built hybrid image into separate persistent
media once. `run-dev` uses that initialized image without rebuilding/copying it.
Candidate runs remain read-only or disposable snapshots. Both still load all
foundational kmodules through Limine; the appended ext4 holds system/user state.
