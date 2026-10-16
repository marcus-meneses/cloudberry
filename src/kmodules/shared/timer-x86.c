#include "timer.h"
#include "timer-platform.h"
#include "../../kernel/cpu.h"
static volatile unsigned char *apic;
static uint32_t apic_mode;
static kernel_surface_call cpu_control;
static uint64_t rdmsr(uint32_t reg) {
    uint32_t low,high;__asm__ volatile("rdmsr" : "=a"(low),"=d"(high) : "c"(reg));return ((uint64_t)high<<32)|low;
}
static void wrmsr(uint32_t reg,uint64_t value) {
    __asm__ volatile("wrmsr" :: "c"(reg),"a"((uint32_t)value),"d"((uint32_t)(value>>32)) : "memory");
}
void timer_platform_configure(uintptr_t address,uint32_t mode) {apic=(void *)address;apic_mode=mode;cpu_control=kernel_surface_find_version((const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS,"cpu.control",1,0);}
uint32_t timer_register_read(uint32_t offset) {
    if (apic_mode==TIMER_X2APIC) return (uint32_t)rdmsr(0x800+offset/16);
    uint32_t value=*(volatile uint32_t *)(apic+offset);__asm__ volatile("" ::: "memory");return value;
}
void timer_register_write(uint32_t offset,uint32_t value) {
    if (apic_mode==TIMER_X2APIC) wrmsr(0x800+offset/16,value);
    else {__asm__ volatile("" ::: "memory");*(volatile uint32_t *)(apic+offset)=value;}
}
uint64_t timer_irq_save(void) {
    uint64_t flags;__asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");if(cpu_control)cpu_control(CPU_IRQ_BEGIN,flags,0,0,0,0);return flags;
}
void timer_irq_restore(uint64_t flags) {if(cpu_control)cpu_control(CPU_IRQ_END,flags,0,0,0,0);__asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory","cc");}
