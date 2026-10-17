#ifndef CLOUDBERRY_PARTITION_H
#define CLOUDBERRY_PARTITION_H
#include "block.h"
/* init(hub,heap); find(parent block,16-byte GPT unique GUID,out block,capacity) -> cookie.
 * Validates primary+backup header/entry CRCs and selected partition boundaries;
 * no match/duplicate/overlap/error fails. Retains parent until close(cookie).
 * read/write/flush/control implement block.h. No MBR fallback or formatting; writes remain inside the selected slice.
 * Explicit close refuses active or retained instances. BSP threads only. */
struct kernel_surface_result partition_surface_init(MODULE_ARGS);
struct kernel_surface_result partition_surface_find(MODULE_ARGS);
struct kernel_surface_result partition_surface_read(MODULE_ARGS);
struct kernel_surface_result partition_surface_control(MODULE_ARGS);
struct kernel_surface_result partition_surface_close(MODULE_ARGS);
struct kernel_surface_result partition_surface_write(MODULE_ARGS);
struct kernel_surface_result partition_surface_flush(MODULE_ARGS);
#endif
