#include "cpu-platform.h"
uint32_t cpu_topology_id(cpu_cpuid_reader read,unsigned vendor) {
    uint32_t max=read(0,0).a;
    if(max>=0x1f) {struct cpu_leaf leaf=read(0x1f,0);if(leaf.b && ((leaf.c>>8)&255))return leaf.d;}
    if(max>=0xb) {struct cpu_leaf leaf=read(0xb,0);if(leaf.b && ((leaf.c>>8)&255))return leaf.d;}
    if(vendor==CPU_VENDOR_AMD && read(0x80000000,0).a>=0x8000001e && (read(0x80000001,0).c&(1u<<22)))return read(0x8000001e,0).a;
    return read(1,0).b>>24;
}
