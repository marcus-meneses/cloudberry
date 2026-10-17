#include "cpu-platform.h"
/* Capability-driven decoding shared by the selected vendor adapter. Never infer
 * invariant TSC from a model name, base/core frequency, or hypervisor presence. */
struct cpu_tsc_info cpu_tsc_probe(cpu_cpuid_reader read,unsigned vendor) {
    struct cpu_tsc_info out={.abi=1,.vendor=vendor};uint32_t max=read(0,0).a,extended=read(0x80000000,0).a;
    if(max>=1 && (read(1,0).d&(1u<<4)))out.flags|=CPU_TSC_PRESENT;
    if(extended>=0x80000001 && (read(0x80000001,0).d&(1u<<27)))out.flags|=CPU_TSC_RDTSCP;
    if(extended>=0x80000007) {out.extended_power_edx=read(0x80000007,0).d;if(out.extended_power_edx&(1u<<8))out.flags|=CPU_TSC_INVARIANT;}
    if(vendor==CPU_VENDOR_AMD && extended>=0x80000021 && (read(0x80000021,0).a&(1u<<2)))out.flags|=CPU_TSC_LFENCE_SERIALIZING;
    if(max>=0x15) {struct cpu_leaf ratio=read(0x15,0);
        if(ratio.a && ratio.b && ratio.c) {out.hz=(uint64_t)ratio.c*ratio.b/ratio.a;if(out.hz) {out.flags|=CPU_TSC_ENUMERATED;out.frequency_source=CPU_TSC_FREQ_CPUID15;}}
    }
    return out;
}
