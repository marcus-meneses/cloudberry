# Cloudberry v2

The active source tree boots through Limine/UEFI into x86-64 long mode.
Its current foundation includes independently loaded kernel modules, a versioned
C ABI hub, owned memory/guarded stacks, hardware/MMIO discovery, clock/RTC,
interrupt ownership, APIC timers, deadline callbacks, preemptible kernel threads, initial SMP execution, measured critical sections,
validated SMP clock selection, deferred device IRQ delivery, PCI/DMA ownership
a read-only NVMe path and an xHCI controller transport, with initial Intel VT-d/AMD-Vi DMA translation. Target hardware is modern (2016+) Intel/AMD x86-64.

Kmodules are coupled kernel methods in one shared address space. They are not
resource handlers, user processes or isolation boundaries. Existing method calls
stay synchronous; a separate threads API explicitly creates saved contexts.
CPU and IOMMU alternatives are selected at boot behind shared contracts; see
[platform portability](kmodules/shared/portability.md).

Application-facing handles/handlers, userland, filesystems, USB core/HID and SATA
drivers do not exist in this rewrite yet. The xHCI transport supplies root-port,
slot/endpoint and asynchronous transfer operations for the future USB layer. NVMe is an initial kernel-only read path. The superseded implementation is preserved locally in `../.src_old/`, excluded
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
owned registry; an explicit caller opens a controller. USB core/HID and SATA
drivers remain future work.

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
Cancellation/process teardown, hotplug, migration and load balancing are not implemented.

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
BOOT OK, then "managed BSP event stack active". The candidate creates two production
deferred workers, submits no demo/test tasks, and sleeps when they are blocked.

The [profiling surfaces](kmodules/shared/profiling.md) report lock wait/hold/attribution,
managed IRQ-off intervals and runnable waiting; raw local TSC ticks include host
vCPU pauses. [Device IRQ routing](kmodules/shared/irq.md) adds exclusive IOAPIC GSI routes,
MSI/MSI-X messages, preallocated/coalesced notifications and foreground drain/rearm.
It does not program PCI capabilities, resolve ACPI PCI INTx routing or initialize
USB/storage devices. Controller-specific work belongs to NVMe/xHCI drivers;
USB enumeration/HID remain deferred. IRQ affinity is CPU0.
Production boot creates no test routes or periodic device polling.

## Focused validation

```sh
make -C src test-portability
make -C src test-threads-smp
make -C src BUILD=../bin/tests/isolated test-clock test-tsc-validation test-irq test-timer test-apic test-pages
make -C src test-inthandle-faults
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

```
src/
├── assets/
│   └── cloudberry.png
├── boot/
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
│   │   ├── apic-platform.h
│   │   ├── apic-x86.c
│   │   ├── apic.c
│   │   ├── apic.h
│   │   ├── apic.json
│   │   ├── apic.md
│   │   ├── clock-platform.h
│   │   ├── clock-x86.c
│   │   ├── clock.c
│   │   ├── clock.h
│   │   ├── clock.json
│   │   ├── clock.md
│   │   ├── cpu-ops.c
│   │   ├── cpu-platform.h
│   │   ├── cpu-topology.c
│   │   ├── cpu-tsc.c
│   │   ├── diagnostics.h
│   │   ├── event.c
│   │   ├── event.h
│   │   ├── event.json
│   │   ├── event.md
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
│   │   ├── pci-platform.h
│   │   ├── pci-x86.c
│   │   ├── pci.c
│   │   ├── pci.h
│   │   ├── pci.json
│   │   ├── pci.md
│   │   ├── portability.md
│   │   ├── profiling.md
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
│   │   ├── usb-host.h
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
│   ├── apic.c
│   ├── backend-contracts.py
│   ├── clock-boot.c
│   ├── clock.c
│   ├── cpu-tsc.c
│   ├── cpu-topology.c
│   ├── events-boot.c
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
│   ├── probe-xhci.c
│   ├── probe.c
│   ├── probe.h
│   ├── probe.json
│   ├── reservations.c
│   ├── scheduler-boot.c
│   ├── scheduler.c
│   ├── splash.c
│   ├── stack-call.S
│   ├── smp-clock.c
│   ├── threads-boot.c
│   ├── timer.c
│   ├── tsc_profile.py
│   ├── tsc-validation.c
│   ├── xhci-boot.c
│   ├── xhci.c
│   └── xstate-host.c
├── tools/
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

Current next priorities are clock boot-cost reporting, USB core/HID on xHCI,
and process address spaces/teardown; see [roadmap](ROADMAP.md#next-priorities).


[xHCI transport](kmodules/shared/xhci.md) is loaded/initialized by the kernel and
publishes the owned `xhci.controllers` registry alongside 14 method surfaces.
Controller instances use PCI/DMA/IOMMU/IRQ/mem/clock/thread hub facilities. Transfers
have wake-driven completion waiting; detach generations reject stale tokens.
Controlled close/reopen supports restart after quiescence; module-image unloading
and the driver manager remain future work. Isolated tests validate actual QEMU
DMA/MSI-X transfers, cancellation and USB unplug/replug with one/four CPUs and
both emulated IOMMUs. Native/physical xHCI acceptance remains pending.
