#ifndef CLOUDBERRY_SATA_H
#define CLOUDBERRY_SATA_H
#include "block.h"
struct sata_open {uint64_t hardware_index;uint32_t port,reserved;};
struct sata_info {uint64_t cookie,hardware_index,blocks,reads,writes,flushes,interrupts,timeouts,route_id;uint32_t port,ports,block_bytes,online,failed,open_stage;};
/* init(hub,heap); open(sata_open*,out block,capacity,out cookie optional) -> unique cookie.
 * Multiple explicitly selected AHCI controllers/ATA ports, LBA48, 512..4096-byte
 * logical sectors, 64-bit DMA. One owner/IRQ route per controller; independent per-port DMA and cookies.
 * Commands serialize per controller; opening/closing siblings never resets HBA. No ATAPI/NCQ/hotplug.
 * read/write/flush/control follow block.h (4KiB calls). Foreground BSP thread,
 * IF=1, outside critical sections/IRQ callback. Completion uses MSI + IRQ worker.
 * info(cookie,out); close(cookie) refuses retained/busy instance. Error poisons
 * the port; an unquiesceable engine poisons the controller and all siblings.
 * Engine stop/reset must prove quiescence before freeing DMA.
 * Failed open can retain ownership; info(0,out) requires exactly one instance; optional
 * open cookie output identifies failed opens even when multiple ports exist. */
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
