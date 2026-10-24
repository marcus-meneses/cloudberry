#ifndef CLOUDBERRY_KERNEL_MODULES_H
#define CLOUDBERRY_KERNEL_MODULES_H
#include <stdbool.h>
#include <stddef.h>
#include "../boot/memory.h"
#include "surfaces.h"
#include "../kmodules/shared/module.h"
#ifndef KERNEL_MODULES_ADDRESS
#define KERNEL_MODULES_ADDRESS UINT64_C(0xffff920000000000)
#endif
/* Image code/data use an independent 1 GiB virtual arena. Physical extents
 * are committed on load; retired/failed extents are never reused yet. */
#define KERNEL_MODULES_BYTES UINT64_C(0x40000000)
#define KERNEL_MODULES_LIMIT (KERNEL_MODULES_ADDRESS+KERNEL_MODULES_BYTES)
#ifndef KERNEL_HUB_ARRAYS_ADDRESS
#define KERNEL_HUB_ARRAYS_ADDRESS UINT64_C(0xffff900000000000)
#endif
/* Disjoint virtual reservations, not physical allocations. Private aliases keep
 * public records RO/NX and mediation RX while construction remains writable. */
#define KERNEL_HUB_WINDOW_BYTES UINT64_C(0x01000000)
#define KERNEL_HUB_ALIAS_DELTA UINT64_C(0x10000000000)
#define KERNEL_METHODS_ADDRESS KERNEL_HUB_ARRAYS_ADDRESS
#define KERNEL_TABLES_ADDRESS (KERNEL_HUB_ARRAYS_ADDRESS+KERNEL_HUB_WINDOW_BYTES)
#define KERNEL_OWNERS_ADDRESS (KERNEL_HUB_ARRAYS_ADDRESS+2*KERNEL_HUB_WINDOW_BYTES)
#define KERNEL_METADATA_ADDRESS (KERNEL_HUB_ARRAYS_ADDRESS+3*KERNEL_HUB_WINDOW_BYTES)
#define KERNEL_MEDIATION_ADDRESS (KERNEL_HUB_ARRAYS_ADDRESS+4*KERNEL_HUB_WINDOW_BYTES)
#define KERNEL_METHOD_HASH_ADDRESS (KERNEL_HUB_ARRAYS_ADDRESS+5*KERNEL_HUB_WINDOW_BYTES)
#define KERNEL_HUB_BYTES BOOT_PAGE_SIZE
#define KERNEL_MEDIATION_STUB_BYTES 32u
const struct kernel_surfaces *kernel_surfaces_publish(struct boot_pages *);
bool kernel_module_load(struct boot_pages *, const void *image, size_t bytes, const char *name);
/* flags=0 permits cooperative retirement; boot path uses BOOT_PINNED.
 * Image/stub mappings remain pinned even after retirement; no unsafe unloading. */
bool kernel_module_load_owned(struct boot_pages *,const void *,size_t,const char *,uint32_t flags);
bool kernel_modules_load(struct boot_pages *);
bool kernel_modules_start(struct boot_pages *, uint64_t rsdp_physical);
#ifdef BOOT_MEM_TEST
bool kernel_mem_test(const struct kernel_surfaces *, struct boot_pages *);
#endif
#endif
