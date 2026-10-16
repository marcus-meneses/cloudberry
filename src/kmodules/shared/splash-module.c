#include "splash.h"
extern const uint32_t splash_logo[] __attribute__((visibility("hidden")));
extern const unsigned char splash_logo_end[] __attribute__((visibility("hidden")));
struct kernel_surface_result splash_surface_show(MODULE_ARGS) {
    MODULE_UNUSED();
    int ok=splash_render((const void *)a,splash_logo,(uintptr_t)splash_logo_end-(uintptr_t)splash_logo);
    if (ok) {
        const char *message="Cloudberry v2: pre-flight Cloudberry logo displayed\n";
        while (*message) __asm__ volatile("outb %0,$0xe9" :: "a"((uint8_t)*message++));
    }
    return module_result(ok,ok);
}
