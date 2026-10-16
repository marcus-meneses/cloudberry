#include "apic.h"
#include "apic-platform.h"
static volatile unsigned char *apic;
static uint32_t apic_mode;
static uint64_t rdmsr(uint32_t reg) {
    uint32_t low,high;__asm__ volatile("rdmsr" : "=a"(low),"=d"(high) : "c"(reg));return ((uint64_t)high<<32)|low;
}
static void wrmsr(uint32_t reg,uint64_t value) {
    __asm__ volatile("wrmsr" :: "c"(reg),"a"((uint32_t)value),"d"((uint32_t)(value>>32)) : "memory");
}
int apic_platform_probe(struct apic_platform *out) {
    uint32_t a,b,c,d;__asm__ volatile("cpuid" : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(1),"c"(0));
    if (!(d&(1u<<9))) return 0;
    uint64_t base=rdmsr(0x1b);out->physical=base&UINT64_C(0x000ffffffffff000);
    out->mode=(base&(1u<<10))?APIC_X2APIC:APIC_XAPIC;
    return out->physical!=0;
}
void apic_platform_activate(uintptr_t address,uint32_t mode) {
    if(!apic_mode) {apic=(void *)address;apic_mode=mode;}wrmsr(0x1b,rdmsr(0x1b)|(1u<<11));
}
uint32_t apic_register_read(uint32_t offset) {
    if (apic_mode==APIC_X2APIC) return (uint32_t)rdmsr(0x800+offset/16);
    uint32_t value=*(volatile uint32_t *)(apic+offset);__asm__ volatile("" ::: "memory");return value;
}
void apic_register_write(uint32_t offset,uint32_t value) {
    if (apic_mode==APIC_X2APIC) wrmsr(0x800+offset/16,value);
    else {__asm__ volatile("" ::: "memory");*(volatile uint32_t *)(apic+offset)=value;}
}
uint64_t apic_irq_save(void) {
    uint64_t flags;__asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");return flags;
}
void apic_irq_restore(uint64_t flags) {__asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory","cc");}
void apic_mask_pic(void) {
    __asm__ volatile("outb %0,$0x21; outb %0,$0xa1" :: "a"((uint8_t)0xff) : "memory");
}
