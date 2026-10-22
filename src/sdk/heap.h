#ifndef CLOUDBERRY_SDK_HEAP_H
#define CLOUDBERRY_SDK_HEAP_H
#include <stddef.h>
#include <stdint.h>
/* Single user-thread allocator. Backends return page-aligned owned memory and
 * must leave it valid when release fails. No global/kernel allocator pointers. */
typedef void *(*cb_heap_acquire_fn)(void *,size_t);
typedef int (*cb_heap_release_fn)(void *,void *,size_t);
struct cb_heap_arena;
struct cb_heap {struct cb_heap_arena *arenas;void *context;cb_heap_acquire_fn acquire;cb_heap_release_fn release;size_t mapped,used,allocations;};
#ifdef __cplusplus
extern "C" {
#endif
void cb_heap_init(struct cb_heap *,void *,cb_heap_acquire_fn,cb_heap_release_fn);
void *cb_heap_alloc(struct cb_heap *,size_t);
void *cb_heap_aligned(struct cb_heap *,size_t alignment,size_t bytes);
void *cb_heap_calloc(struct cb_heap *,size_t,size_t);
void *cb_heap_realloc(struct cb_heap *,void *,size_t);
int cb_heap_free(struct cb_heap *,void *);
size_t cb_heap_usable(struct cb_heap *,const void *);
/* Frees empty arenas only; fails with live allocations or retained backing. */
int cb_heap_destroy(struct cb_heap *);
#ifdef __cplusplus
}
#endif
#endif
