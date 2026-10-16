#include "../shared/cpu-platform.h"
unsigned cpu_backend_vendor(void) {return CPU_VENDOR_AMD;}
uint32_t cpu_backend_id(void) {return cpu_topology_id(cpu_cpuid,CPU_VENDOR_AMD);}
uint64_t cpu_backend_ticks(void) {
    /* LFENCE is not unconditionally dispatch-serializing on our AMD target.
     * Use architectural serialization; no unsupported DE_CFG MSR access. */
    (void)cpu_cpuid(0,0);uint32_t a,d;__asm__ volatile("rdtsc":"=a"(a),"=d"(d)::"memory");
    uint64_t value=((uint64_t)d<<32)|a;(void)cpu_cpuid(0,0);return value;
}

struct cpu_tsc_info cpu_backend_tsc(void) {return cpu_tsc_probe(cpu_cpuid,CPU_VENDOR_AMD);}
