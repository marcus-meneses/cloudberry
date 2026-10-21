# Cloudberry v2

The active source tree boots through Limine/UEFI into x86-64 long mode.
Its current foundation includes independently loaded kernel modules, a versioned
C ABI hub, owned memory/guarded stacks, hardware/MMIO discovery, clock/RTC,
interrupt ownership, APIC timers, deadline callbacks, preemptible kernel threads, initial SMP execution, measured critical sections,
validated SMP clock selection, deferred device IRQ delivery, PCI/DMA ownership,
NVMe/AHCI block read/write/flush, xHCI transport, USB core and external hub class support,
with initial Intel VT-d/AMD-Vi DMA translation. Target hardware is modern (2016+) Intel/AMD x86-64.

Kmodules are coupled kernel methods with shared supervisor-only mappings. They are not
resource handlers, user processes or isolation boundaries. Existing method calls
stay synchronous; a separate threads API explicitly creates saved contexts.
Private lower-half address spaces can be attached to kernel threads through
request ABI2, with ownership, leases, context-switch root selection and targeted
TLB invalidation. [Protected processes](kmodules/shared/process.md) now load static
ELF executables from ext4, enter ring 3 and contain user faults. Application
handle transport now uses copied, manifest-authorized capability calls; the first
keyboard handler now runs as an isolated disk-loaded process. See [mem spaces](kmodules/shared/mem-spaces.md).
The [application handle contract](interfaces/README.md) now defines the C boundary
and the first provider, a privileged [keyboard handler](handlers/keyboard/README.md)
publishing `/@keyboard`. Headers are compile-tested in C/C++20; the [C system library and handle broker](kmodules/shared/handles.md) implement initial boot-policy authorization and isolated request/reply.
The provider decodes descriptor-defined keyboard reports with bundled IBM/ABNT2 layouts.
Bootstrap reads generic provider descriptors/startup files; keyboard interprets
its own config.cnf. Lifecycle owners stop/reap explicitly, and status snapshots
contain no reclaimable record pointers.
CPU and IOMMU alternatives are selected at boot behind shared contracts; see
[platform portability](kmodules/shared/portability.md).

Application handles now provide process-local acquisition, filtered reflection,
self-owned memory and scoped I/O. Privileged HID decoding runs in that isolated keyboard provider. The xHCI transport supplies root/downstream
slot/endpoint and asynchronous transfer operations for USB core and hub policy. NVMe and AHCI are kernel-only block backends. The superseded implementation is preserved locally in `../.src_old/`, excluded
from Git and from this build.

## Build and run

```sh
make -C src build
make -C src run
make -C src run QEMU_ACCEL=kvm QEMU_CPUS=4
# Temporary writable boot snapshot for terminal file redirection:
make -C src run QEMU_ACCEL=kvm QEMU_CPUS=4 QEMU_BOOT_WRITABLE=1
make -C src debug
```

Candidate outputs directly replace bin/kernel.elf and bin/cloudberry.iso.
The existing .toolchain GCC/Limine installation is reused. QEMU, OVMF, xorriso
and Python/Pillow must already be installed; no host bootloader changes occur.
OVMF=/path/to/OVMF.fd overrides firmware, QEMU_DISPLAY=none runs headless,
QEMU_IOMMU=intel|amd|none selects an emulated IOMMU (default none),
X2APIC=1 asks Limine to prefer x2APIC when available (default 0),
and debug waits for GDB at localhost:1234. The archived implementation is outside the active build.

The VM has Q35/PCIe, 512 MiB RAM, one CPU by default, a 1 GiB NVMe disk and
1 GiB SATA disk on AHCI, plus xHCI USB keyboard/mouse and read-only USB boot
media. QEMU_CPUS=N selects N cores in one socket; the kernel now starts them.
QEMU's ide-hd,bus=ide.0 names the AHCI SATA target, not an added IDE/PATA driver.
No PS/2 input or legacy ATA driver is developed. VGA supplies the temporary UEFI
framebuffer; native display support will supersede it. Port 0xe9 is a diagnostic
sink, not a serial driver.

bin/cloudberry-nvme.img and bin/cloudberry-sata.img are blank sparse disks created
once during build and retained by clean. Boot never formats or migrates them.
Storage backends expose explicit kernel surfaces; normal bootstrap now scans
supported disks for the configured driver-volume GUID. xHCI initialization publishes an empty
owned registry; normal boot starts USB-core workers and the hub-class manager to claim
controllers and enumerate root/downstream devices. Boot-keyboard HID runs in the keyboard provider. USB storage and ext4 remain boot-loaded modules; writable mounts require a writable medium and explicit configuration.

## NVMe and SATA block access

Both backends remain Limine-loaded and take explicit device selection. NVMe
opens a hardware index and optional namespace ID, then `nvme.block` exports its
common block descriptor using the returned cookie. Multiple controllers and
namespaces can stay open. SATA opens a hardware index and port through
`sata.open`; multiple ports share controller ownership without sibling resets.
Both backends allocate instance state from the kernel heap and serialize commands
per controller. Zero compatibility selectors fail when ambiguous. Feed either descriptor to `partition.find` using the GUID bytes
exposed by `config.drivers.partition_guid`, then mount the bounded slice through
`ext4_fs.mount`. No filesystem logic lives in the transport drivers.

Normal startup starts the [bootstrap coordinator](kmodules/shared/bootstrap.md),
which scans supported devices for a unique configured GPT partition GUID and
mounts it read-only unless `filesystem.writable=true` and the medium supports writes. Blank disks are skipped; corrupt GPT, failed transports and
duplicate matches fail selection. Copied Limine boot identity is correlated when
available; identifying a configured partition does not itself prove boot provenance.
Current modules stay in the Limine boot partition. The ext4 directory is reserved
for explicitly selected future modules, loaded through the
[mediated kernel loader](kmodules/shared/module-load.md). `drivers.modules` defaults
to empty; all existing modules remain in Limine.

```sh
make -C src test-block-boot BLOCK_BACKEND=nvme QEMU_CPUS=4 QEMU_IOMMU=amd
make -C src test-block-boot BLOCK_BACKEND=sata QEMU_CPUS=4 QEMU_IOMMU=intel
# Add BLOCK_MULTI=1 for two controllers and two namespaces/ports on the first.
```

These focused tests actually boot from the selected transport, use only disposable
media, exercise GPT/ext4 journaled writes and remount, verify IRQ completion,
bounds/lifetime pins and timeout/reset/stale cookies, then run independent host
filesystem checks. See [NVMe](kmodules/shared/nvme.md) and [SATA](kmodules/shared/sata.md).

Keyboard capture keeps idle interrupt-IN receives armed, propagates unexpected
capture-worker failure to callers, and avoids pending-read page churn. Clock and
watchdog HPET reads validate live 32-bit halves and zero samples. Host MMIO
traces identify QEMU 6.2 MTTCG dispatching HPET loads to IOAPIC during AHCI setup;
the upstream emulator fix is required for reliable MTTCG qualification. Two
earlier four-CPU TCG console runs passed, including idle/resume; subsequent runs
exposed an xHCI cancellation/input failure. Recurring idle cancellation is now
removed, and each provider gets its own readiness budget after loading.
Provider startup failures include provider/phase and process details. The latest
four-CPU TCG console fixture passes both fixes. Focused four-CPU KVM startup
and rendering checks now pass; full native console and long-duration acceptance
remain separate. See the
[roadmap investigation](ROADMAP.md) and [keyboard details](handlers/keyboard/README.md).

Text rendering now batches glyphs through graphics, identity self-blits avoid a
full-screen temporary allocation per scroll, and idle terminal input blocks on
notifications. Uniform damaged regions use display fills instead of pixel RPCs.
The ext4 metadata cache retains normal directory working sets, and unchanged
builds no longer relink/repackage the boot image. The diagnostic TCG comparison and remaining synchronous disk
mutation latency are recorded in the roadmap; native performance is not inferred.
When shared TSC is unqualified, diagnostic HPET duration sampling is now 1/64
per CPU/domain rather than several MMIO reads around every lock. Counts and
regressions still cover all operations; system clock and watchdogs are unchanged.
Sampled nanosecond maxima describe observed samples. The focused four-CPU KVM
startup/timing replay now passes: prompt in 6.994 s, shared TSC qualified and no
unavailable duration samples. See the roadmap for measured text/disk latency
and the distinction from broader native acceptance.

## Boot image layout

`bin/cloudberry.iso` is a hybrid ISO/GPT disk image: ISO9660 supplies the kernel,
configuration and foundational kmodules, an embedded FAT EFI System Partition
starts Limine, and GPT partition 4 is a separate 512 MiB ext4 filesystem named
`Cloudberry system`. Xorriso also describes gap partitions around these regions.
The ext4 root follows the [filesystem namespace](maps/filesystem.md): flat apps
in `/system/apps`, providers in `/system/handlers`, root state in `/system/root`,
and `/system/kmodules` for future disk-loaded modules. `/boot` is the read-only
ISO9660 boot filesystem mount; its ext4 mountpoint starts empty. `/system/kmodules/drivers` is reserved for
explicit future drivers; it contains no application bundles in candidate images. Limine 10 reads FAT/ISO9660, not ext4. The boot-loaded `usb-storage`,
`partition` and `ext4-fs` modules provide guest access and explicitly requested
journaled writes. Bootstrap selects the unique driver volume and can load
explicit future modules; the default image selects none.

`tools/build-boot-image.py` formats only generated regular files during image
building and assigns the partition GUID from `boot/cloudberry.cnf`. The packaged
config and GPT therefore agree across rebuilds. `BOOT_DRIVERS_MIB` can increase
capacity (minimum 512). Images built from the same config share this identity;
an installer must assign a fresh GUID for an independent installation. The ext4
filesystem UUID is separate from its GPT partition GUID.

Generated filesystem staging and `layout.json` live in `bin/cloudberry.iso.parts/`
and are removed by `clean`. The entire boot image remains read-only in QEMU;
persistent NVMe/SATA images are unaffected. No mount, loop device, host-disk
formatting or boot-time initialization is involved. `mke2fs` (e2fsprogs) and
`sgdisk` (gdisk) are build dependencies. `make -C src test-boot-image` verifies an
isolated image; ordinary boot tests use the same image builder.

## Boot configuration

[boot/cloudberry.cnf](boot/cloudberry.cnf) is plain newline-separated `key=value`
text. Limine loads it into memory with the `cloudberry-config` tag; the core
passes its address/size to the separate [config kmodule](kmodules/shared/config.md).
The module validates a typed schema, copies values into owned storage and
atomically publishes `config.<key>` descriptor references through the hub.
`boot.show_inventory` controls hardware listing. Bootstrap selects the driver
volume; future module selection remains explicit. `boot.providers` selects
provider bundles; each generic provider.cnf lists executable/manifest/startup files,
and keyboard/config.cnf selects the keyboard layout. USB storage applies the
configured command timeout. The source
configuration supplies the GPT identity of the appended ext4 driver partition; image preparation uses that exact GUID. Missing/duplicate files and invalid configuration
fail startup with a diagnostic. Parsing itself performs no disk access; bootstrap
selection adds readonly access, with no formatting or migration.

## Execution and ownership

Main checks long mode, publishes the hub and delegates loading. The loader
initializes inthandle, loads splash and immediately draws the logo on the inherited
framebuffer, then selects the CPU backend, loads the remaining shared modules and
returns for module startup. Limine quiet mode suppresses its ordinary screen output;
firmware output before Limine remains outside kernel control. Hwenum owns hardware inventory formatting;
main invokes hwenum.dump without interpreting descriptors. Splash owns validation
and centered logo rendering. Modules carry named/versioned export manifests;
payload definitions stay in C headers. Hub ABI3 has a stable one-page header and
separate method/table/provider arrays and stubs backed by pages on demand.
Published pointers never move. [Hub ownership and draining](kernel/hub.md)
defines leases, provider state and retirement; bundled format3 images reject
incompatible old layouts before loading.

The [callback scheduler](kmodules/shared/scheduler.md) supports mediated methods or
copied deferred interrupt frames, process ownership, deadlines and retained
results. It executes callbacks synchronously. [Threads](kmodules/shared/threads.md)
provide guarded stacks, copied payloads, CPU pinning, yield/block/wake,
results/reaping and 2 ms timer quanta outside kernel critical sections.
Completion becomes reapable only after leaving the thread stack.

[SMP](kmodules/shared/smp.md) establishes per-CPU GDT/TSS/IDT, guarded dispatcher and
emergency stacks, calibrated local timers, wake IPIs and TLB acknowledgements.
The BSP transfers from the retained Limine stack to its managed event stack.
APs execute pinned threads and idle on interrupts. The BSP event loop dispatches
one thread context per step; separate preemptible workers execute scheduler
callbacks and IRQ notifications. Blocked-thread deadlines arm local timers; empty
queues without deadlines stop their timers.
External inputs remain masked until an owner enables an explicit IRQ route.

Shared mutable mechanisms use ordered global and clock locks with local IRQs
disabled. Ordinary timestamp reads no longer acquire the allocator/queue lock. Raw mem primitives require caller synchronization; mediated
mem/memory, clock, PCI, mmio, hwenum and scheduler mutations are serialized.
Hub publication instead uses its own nonblocking writer token, with page-sized
ledger/map critical sections and bulk construction outside the CPU lock.
Stack construction/teardown use short transitions and mapping batches of at most
16 pages, with one shootdown per batch. Exited thread cleanup runs outside its
list lock. Callbacks execute outside their queue lock on preemptible workers;
a long callback still delays its own worker queue. Private thread construction
also occurs outside its publication lock. This is a correctness
baseline, with latency/scalability work still needed; it is not unrestricted
kernel preemption. New kmodules must define their critical/reentrancy contracts.
Threads eagerly preserve x87/SSE and available AVX through the shared xstate
kmodule; dispatcher/ISR infrastructure still builds with mgeneral-regs-only.
Forced thread cancellation, CPU hotplug, migration and load
balancing are not implemented. USB device/hub hotplug is implemented.

Single-CPU clock selection prefers a validated invariant TSC with HPET fallback.
SMP requires a healthy HPET reference, validates per-CPU frequency with HPET
brackets and bounds cross-CPU offsets with TSC-only exchanges before enabling
TSC timestamps. Offset/drift/uncertainty or capability
failures retain HPET; per-CPU regression checks and an HPET health guard restore
fallback on later instability. Migration remains disabled. CMOS/NMI policy stays owned by inthandle. The [memory map](maps/startup.md)
describes reservations, mapping references, release/reuse and cross-CPU TLB
invalidation. Heap backing, shared kernel page tables and original boot allocations
remain reserved; private tables are reclaimed when their inactive space is destroyed.
Owned private page tables support kernel threads and protected user processes;
normal exit/fault cleanup waits for actual execution and I/O quiescence.

A successful boot prints module/table names and addresses, hardware descriptors,
BOOT OK, then "managed BSP event stack active". The candidate creates scheduler and IRQ deferred workers, a USB bus worker per
controller, the hub-class manager and a worker per discovered external hub. It
also launches the production keyboard bundle after the ext4 volume is ready.
It submits no demo/test tasks; idle workers block on notifications or deadlines.

The [profiling surfaces](kmodules/shared/profiling.md) report lock wait/hold/attribution,
managed IRQ-off intervals and runnable waiting; raw local TSC ticks include host
vCPU pauses. [Device IRQ routing](kmodules/shared/irq.md) adds exclusive IOAPIC GSI routes,
MSI/MSI-X messages, preallocated/coalesced notifications and foreground drain/rearm.
It does not program PCI capabilities, resolve ACPI PCI INTx routing or initialize
USB/storage devices. Controller-specific work belongs to NVMe/AHCI/xHCI drivers;
USB core owns enumeration; usb-storage provides BOT/SCSI read/write/flush. Descriptor-driven keyboard/mouse providers and shared HID demultiplexing are implemented. IRQ affinity is CPU0.
Production boot creates no test routes or periodic device polling. ACPI reserves
its SCI route masked. The [ACPI/platform modules](kmodules/shared/acpi.md) load
DSDT/SSDT AML, publish boot device/resource inventories and attach stable firmware
companions to PCI records. SCI delivery, runtime namespace reconciliation, embedded
controllers, platform drivers and PCI INTx firmware routing remain deferred.

## Current assessment (2026-10-19)

Latest focused native checks pass with four KVM CPUs: prompt observed at 6.994 s
(including 3.960 s before kernel entry), and five-sample dense-scroll median reduced
from 311.815 ms to 8.322 ms after eliminating steady-state framebuffer reads.
Clear measured 6.521 ms. The startup profile qualified shared TSC; the later
rendering boots selected HPET after uncertainty rejection. Clock qualification
therefore remains variable across boots. These focused results do not replace
full-console, long-duration or physical-device acceptance. Small synchronous ext4
mutations still measured 57–79 ms in the accepted native profile. See the latest
[rendering evidence](ROADMAP.md#native-region-copy-follow-up--2026-10-19).

The kernel foundation now includes storage selection, future disk-module loading
and protected native processes. The disk-loaded `/@keyboard`, `/@mouse` and `/@applications` providers run through copied,
manifest-authorized capability calls. The text → graphics → display chain supplies a shared boot console. Display currently
uses Limine’s framebuffer behind its replaceable contract. The ordinary terminal
launches installed utilities, routes stdin/stdout/stderr and supports `=>`, `->`,
and `|>`. See [terminal](apps/terminal/README.md), [graphics](handlers/graphics/README.md),
and [streams](sdk/streams.md).
The application lifecycle provider launches
ordinary installed native applications and reports exit/fault completion without
blocking its receive loop. Both providers run in isolated privileged userland.

| Area | Implemented | Remaining boundary |
| --- | --- | --- |
| Execution | Preemptible pinned threads, SMP, xstate, static ELF ring-3 processes, isolated faults and owned reap | One user thread per process; kernel chooses initial CPU for ordinary apps; no demand growth, dynamic linking, migration or forced cancellation |
| Applications | Privileged userland launch/info/wait/stop/release, immutable installed identity, argv/cwd, owner notifications and copied terminal results | Read-only image trust; no installer/signatures or forced termination or automatic manager recovery |
| Handles | C system library, copied calls, immutable boot manifests, owner-local tokens, filtered reflection, blocking delivery, scoped memory/I/O | Build-owned installation bindings and launcher/owner policy; one boot input domain; no installer/signature enforcement, approval UI, automatic rebind, vows or C++ wrappers |
| Console | Privileged display/graphics/text providers, software primitives, owned surfaces/damage, ordinary terminal and stream bindings | Temporary framebuffer backend, basic ASCII font, one shared console and synchronous rendering; no native GPU or window/compositor model |
| Devices | PCI/ACPI companions, platform inventory, DMA/IOMMU ownership | Boot inventory; no general driver manager, SCI reconciliation or physical qualification |
| USB | xHCI, root/nested enumeration, interface claims, interrupt hotplug, subtree teardown | BSP execution, 4 KiB submissions/configurations, descriptor-driven keyboard/mouse providers with shared-interface demultiplexing implemented; broader HID/UAS and physical qualification remain open |
| Storage | NVMe, AHCI SATA and USB BOT block read/write/flush; GPT slices; journaled ext4 mutations and an application-facing filesystem provider with file streams | Explicit device/namespace/port selection; multiple active controllers/namespaces/ports; unique-GUID bootstrap selection with boot-origin reporting; parallel command queues and physical durability remain open |
| Modules | Format3 images, dynamic ABI3 hub, stable entries, provider ownership and cooperative call/lease retirement | Current modules boot-pinned; explicit disk loading with versioned dependencies and hidden init/start; table withdrawal and executable reclamation remain future work |

Hub lifecycle and descriptor closure now pass focused validation. Class-only
stop/start reclaims existing hubs while USB core and root attachment identity
remain active. Tests cover blocked child I/O, fresh descendant identities and
worker/manager stack reclamation over two cycles. Complete descriptor reads are
validated against their prefix before configuration. Automatic failed-provider
recovery and executable image unloading remain deferred. See the
[hub contract](kmodules/shared/usb-hub.md) and [next priorities](ROADMAP.md#next-priorities).

The current native handle baseline, `20261018T153448.340455Z`, passes nine
AMD-host KVM boots across one/four CPUs and both emulated IOMMU models, including
concurrent-client, deadline and resource-cleanup checks. Qualified TSC remains
stable during measurement. Raw acquisition medians are about 11.0–11.2 µs,
empty echo 102–126 µs and page allocation/free 38–39 µs, including a roughly
7.7 µs handle-based clock bracket. All measured medians improve against the
previous session; these are observed host results, not hard bounds or proof that
request storage alone caused the full reduction. See the
[accepted native comparison](ROADMAP.md#uninstrumented-native-handle-acceptance-2026-10-18).

Small gate inputs use 256 bytes of per-call stack scratch. Larger temporary
frames use the heap; retained metadata and immutable input share one owned
request allocation. This removes 1,288 of 5,782 broker allocations in the benchmark
(22.3%). Replies keep independent lifetime. [Isolated phase profiling](tests/handle-profiling.md)
separates broker work, copies, blocking and handoff; it is absent from the candidate
and materially affects timings. The current performance/concurrency follow-up is
closed; physical and native Intel acceptance remain open. Application lifecycle
work now adds an installed userland launcher, argv/cwd startup metadata, deferred
waits, cooperative stop and owner-scoped retained results. Isolated four-CPU
AMD-host KVM lifecycle correctness acceptance passes with stable qualified TSC.
See the
[applications provider](handlers/applications/README.md) and [built-in apps](apps/README.md).

The hub grows within reserved virtual windows and allocates physical pages only
as needed. Current production includes process, handle and I/O methods plus a bootstrap
application-state table; counts are reported on each boot. Capacity and committed backing are
reported by the header rather than as a percentage of a fixed 16 KiB allocation.
Provider records expose ownership and lifetime. All current boot modules are
pinned; selected future providers can drain calls and leases and retire without
image unmapping. Published-table owners remain pinned until table-reader lifetime
tracking exists. These mechanisms do not establish a throughput/latency bound.

Current QEMU boot media is a USB BOT disk backed by the hybrid ISO, read-only by default.
`QEMU_BOOT_WRITABLE=1` uses a temporary writable snapshot; writes are discarded on
VM exit and the ISO is unchanged. `filesystem.writable` permits writable ext4
mounting when the medium supports it. The filesystem provider blocks app mutations
under `/boot` and `/system`, except catalog-bound writes beneath the caller’s own
root-session app-data directory. Per-user home/private-container policy is also
enforced on every operation, including delegated file streams.
An explicit `usb-bot`/`scsi-hd` attachment preserves its 512-byte GPT layout;
the readonly medium must not be inferred as a 2048-byte optical device. USB storage exposes read/write/flush on suitably writable media; GPT and ext4 consumers can access the appended
driver partition by its configured GUID. Normal startup runs the bootstrap
coordinator, which claims supported transports as needed and mounts the unique
configured driver volume according to that policy. Other mounts remain explicit caller-owned
operations. Copied Limine metadata reports boot-origin match/different/unknown
separately from driver-volume selection; missing identity is never inferred from
enumeration order. Native/physical USB and real
high-speed TT/SuperSpeed hub qualification remain pending; earlier AMD KVM
clock/SMP acceptance does not qualify the new USB stack.

Ext4 now supplies creation, growth, append, truncate, deletion, directories and
no-replace rename with journaling. The configured production mount requests writes when supported; the ordinary QEMU boot medium
remains read-only; explicit writable mounts replay an internal journal and require
block write/flush support. The private pinned lwext4 engine runs behind the same
frontend for writable and ordinary read-only mounts. Explicit checksum-seed layouts
currently retain the qualified read-only parser and cannot be mounted writable.
Conservative flush-after-each-write ordering is implemented before batching.
Dirty transaction/I/O failures poison an instance; explicit root disconnect can
drain and discard it without writes. USB removal automatically tears down mounts,
slices and unreferenced BOT records. Retained dead slices reject I/O until their
final release. Replacement devices require fresh cookies and an explicit mount,
which replays the journal. See [USB storage](kmodules/shared/usb-storage.md),
[GPT partitions](kmodules/shared/partition.md) and [ext4](kmodules/shared/ext4-fs.md).
All remain boot-loaded; production selects the configured unique driver volume according to writable policy and performs
no formatting or migration. Test fixtures never enter the candidate image.

## Focused validation

```sh
make -C src test-portability
make -C src test-threads-state test-threads-smp
make -C src test-process-elf test-manifest test-handles test-keyboard test-process-boot
make -C src BUILD=../bin/tests/isolated test-clock test-tsc-validation test-irq test-timer test-apic test-pages
make -C src test-inthandle-faults
make -C src test-ext4-write test-ext4-runtime
make -C src test-storage-write-boot QEMU_CPUS=4 QEMU_IOMMU=amd
```

Boot/fault targets build under bin/tests/isolated with disposable disks. They
never run candidate media or attach the developer's persistent disks. Test-only
probes and faults never enter production images. test-threads-smp boots one and
four CPUs under TCG; it checks preemption on each CPU, callback fairness, execution profiling, real HPET/IOAPIC
level IRQs and deferred delivery,
block/wake, critical-section rejection, concurrent allocation, remote TLB reuse,
reaping and the managed BSP stack transfer. Host tests check fake xAPIC/x2APIC
register behavior, clock failures/continuity and page rollback. Current four-CPU
KVM runs on Ryzen 5 5500U pass the AMD CPU backend, guest x87/SSE/AVX preemption
and both emulated Intel VT-d/AMD-Vi NVMe paths. Latest runs (2026-10-15) report
x2APIC mode 2 and qualify shared TSC on all four CPUs at a 250 ms sampling
window. TSC is active at approximately 2.0963 GHz; HPET remains its health reference
and fallback. Native Intel CPU and physical-device qualification remain open.

See [kmodule contracts](kmodules/README.md) and the [roadmap](ROADMAP.md) for
completed milestones, current limits and recommended next priorities.

## Directory tree

```text
src/
├── assets/
│   └── cloudberry.png
├── boot/
│   ├── cloudberry.cnf
│   ├── limine.c
│   ├── limine.conf
│   ├── limine.h
│   ├── linker.ld
│   ├── memory.c
│   ├── memory.h
│   ├── run-qemu.sh
│   ├── spaces.c
│   └── spaces.h
├── kernel/
│   ├── cpu.c
│   ├── cpu.h
│   ├── cpu-profile.h
│   ├── hub.md
│   ├── main.c
│   ├── mediate.S
│   ├── modules.c
│   ├── modules.h
│   ├── surfaces.h
│   ├── spaces.c
│   └── spaces.h
├── common/
│   ├── hid-report.c
│   └── hid-report.h
├── handlers/
│   ├── applications/ (README.md, applications.c, manifest.json, provider.cnf)
│   ├── sessions/ (README.md, sessions.c, manifest.json, provider.cnf)
│   ├── identity/ (README.md, identity.c, auth.inc, home.inc, format.c, store.c, store.h, crypto/, manifest.json, provider.cnf)
│   ├── display/ (README.md, display.c, manifest.json, provider.cnf)
│   ├── filesystem/ (README.md, filesystem.c, manifest.json, provider.cnf)
│   ├── graphics/ (README.md, graphics.c, glyphs.h, raster.c, raster.h, font.h, manifest.json, provider.cnf)
│   ├── text/ (README.md, text.c, manifest.json, provider.cnf)
│   ├── mouse/ (README.md, mouse.c, hid.c, hid.h, manifest.json, provider.cnf)
│   └── keyboard/
│       ├── README.md
│       ├── config.cnf
│       ├── provider.cnf
│       ├── hid.c
│       ├── hid.h
│       ├── keyboard.c
│       ├── report.c
│       ├── report.h
│       ├── layouts/
│       │   ├── abnt2.json
│       │   └── ibm.json
│       └── manifest.json
├── interfaces/
│   ├── README.md
│   ├── admission.h
│   ├── applications.h
│   ├── credentials.h
│   ├── sessions.h
│   ├── identity.h
│   ├── identity-format.h
│   ├── installations.h
│   ├── display.h
│   ├── graphics.h
│   ├── text.h
│   ├── stream.h
│   ├── filesystem.h
│   ├── handle.h
│   ├── io.h
│   ├── memory.h
│   ├── system.h
│   ├── startup.h
│   ├── keyboard.h
│   └── mouse.h
├── kmodules/
│   ├── amd/
│   │   ├── cpu-amd.c
│   │   ├── cpu-amd.json
│   │   ├── iommu-amd.c
│   │   └── iommu-amd.json
│   ├── intel/
│   │   ├── cpu-intel.c
│   │   ├── cpu-intel.json
│   │   ├── iommu-intel.c
│   │   └── iommu-intel.json
│   ├── shared/
│   │   ├── acpi/
│   │   │   ├── vendor/
│   │   │   │   ├── uacpi/
│   │   │   │   │   └── …
│   │   │   │   └── README.md
│   │   │   ├── acpi.c
│   │   │   ├── association.c
│   │   │   ├── association.h
│   │   │   ├── module.ld
│   │   │   ├── runtime.c
│   │   │   └── runtime.h
│   │   ├── ext4-fs/
│   │   │   ├── compat/
│   │   │   │   ├── inttypes.h
│   │   │   │   ├── stdlib.h
│   │   │   │   └── string.h
│   │   │   ├── vendor/
│   │   │   │   ├── lwext4/
│   │   │   │   │   └── …
│   │   │   │   ├── README.md
│   │   │   │   └── cloudberry.patch
│   │   │   ├── engine.c
│   │   │   ├── engine.h
│   │   │   ├── runtime.h
│   │   │   └── strings.c
│   │   ├── acpi.h
│   │   ├── acpi.json
│   │   ├── acpi.md
│   │   ├── apic-platform.h
│   │   ├── apic-x86.c
│   │   ├── apic.c
│   │   ├── apic.h
│   │   ├── apic.json
│   │   ├── apic.md
│   │   ├── block-util.h
│   │   ├── boot-origin.h
│   │   ├── bootstrap-apps.c
│   │   ├── bootstrap-apps.h
│   │   ├── bootstrap-load.c
│   │   ├── bootstrap-load.h
│   │   ├── bootstrap.c
│   │   ├── bootstrap.h
│   │   ├── bootstrap.json
│   │   ├── bootstrap.md
│   │   ├── block.h
│   │   ├── clock-platform.h
│   │   ├── clock-x86.c
│   │   ├── clock.c
│   │   ├── clock.h
│   │   ├── clock.json
│   │   ├── clock.md
│   │   ├── config.c
│   │   ├── config.h
│   │   ├── config.json
│   │   ├── config.md
│   │   ├── cpu-ops.c
│   │   ├── cpu-platform.h
│   │   ├── cpu-topology.c
│   │   ├── cpu-tsc.c
│   │   ├── device.h
│   │   ├── diagnostics.h
│   │   ├── event.c
│   │   ├── event.h
│   │   ├── event.json
│   │   ├── event.md
│   │   ├── ext4-backend.inc
│   │   ├── iso9660-fs.c / iso9660-fs.json / iso9660-fs.md
│   ├── ext4-fs.c
│   │   ├── ext4-fs.h
│   │   ├── ext4-fs.json
│   │   ├── ext4-fs.md
│   │   ├── hwenum-module.c
│   │   ├── handles-wait.inc
│   │   ├── handles-stream.inc
│   │   ├── handles.c
│   │   ├── handles.h
│   │   ├── handles.json
│   │   ├── handles.md
│   │   ├── applications-access.inc
│   ├── applications.c
│   │   ├── applications.h
│   │   ├── applications.json
│   │   ├── display.c
│   │   ├── display.json
│   │   ├── filesystem.c
│   │   ├── filesystem.h
│   │   ├── filesystem.json
│   │   ├── filesystem.md
│   │   ├── hid.c
│   │   ├── hid.json
│   │   ├── hid-reports.c
│   │   ├── hid-reports.h
│   │   ├── io.c
│   │   ├── io.h
│   │   ├── io.json
│   │   ├── io.md
│   │   ├── manifest.h
│   │   ├── hwenum.c
│   │   ├── hpet-counter.h
│   │   ├── hwenum.h
│   │   ├── hwenum.json
│   │   ├── hwenum.md
│   │   ├── inthandle-entry.S
│   │   ├── inthandle.c
│   │   ├── inthandle.h
│   │   ├── inthandle.json
│   │   ├── inthandle.md
│   │   ├── iommu-internal.h
│   │   ├── iommu.c
│   │   ├── iommu.h
│   │   ├── iommu.json
│   │   ├── iommu.md
│   │   ├── irq.c
│   │   ├── irq.h
│   │   ├── irq.json
│   │   ├── irq.md
│   │   ├── mem-module.c
│   │   ├── mem-pages.c
│   │   ├── mem-pages.h
│   │   ├── mem-pages.md
│   │   ├── mem-spaces.h
│   │   ├── mem-spaces.md
│   │   ├── mem.c
│   │   ├── mem.h
│   │   ├── mem.json
│   │   ├── mmio.c
│   │   ├── mmio.h
│   │   ├── mmio.json
│   │   ├── mmio.md
│   │   ├── module-load.h
│   │   ├── module-load.md
│   │   ├── module.h
│   │   ├── module.ld
│   │   ├── nvme.c
│   │   ├── nvme.h
│   │   ├── nvme.json
│   │   ├── nvme.md
│   │   ├── partition.c
│   │   ├── partition.h
│   │   ├── partition.json
│   │   ├── partition.md
│   │   ├── pci-platform.h
│   │   ├── pci-x86.c
│   │   ├── pci.c
│   │   ├── pci.h
│   │   ├── pci.json
│   │   ├── pci.md
│   │   ├── platform.c
│   │   ├── platform.h
│   │   ├── platform.json
│   │   ├── portability.md
│   │   ├── profiling.md
│   │   ├── sata.c
│   │   ├── sata.h
│   │   ├── sata.json
│   │   ├── sata.md
│   │   ├── provider-definition.h
│   │   ├── process-elf.h
│   │   ├── process-entry.S
│   │   ├── process.c
│   │   ├── process.h
│   │   ├── process.json
│   │   ├── process.md
│   │   ├── scheduler.c
│   │   ├── scheduler.h
│   │   ├── scheduler.json
│   │   ├── scheduler.md
│   │   ├── smp-entry.S
│   │   ├── smp.c
│   │   ├── smp.h
│   │   ├── smp.json
│   │   ├── smp.md
│   │   ├── splash-logo.S
│   │   ├── splash-module.c
│   │   ├── splash.c
│   │   ├── splash.h
│   │   ├── splash.json
│   │   ├── splash.md
│   │   ├── threads-entry.S
│   │   ├── threads.c
│   │   ├── thread-account.h
│   │   ├── threads.h
│   │   ├── threads.json
│   │   ├── threads.md
│   │   ├── timer-platform.h
│   │   ├── timer-x86.c
│   │   ├── timer.c
│   │   ├── timer.h
│   │   ├── timer.json
│   │   ├── timer.md
│   │   ├── tsc-validation.c
│   │   ├── tsc-validation.h
│   │   ├── usb-descriptors.c
│   │   ├── usb-descriptors.h
│   │   ├── usb-host.h
│   │   ├── usb-hub.c
│   │   ├── usb-hub.h
│   │   ├── usb-hub.json
│   │   ├── usb-hub.md
│   │   ├── usb-storage.c
│   │   ├── usb-storage.h
│   │   ├── usb-storage.json
│   │   ├── usb-storage.md
│   │   ├── usb.c
│   │   ├── usb.h
│   │   ├── usb.json
│   │   ├── usb.md
│   │   ├── xhci-platform.h
│   │   ├── xhci.c
│   │   ├── xhci.h
│   │   ├── xhci.json
│   │   ├── xhci.md
│   │   ├── xstate.c
│   │   ├── xstate.h
│   │   ├── xstate.json
│   │   └── xstate.md
│   └── README.md
├── maps/
│   ├── filesystem.md
│   └── startup.md
├── apps/
│   ├── README.md
│   ├── installed.json
│   ├── launch/ (launch.c, manifest.json)
│   ├── terminal/ (README.md, terminal.c, parser.h, editor.h, editing.inc, manifest.json)
│   ├── echo/ (echo.c, manifest.json)
│   ├── cat/ (cat.c, manifest.json)
│   ├── ls/ (ls.c, manifest.json)
│   ├── pwd/ (pwd.c, manifest.json)
│   ├── fsctl/ (README.md, fsctl.c, manifest.json)
│   ├── accounts/ (README.md, accounts.c, manifest.json)
│   ├── monitor/ (README.md, monitor.c, model.h, manifest.json)
│   └── true/ (true.c, manifest.json)
├── sdk/
│   ├── service.h
│   ├── stream.h
│   ├── streams.md
│   ├── application.h
│   ├── application.ld
│   ├── system.c
│   └── system.h
├── tests/
│   ├── offline-update.py
│   ├── persistent-sessions.py
│   ├── console-boot.py
│   ├── raster.c
│   ├── graphics-present.c
│   ├── display-copy.c
│   ├── streams.c
│   ├── waitset.c
│   ├── monitor-accounting.c
│   ├── process-samples.c
│   ├── io-credentials.c
│   ├── process-credentials.c
│   ├── iso9660.c
│   ├── filesystem-permissions.c
│   ├── password.py
│   ├── identity-store.c
│   ├── identity-transactions.c
│   ├── identity.py
│   ├── terminal-editor.c
│   ├── mouse-hid.c
│   ├── io-hid.c
│   ├── hid-demux.c
│   ├── hid-fixtures.h
│   ├── filesystem-backends.c
│   ├── usb-endpoints.c
│   ├── apps/
│   │   ├── bench-client.json
│   │   ├── bench-server.json
│   │   ├── application-runtime.c
│   │   ├── graphics-check.c
│   │   ├── private-check.c
│   │   ├── identity-check.c
│   │   ├── mouse-check.c
│   │   ├── handle-bench.c
│   │   ├── handle-stress.c
│   │   ├── stress-client.json
│   │   ├── stress-server.json
│   │   ├── handles-client.json
│   │   ├── handles-server.json
│   │   ├── handles.c
│   │   ├── keyboard-fault.c
│   │   ├── keyboard-client.c
│   │   ├── keyboard-denied.json
│   │   ├── keyboard-read.json
│   │   ├── keyboard-state.json
│   │   ├── startup.c
│   │   └── process.c
│   ├── README.md
│   ├── acpi-boot.c
│   ├── acpi-fixture.py
│   ├── acpi.c
│   ├── apic.c
│   ├── backend-contracts.py
│   ├── block-boot.c
│   ├── bootstrap-apps.c
│   ├── bootstrap-boot.c
│   ├── bootstrap-load.c
│   ├── block-boot.py
│   ├── filesystem-tree.py
│   ├── identity-image.py
│   ├── boot-image.py
│   ├── clock-boot.c
│   ├── clock.c
│   ├── console-diagnostics.py
│   ├── hpet-counter.c
│   ├── hpet-zero-probe.c
│   ├── io-keyboard-failure.c
│   ├── config.c
│   ├── cpu-topology.c
│   ├── cpu-tsc.c
│   ├── cpu-profile.c
│   ├── installation-contract.c
│   ├── installations.py
│   ├── runtime-fixture.py
│   ├── interfaces.c
│   ├── future.c / future.json / future-fail.c / future-fail.json (disk-only fixtures)
│   ├── events-boot.c
│   ├── ext4-runtime.c
│   ├── ext4-write.c
│   ├── ext4-write.py
│   ├── hub-boot.c
│   ├── hub.c
│   ├── hwenum.c
│   ├── inthandle-faults.py
│   ├── iommu-amd-backend.c
│   ├── iommu.c
│   ├── irq-boot.c
│   ├── irq.c
│   ├── lock-watchdog.c
│   ├── mem-boot.c
│   ├── mem-boot.py
│   ├── spaces.c
│   ├── space-fault.c
│   ├── space-fault.h
│   ├── space-fault-entry.S
│   ├── space_report.py
│   ├── test-space-report.py
│   ├── address-space-profiling.md
│   ├── bench-handles-kvm.py
│   ├── handle-clock.h
│   ├── handle-bench.h
│   ├── handles-profile.h / handles-profile.inc / handles-profile.S
│   ├── handle-profiling.md
│   ├── handle_report.py
│   ├── test-handle-report.py
│   ├── bench-spaces-kvm.py
│   ├── baselines/
│   │   └── address-spaces-tcg.json
│   ├── space_compare.py
│   ├── test-space-compare.py
│   ├── spaces-boot.c
│   ├── mem-pages-boot.c
│   ├── mem-pages.c
│   ├── mem.c
│   ├── mmio-boot.c
│   ├── mmio.c
│   ├── native-check.py
│   ├── nvme-boot.c
│   ├── pci.c
│   ├── handles.c
│   ├── keyboard.c
│   ├── keyboard-boot.py
│   ├── keyboard-stream.c
│   ├── manifest.c
│   ├── process-boot.c
│   ├── provider-definition.c
│   ├── process-elf.c
│   ├── probe-process.c
│   ├── probe-applications.inc
│   ├── probe-handle-stress.inc
│   ├── probe-block.c
│   ├── probe-hubs.c
│   ├── probe-storage.c
│   ├── probe-usb.c
│   ├── probe-xhci.c
│   ├── probe-spaces.c
│   ├── probe.c
│   ├── probe.h
│   ├── probe.json
│   ├── reservations.c
│   ├── scheduler-boot.c
│   ├── scheduler.c
│   ├── smp-clock.c
│   ├── splash.c
│   ├── stack-call.S
│   ├── storage-fs.c
│   ├── storage-fs.py
│   ├── threads-boot.c
│   ├── threads-state.c
│   ├── timer.c
│   ├── tsc-validation.c
│   ├── tsc_profile.py
│   ├── usb-boot.c
│   ├── usb-hub.c
│   ├── usb-storage.c
│   ├── usb.c
│   ├── xhci-boot.c
│   ├── xhci.c
│   └── xstate-host.c
├── tools/
│   ├── build-installations.py
│   ├── build-initial.py
│   ├── filesystem-tree.py
│   ├── clean-tests.py
│   ├── image-version.py
│   ├── update-dev-disk.py
│   ├── development-images.md
│   ├── init-dev-disk.py
│   ├── identity-db.py
│   ├── stage-kmodules.py
│   ├── build-boot-image.py
│   ├── check-acpi-image.py
│   ├── check-module-pic.py
│   ├── module-exports.py
│   └── pack-logo.py
├── AGENTS.md
├── Makefile
├── README.md
└── ROADMAP.md
```

## Managed limits and native acceptance

Outer lock waiters now service IRQs; stuck lock/IPI/TLB waits fault through a
five-second HPET watchdog with a finite iteration fallback. Profiling includes
HPET nanosecond maxima and notices above a 2 ms managed critical-section budget.
Bulk memory zeroing/copying/growth happens outside metadata locks. These do not
establish a hard real-time response guarantee across arbitrary kernel code,
firmware SMIs or host pauses; see [profiling](kmodules/shared/profiling.md).

The [IOMMU module](kmodules/shared/iommu.md) supplies separate Intel VT-d/AMD-Vi backends,
requester-owned mapped DMA pages, pinning, acknowledged invalidation and fault
health. Unsupported firmware and active translation features fail closed.
Intel/AMD isolated tests verify real NVMe DMA and denial of an unmapped canary
write. QEMU 6.2 omits AMD missing-PTE fault events; reports flag that coverage gap.

[Acceptance instructions](tests/README.md) cover isolated tests and
`make -C src test-native QEMU_CPUS=4 QEMU_IOMMU=intel`. Native runs require an
accessible /dev/kvm and write report.json beside boot.log. Both current user-host
native IOMMU-model runs pass with matching run IDs in result.json/report.json.
Earlier failed event assertions are corrected. The agent sandbox itself has no
/dev/kvm. Native Intel CPU and physical-device acceptance remain pending. No tests
attach candidate disks or change the host boot procedure.

TSC capability discovery and optional complete CPUID 0x15 crystal-ratio frequency
are exported by the selected Intel/AMD backend as `cpu.tsc`. Shared clock policy
retains HPET calibration, regression checks and cross-CPU qualification. KVM runs
now default to `host,migratable=off,+invtsc`; they intentionally exclude live CPU
migration. Native reports include capability bits, calibration frequency, sample
count and a readable rejection reason. Latest four-CPU AMD KVM runs qualify
and activate shared TSC with both emulated IOMMU models; HPET remains the
health reference and fallback.

Clock qualification now uses adaptive 10–500 ms windows and ten paired trials per
CPU, with per-CPU median/MAD filtering and a seven-trial minimum. All raw data and
filter masks remain in native reports. Worst retained uncertainty-inclusive bounds
control activation; every CPU must pass. See [SMP profiling](kmodules/shared/smp.md#raw-clock-profiling).

A stable longer-window BSP median can now refine TSC frequency once, followed by
fresh validation at a new calibration epoch. Clock profiling also records empty
ordered reads, HPET bracket, complete call and cross-CPU relay durations, with
per-CPU/epoch summaries. Frequency history stays in host reports; no boot-time
profile persistence or compile-time calibration input is introduced.

Cross-CPU clock offsets now qualify through independent TSC-only shared-memory
exchanges; HPET remains the frequency reference. Reports retain raw timestamp
brackets and separate frequency/offset groups. Activation requires all CPU groups
and both offset batches to pass in one epoch; the 10 us limit is unchanged.

Clock outlier filtering now accounts for measured uncertainty, avoiding zero-MAD
rejection of harmless nanosecond variation. Shortages may collect two fresh
batches at the same duration, with all observations and reasons retained in host
reports. Minimum counts and conservative clock acceptance budgets are unchanged.


Clock initialization/calibration runs on every boot. Multi-CPU startup also runs
adaptive qualification before enabling shared TSC; no previous report is reused.
The latest two native runs each used six attempts with requested intervals of
10, 25, 50, 100, 250 and 250 ms: 685 ms of intentional waiting, excluding
measurement, communication and other boot costs. End-to-end qualification time
is not yet reported separately. Full raw profiling storage is retained per CPU.

The selected disk-module loading milestone is complete. The next architectural
phase is process isolation and privilege boundaries; see [roadmap](ROADMAP.md#next-priorities).


[xHCI transport](kmodules/shared/xhci.md) is loaded/initialized by the kernel and
publishes the owned `xhci.controllers` registry alongside 17 method surfaces.
Controller instances use PCI/DMA/IOMMU/IRQ/mem/clock/thread hub facilities. Transfers
have wake-driven completion waiting; detach generations reject stale tokens.
Controlled close/reopen supports restart after quiescence; module-image unloading
remains future work; bootstrap now supplies explicit disk-module selection. Isolated tests validate actual QEMU
DMA/MSI-X transfers, cancellation and USB unplug/replug with one/four CPUs and
both emulated IOMMUs. Native/physical xHCI acceptance remains pending.

[USB core](kmodules/shared/usb.md) publishes `usb.devices` and eighteen surfaces.
Normal boot starts root enumeration in preemptible BSP workers; workers sleep
between xHCI change notifications. Interface-scoped ownership and core-owned I/O
buffers keep class drivers independent of xHCI DMA/context formats.
The separate usb-storage module implements BOT/SCSI; Descriptor-driven keyboard/mouse providers and shared HID demultiplexing are implemented.

[USB hub class](kmodules/shared/usb-hub.md) supplies seven surfaces and the owned
`usb.hubs` registry. Normal boot starts its manager alongside USB core. External
hub workers power/reset ports, enumerate nested devices and block on interrupt
change bitmaps. Subtree disconnect revokes references before postorder slot
cleanup. Isolated one/four-CPU tests cover child hotplug, pending grandchild I/O,
upstream removal and reattachment, including both emulated IOMMUs. Native and
physical hub/USB3 qualification remain pending.

The IO gateway now dispatches through independently loaded `hid`, `display`,
`filesystem` and `applications` boot kmodules. Userland resource contracts stay
unchanged. The filesystem manager probes registered format backends per selected
partition and routes mounts through a common contract; ext4 is the first real
backend; read-only ISO9660 supplies /boot. See [filesystem contracts](kmodules/shared/filesystem.md) and
[IO ownership](kmodules/shared/io.md).

The ordinary installed [monitor app](apps/monitor/README.md) reads copied global process
telemetry through manifest-authorized `/@applications.processes`, including
privileged handlers. It reports scheduled process CPU time and committed user
pages; it grants no process-control authority.

Standard streams have caller-relative names `/@stdin`, `/@stdout`, `/@stderr`;
acquisition duplicates the caller's launch-bound directional endpoint, and apps
cannot publish these reserved names. `cb_standard` wraps named acquisition.
Streams carry bytes independently of terminal presence. The keyboard provider
owns exclusive focus; monitor reads /@keyboard directly while terminal readers
park and resume without replaying application keys.

The continuously running [identity provider](handlers/identity/README.md) owns
protected binary users/groups/credentials storage, provisioned at image build.
Its manifest-authorized management operations commit three-file transactions
through a recoverable journal; credential records are never publicly listed.
Kernel-owned process UID/GID and supplementary groups now inherit at launch.
The identity provider verifies passwords, authorizes root/admin management and
authenticates app execution through the ordinary [accounts app](apps/accounts/README.md).
Filesystem calls enforce ownership/mode bits, including on existing file streams.
Explicit home provisioning and nested interactive sessions are implemented;
per-user installers and live group revocation remain open; app-specific home data
authorization and session-wide resource revocation are implemented.

The default keyboard layout is ABNT2 (IBM remains bundled). Boot filesystem
contents are accessible through /@filesystem at /boot: kernel.elf, cloudberry.cnf,
kmodules, limine and EFI. The ISO root matches those paths without a nested boot/
directory. Only a verified matching boot disk is mounted. ISO descriptor bounds
exclude appended ext4 system data; the FAT EFI partition is not separately mounted.
See [boot filesystem](kmodules/shared/iso9660-fs.md).

## Ownership, interactive sessions and persistent development storage

Filesystem `metadata`, `chmod` and `chown` expose explicit copied UID/GID/mode
metadata without changing the original STAT reply. `fsctl stat PATH`, `fsctl chmod
OCTAL PATH` and `fsctl chown UID GID PATH` are ordinary apps. Owners may change
mode; ownership changes require root/admin, valid 32-bit filesystem IDs and
ancestor search permissions. Protected system/boot policy and manifests still
apply. Metadata mutation uses the format backend, with ext4 journaling/barriers;
ISO9660 remains read-only.

`accounts home USER` explicitly provisions private 0700 user storage and system
application data directories from the immutable catalog. Existing unrelated
folders are not taken over. Root remains `/system/root`; boot cwd stays `/`.
`accounts login USER` authenticates and starts a nested terminal at its existing
home. An optional APP retains authenticated child execution. The continuous
sessions handler owns lifecycle records; kernel process membership and ancestry
inherit at launch. Nested keyboard focus restores its parent on release/death;
mouse input is independent within the foreground session. Logout closes launches
and requests cooperative stop, preserving contexts until actual process exit.
This remains a shared display, without a secure compositor or forced kill.

```sh
# Once, from the built candidate; refuses an existing destination:
make -C src init-dev-disk
# Preserve accounts, credentials, home directories and files across boots:
make -C src run-dev QEMU_ACCEL=kvm QEMU_CPUS=4
```

The separate `bin/cloudberry-dev.img` contains the initialized hybrid boot/system
image. Normal builds, `run-dev`, test cleanup and `clean` do not replace it.
`DEV_DISK=/absolute/path` selects another explicit destination. Updating installed
code on existing persistent images uses the explicit offline update workflow below;
rebuilding the candidate alone does not update that disk. Automated tests use disposable
copies under `bin/tests`; developer persistent disks are never auto-attached.

Focused validation: `python3 src/tests/persistent-sessions.py bin --cpus 4` runs
two isolated TCG boots to verify persisted credentials, homes, files and modes,
plus terminal/monitor focus restoration and root logout behavior. It removes
successful disposable media and retains reports. Native runs may opt into
`--accel kvm`; no native or physical validation is inferred from TCG results.

## Private data, session revocation and offline updates

Authentication binds an owned canonical home together with credentials; launches
copy both. `/@applications.data` resolves the caller's installed app-data path;
`fsctl data` displays its own location. Same-user applications cannot access other
private containers through path calls, mode changes or delegated file streams.
Root keeps administrative exceptions; only identity accesses credential storage.

Session END revokes new resource calls and fails queued work, while dispatched
operations can finish and release their results. Blocked pipe/mount waiters wake
and fail REVOKED. END/STATUS expose running members and stop-request failures;
closing records remain while draining. No executing context is forcibly freed.

`make -C src update-dev-disk` produces `bin/cloudberry-dev-next.img` from the new
candidate plus the previous disk's identity and user state. It preserves numeric
ownership and installation/disk identity, rejects active/corrupt/mixed inputs and
never replaces either input or an existing output. Select the result explicitly
with `run-dev DEV_DISK=../bin/cloudberry-dev-next.img`. Managed code/catalogs come
from one build, checked through a content manifest. Boot only compares a 64-byte
build ID with the mounted system before loading disk modules/providers; all
initialization, repair/import decisions and updates stay outside startup.
See [development image workflow](tools/development-images.md).
