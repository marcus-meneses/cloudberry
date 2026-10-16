# Requester-owned DMA translation

`iommu.kmod` provides one DMA contract with separate Intel VT-d and AMD-Vi
backends. These are different hardware architectures; neither backend implies
support for every firmware topology. This is tightly coupled kernel code, not an
application-facing handler.

`init(hub,heap)` reads the hwenum ACPI inventory, validates DMAR/IVRS record bounds
and caches routing/ownership state. It does not enable translation. First
`attach(hardware_index,owner)` takes a requester domain, disables firmware bus
mastering on enumerated endpoints before activation, and establishes empty tables.
PCI owns the endpoint and must reset/quiesce it before enabling DMA. Firmware-active
translation engines are rejected rather than overwritten.

Intel uses legacy root/context tables and supported three/four-level (39/48-bit) second-stage translation.
Non-coherent page-table walkers receive CLFLUSH plus ordering/write-buffer
handling; coherent walkers avoid those flushes. Context and IOTLB invalidation
complete before publishing/revoking access. AMD uses device tables, v1 page tables,
a command ring and completion-write acknowledgements. Unowned AMD entries have
valid translation state with no DMA permissions, preventing V=0 bypass. Compatible
MSI delivery is retained; interrupt remapping is not enabled.

`map(index,owner,mem_pages*)` accepts exact owned physical allocations, pins their
ledger reservation and installs RW identity IOVAs. Translation permits only those
pages; an identity numeric address does not mean unrestricted identity mapping.
Translated identity IOVAs are limited by the selected unit's supported geometry:
Intel 39/48 bits, AMD 48 bits. Non-identity IOVA allocation, ATS/PRI/PASID and
shared alias groups are not implemented. PCI refuses firmware-enabled
ATS/PRI/PASID. Intel include-all, endpoint bridge paths and hierarchy scopes plus
AMD segment-zero basic ranges are supported. Ambiguous bridge scopes,
reserved-memory requirements for an endpoint, AMD alias/extended layouts and
active firmware engines fail closed. Duplicate IVHD records are parsed and checked
against existing routing; they are never skipped without validating their entries.
Table backing is retained until reboot.

`unmap(index,owner,id)` clears entries, waits for invalidation acknowledgement and
then removes the pin. `detach(index,owner,hardware_quiesced=1)` requires no live
mappings. PCI additionally requires bus mastering disabled before freeing DMA or
releasing an endpoint; its owner must observe device reset/drain completion.
Clearing bus mastering alone is not proof that previous transactions finished.

A failed map with value=1 retains a pinned record, including any mapped prefix.
PCI returns the allocation descriptor/cleanup ID even on that failure. Invalidation
timeout marks the engine failed and retains ownership/storage; uncertain DMA
memory is never recycled. No automatic unsafe teardown or retry is attempted.
Polling has a one-second clock deadline and an iteration fallback. No-IOMMU mode
still tracks/pins allocations but cannot isolate a device from other memory.

`info(out)` consumes bounded fault-record batches and reports backend, engines,
active domains/pages, invalidations, faults and last fault/error. `ready(index,owner)`
checks domain health; recorded requester faults prevent further DMA enabling and
make NVMe reject completion data before copying it. Cleanup remains possible for
quiesced domains. `lookup(index,owner,iova)` diagnoses mappings without issuing DMA.
Fault interrupts remain masked; fault collection is foreground work.

Isolated Intel/AMD QEMU tests perform real NVMe DMA/MSI-X reads, reset/reopen and
prime the hardware IOTLB with a successful DMA write to a canary page, revoke
that exact grant, then repeat the read. The revoked write is blocked and the
canary stays unchanged, testing invalidation rather than only an unused address.
Intel records the hardware fault. QEMU 6.2's AMD walker returns early for a zero
PTE without logging an event, so that emulator's missing-PTE event path is explicitly
unverified; the acceptance report records the limitation. Host tests separately
validate AMD event and Intel fault-record decoding, firmware bounds and DMA cleanup
retention. Physical hardware, interrupt remapping and broader firmware layouts
remain unvalidated.

References: [Intel VT-d architecture specification](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/vt-directed-io-spec.pdf),
[AMD IOMMU specification](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/specifications/48882_IOMMU.pdf),
[QEMU 6.2 AMD walker](https://github.com/qemu/qemu/blob/v6.2.0/hw/i386/amd_iommu.c).

CPU-independent boot selection loads iommu-intel or iommu-amd through the same
backend contract. AMD uses four-level 48-bit translation with coherent table
fetches. Intel bridge paths/hierarchy scopes are resolved, while reserved-memory
endpoints remain refused. Bus-master readback and PCIe pending-transaction waits
do not prove whole-platform firmware DMA quiescence. Fault-log overflow fails
health closed. See [portability](portability.md) for exact coverage and the new
iommu.platform descriptor, which leaves iommu.info unchanged.
