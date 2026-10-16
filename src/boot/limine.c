#include "limine.h"
#include <stddef.h>
/* Limine protocol, base revision 3 (0BSD specification).
 * Boot requests cover the framebuffer, usable RAM, HHDM and loaded modules. */
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = {
    0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf,
    0x785c6ed015d3e316, 0x181e920a7852b9d9
};
__attribute__((used, section(".limine_requests")))
volatile uint64_t base_revision[] = {
    0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, 3
};
__attribute__((used, section(".limine_requests")))
volatile struct limine_framebuffer_request framebuffer_request = {{0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
    0x9d5827dcd881dd75, 0xa3148604f6fab11b}, 0, NULL};
/* File prefix from the Limine protocol. Remaining metadata is not accessed. */
__attribute__((used, section(".limine_requests")))
volatile struct limine_memory_request memory_request = {{0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
    0x67cf3d9d378a806f, 0xe304acdfc50c3c62}, 0, NULL};
__attribute__((used, section(".limine_requests")))
volatile struct limine_hhdm_request hhdm_request = {{0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
    0x48dcf1cb8ad2b852, 0x63984e959a98244b}, 0, NULL};
__attribute__((used, section(".limine_requests")))
volatile struct limine_module_request module_request = {{0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
    0x3e7e279702be32af, 0xca1c4f3bd1280cee}, 0, NULL};
__attribute__((used, section(".limine_requests")))
volatile struct limine_rsdp_request rsdp_request = {{0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
    0xc5e77b6b397e7b43, 0x27637845accdcf3c}, 0, NULL};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = {
    0xadc0e0531bb10d03, 0x9572709f31764c62
};


__attribute__((used,section(".limine_requests")))
volatile struct limine_mp_request mp_request={{0xc7b1dd30df4c8b88,0x0a82e883a194f07b,
    0x95a67b819a1b857e,0xa0b61b723b6a73e0},0,NULL,BOOT_PREFER_X2APIC};
