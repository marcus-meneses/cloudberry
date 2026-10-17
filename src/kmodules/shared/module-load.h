#ifndef CLOUDBERRY_MODULE_LOAD_H
#define CLOUDBERRY_MODULE_LOAD_H
#include "module.h"
enum module_load_error {MODULE_LOAD_OK,MODULE_LOAD_ARGUMENT,MODULE_LOAD_MEMORY,MODULE_LOAD_DEPENDENCY,MODULE_LOAD_IMAGE,MODULE_LOAD_INIT,MODULE_LOAD_START};
struct module_requirement {char name[32];uint16_t major,minor;uint32_t reserved;};
struct module_load_request {
    uint32_t abi,reserved;const void *image;uint64_t bytes;char name[32],init[32],start[32];
    const struct module_requirement *requirements;uint64_t requirement_count;
    const uint64_t *devices;uint64_t device_count;
};
/* ABI2: private synchronous lookup is valid only on the loading thread during
 * init/start. Returned implementations are borrowed until that hook returns;
 * never publish/cache them or use them as asynchronous callback entries. */
typedef kernel_surface_call (*module_private_resolve)(uint64_t,const char *,uint16_t,uint16_t);
struct module_init_context {uint32_t abi,reserved;const struct kernel_surfaces *hub;uintptr_t heap;uint64_t owner;const uint64_t *devices;uint64_t device_count;module_private_resolve resolve;};
struct module_load_result {uint64_t owner,detail;uint32_t error,state;};
/* kernel.module_load(request,out result): trusted CPU0 thread, IF=1, synchronous.
 * Caller owns stable input buffers through return; init/start must copy devices
 * if needed later. Prepare/init runs with hidden exports and must clean private
 * allocations on failure. Init may not publish tables/queue callbacks. Once init
 * succeeds, optional start runs hidden in STARTING. Neither hook may publish
 * tables or queue asynchronous callbacks; both may use ctx.resolve privately.
 * Only successful completion publishes LIVE exports. Start
 * failure permanently hides/rejects exports; active work drains, leases/tables
 * keep their safe lifetimes. There is no forced unwind/unmap/retry or replacement.
 * Only trusted boot configuration/volumes may supply code. No signature sandbox.
 */
#endif
