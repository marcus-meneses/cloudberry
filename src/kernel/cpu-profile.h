#ifndef CLOUDBERRY_CPU_PROFILE_H
#define CLOUDBERRY_CPU_PROFILE_H
#include <stdint.h>
enum cpu_profile_source {CPU_PROFILE_UNAVAILABLE,CPU_PROFILE_TSC,CPU_PROFILE_HPET};
struct cpu_profile_stamp {uint64_t ticks,reference,epoch,hz;};
struct cpu_profile_span {uint64_t ticks,ns;unsigned source,regression;};
struct cpu_profile_peak {
    uint64_t ns,ticks,cpu,method,domain,caller,clock_epoch,source,address_space;
};
static inline uint64_t cpu_profile_scale(uint64_t value,uint64_t numerator,uint64_t denominator){
    if(!numerator || !denominator)return 0;
    uint64_t q=value/denominator,r=value%denominator;
    if(q>UINT64_MAX/numerator || (r && numerator>UINT64_MAX/r))return UINT64_MAX;
    uint64_t whole=q*numerator,fraction=r*numerator/denominator;
    return fraction>UINT64_MAX-whole?UINT64_MAX:whole+fraction;
}
/* No mixed epochs/frequencies or regressed TSC samples are converted. HPET
 * remains a valid independent duration even if its paired raw TSC regresses. */
static inline struct cpu_profile_span cpu_profile_difference(struct cpu_profile_stamp begin,struct cpu_profile_stamp end,uint64_t hpet_period){
    struct cpu_profile_span out={.regression=end.ticks<begin.ticks};
    if(!out.regression)out.ticks=end.ticks-begin.ticks;
    if(begin.hz){
        if(out.regression || begin.epoch!=end.epoch || begin.hz!=end.hz)return out;
        out.ns=cpu_profile_scale(out.ticks,1000000000,begin.hz);out.source=CPU_PROFILE_TSC;
    }else if(!end.hz && hpet_period && end.reference>=begin.reference){
        out.ns=cpu_profile_scale(end.reference-begin.reference,hpet_period,1000000);out.source=CPU_PROFILE_HPET;
    }
    return out;
}
#endif
