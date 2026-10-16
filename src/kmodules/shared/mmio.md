# mmio

`mmio.kmod` is an independent kernel module loaded alongside mem, hwenum,
inthandle and scheduler. After hardware enumeration, boot calls `mmio.init` with
the hub and shared bootstrap heap. It publishes the boot-lifetime `mmio.regions`
table and four version 1.0 surfaces. Structures and register arguments live in
[mmio.h](mmio.h); hub entries contain only names, versions and callable addresses.

The table contains physical base, extent, mapped virtual address, source index
in `hwenum.hardware`, region kind, PCI BAR number and flags. Generation includes
HPET, local APIC, I/O APIC, each discovered PCI function's ECAM configuration page,
and assigned memory BARs. It decodes 32/64-bit BARs, skips paired high words,
port-I/O BARs, unassigned BARs and unsupported encodings, and respects the number
of BARs in each PCI header type. It does not enumerate USB endpoint registers or
probe unreported devices. Generating the table neither maps nor writes devices.

PCI memory BARs initially have zero extent: raw BARs do not encode their size.
Their driver must supply a size from its device contract or a separate safe
BAR-sizing procedure. The module does not perform disruptive write-all-ones
probing or enable PCI memory decoding/bus mastering. Prefetchability is recorded
as information; all current mappings use uncached access, including prefetchable
BARs. The table is a live kernel-owned structure; consumers must not edit it.

- `mmio.init(hub,heap)` returns the table address. Repeating the same configuration
  returns the existing table; a different configuration fails. Allocation or
  publication failure frees temporary storage and permits retry.
- `mmio.map(index,bytes)` returns the region's virtual address. Indexes start at
  zero. Zero bytes uses a known extent; an unsized BAR requires an explicit size.
  A known extent cannot be changed. Mapping rejects overflow and overlap with
  another sized region. Successful mappings update the published region in place.
- `mmio.read(index,offset,width)` returns a register value.
- `mmio.write(index,offset,width,value)` returns 1 on success. Values too large
  for the requested width fail rather than silently truncating.

Read/write require a mapped region, natural physical alignment, bounds and width
1, 2, 4 or 8 bytes. Each operation uses one typed volatile access with x86 memory
fences before and after. The device's own contract still determines which
registers, widths, read side effects and write values are legal. Fences do not
replace device-specific posted-write readback, locking or protocol sequencing.
This module supplies access primitives, not timer/controller drivers.

The kernel exports `hardware.map_mmio(physical,bytes)` to create page-rounded
RW/NX/uncached mappings in the existing `0xffffc00000000000 + physical` window.
Physical addresses must be nonzero and below 32 TiB. The mapper verifies PAT
entry 3 is UC and rejects pages overlapping any original memory-map type other
than reserved: RAM, ACPI storage, loader/kernel storage and the temporary
framebuffer cannot be remapped as generic device memory. Reserved ranges and
memory-map gaps may contain MMIO. Mapping allocates page tables, not device RAM.
Existing read-only UC mappings can become writable; subsequent `hardware.read`
uses the same UC mapping without creating a conflicting cache alias. Accessed
and dirty PTE bits are tolerated on reuse.

Mediated calls are trusted kernel calls serialized by the recursive CPU lock.
MMIO mapping/permission changes wait for remote TLB invalidation. These are not
ISR-safe allocating calls. No user authorization, per-process isolation, MMIO
unmapping or hotplug exists yet.
Mappings and their page tables remain until reboot. A mapping failure leaves the
region unpublished as mapped; any pages already installed remain and can be
reused on retry. Mapping a reserved address cannot establish that hardware is
actually present; enumeration and the driver must establish that first. The MMIO module does not enable IRQs, APIC modes or timers. Clock initialization
subsequently maps HPET and enables its main counter; it leaves comparators and
interrupt routing unchanged.

Focused validation:

```sh
make -C src test-mmio
make -C src test-mmio-boot
```

The UBSan host test checks table publication and allocation-failure cleanup, BAR
pairing/header types, unmapped access, mapping failure/reuse, overlap, integer
overflow, bounds, alignment, widths and read/write. The disposable modern-bus
TCG boot invokes the actual module through the hub, rejects a RAM mapping, reads
HPET capabilities through a read-only mapping, upgrades it to RW, writes back the
unchanged timer configuration, and verifies later read-only-facility reuse. It
preserves the timer's configuration and enables no hardware interrupt.
