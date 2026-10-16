#include "../shared/cpu-platform.h"
unsigned cpu_backend_vendor(void) {return CPU_VENDOR_INTEL;}
uint32_t cpu_backend_id(void) {return cpu_topology_id(cpu_cpuid,CPU_VENDOR_INTEL);}
uint64_t cpu_backend_ticks(void) {
    uint32_t a,d;__asm__ volatile("lfence;rdtsc;lfence":"=a"(a),"=d"(d)::"memory");return ((uint64_t)d<<32)|a;
}

struct cpu_tsc_info cpu_backend_tsc(void) {return cpu_tsc_probe(cpu_cpuid,CPU_VENDOR_INTEL);}
