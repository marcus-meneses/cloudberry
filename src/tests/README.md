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
USB core owns enumeration; usb-storage provides BOT/SCSI read/write/flush. HID remains deferred. IRQ affinity is CPU0.
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
| Applications | Privileged userland launch/info/wait/stop/release, immutable installed identity, argv/cwd, owner notifications and copied terminal results | Pinned issuer; no running-app upgrades or forced termination or automatic manager recovery |
| Handles | C system library, copied calls, immutable boot manifests, owner-local tokens, filtered reflection, blocking delivery, scoped memory/I/O | Build-owned installation bindings and launcher/owner policy; one boot input domain; no general approval UI, automatic rebind, vows or C++ wrappers |
| Console | Privileged display/graphics/text providers, software primitives, owned surfaces/damage, ordinary terminal and stream bindings | Temporary framebuffer backend, Ubuntu Mono TrueType font, one shared console and synchronous rendering; no native GPU/window/session model |
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
root-session app-data directory; this is not general user-folder policy.
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
no-replace rename with journaling. Default mounts and the ordinary QEMU boot medium
remain read-only; explicit writable mounts replay an internal journal and require
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
All remain boot-loaded; production selects the configured unique driver volume read-only and performs
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
│   ├── calendar.h
│   ├── hid-report.c
│   └── hid-report.h
├── handlers/
│   ├── applications/ (README.md, applications.c, manifest.json, provider.cnf)
│   ├── identity/ (README.md, identity.c, auth.inc, format.c, store.c, store.h, crypto/, manifest.json, provider.cnf)
│   ├── display-vga/ (README.md, display-vga.c, manifest.json, provider.cnf)
│   ├── filesystem/ (README.md, filesystem.c, manifest.json, provider.cnf)
│   ├── graphics-opengl/ (README.md, manifest.json, provider.cnf; inactive)
│   ├── display-opengl/ (README.md, manifest.json, provider.cnf; inactive)
│   ├── graphics-vga/ (README.md, graphics-vga.c, glyphs.h, font-engine.c, font-engine.h, font-system.c, overlay.inc, scene.inc, raster.c, raster.h, manifest.json, provider.cnf)
│   ├── text/ (README.md, text.c, manifest.json, provider.cnf)
│   ├── mouse/ (cursor.inc, README.md, mouse.c, hid.c, hid.h, manifest.json, provider.cnf)
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
│   │   ├── applications.c
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
│   ├── accounts/ (README.md, accounts.c, manifest.json)
│   ├── monitor/ (README.md, monitor.c, model.h, manifest.json)
│   └── true/ (true.c, manifest.json)
├── sdk/
│   ├── service.h
│   ├── stream.h
│   ├── README.md
│   ├── heap.h / heap.c
│   ├── runtime.h / runtime.c
│   ├── libc.h / libc.c / crt0.c
│   ├── required-symbols.txt
│   ├── runtime-port.md
│   ├── streams.md
│   ├── application.h
│   ├── application.ld
│   ├── system.c
│   └── system.h
├── tests/
│   ├── execution-acceptance.py / execution-acceptance.md
│   ├── sdk-heap.c / sdk-runtime.c / sdk-link.py / runtime-time.c
│   ├── runtime-audit.py
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
│   │   ├── runtime-foundation.c
│   │   ├── application-runtime.c
│   │   ├── graphics-check.c
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
│   ├── identity-db.py
│   ├── stage-kmodules.py
│   ├── build-sdk.py
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
The separate usb-storage module implements BOT/SCSI; HID remains deferred.

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
Automatic homes, complete login sessions and live group revocation remain open.

The default keyboard layout is ABNT2 (IBM remains bundled). Boot filesystem
contents are accessible through /@filesystem at /boot: kernel.elf, cloudberry.cnf,
kmodules, limine and EFI. The ISO root matches those paths without a nested boot/
directory. Only a verified matching boot disk is mounted. ISO descriptor bounds
exclude appended ext4 system data; the FAT EFI partition is not separately mounted.
See [boot filesystem](kmodules/shared/iso9660-fs.md).

## Boot filesystem and ABNT2 default

`make -C src test-iso9660` reads the generated candidate image through production
backend code under UBSan. It covers both block sizes, nested/UTF-8 names, EOF,
corrupt descriptors/extents, read-only mutation denial, OOM, I/O failures and
retained parent lifecycle. Namespace/installation fixtures use /system/kmodules
for ext4 driver data; physical Limine staging uses root-level kernel/config/modules.

`python3 src/tests/console-boot.py bin --bootfs-only --cpus 4` checks /boot via the
ordinary terminal/apps, the new module location, ABNT2 configuration and write
denial. QMP character injection is generated from the configured keymap rather
than assuming US punctuation. Tests remain isolated; successful media is cleaned.


Focused installer acceptance uses `make -C src test-installer-crash`: six isolated
provider-exit/reboot cases on guest ext4, followed by signed contents/private-data
and independent filesystem checks. Fault code is compiled only into test ELFs.
`console-boot.py --installer-phase upgrade --fixtures` checks active-target refusal
and normal replacement; rescue/quarantine phases use offline-edited copied images.
Reports and exact acceptance paths are recorded in the current roadmap.

Runtime port readiness is tracked in [the foundation audit](../../src/sdk/runtime-port.md). The JavaScript shell stays deferred until its required foundations pass. Focused native execution acceptance: `./scripts/check-execution-kvm.sh`; see [acceptance instructions](../../src/tests/execution-acceptance.md).

The optional [native SDK](../../src/sdk/README.md) supplies handle-backed allocation, target Newlib libc/libm and runtime adapters. Build with `make -C src sdk`; isolated validation uses `make -C src test-runtime-foundations QEMU_CPUS=4`. No SDK test apps or interpreter enter normal boot.

## JavaScript runtime and runner

`make -C src test-js-engine-host` builds the verified QuickJS core and checks
engine allocation/failure cleanup, recursion limits, exceptions, Unicode, math,
Promise jobs and C-to-JS binary construction with host UBSan. It does not qualify
OS adapters. `make -C src test-js-engine QEMU_CPUS=4 QEMU_ACCEL=tcg` runs the target
engine probe from a fixture-only installation on disposable media.

`make -C src test-javascript QEMU_CPUS=4 QEMU_ACCEL=tcg` uses the real installed
`js` application with isolated scripts and a restricted-manifest copy. It checks
binary recreate/append/load, stream prefixes/backpressure/EOF/directions/busy
close, Promise timers, child completion and redirected output, exact 64-bit owner
denial, module normalization/cache/meta, argv/cwd, UTC/manifest denial, terminal
redirection/pipelines and interactive prompt return. Reports retain binary,
engine/compiler, Newlib and script identities; successful media is cleaned.

Repeat either guest command with `QEMU_ACCEL=kvm` for host-native acceptance.
TCG evidence does not imply native KVM/Intel/physical qualification. Test scripts,
engine probes and restricted copies do not enter candidate images. Script-runner
contracts and limits live in [apps/js](../apps/js/README.md).


The graphical sample uses `test-js-editor-model` for Unicode document editing,
undo/redo, dirty state, clipboard, search and strict UTF-8/line-ending tests on the
host. `test-js-editor QEMU_CPUS=4 QEMU_ACCEL=tcg` drives the actual installed sample
with mouse/keyboard, file dialogs/save/reopen, discard/cancel and terminal focus
return on disposable media. Repeat with KVM for host-native acceptance; see
[the sample contract](../script/samples/README.md).

The TrueType UI checks use an ASCII coverage atlas generated by the same font
engine and pinned FreeType build as the guest; OCR/cursor sampling derives cell
geometry from that atlas. `test-console-host` covers malformed font rejection,
font sizes, accented antialiasing, overlay movement/backing repair, accelerated
copy cursor repair, and sparse distant text damage. `test-js-editor-model` includes
retained frame batching and file-browser navigation/failure state.

TrueType/editor acceptance: `bin/tests/console/38a4dd1b9269482c8653daf2b8328c05/result.json`
passes on four CPUs with TCG. It drives real USB keyboard/mouse events, browses
`/system/script/samples`, saves/reopens UTF-8 text, searches, exercises unsaved
changes/cancel/discard and verifies `cat`/`pwd` after returning to terminal. The
runner records code/sample digests in `editor.json`. Candidate and tested JS,
graphics and mouse ELF digests match. Focused host font/raster/overlay/streams,
installation authority, session authority and six JS model/view/navigation tests
pass. Native KVM responsiveness is not measured by this TCG acceptance; run
`make -C src test-js-editor QEMU_CPUS=4 QEMU_ACCEL=kvm` on the host.

## Buffered terminal scrolling (2026-10-23)

`test-console-host` includes text-batching.c against the production text model:
ring scrolling, final visible rows, one blit per write, colored cells, implicit
wrap, split UTF-8, explicit redraw after external graphics changes and repair
without replaying a successful copy. Raster tests check TrueType generations.

The disposable `--scroll-only` check verifies final line contents, `ls` output,
and five guest-timed 4080-byte bulk text writes. Unlike its wall timer, bulk
timings exclude simulated keyboard entry and screenshot/OCR work. Four-CPU TCG
comparison: reference median 1,666,053,670 ns, candidate 190,576,940 ns (8.74×).
Reference report: `bin/tests/console/8a522a3db51a4aefb54991804b29785c/scroll.json`;
candidate: `bin/tests/console/eb9b57a8fb3842e0a44f846828071c9c/scroll.json`.
Both use the same graphics binary; only the text provider differs. The newer
`ls` buffers names/padding into bounded stream writes in both comparison images.
This is a focused TCG regression measurement, not native KVM qualification.

To measure on the host using disposable media:

```sh
make -C src build ../bin/tests/apps/graphics-check.elf
python3 src/tests/console-boot.py bin --fixtures --scroll-only --cpus=4 --accel=kvm
```

The normal candidate catalog contains no graphics-check fixture.

The same candidate also passes four-CPU TCG editor/terminal handoff acceptance:
`bin/tests/console/3a4074bbbd0f41e0a557e60d8d4e33ae/result.json`. Saving/reopening,
menus, unsaved-change prompts, terminal redraw, `cat` and `pwd` remain functional.

## Retained line surfaces (2026-10-23)

Native graphics tests exercise owned bitmap/container trees, ancestor clipping,
cycle and foreign-parent denial, batch reparenting without transient cycles,
parent release with reusable detached children, foreground tree switching,
one-copy scrolling with unchanged line bitmap generations, and failed display
presentation after the RAM composition cache has already changed. Text tests
reject main-canvas FILL/BLIT calls during scrolling and validate retained colors,
row reuse, UTF-8/wrapping and repair without replaying a successful move. Seven
JS model/view/navigation tests pass, including movement of cached row surfaces
under a dedicated editing-area parent.

Final disposable four-CPU TCG reports:

- `bin/tests/console/a30c68ed515546098fdfe40421f8946a/result.json`: terminal row
  contents, lists, bulk output and individual newline scrolling pass. Five-sample
  guest medians: 26,065,760 ns per single-row scroll; 379,073,220 ns per 4080-byte
  fresh-page text write. This hierarchy checkpoint supersedes the earlier flat
  text provider's performance numbers. The earlier 190,576,940 ns bulk median was
  faster for complete page replacement; retained surfaces add per-surface work
  while preserving pixels for unchanged lines. These are TCG observations, not
  native KVM performance qualification.
- `bin/tests/console/032d855a19db4625a5d3c66eac088f24/result.json`: real editor
  menus/save/open/find, 60-line document Page Down/Page Up with stable chrome,
  unsaved-change dialogs, and restored terminal `cat`/`pwd` pass. The long-text
  fixture exists only in disposable test resources. TCG GUI injection gives
  modifier/release reports enough time to be sampled; native KVM keeps the normal
  injector timings.

The ordinary image has no long-document fixture or test command. Pixel movement
still reaches the existing software framebuffer through display fill/blit/copy.

## Placement comparison, pointer suppression and file caret (2026-10-23)

`test-console-host` instruments the production comparison loop and checks that
hierarchy relocation and scrolling use pixel comparisons. It also checks that
visually identical relocation sends no display updates, exposed pixels are
restored after movement, and failed transfers force redraws without comparison. The mouse
control tests cover exact-focus authorization, idempotent hide/show, motion while
hidden, restoration through the owner/focus cleanup path, reuse of the existing
arrow bitmap and watch-registration failure rollback. IO credential tests verify
that the bound mouse provider can watch owners while an unbound process is denied.
Eight JS model/view/navigation tests pass, including scrolling while retaining
file row/column and editing the original line after its caret leaves the viewport.

Disposable four-CPU TCG acceptance passes:

- `bin/tests/console/9b4f888cc99d4e6b81e2d2129ca7cf09/result.json`: terminal
  scrolling/list contents and the bulk/single-line timing probes. Historical bypass medians
  are 28,285,140 ns for a newline scroll and 1,127,731,900 ns for 4080-byte bulk
  text. That removed bypass forced larger uploads; the earlier hierarchy bulk
  median was 379,073,220 ns. This is a measured throughput regression in TCG, not
  evidence of a KVM speedup.
- `bin/tests/console/a7418ddf4afc4085a267f79e1d8f257b/result.json`: hide/recompute/
  show on real editor frames, a 60-line file scrolled with constant Ln 1/Col 1,
  typing while its caret is off-screen (editing file line 1 and revealing it),
  undo, scrolling back, dialogs, save/reopen and working terminal handoff.

Reports record candidate binary and sample digests. Native KVM responsiveness
still requires the host run; no native result is inferred from TCG.


Native editor: `make -C src test-editor-model` runs Unicode/history/viewport and
allocation-failure checks against its actual C document model with UBSan.
`make -C src test-editor QEMU_CPUS=4 QEMU_ACCEL=tcg` uses the existing isolated
editor GUI acceptance (`--editor-only --native-editor`), driving real USB input,
file dialogs, save/reopen, scrolling with a stationary file caret, dirty-exit
choices and terminal focus restoration. Reports identify the native binary.
Use `QEMU_ACCEL=kvm` for a host-native check; no developer disk is attached.


## Text newline presentation (2026-10-23)

`test-console-host` includes the production text provider against retained mock
row surfaces. The text test captures all 60 newline frames from one WRITE and
checks their intermediate visible lines, one-row scrolling, trailing text without
a newline, CRLF, blank lines, automatic wrap, split UTF-8 and resuming a failed
commit without replaying an accepted scroll. Stream chunks are no longer treated
as one presentation boundary. Historical bulk-output timings above predate this
intentional line-by-line presentation policy.


## Renderer backend selection

`test-render-selection` covers PCI discovery, missing drivers/providers, incomplete
or malformed readiness probes, multiple GPUs and exact provider-role aliases.
`test-bootstrap-apps` additionally checks that failed-pair records stay retained
when reap times out and are removed only after cleanup succeeds. Existing graphics,
text, cursor and installation checks use the suffixed VGA providers. The console
runner accepts `--gpu=vga` or `--gpu=virtio` for disposable firmware-framebuffer
fallback acceptance; these do not test accelerated rendering.

Initial fallback runs both pass on four-CPU TCG:
`console/12058f7a4d2044cea44789eafcd014fd` (VGA) and
`console/a486d964ba104a7984ce295ebc9fb1a5` (virtio-vga). They verify terminal
startup and key echo, with one detected PCI display and zero qualified
accelerators. They do not establish OpenGL execution or physical driver cleanup.


## Virtio GPU transport and scanout

`test-virtio-gpu-host` runs the real queue parser and driver against a deterministic
PCI/DMA/IRQ model, including failures and resource/context attachment cleanup.
`test-virtio-gpu` runs an isolated copied GPU test module, checking actual QEMU
scanout pixels and MSI-X completion counts. Default normal boots never reset the
GPU. Optional `QEMU_GPU=virgl` exercises capsets/contexts/textures/submission and
requires host graphics access; software device-model coverage is distinguished
from real VirGL/physical GPU acceptance. See [driver contract](../kmodules/shared/gpu-virtio.md).

Four-CPU TCG transport/frame checks pass with no IOMMU
(`gpu-virtio-boot/01bfa3b68d234b52996eae173687bee8`), Intel IOMMU
(`gpu-virtio-boot/0f91e8bc394243ebb27362342f89aeb9`) and the final driver with AMD
IOMMU (`gpu-virtio-boot/7881fa98a0ca492d842e4286b7686503`). Reports retain binary
and source digests, so earlier-revision coverage is distinguishable. The final
AMD run validates the current native driver; VirGL functionality on a real host
renderer is still pending. Final VGA boot/input fallback also passes
(`console/351d3a384d6d426b8ec687a95ec7eec9`).


VirGL capture correction: host run `gpu-virtio-boot/3f18767285c244969930b9499a7c9a3c`
failed the old screen-pixel assertion after reaching scanout; its QMP capture was
an all-black 320x200 RAM surface. QEMU 6.2 GL scanout uses a separate texture.
The runner now requires fenced GPU readback after erasing the source: 64,000
resource pixels and 1,024 attached-texture pixels must match, followed by context/
submission/cleanup success. Empty/unavailable GL RAM captures are reported as
unverified visible output, not successful screen capture. Nonempty mismatches
still fail, and the ordinary 2D mode still requires matching QMP pixels.
`tests/gpu-frame.py` prevents a blank GL capture from passing without full guest
readback/context evidence. The failed report is preserved; native rerun below now passes.

Corrected-runner regression passes on four-CPU TCG with strict 2D capture:
`gpu-virtio-boot/acbe39b96c59440f8f8e9e9b8a12c39f/result.json`. All four capture
evidence tests and the real driver device-model tests pass; native VirGL readback
is separately qualified by the native host run below.


Native host result `gpu-virtio-boot/b2caeb93e73c47adb9896554b750af3e/result.json` passes
on four-CPU KVM/VirGL with the driver/fixture/validation source digests at that milestone.
It reports 23 fenced commands, 23 IRQ notifications and 65,024 matching GPU
readback pixels. Both resource and attached-texture readback, context/submission
and cleanup passed. `screen_capture_verified=false` remains intentional: the QMP
RAM surface was empty, so visible presentation and host hardware acceleration
performance are not established. This closes the native transport gate only.


## Privileged GPU bridge acceptance

`make -C src test-virtio-gpu-host` includes `gpu-bridge.c`, exercising the
production gateway and driver against the existing PCI/DMA/IRQ device model.
It checks display-provider authority, pointer-free replies, copy/overflow bounds,
reply capacity before mutation, contexts/capsets/submission/readback, stale
cookies, exit cleanup and failed-cleanup retry. The I/O authorization fixture
checks denied callers, an absent optional driver and generic `gpu.invoke` routing.

The isolated four-CPU TCG run
[gpu-virtio-boot/8acd034a4a864962a3a2b9822331e9a2](../../bin/tests/gpu-virtio-boot/8acd034a4a864962a3a2b9822331e9a2/result.json)
passes with `bridge_verified=true`, 14 fenced commands and 14 IRQ notifications.
It checks mediated `io.invoke` routing, copied data, presentation, stale cookie
refusal and `io.exit` resource cleanup with a test-only kernel-bound principal.
This is not a userland OpenGL frontend test. Reports now hash the GPU bridge,
I/O and handle contracts as well as transport and fixture sources.
Normal VGA startup/input also passes on four-CPU TCG:
[console/bb9433db5e77438faed098d0a3730003](../../bin/tests/console/bb9433db5e77438faed098d0a3730003/result.json).
The earlier native KVM/VirGL report remains acceptance for its recorded revision;
the new bridge has not been exercised with host VirGL yet.


## Real VirGL drawing checks

`make -C src test-virgl-render` links the production experimental display engine
and encoder to the installed host `libvirglrenderer.so.1`. It uses surfaceless EGL
with `LIBGL_ALWAYS_SOFTWARE=1`, without KVM, render-node access or a host window.
The test requires that library and a working Mesa software EGL implementation.
It is optional and never installs host packages or adds renderer libraries to the
Cloudberry image. The host-only C ABI declarations and upstream license live in
`virglrenderer-api.h` / `virglrenderer-LICENSE.txt`.

Checks cover all byte-to-float color values, bounded packet encoding, real GPU
clears/fills, bitmap texture uploads, four overlap directions, 3,072 readback
pixels, private batch prevalidation/one-present behavior, automatic scanout
geometry, partial-batch BUSY normalization and sticky failure/cleanup retry.
Host rendering correctness is distinct from guest transport and physical speed.

The disposable `test-virtio-gpu` fixture now links the same handler engine only
into its copied test module. VirGL mode must report `GPU_TEST DRAW PASS pixels=3072`
after drawing, presenting and reading back the target through the I/O bridge;
`drawing_verified` is separate from ordinary 2D regression and screen capture.
Native run: `make -C src test-virtio-gpu QEMU_CPUS=4 QEMU_ACCEL=kvm QEMU_GPU=virgl GPU_TEST_DISPLAY=gtk`.
The experimental display executable compiles separately (`experimental-opengl`)
and remains absent from ordinary boot/catalog media.


Final local checks: real host VirGL/software-GL rendering and C/C++ interface,
I/O authority, transport/bridge checks pass. Four-CPU TCG GPU regression passes
in `gpu-virtio-boot/e732f438f40c424b94f5ab9c1c810650` (15 commands/15 IRQs,
bridge verified, drawing explicitly unverified because 2D mode has no VirGL).
Normal VGA startup/key echo passes in `console/9af164faebd6467fb943b999df0e6176`.
Candidate and isolated GPU/I/O/handle binaries match. The experimental display
provider compiles but is not installed. The subsequent native report below closes guest drawing/readback; full
provider IPC/graphics/font/overlay/restoration acceptance remains pending.


Native guest drawing result:
[gpu-virtio-boot/fbc98226fec0475a93bfe504688342f3](../../bin/tests/gpu-virtio-boot/fbc98226fec0475a93bfe504688342f3/result.json)
passes on four-CPU KVM/VirGL. Source digests and candidate/isolated GPU binaries
match. `bridge_verified=true`, `drawing_verified=true`, 50 commands/50 IRQs,
3,072 renderer pixels and 65,024 resource/texture readback pixels pass.
`screen_capture_verified=false` remains explicit: visible GL output is unverified.
The fixture runs the production rendering engine through I/O in a test module;
userland provider IPC, full graphics/font/overlay semantics and restoration
remain separate acceptance work. No native performance claim follows.


## Cached GPU graphics provider

`make -C src test-graphics-opengl` runs the production shared graphics decoder,
FreeType, private backend and installed VirGL/software GL. It checks nested
clipping, Unicode glyphs, cached moves/scroll without uploads, transparent cursor
restoration, no-op frames, owner refusal and cleanup. `test-console-host` retains
the complete software reference regression after decoder extraction. The host
adapters replace IPC/memory acquisition, so guest provider-process acceptance is
explicitly separate. `experimental-opengl` builds both executables without adding
either to candidate installation media.

`test-virgl-render` additionally checks contiguous upload/commit, unready/foreign
surface rejection, double-buffered frame visibility, explicit alpha shaders,
stale tokens after reopen, and batch/failure cleanup. The new guest fixture
requires `GPU_TEST COMPOSE PASS pixels=3072` through I/O in VirGL mode and records
`composition_verified` separately. The earlier native drawing report is retained
as acceptance for its own revision; it does not qualify the new shader path.


Final local acceptance: real host VirGL/provider/FreeType tests pass, including
cached moves/scroll without uploads, transparent cursor restoration, complete
upload validation, stale tokens and ownership cleanup. The existing software
console/raster/font/mouse suite and C/C++ ABI checks also pass. Candidate build
and four-CPU VGA startup/key echo pass (`console/563d903917fc4a09abdbe2194d4bbdc4`).
The final four-CPU TCG GPU regression is
`gpu-virtio-boot/f53afbf7ac0b4c6baa0ab8b8a967dc0f` (15 commands/15 IRQs).
It explicitly reports composition unverified because that run uses the 2D device.
Report sources match; candidate/isolated kernel modules and VGA provider binaries
match. The subsequent native report below closes the composition/alpha gate. Its command was:
`make -C src test-virtio-gpu QEMU_CPUS=4 QEMU_ACCEL=kvm QEMU_GPU=virgl GPU_TEST_DISPLAY=gtk`.
The test exercises shared composition code through kernel I/O, not both guest
provider receive loops; paired-process acceptance and restoration remain separate.


Native composition/alpha acceptance passed on four-CPU KVM/VirGL:
[report](../../bin/tests/gpu-virtio-boot/982b7a1a52364c9d8f2dd0e77001ac9a/result.json).
The recorded rendering/bridge sources and candidate/isolated GPU binaries match.
All 74 fenced commands and 74 IRQ notifications completed, with I/O bridge,
3,072 drawing pixels, 3,072 composition pixels and 65,024 resource/texture readback
pixels validated. Composition checks cover staged surfaces, an unchanged front
target before END, and the explicit source-over shader (with one-byte rounding
tolerance for blended channels). `composition_verified=true`; visible GL capture
remains unverified. This fixture runs shared rendering code through kernel I/O,
not both provider receive loops as guest userland processes. The log confirms
normal selection still chooses VGA with zero qualified accelerators.
Next: paired guest provider startup/IPC and visible output, followed by proven
scanout restoration/failure fallback before automatic accelerated selection.


## Paired userland GPU-provider acceptance

```sh
# No GPU/KVM required: fixture/catalog checks, real VGA providers and USB input.
make -C src test-gpu-pair QEMU_CPUS=4 QEMU_ACCEL=tcg
# Real 2D native scanout/reset/restoration, without KVM or host GL:
make -C src test-gpu-pair QEMU_CPUS=4 QEMU_ACCEL=tcg QEMU_GPU=virtio
# Native GL scanout readback AND provider teardown/VGA restoration:
make -C src test-gpu-pair-screen QEMU_CPUS=4 QEMU_ACCEL=kvm
```

The new test uses its own build under `bin/tests/isolated`, copies the stage and
bundles, validates that both normal renderer installations are present, and
adds an ordinary `gpu-pair` app instead of launching the terminal. It never attaches the
candidate's NVMe/SATA disks or installs experimental providers on normal media.
`test-gpu-pair-host` checks catalog isolation and screen evidence without a VM.

The disposable GPU-module replacement supplies test-only selection admission and
an observer. The startup-failure release hook still fails closed. A separate
post-success lifecycle test now checks reset and VGA restoration, as described
below. There is no production config bypass.
The observer proves display, graphics and app have distinct address spaces,
checks a clipped four-color hierarchy, Unicode/TrueType text, cached placement,
mouse movement/show/hide and structured keyboard input, then waits for app exit.
The app deliberately leaves its surfaces, focus and hidden cursor owned, so the
last frame must demonstrate automatic layer removal and cursor restoration.
Phases 1–8 establish app-owner cleanup. The new restoration stage then tears
down the actual display pipeline and tests its replacement.

In VirGL mode each present is read back within the display syscall continuation;
the observer checks those snapshots without competing for the GPU command lock.
It records `provider_ipc_verified`, `resource_pixels_verified`, input/exit results,
provider PIDs, source/catalog/binary hashes and individual screen checks. QMP may
capture a stale RAM surface when the visible scanout is GL-backed; therefore
`screen_capture_verified` is separate and may remain false even if GPU resource
pixels pass. Such a result does not qualify visible scanout or automatic GPU
activation. VGA mode requires every screen capture to match. Logs and reports
live under `bin/tests/gpu-pair/<id>/`; successful disposable media are removed.

Four-CPU TCG reference passed:
`gpu-pair/6e15f716dc774e2bb060aa8e1b55a7c3/result.json`.
All five screen observations matched, including mouse movement and exit cleanup;
real display/graphics/client PIDs were 2/3/11. This reference is VGA, not native
OpenGL acceptance. The paired KVM/VirGL result is recorded below.


The repeated paired-provider boot also exposed a volume/driver readiness race.
Provider startup now waits for successful selected-module completion before
catalog loading, `/boot` mounting or provider launch. Focused tests cover pending
completion, success, failure, snapshot errors and timeout without holding the
application-operation token. The post-fix four-CPU paired VGA Make target passes
(`gpu-pair/a7fa0f5961894d9c9d3845db1969a8a2`).


Final paired VGA reference after startup ordering correction:
`gpu-pair/c1031c60a51347c89b058eea0bbdcd86/result.json`.
Two consecutive four-CPU boots pass all five visible frame checks, actual
provider IPC, keyboard/mouse and exit cleanup. The final report's source hashes
and production-module hashes match the candidate and isolated builds. Host
rendering/ABI regressions and candidate build pass. Normal four-CPU VGA terminal
startup/key echo also passes (`console/f4e171ac33b648ea8df496bffd238513`).
Native paired KVM/VirGL acceptance has now passed, as recorded below; visible
GL output and safe firmware restoration remain pending.


Native paired-provider acceptance passed on four-CPU KVM/VirGL:
[report](../../bin/tests/gpu-pair/3553b8e542524e6eb0bd9aa3f29617e2/result.json).
The recorded production sources, candidate provider and production-module
hashes match; the disposable fixture has since gained restoration checks.
Separate display/graphics/app processes (2/3/11) completed 80 graphics calls and
14 GPU readback captures at 640×480, verifying clipped hierarchy, TrueType text,
mouse/keyboard input and automatic app-owner exit cleanup.
`provider_ipc_verified`, `resource_pixels_verified` and `exit_cleanup_verified`
are true. All five QMP observations captured empty RAM surfaces, so
`screen_capture_verified=false`: visible GL scanout is still unverified.
Next: visible-scanout evidence, then safe firmware restoration/startup-failure
fallback. Normal catalogs remain VGA; this result does not authorize automatic
GPU activation or establish provider-restart recovery.


### Strict scanout and restoration gate

`test-gpu-pair-screen` selects VirGL with `egl-headless` and mandatory screen
capture (`GPU_TEST_CAPTURE=1`). It opens no desktop window. QEMU's EGL-headless
frontend reads the GL scanout into its display surface; unlike the blank GTK RAM
surface in the prior report, this supplies host scanout pixels for QMP checks.
See QEMU v6.2.0 [egl_scanout_flush](https://github.com/qemu/qemu/blob/v6.2.0/ui/egl-headless.c).
This is virtual scanout evidence, not physical display hardware validation.
Every GL-phase capture must match. The host permits only a bounded 600 ms update
settling period within the guest's 1 s phase window; it never downgrades a blank
capture or mismatch into success. The target requires usable host EGL/render-node
access in addition to KVM. An initialization error fails with its saved QEMU log.

After the first app exits, all paired modes now stop/reap text, mouse, graphics
and display in dependency order, check withdrawal, and release bootstrap records.
The GPU owner, pin, resources, contexts, attachments, DMA and IRQ route must be
clear before replacement providers may start. VirGL mode also requires CLOSED
controller state. This reuses production exit/reset cleanup, without adding a
second reset path or a test recovery hook to candidate images.

The fixture starts display-vga, graphics-vga, mouse and text again, then loads a
fresh ordinary app through installed-app admission. It claims keyboard focus,
uses the public graphics handle, paints a colored rectangle and TrueType text,
and exits successfully. Phase 10's VGA screen pixels must match in EVERY mode,
including optional GTK diagnostic runs. Provider/app PIDs must all be new.

`provider_restart_verified` records replacement of the pipeline.
`restoration_verified` additionally requires native takeover: VirGL mode uses
the actual accelerated providers; `QEMU_GPU=virtio` exercises a real 2D scanout
and acknowledged reset between provider teardown and restart. Plain VGA mode
cannot qualify a native-to-firmware transition and reports restoration false.

Four-CPU TCG native 2D restoration passed:
[gpu-pair/cbed813cd9db4fc1a4a5710025d72878](../../bin/tests/gpu-pair/cbed813cd9db4fc1a4a5710025d72878/result.json).
All six screen checks matched at 1280×800; new display/graphics/app PIDs were
12/13/16. The initial 118 graphics calls and USB input/exit checks also passed.
The result predates addition of the explicit provider-restart result field.
The strict native GL scanout/restoration run remains pending. Startup-failure
injection/fallback remains a separate gate; normal boot still selects VGA.


The plain VGA replacement control also passes on four-CPU TCG:
[gpu-pair/9d77965796a240418ce7f08ca7a5fbd2](../../bin/tests/gpu-pair/9d77965796a240418ce7f08ca7a5fbd2/result.json).
`provider_restart_verified=true`, `restoration_verified=false` correctly records
that this control never entered native GPU mode. Its recorded source hashes and
candidate production-module hashes match. All four host fixture/evidence checks
pass, including rejection of blank required captures and incorrect restored frames.


Native strict capture result: [report](../../bin/tests/gpu-pair/b31768e6c71e459787599e91c84dc4bd/result.json).
All five GL scanout observations matched at 1024×768, including clipped colors,
TrueType text, cursor movement/hiding and app-exit cleanup. Production source
hashes match. The overall run FAILED in phase 9 before restoration: the test
looked up display by its publication after graphics teardown could already have
triggered display self-exit and unpublication. This is partial scanout evidence,
not a passing restoration or automatic-activation result.

The test now snapshots all four provider PIDs before stopping dependencies,
then terminates/reaps those identities even if they have already unpublished.
Withdrawal, successful reap, bootstrap release and GPU quiescence are still
mandatory; no cleanup check was relaxed. A deterministic host regression models
display self-exit after graphics, cleanup timeout, retained publication and
metadata-release failure. Rerun the same strict native command for restoration.

Post-fix local acceptance: `gpu-pair/c8f6bbfc4de74539ad92289775f177d4` passes on four-CPU
TCG/virtio-vga with native reset/restoration and all six screen observations.
All five host fixture/evidence tests pass, including deterministic early
unpublication. Final report source hashes match. Native GL restoration remains
pending the corrected strict rerun.


### Native scanout and post-success restoration accepted

Four-CPU KVM/VirGL [report](../../bin/tests/gpu-pair/5a7a29d9773644d5a3f546fe22660619/result.json) passes
with current source, candidate provider and production-module hashes matching.
All six mandatory scanout captures match: five accelerated frames at 1024×768
and the restored VGA frame at 1280×800. The run completed 114 graphics calls and
14 GPU snapshots, verified text/input/app-owner cleanup, reaped the original
providers, confirmed GPU quiescence, and started new display/graphics/app
processes (12/13/16) that drew successfully.
`screen_capture_verified`, `provider_restart_verified` and
`restoration_verified` are all true. This closes the native scanout and
post-success restoration gates for this tested virtio-vga/VirGL configuration.
It does not establish physical-GPU portability or recovery from incomplete
provider startup. Next: inject startup failures and verify cleanup/fallback,
then enable production readiness/release selection. Normal catalogs remain VGA
until that remaining gate closes.


## Startup rollback fault matrix

```sh
# Native GL matrix: ownership, scanout, second provider, and refusal to release.
make -C src test-gpu-pair-failures QEMU_CPUS=4 QEMU_ACCEL=kvm
# Focused non-GL references that can run without a render node:
make -C src test-gpu-pair QEMU_CPUS=4 QEMU_ACCEL=tcg QEMU_GPU=virtio GPU_TEST_FAULT=open
make -C src test-gpu-pair QEMU_CPUS=4 QEMU_ACCEL=tcg QEMU_GPU=virtio GPU_TEST_FAULT=release
```

The matrix runs four independent disposable VMs, stopping at the first unexpected
failure. It retains `bin/tests/gpu-pair-failures/<id>/result.json` with per-case
log/report paths; individual evidence remains in `bin/tests/gpu-pair/<id>/`.
The native matrix uses EGL-headless and mandatory matching screen captures.
There is no candidate-media mutation or boot configuration bypass.

`gpu-pair-fault-provider.c` compiles production provider bodies with a test-only
SDK-call wrapper. It invokes the real operation successfully, then exits with a
unique recorded code before initialization can proceed. Copies of these binaries
replace only the selected provider in copied test bundles:

| Case | Injection point | Expected result |
| --- | --- | --- |
| open | After actual GPU OPEN, before the renderer receives its cookie (exit 201) | Reap/cleanup and VGA fallback |
| scanout | After first successful GPU PRESENT, before display readiness (exit 202) | Restore firmware scanout and VGA fallback |
| graphics | After paired private INFO, before graphics readiness (exit 203) | Reap both providers, restore and fall back |
| release | After actual GPU OPEN (exit 204), with a synthetic release refusal | Fail startup; no VGA selection or app launch |

The kernel test module records the authenticated exiting process and exit code.
Its fault-mode release hook succeeds only after both selected/failed process
records are reaped, all four old public/private publications are gone, and the
controller is CLOSED without a bridge owner, PCI owner, owner pin, resource,
context, attachment, DMA allocation or IRQ route. It reuses production exit/reset
and bootstrap rollback. The release case deliberately refuses even after those
checks succeed; this tests honoring the release contract, not a hardware reset
failure. Existing transport tests cover retaining uncertain DMA on actual cleanup
errors. No incomplete cleanup is reinterpreted as readiness.

Positive cases require ACTIVE/VGA/START_FAILED selector status, new provider
identities, matching ordinary-app graphics/text, USB keyboard/mouse, app-exit
cleanup, and the subsequent pipeline replacement/restored-frame checks. The
negative case passes only for the expected code and startup-failure marker with
no fallback selection or app admission. Unexpected faults or missing injection
are failures. OPEN does not establish native scanout and reports restoration
false even though ownership rollback and fallback pass.

Local four-CPU TCG ownership rollback/fallback passes:
`gpu-pair/3fa42c02931540868b34d8514bd7fb74` (six screen matches, 118 graphics calls).
Local uncertain-release rejection passes:
`gpu-pair/cbe5d8375d36475ab256b1beb1e5cc6b` (no fallback/app launch).
All fault variants compile; the five host fixture/evidence tests and existing
bootstrap/renderer-selection regressions pass. Full native GL matrix acceptance
remains pending. Normal catalogs/readiness surfaces remain unchanged.


Final healthy four-CPU TCG control also passes:
`gpu-pair/20c7939e88df42bea4c2950715acce05`, including native 2D reset and six
matching screen captures. Its source hashes and candidate production-module
hashes match. Ordinary paired acceptance still uses the original fail-closed
release hook; only explicit fault fixtures can acknowledge rollback readiness.


### Startup matrix log-framing correction

Native matrix `gpu-pair-failures/a07721ef52b14b7d80e64a53b13f5e44` passed OPEN
rollback, then stopped during the scanout case's app-exit screen check. The
host parsed a partially written phase record and sampled cursor Y=7 instead of
its completed Y=753. Rechecking the saved phase-8 screenshot with the complete
record matches every required pixel; the run remains recorded as failed, and
later cases were not executed.

The runner now consumes only newline-terminated debug records, including PASS
and release-rejection records. A regression checks every character boundary of
phase/result messages, preserves earlier complete records, and verifies the
full cursor coordinate. No pixel, cleanup or fallback check was weakened, and
no OS/provider code changed. All six host checks pass. The complete corrected
native matrix remains required before production activation.

Corrected-runner four-CPU TCG OPEN rollback passes with all six screen checks:
`gpu-pair/0af3ff2a518c447eb1b282a27157492b`. Final report source hashes match.


### Native startup-failure matrix accepted

Four-CPU KVM/VirGL [matrix report](../../bin/tests/gpu-pair-failures/12f783373a554ba198869b0e2b094fa1/result.json)
passes all four cases. OPEN, first-PRESENT and graphics-private-INFO failures
reaped the failed processes and cleared GPU ownership/resources/DMA/IRQ before
VGA fallback. Each positive case completed 118 public graphics calls, USB input,
app-exit cleanup, replacement providers and six matching screen observations.
The synthetic release refusal correctly stopped startup without fallback or
app launch. All individual source hashes and candidate production-module hashes
match the accepted reports.

Together with native successful-pair/scanout/restoration acceptance, this closes
the planned runtime qualification gates for the tested virtio-vga/VirGL path.
It does not turn the fixture admission/release hooks into production policy.
Next: implement production capability detection and release surfaces, then
install/select the qualified provider pair through the normal catalog. Normal
boot remains VGA until that implementation is validated.


## Production rendering policy and activation

```sh
make -C src test-gpu-probe test-virtio-gpu-host test-interfaces test-render-selection test-bootstrap-apps test-gpu-pair-host
make -C src test-gpu-pair-screen test-gpu-pair-failures QEMU_CPUS=4 QEMU_ACCEL=kvm
```

Both renderer installations now come from the production catalog; the fixture
no longer appends duplicate OpenGL definitions. Ordinary GL acceptance delegates
qualification to production `gpu_virtio_probe`, records `GPU_PAIR PRODUCTION PROBE
verified=1`, and requires the kernel-selected device. Teardown and native startup
rollback must execute production `gpu_virtio_release` and emit its verification
marker before VGA may restart. Native GL runs fail if either marker is missing.
Result files record `production_probe_verified` and `production_release_verified`.

Only explicit non-GL OPEN/release fault references may force 2D admission after
production probing reaches a firmware-matched cold feature check. They cannot
qualify native GL or claim its production-policy acceptance. Ordinary software
references and normal images have no forced GL admission path.

`test-gpu-probe` covers saved feature-bank selectors and no reset/claim/DMA/IRQ,
firmware ownership/ambiguity, exact boot display count/geometry/pitch, enabled
32-bit VGA mode, 2D fallback, Intel/AMD DMA
requirements, serialization and fail-closed/idempotent release. Transport/bridge
checks include exact selected-device lookup and unused-field rejection. Packaging
checks verify both normal provider records, canonical font aliases and only the
expected disk-loaded driver files. No developer persistent disk is attached.

The candidate build and host policy/transport/ABI/catalog/rendering regressions
pass. Production-image four-CPU TCG 2D-only VGA startup/key echo passes in
`console/4d50217c55bd44ffa4578ccbe3a8bbb0`; it reports incomplete acceleration
support and selects VGA. Updated isolated ownership rollback also passes in
`gpu-pair/e0fa2ea7e5204ef994a276e21fda8845`. The later native production-hook
acceptance is recorded below; earlier native reports qualify their recorded
fixture policy.

`display.boot_info(out,bytes)` is a kernel-only copied metadata method, tested
for exact framing and geometry. The GPU policy rejects multiple boot displays
or a mode/pitch disagreement before qualification. Its implementation remains
in the existing display I/O backend; no kernel bootstrap inspection was added.


Final production build and host policy/transport/ABI/catalog/namespace checks
pass. The default VGA startup/key-echo reference is
`console/605eeadd8ee54f189574cf275fbf4647`; the final unmodified production 2D
fallback reference is `console/11fbc770383341bd821447e4c1e98dfa`.
The final four-CPU isolated OPEN-failure reference is
`gpu-pair/5acfaaefc1124934a5c1e729b005ab50`, with six matching frames and current
source/candidate production-module hashes, including the boot geometry backend.
All four updated startup-fault binaries build. Native acceptance of the
production hooks now passes; see the accepted reports below.


### Production rendering acceptance — 2026-10-24

Four-CPU native KVM/VirGL acceptance passes with the production capability and
release hooks, without forced native GL admission:

- Healthy pair: `bin/tests/gpu-pair/b5224bd82b29405fb06a828979f23fff/result.json`;
  114 graphics calls, 14 resource captures and six matching scanout frames.
  Input, client exit cleanup, provider restart and restored VGA output pass.
- Startup-failure matrix:
  `bin/tests/gpu-pair-failures/642d341ef6c7401e907328c26ec62cce/result.json`.
  OPEN, scanout and graphics failures restore VGA with matching frames; the
  release-refusal case correctly rejects fallback. All four cases pass.

All five runs record verified production probe/release execution. Their source
and candidate production-module hashes match the accepted build. Normal catalog
activation is enabled; unsupported/default/2D devices retain VGA fallback. This
acceptance covers the tested virtual GPU configuration, not physical GPU drivers
or a rendering performance benchmark. Reports are local artifacts under the
workspace root; isolated tests use disposable media.


## Kernel hub numeric method index

`make -C src test-hub test-interfaces` checks numeric and name-adapter resolution,
version filtering, missing keys, duplicate rejection, chained bucket collisions,
unpublished-node visibility, unchanged existing nodes across incremental loads,
concurrent readers, allocation failure and existing provider drain/retirement.
`make -C src test-hub-boot QEMU_CPUS=4 QEMU_ACCEL=tcg` exercises late module
publication and numeric lookup in the guest, verifies the RO/NX index mapping and
retains the existing cross-CPU stub/lifetime tests. Tests use disposable media.
See [the index and publication contract](../kernel/hub.md).

Validation on 2026-10-24: the candidate build, `test-hub`, C/C++ interface checks,
four-CPU TCG hub boot and four-CPU disk-module initialization/failure checks pass.
Reports: `bin/tests/isolated/tests/hub-4cpu-none/report.json` and
`bin/tests/isolated/tests/bootstrap-usb-modules-4cpu-none/report.json` relative to
the workspace root. The latter exercises private initializer resolution and
failed-owner visibility through the same index. No native timing claim follows.


## Partial redraw and 1024×768 startup — 2026-10-24

`test-graphics-opengl` now reports three `GRAPHICS_MEASURE` JSON records for
32 typing, rectangle-edit and cached-scroll updates, with per-frame pixel
comparison against real VirGL/software GL. A 1024×768 cached edit bitmap uses
a 96×64 diagnostic scanout; the counters exclude first initialization. Uploads
fall from 100,663,296 bytes to 10,352 (typing) / 49,152 (rectangle edits), and
8,384 to 224 GPU transport calls. Cached scrolling still uploads zero bytes.
The test enforces bounded work and also checks larger multi-chunk patches and
edits made while hidden. `bin/tests/graphics-redraw-comparison.json` stores both
samples and the current source hashes; logs are `graphics-redraw-before.log` and
`graphics-redraw-after.log` in `bin/tests`. Timings are host diagnostics, not KVM
measurements; initial cold uploads remain full-sized.

`test-virgl-render` rejects uninitialized/foreign/out-of-bounds patches, changed
transaction geometry, partial sampling and malformed composition flags. Driver
and bridge tests verify packed row copies, strided rectangle transfer commands,
origin offsets, ownership and invalid bounds before GPU submission. The existing
software-console and C/C++ interface checks pass. Public operations are unchanged.

Candidate build and isolated four-CPU TCG scrolling pass:
`bin/tests/console/158a84f213d6417f9476ab967b682225/result.json`. The test uses
the requested 1024×768 mode and derives visible line expectations from the actual
screenshot height. It verifies line-by-line scrolling, bulk output and subsequent
`ls`; media are disposable. Newline presentation remains unchanged.

Native regression command for this renderer revision:

```sh
make -C src test-gpu-pair-screen QEMU_CPUS=4 QEMU_ACCEL=kvm
```

Earlier native production-selection/failure-matrix reports qualify their recorded
revision. This environment has neither `/dev/kvm` nor a render node for the guest
GL scanout check; real host VirGL validation above uses surfaceless software GL.


Native redraw acceptance (2026-10-24):
`bin/tests/gpu-pair/9d8a3f857b89400bbba136f7ef118895/result.json` passes on
four-CPU KVM/VirGL. All six scanout observations match at 1024×768; 14 resource
captures and 114 graphics calls pass. Production qualification/release, provider
IPC, mouse/keyboard, client exit cleanup, provider restart and restored VGA
output are verified. All 56 recorded source hashes, seven production module
hashes and four provider executable hashes match the accepted build. This
closes the pending native redraw correctness check; no native timing or new
startup-failure matrix claim follows from this healthy-pair run.


## GPU reply allocation and broker/fence attribution — 2026-10-24

The production change replaces two blanket 15 KiB GPU reply bodies with
operation-sized results. Host `test-handles` checks all sub-operations, counted
READ/CAPSET overflow, malformed requests, real reply sizes, no-result replies
and allocation failure before dispatch. Its instrumented variant also checks
pause/drain of a pending probe. `test-virtio-gpu-host` verifies optional trace
hooks against actual modeled IRQ completions without changing DMA/fence safety.
Fixture/collector host tests cover copied-only broker replacement and rejection
of partial/missing/inconsistent samples or readback contamination.

Native profiling command:

```sh
make -C src test-gpu-pair-profile QEMU_CPUS=4 QEMU_ACCEL=kvm
```

This builds under `bin/tests/isolated`, copies the ordinary catalog/media, then
replaces only the copied GPU module, broker and test app. Normal candidate
modules expose neither diagnostic surface nor timestamp hooks. The 32 edits
run after warmup at the normal display mode. A private app/observer barrier
closes the window before restoration and diagnostic readback; screenshots
resume at the ordinary validation phases. No installation or developer disk is
attached. Up to four CPUs are supported by this test-only profiler.

Each `bin/tests/gpu-pair/<id>/` retains `profile.json`, `result.json`, boot log,
host/QEMU metadata, source/binary hashes and commands. Records include 32 raw
client frame intervals (rectangle operation + present, with clock-call endpoint
overhead), GPU I/O sub-operations, command completion/block counts, broker INVOKE
phases per actor, system-wide broker lock waits/holds, allocations and handoff
queue/service/resume spans. Clock endpoints must retain source/flags/frequency;
32 timestamp-overhead samples are reported. Do not pool HPET and TSC runs or
compare different host/CPU configurations. These instrumented spans overlap and
include scheduling; they are not pure hardware latency or uninstrumented frame
rate. Keep raw samples; do not blindly subtract profiler overhead.

Candidate build, host broker/device/trace/fixture tests pass. Native profiling
also passes in the accepted run below. Further broker/queue/fence optimization
requires attribution and a control for profiler overhead. Filesystem write/flush policy remains unchanged and deferred.

Four-CPU production-image TCG startup/key-echo smoke passes in
`bin/tests/console/964fba1d6bf648efa9d547e26e674024/result.json`. Candidate symbols
contain no `gp_issue`, `gp_dump` or `handles_surface_profile`. The diagnostic
variants compile with PIC validation; collector/fixture host tests pass.


### Profiling pause/finalization repair — 2026-10-24

Native run `bin/tests/gpu-pair/14915376a7874e78816c09e3656cb413/result.json`
failed at profiler finalization after the restoration phases. The pause path
continued collecting admitted calls but stopped their dispatch/output timestamps,
which falsely recorded clock regressions when those calls returned. A positive,
advancing host clock and pause inside the actual native gate reproduce this
failure (`bin/tests/gpu-profile-pause-before.log`); the old zero-clock unit
fixture had masked it.

Admitted probes now keep phase timestamps after pause. Generation-tagged entry
stamps reject gates entering outside or across measurement windows. Admission
and profile-control transitions serialize through the existing CPU lock; a new
window cannot overwrite a still-draining report. Genuine clock and allocation
failures still reject profiling. Host tests cover pause during input capture
and kernel dispatch, pending-probe drain, zero-valued valid timestamps and
inactive/stale entry generations. Broker/fixture host checks and both diagnostic
module builds pass (`bin/tests/gpu-profile-pause-final.log`).

Finalization now prints active probes, generation, clock/allocation errors and
sample count before returning a failure. Raw GPU/frame observations are retained
even when the report is rejected; incomplete broker results are not presented
as valid rows. The collector saves these under `profile_diagnostics` on failure.
The production broker rebuild is byte-identical to its prior image; these changes
affect diagnostic bookkeeping only. The same
`test-gpu-pair-profile QEMU_CPUS=4 QEMU_ACCEL=kvm` command now passes; see below.


### Accepted native GPU/broker profile — 2026-10-24

`bin/tests/gpu-pair/c9ab0d3d50ba4e0da57f515fcbaab2d0/result.json` passes on
four-CPU KVM/VirGL, with all six scanout phases matching at 1024×768, 15 resource
captures, 180 graphics calls, input, exit cleanup, provider restart and restored
VGA output. All 59 source hashes, seven production module hashes, four provider
executables and the diagnostic GPU/broker/app hashes match their recorded build.
Finalization reports zero active probes, clock errors and allocation failures.

The 32 edit samples use HPET (source 2), unchanged across the window. Mean is
50.934 ms, median 51.141 ms, nearest-rank p95 53.029 ms and maximum 53.667 ms.
128 fenced commands average 27.852 ms combined per edit (54.7% of the client
interval). Timestamp-read overhead median is 29.12 µs; this is an instrumented
baseline, not normal GUI frame rate or hardware-only completion latency. Broker
lock maximum hold/wait is 301.29 µs / 2.635 ms; allocations/frees both count
1,478 during the window. Counters and nested phases overlap.

`analysis.json` beside the raw report records the derivations. Follow-up should
separate profiler overhead using an uninstrumented control, then attribute GPU
notification, interrupt/deferred service and caller wake/resume delays. Keep
clock-source/configuration cohorts separate. The failed earlier run remains
unchanged as regression evidence.


## Native 3D benchmark acceptance — 2026-10-24

`bench3d` ships in the ordinary catalog and uses the public graphics, input and
clock handles. `test-bench3d-model` passes 1,668 UBSan geometry cases (culling,
depth order, bounded coordinates, rotation periodicity and max load). Candidate
build and four-CPU TCG GUI acceptance pass in
`bin/tests/console/c09a24a053b5484e9cb53f4e038400f7/result.json`. The check verifies
filled scene pixels, live HUD, default four-cube load, load increase, wireframe,
pause/resume, Esc and focus/prompt restoration, then a two-frame auto-exit run.
The retained `bench3d.png` is a paused actual guest screenshot; a copy lives in
`assets/bench3d.png` for the project presentation. Test images are disposable.

```sh
make -C src test-bench3d-model
make -C src test-bench3d QEMU_CPUS=4 QEMU_ACCEL=tcg
```

The app performs fixed-point CPU projection and submits filled paths through
ordinary graphics handles. It benchmarks the current pipeline with either
backend; it is not native GPU 3D submission or a KVM performance acceptance.
See [controls and statistics](../apps/bench3d/README.md).
