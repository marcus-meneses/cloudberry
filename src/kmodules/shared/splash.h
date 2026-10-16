#ifndef CLOUDBERRY_KMODULE_SPLASH_H
#define CLOUDBERRY_KMODULE_SPLASH_H
#include "module.h"
#include "../../boot/limine.h"
/* show(Limine framebuffer response) -> 1; validate and draw the bundled logo.
 * Temporary pre-flight rendering using Limine's existing framebuffer mapping.
 * No display handler, allocation, mode setting or persistent framebuffer state. */
struct kernel_surface_result splash_surface_show(MODULE_ARGS);
/* Core renderer, also usable by focused host validation with a packed RGB asset. */
int splash_render(const struct framebuffer_response *,const uint32_t *image,size_t bytes);
#endif
