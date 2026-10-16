#ifndef CLOUDBERRY_MEM_PAGES_H
#define CLOUDBERRY_MEM_PAGES_H
#include "module.h"
#define MEM_STACK_BASE UINT64_C(0xffffa00000000000)
#define MEM_STACK_LIMIT UINT64_C(0xffffb00000000000)
struct mem_pages {uint64_t id,physical,count;uintptr_t address,owner;};
struct mem_stack {uint64_t id;uintptr_t bottom,top,owner;uint64_t bytes;};
struct mem_pages_info {uint64_t allocations,physical_pages,stacks,stack_pages;};
/* Foreground SMP-safe; kernel-owned owner 0 is valid. IDs never repeat.
 * pages_init(hub,heap) -> 1; binds the kernel ledger/mapping authority.
 * pages_alloc(count,owner,out mem_pages) -> id; contiguous, zeroed physical pages.
 * pages_free(id,owner) -> 1; exact owned allocation only, no managed mappings.
 * stack_alloc(bytes,owner,out mem_stack) -> id; independent zeroed frames,
 * RW/NX body, one unmapped guard page at either end, no physical contiguity needed.
 * stack_free(id,owner) -> 1; rejects the current stack, unmaps then frees frames.
 * pages_info(out) -> live allocation/page counts; heap metadata backing is retained.
 * Caller must quiesce all users first; DMA pins also prevent release. No process isolation; kernel authority acknowledges SMP shootdowns.
 * Failure never writes an output descriptor. Released frames are sanitized. */
struct kernel_surface_result mem_surface_pages_init(MODULE_ARGS);
struct kernel_surface_result mem_surface_pages_alloc(MODULE_ARGS);
struct kernel_surface_result mem_surface_pages_free(MODULE_ARGS);
struct kernel_surface_result mem_surface_stack_alloc(MODULE_ARGS);
struct kernel_surface_result mem_surface_stack_free(MODULE_ARGS);
struct kernel_surface_result mem_surface_pages_info(MODULE_ARGS);
#endif
