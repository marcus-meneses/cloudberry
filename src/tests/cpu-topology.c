#include <assert.h>
#include <stdio.h>
#include "../kmodules/shared/cpu-platform.h"
static unsigned mode;
static struct cpu_leaf read(uint32_t leaf,uint32_t sub) {
    (void)sub;struct cpu_leaf r={0};
    if(leaf==0)r.a=0x1f;
    if(leaf==1)r.b=0x42000000;
    if(leaf==0x1f && mode==1)r=(struct cpu_leaf){.b=2,.c=0x100,.d=0x1234};
    if(leaf==0xb && mode==2)r=(struct cpu_leaf){.b=2,.c=0x100,.d=0x2345};
    if(leaf==0x80000000)r.a=0x8000001e;
    if(leaf==0x80000001 && mode==3)r.c=1u<<22;
    if(leaf==0x8000001e)r.a=0x3456;
    return r;
}
int main(void) {
    mode=1;assert(cpu_topology_id(read,CPU_VENDOR_INTEL)==0x1234);
    mode=2;assert(cpu_topology_id(read,CPU_VENDOR_AMD)==0x2345);
    mode=3;assert(cpu_topology_id(read,CPU_VENDOR_AMD)==0x3456);assert(cpu_topology_id(read,CPU_VENDOR_INTEL)==0x42);
    mode=0;assert(cpu_topology_id(read,CPU_VENDOR_AMD)==0x42);
    puts("PASS topology: Intel/AMD modern leaves, full AMD extended IDs, gated legacy fallback");
}
