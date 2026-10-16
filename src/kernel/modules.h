#ifndef CLOUDBERRY_KERNEL_MODULES_H
#define CLOUDBERRY_KERNEL_MODULES_H
#include <stdbool.h>
#include <stddef.h>
#include "../boot/memory.h"
#include "surfaces.h"
#include "../kmodules/shared/module.h"
#define KERNEL_MODULES_ADDRESS UINT64_C(0xffffffffa0000000)
#define KERNEL_MEDIATION_ADDRESS UINT64_C(0xffffffffa2000000)
#define KERNEL_HUB_BYTES (4u * BOOT_PAGE_SIZE)
#define KERNEL_MEDIATION_STUB_BYTES 32u
#define KERNEL_METHOD_CAPACITY (KERNEL_HUB_BYTES / sizeof(struct kernel_surface))
#define KERNEL_MEDIATION_PAGES ((KERNEL_METHOD_CAPACITY * KERNEL_MEDIATION_STUB_BYTES + BOOT_PAGE_SIZE - 1) / BOOT_PAGE_SIZE)
const struct kernel_surfaces *kernel_surfaces_publish(struct boot_pages *);
bool kernel_module_load(struct boot_pages *, const void *image, size_t bytes, const char *name);
bool kernel_modules_load(struct boot_pages *);
bool kernel_modules_start(struct boot_pages *, uint64_t rsdp_physical);
#ifdef BOOT_MEM_TEST
bool kernel_mem_test(const struct kernel_surfaces *, struct boot_pages *);
#endif
#endif
