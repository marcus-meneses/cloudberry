#ifndef CLOUDBERRY_NVME_H
#define CLOUDBERRY_NVME_H
#include "module.h"
struct nvme_info {uint64_t hardware_index,blocks,reads,interrupts,timeouts,route_id;uint32_t namespace_id,block_bytes,online,failed,open_stage,reserved;};
/* init(hub); open(hardware_index,out info); read(lba,blocks,destination);
 * info(out); close(). open/close require IF=1 outside critical sections or IRQ
 * callback context. One explicitly selected controller/namespace, BSP only.
 * read requires a preemptible thread and IF=1 outside critical sections. It
 * chunks through a private 4KiB DMA buffer, preserving destination on failure
 * only for chunks not yet copied. No write/format/admin passthrough surface.
 * MSI-X completions are drained by the IRQ worker. Timeouts reset the controller;
 * buffers remain pinned if reset cannot prove RDY=0. close is retryable.
 * A failed/timed-out instance must close and reopen; no automatic retry. */
struct kernel_surface_result nvme_surface_init(MODULE_ARGS);
struct kernel_surface_result nvme_surface_open(MODULE_ARGS);
struct kernel_surface_result nvme_surface_read(MODULE_ARGS);
struct kernel_surface_result nvme_surface_info(MODULE_ARGS);
struct kernel_surface_result nvme_surface_close(MODULE_ARGS);
struct kernel_surface_result nvme_surface_interrupt(MODULE_ARGS);
#endif
