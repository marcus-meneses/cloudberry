#ifndef CLOUDBERRY_XSTATE_H
#define CLOUDBERRY_XSTATE_H
#include "module.h"
enum xstate_mode {XSTATE_FXSAVE=1,XSTATE_XSAVE=2};
struct xstate_info {uint64_t mask;uint32_t bytes,alignment,mode,avx_offset,avx_bytes,reserved;};
/* cpu_init(): BSP publishes a layout, every AP must match it before execution.
 * info(out); init(buffer,bytes); save/restore(buffer,bytes). Aligned, trusted
 * kernel buffers only. Eager x87/SSE/AVX preservation; no lazy #NM ownership.
 * Interrupt/dispatcher infrastructure stays -mgeneral-regs-only. */
struct kernel_surface_result xstate_cpu_init(MODULE_ARGS);
struct kernel_surface_result xstate_info(MODULE_ARGS);
struct kernel_surface_result xstate_init(MODULE_ARGS);
struct kernel_surface_result xstate_save(MODULE_ARGS);
struct kernel_surface_result xstate_restore(MODULE_ARGS);
#endif
