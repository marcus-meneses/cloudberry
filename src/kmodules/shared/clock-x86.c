#include "clock.h"
#include "clock-platform.h"
#include "hpet-counter.h"

#include "cpu-platform.h"
static kernel_surface_call tsc_info;
struct clock_cpu clock_cpu_detect(void) {
    struct cpu_tsc_info info={0};
    if(!tsc_info || tsc_info((uintptr_t)&info,0,0,0,0,0).status || info.abi!=1)return (struct clock_cpu){0};
    struct clock_cpu cpu={.hz=info.hz};
    if(info.flags&CPU_TSC_PRESENT)cpu.flags|=CLOCK_TSC_PRESENT;
    if(info.flags&CPU_TSC_INVARIANT)cpu.flags|=CLOCK_TSC_INVARIANT;
    if(info.flags&CPU_TSC_ENUMERATED)cpu.flags|=CLOCK_TSC_ENUMERATED;
    if(info.flags&CPU_TSC_RDTSCP)cpu.flags|=CLOCK_TSC_RDTSCP;
    return cpu;
}
static kernel_surface_call ordered_ticks;
uint64_t clock_cpu_ticks(void) {
    return ordered_ticks(0,0,0,0,0,0).value;
}
static kernel_surface_call cmos_read;
int clock_platform_configure(const struct kernel_surfaces *hub) {
    tsc_info=kernel_surface_find_version(hub,"cpu.tsc",1,0);
    ordered_ticks=kernel_surface_find_version(hub,"cpu.ticks",1,0);
    cmos_read=kernel_surface_find_version(hub,"inthandle.cmos_read",1,0);return cmos_read && ordered_ticks && tsc_info;
}
uint8_t clock_cmos_read(uint8_t index) {
    struct kernel_surface_result r=cmos_read(index,0,0,0,0,0);
    return r.status?0xff:(uint8_t)r.value;
}

int clock_hpet_read(uintptr_t address,uint64_t *out) {
    if(!address || !out)return 0;
    return hpet_counter_read(address+0xf0,out);
}
