#ifndef CLOUDBERRY_KMODULE_IMAGE_H
#define CLOUDBERRY_KMODULE_IMAGE_H
#include "../../kernel/surfaces.h"
#define KMODULE_MAGIC UINT64_C(0x32444f4d4b424343)
#define KMODULE_FORMAT 3u /* Requires kernel hub ABI3; format2 is rejected. */
struct kmodule_image {
    uint64_t magic;
    uint32_t format, header_bytes;
    uint64_t image_bytes, memory_bytes, writable_offset;
    uint64_t exports_offset;
    uint32_t exports_count, export_bytes;
    char name[32];
};
struct kmodule_context_info { uint64_t bytes, alignment; };
struct kmodule_export {
    char name[32];
    uint64_t offset;
    uint16_t major, minor;
    uint32_t reserved;
};
#define MODULE_ARGS uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f
#define MODULE_UNUSED() do { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; } while (0)
static inline struct kernel_surface_result module_result(uintptr_t value, int ok) {
    return (struct kernel_surface_result){value, ok ? KERNEL_SURFACE_OK : KERNEL_SURFACE_FAILED};
}
#endif
