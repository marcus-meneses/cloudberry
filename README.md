<p align="center">
  <img src="src/assets/cloudberry.png" alt="Cloudberry logo" width="240">
</p>

# Cloudberry

An operating-system project exploring software built around explicit, replaceable
resource contracts.

I'm [Marcus Meneses](https://github.com/marcus-meneses), a software engineer.
Cloudberry is my way of exploring a question that reaches beyond kernel code:
**how much simpler could software become if the operating system made resources
interchangeable by design?**

## Why I'm building it

Applications spend substantial effort adapting to different libraries, services,
storage systems and communication mechanisms. I want to explore an OS where
applications request a capability through a defined interface, while the system
supplies the implementation and enforces access.

A filesystem could be local, remote or backed by an object-storage service.
The application would use the agreed filesystem contract. Differences in latency,
failures and supported operations would still be explicit; moving a resource
would not require rebuilding the application's integration around another API.

The original name, TheseOS, came from the Ship of Theseus: replace the parts while
preserving the identity of the whole. That remains Cloudberry's central idea.

I'm also interested in making this a platform where AI-assisted development is
practical: discoverable interfaces, versioned contracts, explicit permissions and
inspectable failures should help both people and development tools understand
what an application can do.

## The intended model

A **handler** provides a resource or service. A **handle** is an OS-granted
reference through which an application invokes its permitted operations.

The design calls for:

- Resource-specific operations behind versioned contracts, with reflection limited
  to the operations and data the caller is authorized to access.
- Application manifests declaring requested access, with OS-mediated authorization.
- A stable C ABI, first-class C support and C++20 developer wrappers, with future
  language bindings generated from interface definitions.
- Application-facing contracts that can span in-process, cross-process and remote
  providers, with explicit failure and execution semantics.

These are architectural goals. The current kernel modules are trusted, coupled
kernel components in a shared address space; they are the foundation underneath
the isolated handler/handle layer.

## What runs today

The active implementation boots through Limine and UEFI on modern Intel/AMD
x86-64 hardware, targeting machines from roughly 2016 onward. Development uses
QEMU with PCIe, NVMe, AHCI SATA and xHCI USB devices.

Implemented foundations include:

- Independently loaded format3 kernel modules with a dynamic ABI3 method/table/provider hub.
- Stable published entries, explicit provider ownership, queued/active call leases and cooperative retirement.
- Limine-loaded text configuration parsed into owned typed hub references.
- A build-prepared 512 MiB ext4 driver partition, unique-GUID bootstrap selection and boot-origin reporting.
- Explicit future disk-module loading with versioned dependency leases, hidden initialization/startup and failure diagnostics.
- Owned memory allocation, guarded stacks, private address-space roots and SMP TLB coordination.
- Preemptible kernel threads, SMP startup, deadline scheduling and deferred IRQs.
- Calibrated clocks, cross-CPU TSC qualification and HPET fallback.
- Hardware/MMIO discovery, PCI ownership and initial Intel/AMD IOMMU backends.
- ACPI AML device enumeration, PCI firmware companions and a platform-device inventory.
- USB core with root/downstream enumeration, interface ownership and event-driven hotplug.
- Boot-loaded USB BOT/SCSI read/write/flush, GPT GUID selection and journaled ext4 file mutations.
- External hub class module with nested topology and interrupt-driven port changes.
- Multiple active NVMe controllers/namespaces and AHCI controllers/ports, with independent lifetimes.
- USB removal cleanup through storage, partition views and abortable ext4 mounts.
- An xHCI transport with asynchronous transfers, hotplug detection and controlled controller restart.
- Static ext4 ELF processes, ring-3 fault containment and owned address-space cleanup.
- Copied capability calls, boot manifest grants, filtered reflection and scoped memory/I/O.
- A disk-loaded isolated `/@keyboard` provider with IBM/ABNT2 layouts and USB hotplug.
- Declarative provider startup, handler-owned configuration, explicit stop/reap ownership and copied status snapshots.
- A privileged userland `/@applications` lifecycle service, build-owned installation bindings, and ordinary native apps.
- Replaceable text → graphics → display providers, software graphics primitives, owned surfaces and modification tracking.
- An ordinary terminal with explicit stdin/stdout/stderr handles, concurrent pipelines and ext4 file redirection.

Keyboard, display, graphics, text, filesystem and application providers run through
the handle boundary.
Installation identity is bound by the immutable build-owned catalog; general per-user
installation policy, interactive approval, automatic provider recovery,
general HID formats and runtime ACPI notification/driver policy remain ahead.
QEMU acceptance includes isolated one/four-CPU runs with emulated IOMMU models.
AMD-host KVM validates SMP, clocks, extended state and the native process/input/handle
boundary across nine repeated one/four-CPU boots. Physical-device and native Intel
CPU acceptance remain pending.

The [current assessment](src/README.md#current-assessment-2026-10-19) separates
implemented mechanisms, validation and remaining limits. Foundational kmodules
remain Limine-loaded; the first isolated keyboard handler comes from the ext4
bundle. The full sequence is tracked in the [roadmap](src/ROADMAP.md).

## Build and run

Follow the [Ubuntu toolchain setup](scripts/README.md) first, then run from the
project root:

```sh
make -C src build
make -C src run QEMU_ACCEL=kvm QEMU_CPUS=4
```

For software emulation, use `make -C src run QEMU_ACCEL=tcg`.
Build outputs live in `bin/`; the local cross-toolchain lives in `.toolchain/`.
Automated boot tests use separate images and disposable disks.

## Explore the project

- [Implementation overview and directory tree](src/README.md)
- [Roadmap and validation history](src/ROADMAP.md)
- [Kernel module contracts](src/kmodules/README.md)
- [Startup memory ownership](src/maps/startup.md)
- [Tests and acceptance evidence](src/tests/README.md)

The superseded implementation is preserved locally in `.src_old/`, outside Git
and the active build. The public repository tracks the current `src/` tree.
