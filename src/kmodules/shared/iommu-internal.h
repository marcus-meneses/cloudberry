#ifndef CLOUDBERRY_IOMMU_INTERNAL_H
#define CLOUDBERRY_IOMMU_INTERNAL_H
#include "iommu.h"
#include "hwenum.h"
#define IOMMU_OWNER UINT64_C(0x494f4d4d55000001)
#define IOMMU_ADDRESS_MASK UINT64_C(0x000ffffffffff000)
struct iommu_node {struct iommu_node *next;struct mem_pages page;};
struct iommu_mapping {struct iommu_mapping *next;struct mem_pages pages;size_t installed;};
struct iommu_unit {
    uint64_t physical;uintptr_t registers;uint16_t segment;uint8_t include_all,active;
    unsigned char *scopes;size_t scope_bytes;uint32_t levels,address_bits,coherent;
    struct mem_pages table,commands,events,completion,contexts[256];
    uint64_t capability,extended,sequence;uint32_t tail,iotlb,event_head;
};
struct iommu_domain {uint64_t owner;int faulted;uint16_t requester,domain;struct iommu_unit *unit;struct iommu_node *nodes,*root;struct iommu_mapping *mappings;};
struct iommu_state {
    const struct kernel_surfaces *hub;const struct hwenum_table *hw;uintptr_t heap;
    struct iommu_unit *units;size_t unit_count;struct iommu_domain *domains;
    unsigned char *firmware;size_t firmware_bytes;uint16_t *routing;unsigned char *reserved;
    struct iommu_info info;int busy,unsupported,quiesced;unsigned cache_line;
    uint64_t leaf_flags;unsigned level_shift,register_bytes;
    kernel_surface_call backend_discover,backend_route,backend_collect,backend_init,backend_start,backend_context,backend_invalidate;
    kernel_surface_call alloc,free,pages,release,read,map_mmio,pin,clock,cpu,control;
};
extern struct iommu_state iommu_state __attribute__((visibility("hidden")));
void iommu_publish(struct iommu_unit *,uintptr_t,size_t) __attribute__((visibility("hidden")));
int iommu_alloc_pages(size_t,struct mem_pages *) __attribute__((visibility("hidden")));
uint64_t iommu_now(void) __attribute__((visibility("hidden")));
int iommu_wait32(uintptr_t,uint32_t,uint32_t) __attribute__((visibility("hidden")));
int iommu_wait64(uintptr_t,uint64_t,uint64_t) __attribute__((visibility("hidden")));
int iommu_invalidate(struct iommu_unit *,uint16_t,uint16_t) __attribute__((visibility("hidden")));
/* Backend contract: tightly coupled trusted-kernel C helpers, not user ABI.
 * The frontend retains ownership, allocator ledger and failure policy. */
struct iommu_backend_env {
    uint32_t abi,backend;unsigned cache_line;uint32_t *last_error;
    int (*alloc_pages)(size_t,struct mem_pages *);
    int (*wait32)(uintptr_t,uint32_t,uint32_t);
    int (*wait64)(uintptr_t,uint64_t,uint64_t);
    void (*publish)(struct iommu_unit *,uintptr_t,size_t);
    struct iommu_state *frontend;
    void *(*allocate)(size_t,size_t);
    int (*read_bytes)(uint64_t,void *,size_t);
    void (*record_fault)(struct iommu_unit *,uint32_t,uint64_t,uint32_t);
};
static inline uint32_t iommu_r32(uintptr_t at) {return *(volatile uint32_t *)at;}
static inline uint64_t iommu_r64(uintptr_t at) {return *(volatile uint64_t *)at;}
static inline void iommu_w32(uintptr_t at,uint32_t v) {__asm__ volatile("mfence":::"memory");*(volatile uint32_t *)at=v;__asm__ volatile("mfence":::"memory");}
static inline void iommu_w64(uintptr_t at,uint64_t v) {__asm__ volatile("mfence":::"memory");*(volatile uint64_t *)at=v;__asm__ volatile("mfence":::"memory");}
#endif
