#ifndef CLOUDBERRY_PCI_H
#define CLOUDBERRY_PCI_H
#include "module.h"
#include "mem-pages.h"
#include "irq.h"
#include "device.h"
struct pci_bar {uint64_t physical,bytes;uintptr_t address;};
#define PCI_TABLE_NAME "pci.devices"
#define PCI_TABLE_MAGIC UINT64_C(0x3149435041424343)
struct pci_record {struct device_identity identity;uint64_t hardware_index,parent_hardware_index,config_physical;uint16_t segment;uint8_t bus,slot,function,header_type,secondary_bus,subordinate_bus;};
struct pci_table {uint64_t magic;uint16_t major,minor;uint32_t entry_bytes;uint64_t count;struct pci_record *entries;};
struct pci_firmware_access {uint16_t segment;uint8_t bus,slot,function,width;uint16_t offset;uint32_t value;};
struct pci_device {uint64_t hardware_index,owner;struct pci_bar bars[6];uint16_t msix_cap,msix_count;uint32_t reserved;struct device_acpi_ref acpi;};
struct pci_msix {uint64_t hardware_index,owner;uint32_t entry,masked;struct irq_route route;};
/* BSP foreground only, trusted owners. claim(index,owner,out,optional_bar_mask) takes exclusive
 * boot ownership, disables DMA/INTx/MSI-X, probes and restores BARs with decode
 * disabled. ABI1.1: mask 0 maps all memory BARs, preserving old callers;
 * otherwise map selected BARs plus MSI-X table/PBA BARs. Unselected BARs keep
 * physical/bytes metadata with address=0. No RAM/framebuffer cache-type override.
 * Never reassigns BAR addresses. Caller resets the device before DMA.
 * busmaster(index,owner,enable); dma_alloc(index,owner,pages,out mem_pages);
 * dma_free(index,owner,id) requires bus mastering disabled; release likewise,
 * with no outstanding DMA allocations and hardware_quiesced=1. Hardware must
 * finish/reset before freeing DMA; clearing bus mastering is not a DMA drain.
 * msix(request) masks the entry/function while programming one owned entry.
 * DMA ownership/pinning/translation is delegated to iommu. Unsupported firmware
 * fails closed. A failed mapping may return an owned cleanup descriptor/ID; retain
 * it and retry dma_free after quiescence. No port BARs/bridges/shared INTx. MSI supports one owned vector. */
struct kernel_surface_result pci_surface_init(MODULE_ARGS);
struct kernel_surface_result pci_surface_claim(MODULE_ARGS);
struct kernel_surface_result pci_surface_busmaster(MODULE_ARGS);
struct kernel_surface_result pci_surface_msix(MODULE_ARGS);
/* msi(pci_msix request): entry must be zero; masked disables MSI. Programs
 * one 32/64-bit MSI message with MME=0. Same owned IRQ_MSI route contract. */
struct kernel_surface_result pci_surface_msi(MODULE_ARGS);
struct kernel_surface_result pci_surface_dma_alloc(MODULE_ARGS);
struct kernel_surface_result pci_surface_dma_free(MODULE_ARGS);
struct kernel_surface_result pci_surface_release(MODULE_ARGS);
/* Companion changes are boot-only and refuse claimed devices or duplicates.
 * associate(hardware_index,ref) updates the operational record; device(index,out)
 * snapshots discovery/ownership even when unclaimed. firmware_read/write(request)
 * validates BDF/width/bounds. Firmware writes refuse owned devices and resource,
 * command and capability registers managed by the PCI owner. */
struct kernel_surface_result pci_surface_device(MODULE_ARGS);
struct kernel_surface_result pci_surface_associate(MODULE_ARGS);
struct kernel_surface_result pci_surface_firmware_read(MODULE_ARGS);
struct kernel_surface_result pci_surface_firmware_write(MODULE_ARGS);
#endif
