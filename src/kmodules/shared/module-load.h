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
struct module_init_context {uint32_t abi,reserved;const struct kernel_surfaces *hub;uintptr_t heap;uint64_t owner;const uint64_t *devices;uint64_t device_count;};
struct module_load_result {uint64_t owner,detail;uint32_t error,state;};
/* kernel.module_load(request,out result): trusted CPU0 thread, IF=1, synchronous.
 * Caller owns stable input buffers through return; init/start must copy devices
 * if needed later. Prepare/init runs with hidden exports and must clean private
 * allocations on failure. Init may not publish tables/queue callbacks. Once init
 * succeeds, exports become live and optional start may bind hardware/async work.
 * Methods must refuse premature operations until start has completed. Start
 * failure permanently hides/rejects exports; active work drains, leases/tables
 * keep their safe lifetimes. There is no forced unwind/unmap/retry or replacement.
 * Only trusted boot configuration/volumes may supply code. No signature sandbox.
 */
#endif
