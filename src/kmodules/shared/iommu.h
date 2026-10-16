#ifndef CLOUDBERRY_IOMMU_H
#define CLOUDBERRY_IOMMU_H
#include "module.h"
#include "mem-pages.h"
enum iommu_backend {IOMMU_NONE,IOMMU_INTEL,IOMMU_AMD};
/* Separate descriptor leaves the existing info ABI unchanged. Width is zero
 * until a translation unit is active. VM acceptance is not physical takeover. */
struct iommu_platform {uint32_t abi,backend,firmware_supported,unit_count,minimum_iova_bits,busmaster_disabled,controller_quiescence_required,interrupt_remapping,firmware_takeover_verified,reserved;};
struct kernel_surface_result iommu_surface_platform(MODULE_ARGS);
struct iommu_info {uint32_t backend,units,active,failed,last_error,reserved;uint64_t attached,mapped_pages,invalidations,faults,last_fault_address;uint32_t last_fault_requester,fault_reason;};
/* Trusted BSP foreground only. init(hub,heap) discovers firmware without enabling
 * translation; attach(index,owner) selects an exclusively owned requester domain.
 * map(index,owner,mem_pages*) pins exact owned pages and installs RW identity IOVAs;
 * unmap(index,owner,id) removes them and waits for hardware invalidation before
 * unpinning. Failed map value=1 means retained/pinned storage, never free it.
 * detach requires no mappings and DMA quiescence asserted by the PCI owner.
 * info(out), lookup(index,owner,iova) is a diagnostic page-table walk (no DMA).
 * No-IOMMU mode retains ownership/pinning but offers no device isolation.
 * Table pages are retained; unsupported firmware/requester layouts fail closed. */
struct kernel_surface_result iommu_surface_ready(MODULE_ARGS);
struct kernel_surface_result iommu_surface_init(MODULE_ARGS);
struct kernel_surface_result iommu_surface_attach(MODULE_ARGS);
struct kernel_surface_result iommu_surface_map(MODULE_ARGS);
struct kernel_surface_result iommu_surface_unmap(MODULE_ARGS);
struct kernel_surface_result iommu_surface_detach(MODULE_ARGS);
struct kernel_surface_result iommu_surface_info(MODULE_ARGS);
struct kernel_surface_result iommu_surface_lookup(MODULE_ARGS);
#endif
