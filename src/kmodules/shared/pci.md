# Owned PCI configuration and DMA

`pci.kmod` supplies BSP foreground ownership of enumerated PCI endpoints. It uses
hwenum's ECAM addresses and mmio's checked mappings; it does not duplicate PCI
enumeration. Definitions are in [pci.h](pci.h).

- `init(hub,heap)` allocates endpoint ownership records and publishes the owned
  `pci.devices` inventory, without claiming devices. Bridge bus information
  identifies parents; this does not add bridge ownership or BAR relocation.
- `device(hardware_index,out)` snapshots identity, parent and companion metadata.
  `associate(hardware_index,acpi_ref)` sets a boot-time generation-qualified ACPI
  companion before driver ownership; an existing different reference is refused.
- `firmware_read/write(access)` adapt AML PCI operation regions through enumerated
  ECAM. Writes refuse owned devices, headers/BARs, extended configuration and
  the standard capability area. These conservative accesses never claim devices.
- `claim(hardware_index,owner,out)` requires an unclaimed type-0 endpoint and a
  nonzero owner. It disables bus mastering, port/memory decode and old message
  interrupts, walks standard capabilities with bounds/cycle checks, probes each
  assigned memory BAR with decoding disabled, restores BAR values, checks sizes
  and alignment. ABI1.1 adds an optional fourth native argument, a BAR mask:
  zero preserves full mapping; otherwise only requested BARs and MSI-X table/PBA
  BARs are mapped. Unselected extents retain physical/size metadata with address
  zero. This lets GPU control mappings avoid a firmware framebuffer BAR without
  weakening cache-type protections. Memory decoding is then available;
  bus mastering remains off. No BAR relocation is performed.
- `busmaster(index,owner,enable)` explicitly controls DMA enabling.
- `dma_alloc(index,owner,pages,out)` returns tracked, zeroed, physically contiguous
  page-aligned memory. `dma_free(index,owner,id)` requires bus mastering off.
- `msix(request)` programs an owned, bounds-checked table entry using an irq route
  message, masking the entry/function while changing it. MSI-X remains enabled
  with the requested entry mask. The driver supplies queue/vector associations.
- `msi(request)` uses the same request structure and IRQ_MSI route contract,
  with entry zero only. It disables delivery while programming the device's
  32/64-bit message address and 16-bit data, clears Multiple Message Enable,
  and enables exactly one vector. A masked request disables MSI and masks its
  optional per-vector mask. Capability extent, owner and address width are checked.
- `release(index,owner,hardware_quiesced=1)` requires DMA disabled and no tracked
  buffers. It restores command settings with bus mastering kept disabled;
  firmware's old DMA queues/interrupt enable state are never resurrected.

Clearing bus mastering alone does not prove old transactions have drained. The
owner must reset/quiesce its device and observe completion before freeing DMA or
releasing routes. Initialization failure can leave boot-lifetime MMIO mappings,
which are retained rather than reclaimed unsafely.

PCI delegates DMA attachment, pinning, translation and invalidation to
[iommu](iommu.md), with Intel VT-d and AMD-Vi backends. Supported firmware no longer
causes blanket DMA rejection. Unsupported scopes/features still fail closed.
DMA allocation/free/release run outside an operation-wide global lock; per-device
operation ownership prevents concurrent enabling/release. A failed map returns
its retained allocation descriptor/ID for explicit cleanup; it must not be lost. There is no bridge ownership/port-BAR support, INTx routing, hotplug,
interrupt remapping, or arbitrary DMA address-width support. Active ATS/PRI/PASID is rejected. Generic routing
produces MSI messages; this module programs both MSI and MSI-X capabilities.

Focused UBSan host tests cover BAR probe/restore, malformed capability cycles,
ownership, MSI/MSI-X bounds, failed allocation cleanup, DMA lifetime and IOMMU
rejection. Isolated NVMe boots validate the adapter against actual QEMU ECAM and
MSI-X delivery; focused AHCI boots validate MSI completion delivery.
