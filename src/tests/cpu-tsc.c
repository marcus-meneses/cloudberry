#include <assert.h>
#include <stdio.h>
#include "../kmodules/shared/cpu-platform.h"
static uint32_t maximum=0x16,extended=0x80000007,power=1u<<8;
static struct cpu_leaf ratio={.a=1,.b=100,.c=24000000};
static uint32_t extended_features;
#ifdef TEST_CPU_ORDERING
/* Mock capability responses only. This checks boot/AP selection, not physical
 * instruction ordering; hardware sequences are exercised by isolated boots. */
struct cpu_leaf cpu_cpuid(uint32_t leaf,uint32_t sub);
#endif
static struct cpu_leaf read(uint32_t leaf,uint32_t sub) {
    assert(!sub);struct cpu_leaf out={0};
    if(leaf==0)out.a=maximum;
    else if(leaf==0x80000000)out.a=extended;
    else if(leaf==1) {assert(maximum>=leaf);out.d=1u<<4;}
    else if(leaf==0x80000001) {assert(extended>=leaf);out.d=1u<<27;}
    else if(leaf==0x80000007) {assert(extended>=leaf);out.d=power;}
    else if(leaf==0x80000021) {assert(extended>=leaf);out.a=extended_features;}
    else if(leaf==0x15) {assert(maximum>=leaf);out=ratio;}
    else assert(0); /* Core/base frequency is not a TSC frequency source. */
    return out;
}
#ifdef TEST_CPU_ORDERING
struct cpu_leaf cpu_cpuid(uint32_t leaf,uint32_t sub) {return read(leaf,sub);}
int main(int argc,char **argv) {
    assert(argc==2 && argv[1][1]==0);extended=0x80000021;
    if(argv[1][0]=='l')extended_features=1u<<2;
    else if(argv[1][0]=='c')extended=0x80000000;
    else assert(argv[1][0]=='r');
    assert(cpu_backend_init() && cpu_backend_init());
    /* Each AP must accept the same selected ordering mode. No mutation after
     * selection may silently weaken the BSP's instruction requirements. */
    extended=0x80000021;extended_features=extended_features?0:1u<<2;
    assert(!cpu_backend_init());
    puts("PASS AMD timestamp selection: stable initialization and incompatible AP rejection");
}
#else
int main(void) {
    for(unsigned vendor=CPU_VENDOR_INTEL;vendor<=CPU_VENDOR_AMD;++vendor) {
        struct cpu_tsc_info info=cpu_tsc_probe(read,vendor);
        assert(info.abi==1 && info.vendor==vendor && info.hz==2400000000);
        assert(info.flags==(CPU_TSC_PRESENT|CPU_TSC_INVARIANT|CPU_TSC_ENUMERATED|CPU_TSC_RDTSCP));
        assert(info.frequency_source==CPU_TSC_FREQ_CPUID15 && info.extended_power_edx==power);
    }
    power=0;assert(!(cpu_tsc_probe(read,CPU_VENDOR_AMD).flags&CPU_TSC_INVARIANT));
    extended=0x80000021;extended_features=1u<<2;
    assert(cpu_tsc_probe(read,CPU_VENDOR_AMD).flags&CPU_TSC_LFENCE_SERIALIZING);
    assert(!(cpu_tsc_probe(read,CPU_VENDOR_INTEL).flags&CPU_TSC_LFENCE_SERIALIZING));
    extended_features=0;assert(!(cpu_tsc_probe(read,CPU_VENDOR_AMD).flags&CPU_TSC_LFENCE_SERIALIZING));
    extended=0x80000007;
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
#endif
