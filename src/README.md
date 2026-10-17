# Cloudberry v2

The active source tree boots through Limine/UEFI into x86-64 long mode.
Its current foundation includes independently loaded kernel modules, a versioned
C ABI hub, owned memory/guarded stacks, hardware/MMIO discovery, clock/RTC,
interrupt ownership, APIC timers, deadline callbacks, preemptible kernel threads, initial SMP execution, measured critical sections,
validated SMP clock selection, deferred device IRQ delivery, PCI/DMA ownership,
NVMe/AHCI block read/write/flush, xHCI transport, USB core and external hub class support,
with initial Intel VT-d/AMD-Vi DMA translation. Target hardware is modern (2016+) Intel/AMD x86-64.

Kmodules are coupled kernel methods in one shared address space. They are not
resource handlers, user processes or isolation boundaries. Existing method calls
stay synchronous; a separate threads API explicitly creates saved contexts.
CPU and IOMMU alternatives are selected at boot behind shared contracts; see
[platform portability](kmodules/shared/portability.md).

Application-facing handles/handlers, userland and HID class drivers do not exist in this rewrite yet. The xHCI transport supplies root/downstream
slot/endpoint and asynchronous transfer operations for USB core and hub policy. NVMe and AHCI are kernel-only block backends. The superseded implementation is preserved locally in `../.src_old/`, excluded
from Git and from this build.

## Build and run

```sh
make -C src build
make -C src run
make -C src run QEMU_ACCEL=kvm QEMU_CPUS=4
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
The NVMe module is available through explicit kernel surfaces; normal boot does
not claim or access the persistent disks. xHCI initialization publishes an empty
owned registry; normal boot starts USB-core workers and the hub-class manager to claim
controllers and enumerate root/downstream devices. HID class drivers remain future work; USB storage and ext4 are boot-loaded modules, with explicit writable mounts.

## NVMe and SATA block access

Both backends remain Limine-loaded and take explicit device selection. NVMe
opens a hardware index and optional namespace ID, then `nvme.block` exports its
common block descriptor. SATA opens a hardware index and port through
`sata.open`. Feed either descriptor to `partition.find` using the GUID bytes
exposed by `config.drivers.partition_guid`, then mount the bounded slice through
`ext4_fs.mount`. No filesystem logic lives in the transport drivers.

Normal startup initializes these modules without opening the persistent disks.
Automatic boot-device correlation/duplicate-GUID handling is still pending;
identifying a configured partition does not by itself prove boot provenance.
Current modules stay in the Limine boot partition. The ext4 directory is reserved
for explicitly selected future modules; disk loading is not implemented yet.

```sh
make -C src test-block-boot BLOCK_BACKEND=nvme QEMU_CPUS=4 QEMU_IOMMU=amd
make -C src test-block-boot BLOCK_BACKEND=sata QEMU_CPUS=4 QEMU_IOMMU=intel
```

These focused tests actually boot from the selected transport, use only disposable
media, exercise GPT/ext4 journaled writes and remount, verify IRQ completion,
bounds/lifetime pins and timeout/reset/stale cookies, then run independent host
filesystem checks. See [NVMe](kmodules/shared/nvme.md) and [SATA](kmodules/shared/sata.md).

## Boot image layout

`bin/cloudberry.iso` is a hybrid ISO/GPT disk image: ISO9660 supplies the kernel,
configuration and foundational kmodules, an embedded FAT EFI System Partition
starts Limine, and GPT partition 4 is a separate 512 MiB ext4 filesystem named
`Cloudberry drivers`. Xorriso also describes gap partitions around these regions.
The ext4 root contains the configured `/boot/system/drivers` directory, currently
empty. Limine 10 reads FAT/ISO9660, not ext4. The boot-loaded `usb-storage`,
`partition` and `ext4-fs` modules provide guest access and explicitly requested journaled writes; disk-loaded
drivers remain future work.

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
`boot.show_inventory` controls hardware listing. Driver selection remains
caller-owned; USB storage applies the configured command timeout. The source
configuration supplies the GPT identity of the appended ext4 driver partition; image preparation uses that exact GUID. Missing/duplicate files and invalid configuration
fail startup with a diagnostic. No disk access, formatting or migration is added.

## Execution and ownership

Main checks long mode, publishes the hub and delegates loading. The loader
initializes inthandle, loads splash and immediately draws the logo on the inherited
framebuffer, then selects the CPU backend, loads the remaining shared modules and
returns for module startup. Limine quiet mode suppresses its ordinary screen output;
firmware output before Limine remains outside kernel control. Hwenum owns hardware inventory formatting;
main invokes hwenum.dump without interpreting descriptors. Splash owns validation
and centered logo rendering. Modules carry named/versioned export manifests;
payload definitions stay in C headers. The compact hub occupies four pages (16 KiB); mediation entries occupy three
pages (12 KiB). Consumers use the hub's reported extent.

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
mem/memory, hub, clock, PCI, mmio, hwenum and scheduler mutations are serialized.
Stack construction/teardown use short transitions and mapping batches of at most
16 pages, with one shootdown per batch. Exited thread cleanup runs outside its
list lock. Callbacks execute outside their queue lock on preemptible workers;
a long callback still delays its own worker queue. Private thread construction
also occurs outside its publication lock. This is a correctness
baseline, with latency/scalability work still needed; it is not unrestricted
kernel preemption. New kmodules must define their critical/reentrancy contracts.
Threads eagerly preserve x87/SSE and available AVX through the shared xstate
kmodule; dispatcher/ISR infrastructure still builds with mgeneral-regs-only.
Forced thread cancellation/process teardown, CPU hotplug, migration and load
balancing are not implemented. USB device/hub hotplug is implemented.

Single-CPU clock selection prefers a validated invariant TSC with HPET fallback.
SMP requires a healthy HPET reference, validates per-CPU frequency with HPET
brackets and bounds cross-CPU offsets with TSC-only exchanges before enabling
TSC timestamps. Offset/drift/uncertainty or capability
failures retain HPET; per-CPU regression checks and an HPET health guard restore
fallback on later instability. Migration remains disabled. CMOS/NMI policy stays owned by inthandle. The [memory map](maps/startup.md)
describes reservations, mapping references, release/reuse and cross-CPU TLB
invalidation. Heap backing, page tables and original boot allocations remain
reserved. No per-process page tables or user isolation exists yet.

A successful boot prints module/table names and addresses, hardware descriptors,
BOOT OK, then "managed BSP event stack active". The candidate creates scheduler and IRQ deferred workers, a USB bus worker per
controller, the hub-class manager and a worker per discovered external hub. It
submits no demo/test tasks; idle workers block on notifications or deadlines.

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

## Current assessment (2026-10-16)

The shared kernel foundation supports the next storage milestone. It does not
implement the original application's handler/handle model yet; that layer must
follow process isolation and safe boundary transfer.

| Area | Implemented | Remaining boundary |
| --- | --- | --- |
| Execution | Preemptible pinned kernel threads, SMP, deadlines, deferred IRQs, xstate | Shared address space; no process isolation, migration or forced thread cancellation |
| Devices | PCI/ACPI companions, platform inventory, DMA/IOMMU ownership | Boot inventory; no general driver manager, SCI reconciliation or physical qualification |
| USB | xHCI, root/nested enumeration, interface claims, interrupt hotplug, subtree teardown | BSP execution, 4 KiB submissions/configurations, no HID or UAS driver; BOT storage is implemented |
| Storage | NVMe, AHCI SATA and USB BOT block read/write/flush; GPT slices; journaled ext4 mutations | Explicit device/namespace/port selection; automatic boot-volume selection, parallel controllers and physical durability remain open |
| Modules | Separate Limine-loaded images, validated versioned exports and mediated hub | Disk loading, dependency-driven driver selection and executable hot replacement remain future work |

Hub lifecycle and descriptor closure now pass focused validation. Class-only
stop/start reclaims existing hubs while USB core and root attachment identity
remain active. Tests cover blocked child I/O, fresh descendant identities and
worker/manager stack reclamation over two cycles. Complete descriptor reads are
validated against their prefix before configuration. Automatic failed-provider
recovery and executable image unloading remain deferred. See the
[hub contract](kmodules/shared/usb-hub.md) and [next priorities](ROADMAP.md#next-priorities).

The production hub uses 77.00% of its 16 KiB extent, or 79.05% with the selected
IOMMU backend; mediation uses 62.50%/64.32% of 12 KiB. Capacity is sufficient for
the immediate storage work. These figures measure registry/stub occupancy, not
whole-kernel RAM use or a demonstrated throughput/latency bound.

Current QEMU boot media is a read-only USB mass-storage ISO. USB storage exposes read/write/flush on suitably writable media; GPT and ext4 consumers can access the appended
driver partition by its configured GUID. Production init claims no storage interface
or mounted filesystem automatically; explicit callers own selection and lifecycle.
The kernel does not currently correlate Limine boot-volume identity with device
transport. Future matching must report absent/ambiguous identity rather than
infer the boot device from enumeration order. Native/physical USB and real
high-speed TT/SuperSpeed hub qualification remain pending; earlier AMD KVM
clock/SMP acceptance does not qualify the new USB stack.

Ext4 now supplies creation, growth, append, truncate, deletion, directories and
no-replace rename with journaling. Default mounts and the ordinary QEMU boot medium
remain read-only; explicit writable mounts replay an internal journal and require
block write/flush support. The private pinned lwext4 engine runs behind the same
frontend for writable and ordinary read-only mounts. Explicit checksum-seed layouts
currently retain the qualified read-only parser and cannot be mounted writable.
Conservative flush-after-each-write ordering is implemented before batching.
Dirty transaction/I/O failures poison and retain an instance until reboot; a fresh
mount replays the journal. See [USB storage](kmodules/shared/usb-storage.md),
[GPT partitions](kmodules/shared/partition.md) and [ext4](kmodules/shared/ext4-fs.md).
All remain boot-loaded; production initialization selects/mounts no filesystem and
performs no formatting or migration. Test fixtures never enter the candidate image.

## Focused validation

```sh
make -C src test-portability
make -C src test-threads-smp
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
│   └── run-qemu.sh
├── kernel/
│   ├── cpu.c
│   ├── cpu.h
│   ├── main.c
│   ├── mediate.S
│   ├── modules.c
│   ├── modules.h
│   └── surfaces.h
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
│   │   ├── ext4-fs.c
│   │   ├── ext4-fs.h
│   │   ├── ext4-fs.json
│   │   ├── ext4-fs.md
│   │   ├── hwenum-module.c
│   │   ├── hwenum.c
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
│   │   ├── mem.c
│   │   ├── mem.h
│   │   ├── mem.json
│   │   ├── mmio.c
│   │   ├── mmio.h
│   │   ├── mmio.json
│   │   ├── mmio.md
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
│   └── startup.md
├── tests/
│   ├── README.md
│   ├── acpi-boot.c
│   ├── acpi-fixture.py
│   ├── acpi.c
│   ├── apic.c
│   ├── backend-contracts.py
│   ├── block-boot.c
│   ├── block-boot.py
│   ├── boot-image.py
│   ├── clock-boot.c
│   ├── clock.c
│   ├── config.c
│   ├── cpu-topology.c
│   ├── cpu-tsc.c
│   ├── events-boot.c
│   ├── ext4-runtime.c
│   ├── ext4-write.c
│   ├── ext4-write.py
│   ├── hwenum.c
│   ├── inthandle-faults.py
│   ├── iommu-amd-backend.c
│   ├── iommu.c
│   ├── irq-boot.c
│   ├── irq.c
│   ├── lock-watchdog.c
│   ├── mem-boot.c
│   ├── mem-boot.py
│   ├── mem-pages-boot.c
│   ├── mem-pages.c
│   ├── mem.c
│   ├── mmio-boot.c
│   ├── mmio.c
│   ├── native-check.py
│   ├── nvme-boot.c
│   ├── pci.c
│   ├── probe-block.c
│   ├── probe-hubs.c
│   ├── probe-storage.c
│   ├── probe-usb.c
│   ├── probe-xhci.c
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

Current next priorities are boot-volume selection and driver
reading and loading driver modules from disk, followed by process isolation; see [roadmap](ROADMAP.md#next-priorities).


[xHCI transport](kmodules/shared/xhci.md) is loaded/initialized by the kernel and
publishes the owned `xhci.controllers` registry alongside 17 method surfaces.
Controller instances use PCI/DMA/IOMMU/IRQ/mem/clock/thread hub facilities. Transfers
have wake-driven completion waiting; detach generations reject stale tokens.
Controlled close/reopen supports restart after quiescence; module-image unloading
and the driver manager remain future work. Isolated tests validate actual QEMU
DMA/MSI-X transfers, cancellation and USB unplug/replug with one/four CPUs and
both emulated IOMMUs. Native/physical xHCI acceptance remains pending.

[USB core](kmodules/shared/usb.md) publishes `usb.devices` and seventeen surfaces.
Normal boot starts root enumeration in preemptible BSP workers; workers sleep
between xHCI change notifications. Interface-scoped ownership and core-owned I/O
buffers keep class drivers independent of xHCI DMA/context formats.
The separate usb-storage module implements BOT/SCSI; HID remains deferred.

[USB hub class](kmodules/shared/usb-hub.md) supplies six surfaces and the owned
`usb.hubs` registry. Normal boot starts its manager alongside USB core. External
hub workers power/reset ports, enumerate nested devices and block on interrupt
change bitmaps. Subtree disconnect revokes references before postorder slot
cleanup. Isolated one/four-CPU tests cover child hotplug, pending grandchild I/O,
upstream removal and reattachment, including both emulated IOMMUs. Native and
physical hub/USB3 qualification remain pending; disk-loaded drivers remain the next priority.
