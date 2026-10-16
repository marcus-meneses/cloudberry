#ifndef CLOUDBERRY_BOOT_LIMINE_H
#define CLOUDBERRY_BOOT_LIMINE_H
#include <stdint.h>
#include "memory.h"
/* Limine base revision 3 handoff prefixes consumed by the bootstrap/modules. */
struct framebuffer {
    void *address;
    uint64_t width, height, pitch;
    uint16_t bpp;
    uint8_t model, red_size, red_shift, green_size, green_shift, blue_size, blue_shift;
    uint8_t unused[7];
    uint64_t edid_size;
    void *edid;
};
struct framebuffer_response {
    uint64_t revision, count;
    struct framebuffer **frames;
};
struct hhdm_response { uint64_t revision, offset; };
struct module_file { uint64_t revision; void *address; uint64_t size; char *path, *cmdline; };
struct module_response { uint64_t revision, count; struct module_file **files; };
struct rsdp_response { uint64_t revision, address; };
struct limine_framebuffer_request { uint64_t id[4],revision;struct framebuffer_response *response; };
struct limine_memory_request { uint64_t id[4],revision;struct boot_memory_map *response; };
struct limine_hhdm_request { uint64_t id[4],revision;struct hhdm_response *response; };
struct limine_module_request { uint64_t id[4],revision;struct module_response *response; };
struct limine_rsdp_request { uint64_t id[4],revision;struct rsdp_response *response; };
extern volatile uint64_t base_revision[3];
extern volatile struct limine_framebuffer_request framebuffer_request;
extern volatile struct limine_memory_request memory_request;
extern volatile struct limine_hhdm_request hhdm_request;
extern volatile struct limine_module_request module_request;
extern volatile struct limine_rsdp_request rsdp_request;
/* MP prefix from the 0BSD Limine protocol (limine-protocol/include/limine.h). */
struct limine_mp_info {uint32_t processor_id,lapic_id;uint64_t reserved;void (*goto_address)(struct limine_mp_info *);uint64_t extra_argument;};
struct limine_mp_response {uint64_t revision;uint32_t flags,bsp_lapic_id;uint64_t cpu_count;struct limine_mp_info **cpus;};
struct limine_mp_request {uint64_t id[4],revision;struct limine_mp_response *response;uint64_t flags;};
extern volatile struct limine_mp_request mp_request;
#endif
