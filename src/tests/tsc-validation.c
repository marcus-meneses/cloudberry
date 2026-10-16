#include "../kmodules/shared/tsc-validation.h"
#include "../kmodules/shared/clock.h"
#include <assert.h>
#include <stdio.h>
static void adaptive(void) {
    struct clock_sample bf[TSC_TRIALS],bl[TSC_TRIALS],first[TSC_TRIALS],last[TSC_TRIALS];
    for(unsigned i=0;i<TSC_TRIALS;++i) {
        bf[i]=(struct clock_sample){1000000000+i*1000,1000000000+i*1000,100,CLOCK_TSC_PRESENT|CLOCK_TSC_INVARIANT,0};
        bl[i]=bf[i];bl[i].hpet_ns+=10000000;bl[i].tsc_ns+=10001000;
        first[i]=bf[i];last[i]=bl[i];first[i].tsc_ns+=2000;last[i].tsc_ns+=2000;
    }
    struct tsc_group group={.target_ns=10000000};
    tsc_analyze_group(bf,bl,first,last,0,&group);
    assert(group.status==TSC_VALID && group.retained==10 && group.drift_median_ppb==100000 && group.offset_median_ns==2000);
    assert(group.max_offset_ns==2200 && group.max_drift_ppm==120 && group.interval_median_ns==10000000);
    /* Stable differences are not outliers within a CPU's own group. */
    for(unsigned i=0;i<TSC_TRIALS;++i) {first[i].tsc_ns+=20000;last[i].tsc_ns+=20000;}
    tsc_analyze_group(bf,bl,first,last,0,&group);assert(group.status==TSC_OFFSET && group.retained==10 && !group.outlier_mask);
    for(unsigned i=0;i<TSC_TRIALS;++i) {first[i]=bf[i];last[i]=bl[i];last[i].tsc_ns+=5000;}
    tsc_analyze_group(bf,bl,first,last,0,&group);assert(group.status==TSC_DRIFT && group.retained==10 && !group.outlier_mask);
    for(unsigned i=0;i<TSC_TRIALS;++i) {first[i]=bf[i];last[i]=bl[i];}
    last[0].tsc_ns+=1000000;
    tsc_analyze_group(bf,bl,first,last,0,&group);assert(group.status==TSC_VALID && group.retained==9 && group.outlier_mask==1);
    last[0]=bl[0];last[0].uncertainty_ns=100000;
    tsc_analyze_group(bf,bl,first,last,0,&group);assert(group.status==TSC_VALID && group.retained==9 && group.bracket_mask==1);
    for(unsigned i=0;i<4;++i)last[i].uncertainty_ns=100000;
    tsc_analyze_group(bf,bl,first,last,0,&group);assert(group.status==TSC_UNCERTAIN && group.retained==6);
    /* Correlated BSP self-offset cancels, while real temporal drift is still checked. */
    for(unsigned i=0;i<TSC_TRIALS;++i)bf[i].uncertainty_ns=bl[i].uncertainty_ns=12000;
    tsc_analyze_group(bf,bl,bf,bl,1,&group);assert(group.max_offset_ns==0 && group.status==TSC_DRIFT);
    for(unsigned i=0;i<TSC_TRIALS;++i) {bl[i].hpet_ns=bf[i].hpet_ns+100000000;bl[i].tsc_ns=bf[i].tsc_ns+100010000;}
    tsc_analyze_group(bf,bl,bf,bl,1,&group);assert(group.status==TSC_VALID && group.max_drift_ppm==340);
    /* Small frequency fluctuations within endpoint uncertainty are not outliers. */
    for(unsigned i=0;i<TSC_TRIALS;++i) {bl[i].tsc_ns=bf[i].tsc_ns+100010000;bf[i].uncertainty_ns=bl[i].uncertainty_ns=12000;}
    bl[0].tsc_ns+=500;bl[1].tsc_ns-=500;bl[2].tsc_ns+=1000;bl[3].tsc_ns-=1000;
    tsc_analyze_group(bf,bl,bf,bl,1,&group);assert(group.status==TSC_VALID && group.retained==10 && !group.outlier_mask);
    bl[0].tsc_ns=0;tsc_analyze_group(bf,bl,bf,bl,1,&group);assert(group.status!=TSC_VALID);
    bl[0]=bf[0];bl[0].flags=0;tsc_analyze_group(bf,bl,bf,bl,1,&group);assert(group.status==TSC_UNAVAILABLE);
    puts("PASS adaptive groups: medians/MAD, isolated outliers, seven-trial minimum, persistent skew/drift rejection and self-correlation");
}
static void exchanges(void) {
    struct tsc_exchange samples[TSC_TRIALS];struct tsc_offset_group group;
    for(unsigned i=0;i<TSC_TRIALS;++i) {
        samples[i]=(struct tsc_exchange){.before=1000000+i*10000,.after=1000200+i*10000,.remote=1000300+i*10000,.hz=1000000000,.epoch=1};
        assert(tsc_exchange_measure(&samples[i]) && samples[i].offset_ns==200 && samples[i].uncertainty_ns==101);
    }
    tsc_analyze_offsets(samples,&group);assert(group.status==TSC_VALID && group.retained==10 && group.max_offset_ns==301);
    for(unsigned i=0;i<TSC_TRIALS;++i) {samples[i].remote+=20000;assert(tsc_exchange_measure(&samples[i]));}
    tsc_analyze_offsets(samples,&group);assert(group.status==TSC_OFFSET && group.retained==10 && !group.outlier_mask);
    for(unsigned i=0;i<TSC_TRIALS;++i) {samples[i].remote-=20000;assert(tsc_exchange_measure(&samples[i]));}
    /* Asymmetric remote response: its entire causal interval remains bounded. */
    samples[0].remote=samples[0].before;assert(tsc_exchange_measure(&samples[0]) && samples[0].offset_ns==-100 && samples[0].uncertainty_ns==101);
    samples[0].remote=samples[0].before+300;assert(tsc_exchange_measure(&samples[0]));
    for(unsigned i=0;i<4;++i)samples[i].uncertainty_ns=100000;
    tsc_analyze_offsets(samples,&group);assert(group.status==TSC_UNCERTAIN && group.retained==6);
    /* Replay the zero-MAD pattern from native CPU2: 5–10ns deviations with
     * approximately 1.7us uncertainty must retain all ten observations. */
    const int64_t native_offsets[TSC_TRIALS]={128,138,138,138,138,138,138,143,133,128};
    for(unsigned i=0;i<TSC_TRIALS;++i) {
        samples[i]=(struct tsc_exchange){.before=1000000+i*10000,.after=1003366+i*10000,
            .remote=1001683+i*10000+native_offsets[i],.hz=1000000000,.epoch=1};
        assert(tsc_exchange_measure(&samples[i]));
    }
    tsc_analyze_offsets(samples,&group);assert(group.status==TSC_VALID && group.retained==10 && !group.outlier_mask);
    samples[9].remote+=100000;assert(tsc_exchange_measure(&samples[9]));
    tsc_analyze_offsets(samples,&group);assert(group.status==TSC_VALID && group.retained==9 && group.outlier_mask==(1u<<9));
    samples[0].after=samples[0].before-1;assert(!tsc_exchange_measure(&samples[0]));
    puts("PASS TSC exchange: causal RTT bounds, asymmetry, persistent skew, minimum retained and regression rejection");
}
int main(void) {
    adaptive();exchanges();
    struct clock_sample base={1000000000,1000000000,100,CLOCK_TSC_PRESENT|CLOCK_TSC_INVARIANT,0},end=base,first=base,last=base;
    end.tsc_ns+=50000000;end.hpet_ns+=50000000;first.tsc_ns+=200;last=end;last.tsc_ns+=200;
    struct tsc_report report={0};assert(tsc_validate_pair(&base,&end,&first,&last,&report));assert(report.max_offset_ns==400 && report.max_drift_ppm==4);
    first.tsc_ns+=20000;report=(struct tsc_report){0};assert(!tsc_validate_pair(&base,&end,&first,&last,&report) && report.status==TSC_OFFSET);
    first=base;last=end;first.uncertainty_ns=50001;report=(struct tsc_report){0};assert(!tsc_validate_pair(&base,&end,&first,&last,&report) && report.status==TSC_UNCERTAIN);
    end=base;end.tsc_ns+=20000000;end.hpet_ns+=20000000;first=base;last=end;first.tsc_ns-=6000;last.tsc_ns+=6000;
    report=(struct tsc_report){0};assert(!tsc_validate_pair(&base,&end,&first,&last,&report) && report.status==TSC_DRIFT);
    first=base;first.flags=0;report=(struct tsc_report){0};assert(!tsc_validate_pair(&base,&end,&first,&last,&report) && report.status==TSC_UNAVAILABLE);
    puts("PASS: cross-CPU TSC offset/drift bounds include uncertainty; missing capabilities and uncertain samples fail closed");
}
