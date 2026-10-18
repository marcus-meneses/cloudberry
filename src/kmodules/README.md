# Kernel modules

Kmodules are coupled kernel methods sharing an address space and normal C ABI:
six register arguments, value/status in RAX/RDX. They are not handlers or processes.
A format3 PIC image and JSON export manifest describe named/versioned surfaces;
the generic loader maps RX code and RW/NX state and generates mediation entries.
Hub ABI3 has a stable header and separate growable method/table/provider arrays.
Records and callable entries never move; physical pages are allocated on demand.
[Ownership and lifetime](../kernel/hub.md) defines boot pinning, active calls,
retained callback leases and cooperative retirement. Image unmapping and table
withdrawal remain unsupported. Current modules stay Limine-loaded; only selected
future modules will use the ext4 driver directory.

| Module | Responsibility and contract |
| --- | --- |
| [config](shared/config.md) | Strict boot-text schema, owned typed values and atomic config.<key> hub references |
| [acpi/platform](shared/acpi.md) | AML namespace, stable firmware companions and non-PCI platform inventory; runtime SCI/driver policy deferred |
| [usb](shared/usb.md) | Root/downstream enumeration, configuration validation, interface ownership and scoped synchronous I/O; HID/UAS class drivers deferred; BOT storage implemented |
| [usb-hub](shared/usb-hub.md) | Hub discovery, owned port registry, power/reset, nested enumeration and interrupt-driven hotplug |
| [xhci](shared/xhci.md) | Owned controller/port registry, command/event rings, root/downstream slots/endpoints, asynchronous transfers and restart cleanup; HID/UAS class drivers deferred; BOT storage implemented |
| [mem](shared/mem-pages.md) | Caller-owned heaps, owned pages/guarded stacks and [private address-space roots](shared/mem-spaces.md) |
| [inthandle](shared/inthandle.md) | Early faults, per-CPU descriptor tables, owned vectors and CMOS/NMI policy |
| [hwenum](shared/hwenum.md) | Firmware/PCI enumeration and hardware descriptor table/dump |
| [mmio](shared/mmio.md) | Hardware region inventory and checked UC mapping/register access |
| [clock](shared/clock.md) | Validated TSC/HPET timestamps, shared HPET mode and RTC |
| [apic](shared/apic.md) | Global controller ownership and CPU-local controller setup/spurious delivery |
| [timer](shared/timer.md) | Per-CPU calibrated one-shots, EOI, notifications and thread preemption hook |
| [scheduler](shared/scheduler.md) | Deadline/FIFO callback records, copied payloads, process removal and results |
| [threads](shared/threads.md) | Independent guarded contexts, timer preemption, block/wake and teardown |
| [event](shared/event.md) | BSP dispatch, separate preemptible deferred workers and interrupt-driven idle |
| [smp](shared/smp.md) | Limine AP startup, CPU-local stacks/tables, shared clock, wake/TLB IPIs |
| [irq](shared/irq.md) | Owned IOAPIC routes/MSI messages and deferred device notifications |
| [iommu](shared/iommu.md) | Intel VT-d/AMD-Vi requester domains, pinned translations, invalidation and faults |
| [pci](shared/pci.md) | Exclusive endpoint/BAR/MSI/MSI-X ownership and tracked/pinned DMA |
| [nvme](shared/nvme.md) | Explicit controller/namespace block read/write/flush, completion IRQs and reset/quiescence |
| [sata](shared/sata.md) | Explicit AHCI controller/port block read/write/flush, MSI completion and reset/quiescence |
| [usb-storage](shared/usb-storage.md) | USB BOT/SCSI block read/write/flush and interface ownership |
| [partition](shared/partition.md) | Validated GPT GUID selection and bounded common-block slices |
| [ext4-fs](shared/ext4-fs.md) | Interchangeable block-backed ext4, journaled mutations and crash replay |
| [splash](shared/splash.md) | Temporary UEFI framebuffer validation and centered pre-flight logo |

Mem core primitives implement malloc/free/calloc/realloc/aligned allocation and
usable size using caller-owned state. Segregated bins, boundary tags, splitting
and coalescing reuse blocks; separate backing regions never coalesce. Invalid
pointers/double free are caller errors. Failed realloc preserves the original;
zero-size and alignment rules are described in mem.h. Configured adapters grow
heaps through memory.reserve; heap backing is retained, while owned PAGES/STACK
allocations support release. Raw primitives need external locking.

Mediated mem/memory, hwenum, PCI, mmio, clock and callback scheduler surfaces share
ordered global/clock CPU locks with IF preservation. Interrupt binding mutations use that
lock; hardware readers take atomic sequence snapshots. Timer state is CPU-local.
Stack allocation/free use internal short critical sections and bounded mapping
batches rather than one operation-wide lock. Thread control protects short transitions without holding the lock across thread
execution or context switches. Callback invocation releases its queue lock and stays synchronous on its caller.
Clock-only operations never acquire the global lock; init/RTC/activation acquire
global before clock. Lock-order inversion is fatal. Never call allocating/foreground methods from a hardware ISR.
Hub publication uses a separate nonblocking writer token, with short ledger/map
critical sections and no lock across bulk construction or SMP invalidation.
New modules must supply their own shared-state/reentrancy contract.

Threads are pinned, preemptible outside critical sections and eagerly preserve
x87/SSE/available AVX through [xstate](shared/xstate.md). Infrastructure uses
general registers only. Migration/balancing, CPU hotplug and process lifetime are future work. USB
device/hub hotplug is implemented. SMP begins on HPET and requires successful uncertainty/offset/drift
validation before TSC activation, retaining HPET health checks/fallback. Shared kernel mapping changes wait for every online CPU; private-root changes
invalidate CPUs using that space before physical reuse. ABI2 kernel threads lease
owned private roots and restore the kernel dispatcher root on suspension; ring 3
and application interfaces remain deferred.
Owners must quiesce in-flight IRQ callbacks before freeing a binding's context.

Focused test targets include test-hub, test-hub-boot, test-block-boot, test-config, test-config-boot, test-cpu-tsc, test-cpu-ordering, test-clock, test-timer, test-apic, test-pages,
test-scheduler, test-irq, test-acpi, test-acpi-boot, test-usb, test-usb-boot, test-usb-hub, test-usb-hub-boot, test-pci, test-tsc-validation and test-threads-smp. The latter uses one/four-CPU disposable TCG
boots, including timer preemption on every CPU and remote cached mapping reuse.
These boots also validate real NVMe MSI-X reads and lost-completion timeout/reset
on read-only disposable media. Native acceptance is available through test-native; physical-machine validation
remains outstanding. See ../tests/README.md for commands and report locations. Test builds and
media stay below bin/tests/isolated; no probe enters candidate media.

See ../ROADMAP.md for completed milestones and remaining priorities.


[Profiling](shared/profiling.md) exposes per-CPU/domain lock timings/attribution, managed
IRQ-off intervals and runnable waiting. IRQ notifications report deferred delay.
Tick durations include host vCPU pauses and are not guest CPU execution time.
The new irq foundation is CPU0-owned; MSI/MSI-X device programming, ACPI PCI
routing and affinity are not supplied by generic routing. PCI now owns MSI-X
programming/tracked DMA; iommu provides translated requester domains; NVMe owns its queues and device reset.

Sources live in shared/, intel/ and amd/. Only selected CPU/IOMMU alternatives
are mapped at boot; [portability contracts](shared/portability.md) document selection,
shared interfaces and hardware acceptance limits.


The I/O module now also contains `shared/io-process.c`, the catalog-bound process
execution/notification bridge. Lifecycle policy lives in the privileged userland
[applications handler](../handlers/applications/README.md), not in bootstrap or a
new kernel application-manager module. `THREAD_WAIT` adds allocation-free join
registration and wake-driven full-thread completion before owned reap.
