#ifndef CLOUDBERRY_PCI_H
#define CLOUDBERRY_PCI_H
#include "module.h"
#include "mem-pages.h"
#include "irq.h"
struct pci_bar {uint64_t physical,bytes;uintptr_t address;};
struct pci_device {uint64_t hardware_index,owner;struct pci_bar bars[6];uint16_t msix_cap,msix_count;uint32_t reserved;};
struct pci_msix {uint64_t hardware_index,owner;uint32_t entry,masked;struct irq_route route;};
/* BSP foreground only, trusted owners. claim(index,owner,out) takes exclusive
 * boot ownership, disables DMA/INTx/MSI-X, probes and restores BARs with decode
 * disabled. Never reassigns BAR addresses. Caller resets the device before DMA.
 * busmaster(index,owner,enable); dma_alloc(index,owner,pages,out mem_pages);
 * dma_free(index,owner,id) requires bus mastering disabled; release likewise,
 * with no outstanding DMA allocations and hardware_quiesced=1. Hardware must
 * finish/reset before freeing DMA; clearing bus mastering is not a DMA drain.
 * msix(request) masks the entry/function while programming one owned entry.
 * DMA ownership/pinning/translation is delegated to iommu. Unsupported firmware
 * fails closed. A failed mapping may return an owned cleanup descriptor/ID; retain
 * it and retry dma_free after quiescence. No port BARs/bridges/shared INTx. */
struct kernel_surface_result pci_surface_init(MODULE_ARGS);
struct kernel_surface_result pci_surface_claim(MODULE_ARGS);
struct kernel_surface_result pci_surface_busmaster(MODULE_ARGS);
struct kernel_surface_result pci_surface_msix(MODULE_ARGS);
struct kernel_surface_result pci_surface_dma_alloc(MODULE_ARGS);
struct kernel_surface_result pci_surface_dma_free(MODULE_ARGS);
struct kernel_surface_result pci_surface_release(MODULE_ARGS);
#endif
