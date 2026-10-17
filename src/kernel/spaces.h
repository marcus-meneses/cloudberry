#ifndef CLOUDBERRY_KERNEL_SPACES_H
#define CLOUDBERRY_KERNEL_SPACES_H
#include "surfaces.h"
#include "../boot/memory.h"
bool kernel_spaces_prepare(struct boot_pages *);
bool kernel_spaces_init(const struct kernel_surfaces *,uintptr_t heap);
struct kernel_surface_result kernel_spaces_create(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
struct kernel_surface_result kernel_spaces_map(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
struct kernel_surface_result kernel_spaces_unmap(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
struct kernel_surface_result kernel_spaces_protect(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
struct kernel_surface_result kernel_spaces_query(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
struct kernel_surface_result kernel_spaces_info(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
struct kernel_surface_result kernel_spaces_pin(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
struct kernel_surface_result kernel_spaces_switch(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
struct kernel_surface_result kernel_spaces_destroy(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
#endif
