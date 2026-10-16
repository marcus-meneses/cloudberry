#ifndef CLOUDBERRY_KMODULE_MEM_H
#define CLOUDBERRY_KMODULE_MEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEM_ALIGNMENT 16u
#define MEM_BIN_COUNT 64u

struct mem_block;
/* Caller-owned state; do not copy or modify after initialization. */
struct mem_heap {
    struct mem_block *bins[MEM_BIN_COUNT];
    uint64_t nonempty;
    uintptr_t begin, end;
    struct mem_block *regions;
};

/* Synchronous x86-64 System V C ABI; no allocation, syscalls or global heap.
 * The caller supplies exclusive, writable, mapped storage and serializes ALL
 * accesses to each heap. This module does not disable interrupts or take locks.
 * Initialization discards previous state; live allocations must not exist.
 * Failed initialization leaves a disabled heap (allocations return NULL).
 */
bool mem_init(struct mem_heap *heap, void *storage, size_t bytes);
bool mem_add_region(struct mem_heap *heap, void *storage, size_t bytes);
void *mem_malloc(struct mem_heap *heap, size_t bytes);
void mem_free(struct mem_heap *heap, void *pointer);
void *mem_calloc(struct mem_heap *heap, size_t count, size_t bytes);
/* Metadata-only resize; NULL means unchanged and a replacement is needed. */
void *mem_resize_in_place(struct mem_heap *,void *,size_t);
void *mem_realloc(struct mem_heap *heap, void *pointer, size_t bytes);
/* Power-of-two alignment; bytes must be a multiple of alignment. */
void *mem_aligned_alloc(struct mem_heap *heap, size_t alignment, size_t bytes);
/* NULL -> 0. Otherwise pointer must be a live allocation from this heap. */
size_t mem_usable_size(const void *pointer);

/* All allocations are at least 16-byte aligned. Zero-sized allocations return
 * NULL. realloc(p, 0) frees p. Overflow/exhaustion returns NULL; failed realloc
 * leaves p unchanged. free(NULL) is harmless. Invalid pointers, double frees,
 * out-of-bounds writes and overlapping heap/state storage are caller errors.
 */
#endif
