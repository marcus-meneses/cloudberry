#ifndef CLOUDBERRY_DISPLAY_BOOT_H
#define CLOUDBERRY_DISPLAY_BOOT_H
#include "module.h"
/* Kernel-only copied handoff metadata; no framebuffer address is exposed.
 * boot_info(out,bytes) requires the exact structure size and initialized I/O. */
struct display_boot_info {uint64_t frames,width,height,pitch;};
struct kernel_surface_result display_surface_boot_info(MODULE_ARGS);
#endif
