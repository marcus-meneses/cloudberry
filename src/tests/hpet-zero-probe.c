/* Diagnostic kernel CPU object only. Never linked into the candidate kernel.
 * Trap at the first rejected all-zero sample so the host MMIO trace and debugger
 * can distinguish a device response from a guest access/translation problem. */
#include <stdint.h>
static void probe_zero(uintptr_t address);
#define CB_HPET_ZERO(address) probe_zero(address)
#include "../kernel/cpu.c"
static struct {
    uintptr_t address,cr3,cr4;
    uint32_t first,repeated,high,enabled,capability,cpu,ready;
} hpet_zero_probe;
static void probe_zero(uintptr_t address){
    if((address&0xfff)==0xf0){
        unsigned empty=0;
        if(__atomic_compare_exchange_n(&hpet_zero_probe.ready,&empty,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
            hpet_zero_probe.address=address;hpet_zero_probe.first=0;
            hpet_zero_probe.repeated=*(const volatile uint32_t *)address;
            hpet_zero_probe.high=*(const volatile uint32_t *)(address+4);
            hpet_zero_probe.enabled=*(const volatile uint32_t *)(address-0xe0);
            hpet_zero_probe.capability=*(const volatile uint32_t *)(address-0xf0);
            hpet_zero_probe.cpu=current()->index;
            __asm__ volatile("mov %%cr3,%0;mov %%cr4,%1":"=r"(hpet_zero_probe.cr3),"=r"(hpet_zero_probe.cr4));
            __atomic_store_n(&hpet_zero_probe.ready,2,__ATOMIC_RELEASE);
            panic("Cloudberry: fatal HPET zero probe captured\n");
        }
    }
}
