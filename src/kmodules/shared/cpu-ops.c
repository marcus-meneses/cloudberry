#include "cpu-platform.h"
struct cpu_leaf cpu_cpuid(uint32_t leaf,uint32_t subleaf) {
    struct cpu_leaf r;__asm__ volatile("cpuid":"=a"(r.a),"=b"(r.b),"=c"(r.c),"=d"(r.d):"a"(leaf),"c"(subleaf):"memory");return r;
}
struct kernel_surface_result cpu_surface_init(MODULE_ARGS) {MODULE_UNUSED();if(!cpu_backend_init())return module_result(0,0);return module_result(cpu_backend_id(),1);}
struct kernel_surface_result cpu_surface_id(MODULE_ARGS) {MODULE_UNUSED();return module_result(cpu_backend_id(),1);}
struct kernel_surface_result cpu_surface_ticks(MODULE_ARGS) {MODULE_UNUSED();return module_result(cpu_backend_ticks(),1);}
struct kernel_surface_result cpu_surface_fence(MODULE_ARGS) {
    MODULE_UNUSED();/* CPUID after MFENCE provides dispatch serialization on both
     * vendors without assuming AMD LFENCE mode or writing an optional MSR. */
    __asm__ volatile("mfence":::"memory");(void)cpu_cpuid(0,0);return module_result(1,1);
}
struct kernel_surface_result cpu_surface_features(MODULE_ARGS) {
    MODULE_UNUSED();if(!a)return module_result(0,0);
    struct cpu_features out={.vendor=cpu_backend_vendor(),.physical_bits=36,.linear_bits=48};
    if(cpu_cpuid(0x80000000,0).a>=0x80000008) {uint32_t bits=cpu_cpuid(0x80000008,0).a;out.physical_bits=bits&255;out.linear_bits=(bits>>8)&255;}
    *(struct cpu_features *)a=out;return module_result(1,1);
}

struct kernel_surface_result cpu_surface_tsc(MODULE_ARGS) {
    MODULE_UNUSED();if(!a)return module_result(0,0);*(struct cpu_tsc_info *)a=cpu_backend_tsc();return module_result(1,1);
}
