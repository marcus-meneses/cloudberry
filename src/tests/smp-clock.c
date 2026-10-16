/* Host-only deterministic mailbox/clock adapter; production adaptive control flow. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/smp.c"
static struct {struct kernel_surfaces hub;struct kernel_surface methods[6];} fixture;
static struct startup records[4];
static uint64_t fake_ns,fake_hz,fake_epoch;static unsigned fake_cpu,mode,activations,refinements;
void smp_entry(struct limine_mp_info *info) {(void)info;assert(0);}
void smp_boot_run(uintptr_t stack,kernel_surface_call event) {(void)stack;(void)event;assert(0);__builtin_trap();}
static struct kernel_surface_result sample(MODULE_ARGS) {
    MODULE_UNUSED();unsigned round=(struct clock_sample *)a-records[fake_cpu].samples;
    assert(round<TSC_OBSERVATIONS);
    *(struct clock_sample *)a=(struct clock_sample){.tsc_ns=(mode==3 || mode==4)?fake_ns*(mode==3?1999400000:1998000000)/fake_hz:fake_ns,.hpet_ns=fake_ns,.uncertainty_ns=100,.flags=CLOCK_TSC_PRESENT|CLOCK_TSC_INVARIANT};
    if((mode==1 || mode==3 || mode==8) && fake_cpu==1)((struct clock_sample *)a)->tsc_ns+=20000;
    if(((mode==2 || (mode==7 && round<20)) && fake_cpu==2 && round%TSC_TRIALS<4) || (mode==8 && !fake_cpu && round%TSC_TRIALS<4))((struct clock_sample *)a)->uncertainty_ns=100000;
    if(b)*(struct clock_probe *)b=(struct clock_probe){.empty_ns=50,.bracket_ns=100};
    fake_ns+=1000;return module_result(1,1);
}
static struct kernel_surface_result control(MODULE_ARGS) {
    MODULE_UNUSED();if(a==CPU_WAKE) {
        assert(b>0 && b<4);
        if(records[b].exchange_active) {records[b].exchange_ready=1;return module_result(0,1);}
        fake_cpu=b;
        struct startup *s=&records[b];assert(s->request && s->request<=TSC_OBSERVATIONS);
        assert(!take_sample(s,s->request-1).status);
        s->ack=s->request;fake_cpu=0;
    }
    return module_result(0,1);
}
static struct kernel_surface_result now(MODULE_ARGS) {
    MODULE_UNUSED();for(unsigned i=1;i<4;++i)if(!records[i].exchange_active)records[i].exchange_ready=0;
    fake_ns+=1000000;return module_result(fake_ns,1);
}
static struct kernel_surface_result info(MODULE_ARGS) {
    MODULE_UNUSED();*(struct clock_info *)a=(struct clock_info){.source=CLOCK_HPET,.flags=CLOCK_HPET_AVAILABLE|CLOCK_TSC_PRESENT|CLOCK_TSC_INVARIANT,.calibrations=fake_epoch,.tsc_hz=fake_hz};return module_result(1,1);
}
static struct kernel_surface_result diagnostics(MODULE_ARGS) {MODULE_UNUSED();*(struct tsc_diagnostics *)a=(struct tsc_diagnostics){0};return module_result(1,1);}
static struct kernel_surface_result shared(MODULE_ARGS) {MODULE_UNUSED();return module_result(1,1);}
static struct kernel_surface_result enable(MODULE_ARGS) {
    MODULE_UNUSED();const struct tsc_report *proof=(void *)c;
    assert(proof->calibration==fake_epoch);
    assert(a==4 && proof->status==TSC_VALID && proof->max_offset_ns<=10000 && proof->max_drift_ppm<=500);
    ++activations;return module_result(1,1);
}
static struct kernel_surface_result ticks(MODULE_ARGS) {
    MODULE_UNUSED();fake_ns+=100;
    if(!fake_cpu)for(unsigned i=1;i<4;++i)if(records[i].exchange_active && records[i].exchange_ready && records[i].exchange_request!=records[i].exchange_ack) {
        if(mode==6) {fake_ns+=3000000000;continue;}
        if(mode==5)fake_ns+=50000;
        fake_cpu=i;exchange_reply(&records[i]);fake_cpu=0;
    }
    return module_result(fake_ns*2+((mode==1 || mode==3 || mode==8) && fake_cpu==1?40000:0),1);
}
static struct kernel_surface_result refine(MODULE_ARGS) {
    MODULE_UNUSED();const struct tsc_group *g=(void *)a;
    if((mode!=3 && mode!=4) || refinements || g->target_ns<100000000 || g->retained<7)return module_result(0,0);
    assert(b==fake_epoch && c==fake_hz && g->drift_median_ppb<0);
    fake_hz=mode==3?1999400000:1998000000;++fake_epoch;++refinements;return module_result(fake_hz,1);
}
static void setup(unsigned test_mode) {
    memset(records,0,sizeof(records));memset(&fixture,0,sizeof(fixture));
    state=(__typeof__(state)){.prepared=1,.started=1,.count=4,.startup=records,.hub=&fixture.hub,.control=control,.sample=sample,.now=now,.heap=1};
    fixture.hub=(struct kernel_surfaces){.magic=KERNEL_SURFACES_MAGIC,.major=2,.count=6,.header_bytes=offsetof(__typeof__(fixture),methods)};
    const char *names[]={"clock.info","clock.shared","clock.tsc_status","clock.smp_tsc","cpu.ticks","clock.refine"};
    kernel_surface_call calls[]={info,shared,diagnostics,enable,ticks,refine};
    for(unsigned i=0;i<6;++i) {strcpy(fixture.methods[i].name,names[i]);fixture.methods[i].call=calls[i];fixture.methods[i].major=1;}
    fake_ns=1000000000;fake_hz=2000000000;fake_epoch=1;fake_cpu=activations=refinements=0;mode=test_mode;
}
int main(void) {
    setup(0);assert(!smp_surface_validate_clock(0,0,0,0,0,0).status);
    assert(state.attempts==1 && state.observations==80 && activations==1 && state.report.status==TSC_VALID);
    struct tsc_group group;assert(!smp_surface_clock_group((uintptr_t)&group,3,0,0,0,0).status && group.retained==10);
    assert(smp_surface_clock_group((uintptr_t)&group,3,1,0,0,0).status);
    struct clock_sample raw;assert(!smp_surface_clock_sample((uintptr_t)&raw,3,19,0,0,0).status);
    assert(smp_surface_clock_sample((uintptr_t)&raw,3,20,0,0,0).status);
    setup(1);assert(smp_surface_validate_clock(0,0,0,0,0,0).status);
    assert(state.attempts==6 && state.observations==480 && !activations && state.report.status==TSC_OFFSET && state.clock_status.cpu==1);
    for(unsigned i=0;i<6;++i)assert(records[1].groups[i].retained==10 && !records[1].groups[i].outlier_mask);
    setup(2);assert(smp_surface_validate_clock(0,0,0,0,0,0).status);
    assert(state.attempts==18 && state.observations==1440 && !activations && state.report.status==TSC_UNCERTAIN && state.clock_status.cpu==2);
    struct smp_clock_cost cost;assert(!smp_surface_clock_cost((uintptr_t)&cost,1,0,0,0,0).status);
    assert(cost.call_ns && cost.relay_ns && cost.empty_ns==50 && cost.epoch==1);
    setup(3);assert(smp_surface_validate_clock(0,0,0,0,0,0).status);
    assert(refinements==1 && fake_epoch==2 && state.attempts==7 && !activations && state.report.calibration==2);
    assert(records[0].groups[3].target_ns==100000000 && records[0].groups[4].target_ns==100000000);
    assert(records[0].costs[60].epoch==1 && records[0].costs[80].epoch==2);
    assert(records[0].groups[4].drift_median_ppb==0);
    setup(4);assert(!smp_surface_validate_clock(0,0,0,0,0,0).status);
    assert(refinements==1 && activations==1 && fake_epoch==2 && state.report.calibration==2 && state.attempts==5);
    assert(records[0].groups[3].status==TSC_DRIFT && records[0].groups[4].status==TSC_VALID);
    setup(5);assert(smp_surface_validate_clock(0,0,0,0,0,0).status);
    assert(!activations && state.report.status==TSC_OFFSET && state.clock_status.cpu==1);
    assert(records[1].offsets[5].end.max_offset_ns>10000);
    setup(6);assert(smp_surface_validate_clock(0,0,0,0,0,0).status);
    assert(!activations && state.report.status==TSC_TIMEOUT && state.clock_status.reason==TSC_REASON_TIMEOUT);
    assert(!records[1].exchange_active && !records[1].exchange_ready);
    setup(7);assert(!smp_surface_validate_clock(0,0,0,0,0,0).status);
    assert(state.attempts==2 && state.observations==160 && activations==1);
    assert(records[2].groups[0].retained==6 && records[2].groups[1].retained==10);
    assert(records[2].groups[0].target_ns==10000000 && records[2].groups[1].target_ns==10000000);
    setup(8);assert(smp_surface_validate_clock(0,0,0,0,0,0).status);
    assert(state.attempts==6 && state.observations==480 && !activations);
    assert(records[1].offsets[5].end.max_offset_ns>10000);
    puts("PASS adaptive sampler: first-window stop, six-window exhaustion, every CPU required, snapshot bounds and HPET retention");
}
