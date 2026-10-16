#include <assert.h>
#include <stdio.h>
#include "../kmodules/shared/cpu-platform.h"
static uint32_t maximum=0x16,extended=0x80000007,power=1u<<8;
static struct cpu_leaf ratio={.a=1,.b=100,.c=24000000};
static struct cpu_leaf read(uint32_t leaf,uint32_t sub) {
    assert(!sub);struct cpu_leaf out={0};
    if(leaf==0)out.a=maximum;
    else if(leaf==0x80000000)out.a=extended;
    else if(leaf==1) {assert(maximum>=leaf);out.d=1u<<4;}
    else if(leaf==0x80000001) {assert(extended>=leaf);out.d=1u<<27;}
    else if(leaf==0x80000007) {assert(extended>=leaf);out.d=power;}
    else if(leaf==0x15) {assert(maximum>=leaf);out=ratio;}
    else assert(0); /* Core/base frequency is not a TSC frequency source. */
    return out;
}
int main(void) {
    for(unsigned vendor=CPU_VENDOR_INTEL;vendor<=CPU_VENDOR_AMD;++vendor) {
        struct cpu_tsc_info info=cpu_tsc_probe(read,vendor);
        assert(info.abi==1 && info.vendor==vendor && info.hz==2400000000);
        assert(info.flags==(CPU_TSC_PRESENT|CPU_TSC_INVARIANT|CPU_TSC_ENUMERATED|CPU_TSC_RDTSCP));
        assert(info.frequency_source==CPU_TSC_FREQ_CPUID15 && info.extended_power_edx==power);
    }
    power=0;assert(!(cpu_tsc_probe(read,CPU_VENDOR_AMD).flags&CPU_TSC_INVARIANT));
    for(unsigned missing=0;missing<3;++missing) {
        struct cpu_leaf saved=ratio;
        if(missing==0)ratio.a=0;else if(missing==1)ratio.b=0;else ratio.c=0;
        struct cpu_tsc_info info=cpu_tsc_probe(read,CPU_VENDOR_INTEL);
        assert(!info.hz && !info.frequency_source && !(info.flags&CPU_TSC_ENUMERATED));ratio=saved;
    }
    maximum=1;extended=0x80000000;
    assert(cpu_tsc_probe(read,CPU_VENDOR_AMD).flags==CPU_TSC_PRESENT);
    maximum=0;assert(!cpu_tsc_probe(read,CPU_VENDOR_INTEL).flags);
    puts("PASS TSC discovery: Intel/AMD capabilities, complete crystal ratio, absent leaves and incomplete ratios");
}
