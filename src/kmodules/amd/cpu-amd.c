#include "../shared/cpu-platform.h"
unsigned cpu_backend_vendor(void) {return CPU_VENDOR_AMD;}
/* Selected before timing is used; AP attachment must confirm the same mode.
 * No optional MSR writes, host/model guesses, or per-timestamp CPUID probing. */
static unsigned timestamp_mode;
int cpu_backend_init(void) {
    struct cpu_tsc_info info=cpu_tsc_probe(cpu_cpuid,CPU_VENDOR_AMD);
    unsigned mode=(info.flags&CPU_TSC_LFENCE_SERIALIZING)?2:(info.flags&CPU_TSC_RDTSCP)?3:1,empty=0;
    if(__atomic_compare_exchange_n(&timestamp_mode,&empty,mode,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 1;
    return empty==mode;
}
uint32_t cpu_backend_id(void) {return cpu_topology_id(cpu_cpuid,CPU_VENDOR_AMD);}
uint64_t cpu_backend_ticks(void) {
    unsigned mode=__atomic_load_n(&timestamp_mode,__ATOMIC_ACQUIRE);
    if(mode==2) {
        uint32_t a,d;__asm__ volatile("lfence;rdtsc;lfence":"=a"(a),"=d"(d)::"memory");return ((uint64_t)d<<32)|a;
    }
    if(mode==3) {
        /* RDTSCP waits for preceding execution; MFENCE also drains stores.
         * Keep a trailing CPUID to prevent following execution from moving
         * before the read without assuming AMD LFENCE dispatch serialization. */
        uint32_t a,c,d;__asm__ volatile("mfence;rdtscp":"=a"(a),"=c"(c),"=d"(d)::"memory");
        uint64_t value=((uint64_t)d<<32)|a;(void)cpu_cpuid(0,0);return value;
    }
    /* LFENCE is not unconditionally dispatch-serializing on our AMD target.
     * Use architectural serialization; no unsupported DE_CFG MSR access. */
    (void)cpu_cpuid(0,0);uint32_t a,d;__asm__ volatile("rdtsc":"=a"(a),"=d"(d)::"memory");
    uint64_t value=((uint64_t)d<<32)|a;(void)cpu_cpuid(0,0);return value;
}

struct cpu_tsc_info cpu_backend_tsc(void) {return cpu_tsc_probe(cpu_cpuid,CPU_VENDOR_AMD);}
