#include <stdint.h>
#include <stddef.h>
#include "modules.h"
#include "../boot/limine.h"

static struct boot_pages bootstrap_pages;
const struct kernel_surfaces *kmodule_hub;

/* QEMU diagnostic port only; not a serial or device driver. */
static void debug(const char *message) {
    for (; *message; ++message)
        __asm__ volatile ("outb %0, $0xe9" : : "a"((uint8_t)*message));
}

__attribute__((noreturn)) static void halt(void) {
    for (;;) __asm__ volatile ("cli; hlt" : : : "memory");
}

__attribute__((noreturn)) void kmain(void) {
    __asm__ volatile ("cli" : : : "memory");
    if (base_revision[2] != 0) {
        debug("Cloudberry v2: unsupported Limine protocol\n");
        halt();
    }
    uint64_t cr0, efer_low, efer_high;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("rdmsr" : "=a"(efer_low), "=d"(efer_high) : "c"(0xc0000080u));
    (void)efer_high;
    if (!(cr0 & 1) || !(efer_low & (1u << 10))) {
        debug("Cloudberry v2: expected protected x86-64 long mode\n");
        halt();
    }
    debug("Cloudberry v2: Limine entered protected x86-64 long mode\n");
    if (!hhdm_request.response || !module_request.response ||
        !boot_pages_init(&bootstrap_pages, memory_request.response, hhdm_request.response->offset)) {
        debug("Cloudberry v2: missing boot memory/module information\n"); halt();
    }
    kmodule_hub = kernel_surfaces_publish(&bootstrap_pages);
    if (!kmodule_hub) { debug("Cloudberry v2: kmodule hub creation failed\n"); halt(); }
    if (!kernel_modules_load(&bootstrap_pages)) {debug("Cloudberry v2: module loading failed\n");halt();}
#if defined(BOOT_FAULT) && BOOT_FAULT == 1
    __asm__ volatile("ud2");
#elif defined(BOOT_FAULT) && BOOT_FAULT == 2
    *(volatile uint64_t *)(uintptr_t)UINT64_C(0xfffffe0000000000) = 1;
#elif defined(BOOT_FAULT) && BOOT_FAULT == 3
    __asm__ volatile("xor %%rsp, %%rsp; ud2" : : : "memory");
#endif
    if (!rsdp_request.response || !kernel_modules_start(&bootstrap_pages, rsdp_request.response->address)) {
        debug("Cloudberry v2: module startup failed (see stage above)\n"); halt();
    }
    kernel_surface_call hub_dump=kernel_surface_find_version(kmodule_hub,"hub.dump",1,0);
    if (!hub_dump || hub_dump(0,0,0,0,0,0).status) {debug("Cloudberry v2: hub dump failed\n");halt();}
    kernel_surface_call hardware_dump=kernel_surface_find_version(kmodule_hub,"hwenum.dump",1,0);
    if (!hardware_dump || hardware_dump(0,0,0,0,0,0).status) {
        debug("Cloudberry v2: hardware inventory dump failed\n");halt();
    }
#ifdef BOOT_MEM_TEST
    if (!kernel_mem_test(kmodule_hub, &bootstrap_pages)) {
        debug("Cloudberry v2: FAIL mem surface test\n"); halt();
    }
    debug("Cloudberry v2: PASS mem surface test\n");
#endif
#ifndef BOOT_MEM_TEST
    kernel_surface_call usb_start=kernel_surface_find_version(kmodule_hub,"usb.start",1,0);
    kernel_surface_call hub_start=kernel_surface_find_version(kmodule_hub,"usb_hub.start",1,0);
    if(!hub_start || hub_start(0,0,0,0,0,0).status){debug("Cloudberry v2: USB hub startup failed\n");halt();}
    if(!usb_start || usb_start(0,0,0,0,0,0).status){debug("Cloudberry v2: USB startup failed\n");halt();}
#endif
    debug("Cloudberry v2: BOOT OK; framebuffer ready; event loop active\n");
    kernel_surface_call event_run=kernel_surface_find_version(kmodule_hub,"smp.run",1,0);
    if (event_run) event_run(64,0,0,0,0,0);
    debug("Cloudberry v2: event loop failed\n");halt();
}
