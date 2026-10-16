# hwenum

`hwenum.kmod` is loaded separately by Limine and mapped as synchronous kernel
code. Its implementation is in `hwenum.c`; structures and typed methods are in
`hwenum.h`, with named export adapters in `hwenum-module.c` and `hwenum.json`. It has no writable globals, libc dependencies or link dependency
on the mem implementation.

Hub methods:

- `hwenum.enumerate(heap, boot)` returns the allocated hardware table. The kernel
  wrapper supplies the hub to the module. `boot` supplies the physical RSDP
  address and the original Limine memory map. Repeat calls return the published
  snapshot without allocating or rescanning.
- `hwenum.find(table, kind, ordinal)` returns a descriptor, or failure when there
  is no matching zero-based ordinal. Kind zero includes all descriptor kinds.

The module discovers physical memory-map ranges, checksummed RSDT/XSDT tables,
FADT-referenced DSDT, MADT local/x2APIC CPUs and interrupt controllers, HPET and
all PCI functions in the bus/segment ranges advertised by MCFG. PCI enumeration
uses ECAM through `hardware.read`; it covers multifunction devices and nonzero
segment/bus ranges. It records vendor/device IDs, BDF, class/subclass/interface,
revision, header type, raw BARs and interrupt line/pin. It does not size BARs,
write configuration registers, enable interrupts or initialize devices.

This is firmware/PCI discovery, not exhaustive child-bus discovery. USB endpoints,
NVMe namespaces, SATA targets and AML-described platform devices require their
respective bus drivers or an AML interpreter. Their controllers are discoverable
now. Coverage bits report which supported discovery categories were available;
missing optional MADT/MCFG tables do not pretend those categories were scanned.

Allocation goes through `mem.calloc`, `mem.realloc` and `mem.free` hub methods.
The descriptor vector starts with 16 entries and grows geometrically; 16 is an
initial capacity, not a device-count limit. Checks cover size arithmetic, malformed
ACPI lengths/checksums/records, MCFG ranges, read/allocation failure and failed
publication. Failure frees the candidate table and leaves the hub unchanged.
Only a complete successful pass is published. The table includes its format
version, coverage, count, stride and owning heap context.

`hub.publish_table(descriptor)` copies a `kernel_hub_table` record into the hub's
compact table directory through the kernel's writable alias. It validates name
termination, range/stride/count arithmetic, uniqueness and available hub capacity.
Hub ABI2 stores compact methods and table references; argument definitions remain
in headers. The published record `hwenum.hardware` contains the table address, byte size,
count, descriptor stride and entries offset. The hardware records themselves
live in mem-allocated storage outside the hub page. Modules never write the
read-only hub mapping directly. Publication is trusted kernel code and currently
serialized by boot; it is not a userland authorization mechanism.

The snapshot and its owning heap have boot lifetime. Unpublish, refresh, hotplug
and module unload are not implemented. Normal boot starts a private heap with
one backing page and a context described by mem. Configured mem adapters request
additional reserved pages as needed; there is no fixed 64 KiB inventory heap.
Exhaustion still fails enumeration without publishing a truncated inventory.
The module also accepts a separately provisioned caller heap. No process memory
manager or device driver is introduced here.

Memory records carry HWENUM_MEMORY_FIRMWARE_MAP: original availability is distinct
from current ownership. Consumers must also consult the live memory.reservations
hub table, whose linked chunks include allocated modules, hub, page tables, heap
contexts/backing and reservation metadata. Boot diagnostics say firmware-usable
to avoid presenting those original ranges as currently free.

The kernel's `hardware.read(physical, destination, bytes)` uses supervisor-only,
read-only, NX, uncached mappings in a separate physical-address window. PCI
configuration DWORD reads stay 32-bit accesses. These mappings persist for boot
lifetime, are reused for subsequent reads and accept physical ranges below
32 TiB. Firmware/physical addresses are trusted; this primitive is not safe for
arbitrary untrusted addresses and is not a fault-recovery mechanism.

Validation:

```sh
make -C src test-hwenum
make -C src test-hwenum-boot
```

The host test uses synthetic firmware and PCI data, real mem allocation, UBSan,
nonzero segment/start-bus, sparse multifunction records, x2APIC/address overrides,
vector growth, idempotence and rollback on malformed data/read/allocation/publication
failure. One modern-bus TCG boot validates both loaded modules, RX/NX mappings,
all mem calls, hub table validation and real memory/CPU/NVMe/AHCI/xHCI descriptors, alongside inthandle and generic export
registration checks.
Production boot has no test code; automated boot uses disposable virtual disks.

References: [ACPI system tables](https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html),
[PCI host bridge and MCFG address conventions](https://www.kernel.org/doc/html/latest/PCI/acpi-info.html).

`hwenum.dump()` validates and formats the published hardware table to the current
pre-flight debug sink (port 0xe9), returning its descriptor count. It neither
re-enumerates hardware nor modifies the snapshot. Main invokes this version 1.0
hub surface and does not import or interpret hardware descriptor definitions.
The output remains one line per record:
memory type/address/size, ACPI signature/revision, CPU UID/APIC/flags, PCI
BDF/IDs/class/revision/header/IRQ/raw BARs, and APIC/HPET addresses. Output goes
to the host terminal through port 0xe9; the framebuffer retains the logo.
