#include <assert.h>
#include <stdio.h>
#include "../kernel/cpu-profile.h"
int main(void){
    struct cpu_profile_stamp a={.ticks=100,.epoch=4,.hz=2000000000},b=a;
    b.ticks+=200;struct cpu_profile_span s=cpu_profile_difference(a,b,10000000);
    assert(s.source==CPU_PROFILE_TSC && s.ns==100 && s.ticks==200 && !s.regression);
    ++b.epoch;assert(!cpu_profile_difference(a,b,10000000).source);b=a;b.hz=3000000000;assert(!cpu_profile_difference(a,b,10000000).source);
    b=a;b.ticks=99;s=cpu_profile_difference(a,b,10000000);assert(s.regression && !s.source && !s.ticks);
    a=(struct cpu_profile_stamp){.ticks=100,.reference=5};b=(struct cpu_profile_stamp){.ticks=99,.reference=25};
    s=cpu_profile_difference(a,b,10000000);assert(s.regression && s.source==CPU_PROFILE_HPET && s.ns==200);
    b.reference=4;assert(!cpu_profile_difference(a,b,10000000).source);
    assert(!cpu_profile_difference(a,b,0).source);
    b.reference=25;b.hz=2000000000;assert(!cpu_profile_difference(a,b,10000000).source);
    assert(cpu_profile_scale(UINT64_MAX,1000000000,1)==UINT64_MAX);
    assert(cpu_profile_scale(3999999999,1000000000,2000000000)==1999999999);
    assert(!cpu_profile_scale(1,0,2) && !cpu_profile_scale(1,1,0));
    puts("PASS profiling durations: qualified TSC, epoch/rate transitions, regression, independent HPET fallback and saturation");
}
