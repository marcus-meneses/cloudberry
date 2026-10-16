#include "tsc-validation.h"
#include "clock.h"
static uint64_t distance(uint64_t a,uint64_t b) {return a>b?a-b:b-a;}
static int offset(const struct clock_sample *s,int64_t *out) {
    uint64_t d=distance(s->tsc_ns,s->hpet_ns);if(d>INT64_MAX)return 0;
    *out=s->tsc_ns>=s->hpet_ns?(int64_t)d:-(int64_t)d;return 1;
}
static uint64_t signed_distance(int64_t a,int64_t b) {
    return a>=b?(uint64_t)a-(uint64_t)b:(uint64_t)b-(uint64_t)a;
}
int tsc_validate_pair(const struct clock_sample *base_first,const struct clock_sample *base_last,const struct clock_sample *first,const struct clock_sample *last,struct tsc_report *out) {
    const struct clock_sample *s[]={base_first,base_last,first,last};int64_t offsets[4];
    for(unsigned i=0;i<4;++i) {
        if((s[i]->flags&(CLOCK_TSC_PRESENT|CLOCK_TSC_INVARIANT))!=(CLOCK_TSC_PRESENT|CLOCK_TSC_INVARIANT) || !offset(s[i],&offsets[i])) {out->status=TSC_UNAVAILABLE;return 0;}
        if(s[i]->uncertainty_ns>out->max_uncertainty_ns)out->max_uncertainty_ns=s[i]->uncertainty_ns;
        if(s[i]->uncertainty_ns>50000) {out->status=TSC_UNCERTAIN;return 0;}
    }
    uint64_t error=first->uncertainty_ns+base_first->uncertainty_ns;
    uint64_t gap=signed_distance(offsets[2],offsets[0]);gap=gap>UINT64_MAX-error?UINT64_MAX:gap+error;
    if(gap>out->max_offset_ns)out->max_offset_ns=gap;
    if(gap>10000) {out->status=TSC_OFFSET;return 0;}
    error=last->uncertainty_ns+base_last->uncertainty_ns;gap=signed_distance(offsets[3],offsets[1]);gap=gap>UINT64_MAX-error?UINT64_MAX:gap+error;
    if(gap>out->max_offset_ns)out->max_offset_ns=gap;
    if(gap>10000) {out->status=TSC_OFFSET;return 0;}
    if(last->hpet_ns<=first->hpet_ns || last->tsc_ns<first->tsc_ns || last->hpet_ns-first->hpet_ns<20000000) {out->status=TSC_UNCERTAIN;return 0;}
    uint64_t interval=last->hpet_ns-first->hpet_ns,drift=distance(last->tsc_ns-first->tsc_ns,interval);
    error=first->uncertainty_ns+last->uncertainty_ns;drift=drift>UINT64_MAX-error?UINT64_MAX:drift+error;
    uint64_t ppm=drift>UINT64_MAX/1000000?UINT64_MAX:drift*1000000/interval;
    if(ppm>out->max_drift_ppm)out->max_drift_ppm=ppm;
    if(ppm>500) {out->status=TSC_DRIFT;return 0;}
    return 1;
}

static uint64_t add_sat(uint64_t a,uint64_t b) {return a>UINT64_MAX-b?UINT64_MAX:a+b;}
static uint64_t rate(uint64_t numerator,uint64_t denominator,uint64_t multiplier,int ceil) {
    if(!denominator || numerator>UINT64_MAX/multiplier)return UINT64_MAX;
    uint64_t value=numerator*multiplier;return value/denominator+(ceil && value%denominator!=0);
}
static int64_t median(int64_t *values,unsigned n) {
    for(unsigned i=1;i<n;++i) {int64_t v=values[i];unsigned j=i;while(j && values[j-1]>v) {values[j]=values[j-1];--j;}values[j]=v;}
    return values[n/2]; /* Upper median for an even group; no floating point. */
}
static uint64_t mad(const int64_t *values,unsigned n,int64_t center) {
    int64_t deviations[TSC_TRIALS];
    for(unsigned i=0;i<n;++i) {uint64_t d=signed_distance(values[i],center);deviations[i]=d>INT64_MAX?INT64_MAX:(int64_t)d;}
    return median(deviations,n);
}
static uint64_t cutoff(uint64_t deviation,uint64_t floor) {if(deviation<floor)deviation=floor;return deviation>UINT64_MAX/3?UINT64_MAX:deviation*3;}
/* Delay widths are bounded measurements, not exact statistical point values.
 * Keep ordinary width variation; the absolute 50us acceptance gate still applies. */
static uint64_t width_limit(uint64_t center,uint64_t deviation) {
    uint64_t spread=cutoff(deviation,1);if(spread<center)spread=center;
    return add_sat(center,spread);
}
void tsc_analyze_group(const struct clock_sample *bf,const struct clock_sample *bl,const struct clock_sample *first,const struct clock_sample *last,int self,struct tsc_group *out) {
    uint64_t target=out->target_ns;*out=(struct tsc_group){.status=TSC_UNCERTAIN,.target_ns=target};
    int64_t widths[TSC_TRIALS],drifts[TSC_TRIALS],offsets[TSC_TRIALS],intervals[TSC_TRIALS];
    uint64_t offset_bounds[TSC_TRIALS],drift_bounds[TSC_TRIALS],drift_errors[TSC_TRIALS],offset_errors[TSC_TRIALS];
    for(unsigned i=0;i<TSC_TRIALS;++i) {
        const struct clock_sample *s[]={&bf[i],&bl[i],&first[i],&last[i]};int64_t o[4];uint64_t width=0;
        for(unsigned j=0;j<4;++j) {
            if((s[j]->flags&(CLOCK_TSC_PRESENT|CLOCK_TSC_INVARIANT))!=(CLOCK_TSC_PRESENT|CLOCK_TSC_INVARIANT) || !offset(s[j],&o[j])) {out->status=TSC_UNAVAILABLE;return;}
            if(s[j]->uncertainty_ns>width)width=s[j]->uncertainty_ns;
        }
        if(last[i].hpet_ns<=first[i].hpet_ns || last[i].tsc_ns<first[i].tsc_ns || width>INT64_MAX)return;
        uint64_t elapsed=last[i].hpet_ns-first[i].hpet_ns,elapsed_tsc=last[i].tsc_ns-first[i].tsc_ns;
        if(elapsed>INT64_MAX)return;
        uint64_t magnitude=rate(distance(elapsed_tsc,elapsed),elapsed,1000000000,0);
        if(magnitude>INT64_MAX)return;
        drifts[i]=elapsed_tsc>=elapsed?(int64_t)magnitude:-(int64_t)magnitude;
        intervals[i]=elapsed;widths[i]=width;
        uint64_t gaps[2]={self?0:signed_distance(o[2],o[0]),self?0:signed_distance(o[3],o[1])};
        if(gaps[0]>INT64_MAX || gaps[1]>INT64_MAX)return;
        unsigned worst=gaps[1]>gaps[0];unsigned index=worst?3:2,reference=worst?1:0;
        offsets[i]=o[index]>=o[reference]?(int64_t)gaps[worst]:-(int64_t)gaps[worst];
        offset_bounds[i]=self?0:add_sat(gaps[0],add_sat(first[i].uncertainty_ns,bf[i].uncertainty_ns));
        uint64_t last_bound=self?0:add_sat(gaps[1],add_sat(last[i].uncertainty_ns,bl[i].uncertainty_ns));
        if(last_bound>offset_bounds[i])offset_bounds[i]=last_bound;
        offset_errors[i]=self?0:add_sat(first[i].uncertainty_ns,bf[i].uncertainty_ns);
        uint64_t last_error=self?0:add_sat(last[i].uncertainty_ns,bl[i].uncertainty_ns);if(last_error>offset_errors[i])offset_errors[i]=last_error;
        drift_errors[i]=rate(add_sat(first[i].uncertainty_ns,last[i].uncertainty_ns),elapsed,1000000000,1);
        drift_bounds[i]=rate(add_sat(distance(elapsed_tsc,elapsed),add_sat(first[i].uncertainty_ns,last[i].uncertainty_ns)),elapsed,1000000,1);
    }
    int64_t scratch[TSC_TRIALS];for(unsigned i=0;i<TSC_TRIALS;++i)scratch[i]=widths[i];
    int64_t middle=median(scratch,TSC_TRIALS);uint64_t limit=width_limit(middle,mad(widths,TSC_TRIALS,middle));
    unsigned n=0;int64_t ds[TSC_TRIALS],os[TSC_TRIALS],de[TSC_TRIALS],oe[TSC_TRIALS];
    for(unsigned i=0;i<TSC_TRIALS;++i) {
        if(widths[i]>50000 || (uint64_t)widths[i]>limit)out->bracket_mask|=1u<<i;
        else {ds[n]=drifts[i];os[n]=offsets[i];de[n]=drift_errors[i]>INT64_MAX?INT64_MAX:drift_errors[i];oe[n]=offset_errors[i]>INT64_MAX?INT64_MAX:offset_errors[i];++n;}
    }
    out->retained=n;if(!n)return;
    int64_t dm=median(ds,n),om=median(os,n);
    uint64_t dl=cutoff(mad(ds,n,dm),1000),ol=cutoff(mad(os,n,om),1);
    uint64_t median_de=median(de,n),median_oe=median(oe,n);
    n=0;int64_t retained_widths[TSC_TRIALS];
    for(unsigned i=0;i<TSC_TRIALS;++i)if(!(out->bracket_mask&(1u<<i))) {
        uint64_t drift_limit=add_sat(drift_errors[i],median_de),offset_limit=add_sat(offset_errors[i],median_oe);
        if(drift_limit<dl)drift_limit=dl;
        if(offset_limit<ol)offset_limit=ol;
        if(signed_distance(drifts[i],dm)>drift_limit || signed_distance(offsets[i],om)>offset_limit) {out->outlier_mask|=1u<<i;continue;}
        ds[n]=drifts[i];os[n]=offsets[i];scratch[n]=intervals[i];retained_widths[n]=widths[i];++n;
        if(offset_bounds[i]>out->max_offset_ns)out->max_offset_ns=offset_bounds[i];
        if(drift_bounds[i]>out->max_drift_ppm)out->max_drift_ppm=drift_bounds[i];
        if((uint64_t)widths[i]>out->max_uncertainty_ns)out->max_uncertainty_ns=widths[i];
    }
    out->retained=n;if(!n)return;
    out->drift_median_ppb=median(ds,n);out->offset_median_ns=median(os,n);
    out->drift_mad_ppb=mad(ds,n,out->drift_median_ppb);out->offset_mad_ns=mad(os,n,out->offset_median_ns);
    out->interval_median_ns=median(scratch,n);out->uncertainty_median_ns=median(retained_widths,n);
    out->status=n<TSC_MIN_RETAINED?TSC_UNCERTAIN:out->max_offset_ns>10000?TSC_OFFSET:out->max_drift_ppm>500?TSC_DRIFT:TSC_VALID;
}

int tsc_exchange_measure(struct tsc_exchange *s) {
    if(!s->hz || s->hz>10000000000 || s->after<s->before)return 0;
    uint64_t middle=s->before+(s->after-s->before)/2;
    uint64_t magnitude=rate(distance(s->remote,middle),s->hz,1000000000,1);
    uint64_t half=(s->after-s->before)/2+((s->after-s->before)&1);
    uint64_t uncertainty=rate(half,s->hz,1000000000,1);
    if(magnitude>INT64_MAX || uncertainty==UINT64_MAX)return 0;
    s->offset_ns=s->remote>=middle?(int64_t)magnitude:-(int64_t)magnitude;
    s->uncertainty_ns=add_sat(uncertainty,1); /* midpoint/counter rounding */
    return 1;
}
void tsc_analyze_offsets(const struct tsc_exchange *samples,struct tsc_offset_group *out) {
    *out=(struct tsc_offset_group){.status=TSC_UNCERTAIN};
    int64_t widths[TSC_TRIALS],offsets[TSC_TRIALS],scratch[TSC_TRIALS];
    for(unsigned i=0;i<TSC_TRIALS;++i) {
        if(!samples[i].hz || samples[i].after<samples[i].before || samples[i].hz!=samples[0].hz || samples[i].epoch!=samples[0].epoch || samples[i].uncertainty_ns>INT64_MAX)return;
        if(i && (samples[i].before<samples[i-1].after || samples[i].remote<samples[i-1].remote))return;
        widths[i]=samples[i].uncertainty_ns;offsets[i]=samples[i].offset_ns;scratch[i]=widths[i];
    }
    int64_t center=median(scratch,TSC_TRIALS);
    uint64_t limit=width_limit(center,mad(widths,TSC_TRIALS,center));unsigned n=0;int64_t errors[TSC_TRIALS];
    for(unsigned i=0;i<TSC_TRIALS;++i) {
        if(widths[i]>50000 || (uint64_t)widths[i]>limit)out->wide_mask|=1u<<i;
        else {scratch[n]=offsets[i];errors[n]=widths[i];++n;}
    }
    out->retained=n;if(!n)return;
    center=median(scratch,n);uint64_t spread=cutoff(mad(scratch,n,center),1),center_error=median(errors,n);n=0;
    for(unsigned i=0;i<TSC_TRIALS;++i)if(!(out->wide_mask&(1u<<i))) {
        uint64_t tolerance=add_sat(widths[i],center_error);if(tolerance<spread)tolerance=spread;
        if(signed_distance(offsets[i],center)>tolerance) {out->outlier_mask|=1u<<i;continue;}
        scratch[n++]=offsets[i];
        uint64_t magnitude=offsets[i]<0?UINT64_C(0)-(uint64_t)offsets[i]:(uint64_t)offsets[i];
        uint64_t bound=add_sat(magnitude,widths[i]);
        if(bound>out->max_offset_ns)out->max_offset_ns=bound;
        if((uint64_t)widths[i]>out->max_uncertainty_ns)out->max_uncertainty_ns=widths[i];
    }
    out->retained=n;if(!n)return;
    out->median_ns=median(scratch,n);out->mad_ns=mad(scratch,n,out->median_ns);
    out->status=n<TSC_MIN_RETAINED?TSC_UNCERTAIN:out->max_offset_ns>10000?TSC_OFFSET:TSC_VALID;
}
