#include "splash.h"
extern const uint32_t splash_logo[] __attribute__((visibility("hidden")));
extern const unsigned char splash_logo_end[] __attribute__((visibility("hidden")));
struct kernel_surface_result splash_surface_show(MODULE_ARGS) {
    MODULE_UNUSED();
    const struct framebuffer_response *response=(void *)a;
    const struct kernel_surfaces *hub=(void *)b;
    kernel_surface_call map=kernel_surface_find(hub,"hardware.framebuffer_map");
    if(!splash_validate(response) || !map)return module_result(0,0);
    const struct framebuffer *fb=response->frames[0];
    if(map((uintptr_t)fb->address,fb->pitch*fb->height,0,0,0,0).status)return module_result(0,0);
    int ok=splash_render((const void *)a,splash_logo,(uintptr_t)splash_logo_end-(uintptr_t)splash_logo);
    if (ok) {
        const char *message="Cloudberry v2: pre-flight Cloudberry logo displayed\n";
        while (*message) __asm__ volatile("outb %0,$0xe9" :: "a"((uint8_t)*message++));
    }
    return module_result(ok,ok);
}
