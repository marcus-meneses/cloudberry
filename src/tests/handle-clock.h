#ifndef CLOUDBERRY_TEST_HANDLE_CLOCK_H
#define CLOUDBERRY_TEST_HANDLE_CLOCK_H
#include "../kmodules/shared/smp.h"
/* Test-only observation of existing qualification records; no recalibration. */
static void clock_row(const char *kind,const uint64_t *fields,unsigned count){text("PROFILE handles ");text(kind);for(unsigned i=0;i<count;++i){text(" ");number(fields[i]);}text("\n");}
static int clock_diagnostic(const struct kernel_surfaces *hub,unsigned phase){
    struct tsc_diagnostics current,qualification;struct tsc_report proof;
    if(CALL(kernel_surface_find(hub,"clock.tsc_status"),&current,0,0).status || CALL(kernel_surface_find(hub,"smp.clock_status"),&qualification,0,0).status || CALL(kernel_surface_find(hub,"smp.clock_info"),&proof,0,0).status)return 0;
    uint64_t values[]={phase,current.source,current.reason,current.flags,current.hz,current.calibrations,qualification.reason,qualification.cpu,qualification.samples,proof.status,proof.cpus,proof.max_offset_ns,proof.max_drift_ppm,proof.max_uncertainty_ns,proof.calibration};
    clock_row("clock",values,15);return 1;
}
static int clock_records(const struct kernel_surfaces *hub,unsigned cpus){
    kernel_surface_call sample=kernel_surface_find(hub,"smp.clock_sample"),cost=kernel_surface_find(hub,"smp.clock_cost"),group=kernel_surface_find(hub,"smp.clock_group"),offset=kernel_surface_find(hub,"smp.clock_offsets"),exchange=kernel_surface_find(hub,"smp.clock_exchange");
    if(!sample || !cost || !group || !offset || !exchange)return 0;
    for(unsigned round=0;round<TSC_OBSERVATIONS;++round)for(unsigned cpu=0;cpu<cpus;++cpu){
        struct clock_sample s;struct smp_clock_cost c;if(CALL(sample,&s,cpu,round).status)continue;if(CALL(cost,&c,cpu,round).status)return 0;
        uint64_t fields[]={cpu,round,s.tsc_ns,s.hpet_ns,s.uncertainty_ns,s.flags,c.empty_ns,c.bracket_ns,c.call_ns,c.relay_ns,c.hz,c.epoch};clock_row("sample",fields,12);
        if(cpu){struct tsc_exchange x;if(!CALL(exchange,&x,cpu,round).status){uint64_t raw[]={cpu,round,x.before,x.remote,x.after,x.hz,x.epoch,(uint64_t)x.offset_ns,x.uncertainty_ns};clock_row("exchange",raw,9);}}
    }
    for(unsigned attempt=0;attempt<TSC_ATTEMPTS;++attempt)for(unsigned cpu=0;cpu<cpus;++cpu){
        struct tsc_group g;if(CALL(group,&g,cpu,attempt).status)continue;
        /* Signed quantities are encoded as uint64 two's complement in this
         * private log framing; the report decoder restores their signs. */
        uint64_t fields[]={cpu,attempt,g.status,g.retained,g.bracket_mask,g.outlier_mask,g.target_ns,g.interval_median_ns,g.uncertainty_median_ns,(uint64_t)g.drift_median_ppb,(uint64_t)g.offset_median_ns,g.drift_mad_ppb,g.offset_mad_ns,g.max_offset_ns,g.max_drift_ppm,g.max_uncertainty_ns};clock_row("group",fields,16);
        struct tsc_offset_pair pair;if(CALL(offset,&pair,cpu,attempt).status)return 0;
        for(unsigned phase=0;phase<2;++phase){const struct tsc_offset_group *o=phase?&pair.end:&pair.start;uint64_t raw[]={cpu,attempt,phase,o->status,o->retained,o->wide_mask,o->outlier_mask,(uint64_t)o->median_ns,o->mad_ns,o->max_offset_ns,o->max_uncertainty_ns};clock_row("offset",raw,11);}
    }return 1;
}
#endif
