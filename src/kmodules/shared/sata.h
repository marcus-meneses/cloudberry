#ifndef CLOUDBERRY_SATA_H
#define CLOUDBERRY_SATA_H
#include "block.h"
struct sata_open {uint64_t hardware_index;uint32_t port,reserved;};
struct sata_info {uint64_t cookie,hardware_index,blocks,reads,writes,flushes,interrupts,timeouts,route_id;uint32_t port,ports,block_bytes,online,failed,open_stage;};
/* init(hub); open(sata_open*,out block,capacity) -> unique cookie.
 * One explicitly selected AHCI controller/ATA port, LBA48, 512..4096-byte
 * logical sectors, 64-bit DMA. Whole-controller exclusive ownership/reset;
 * do not share the controller with another port driver. No ATAPI/NCQ/hotplug.
 * read/write/flush/control follow block.h (4KiB calls). Foreground BSP thread,
 * IF=1, outside critical sections/IRQ callback. Completion uses MSI + IRQ worker.
 * info(cookie,out); close(cookie) refuses retained/busy instance. Error poisons
 * the instance, reset/engine stop must prove quiescence before freeing DMA.
 * Failed open can retain ownership; info(0,out) reports cookie for cleanup. */
struct kernel_surface_result sata_surface_init(MODULE_ARGS);
struct kernel_surface_result sata_surface_open(MODULE_ARGS);
struct kernel_surface_result sata_surface_read(MODULE_ARGS);
struct kernel_surface_result sata_surface_write(MODULE_ARGS);
struct kernel_surface_result sata_surface_flush(MODULE_ARGS);
struct kernel_surface_result sata_surface_control(MODULE_ARGS);
struct kernel_surface_result sata_surface_info(MODULE_ARGS);
struct kernel_surface_result sata_surface_close(MODULE_ARGS);
struct kernel_surface_result sata_surface_interrupt(MODULE_ARGS);
#endif
