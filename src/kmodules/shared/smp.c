#include "smp.h"
#include "apic.h"
#include "mem-pages.h"
#include "tsc-validation.h"
#include "clock.h"
#include "threads.h"
#include "mmio.h"
#include "../../boot/limine.h"
struct startup {uintptr_t top,cr3,tables,ist[3];struct cpu_record *cpu;uint64_t request,ack;struct clock_sample samples[TSC_OBSERVATIONS];struct tsc_group groups[TSC_ATTEMPTS];struct smp_clock_cost costs[TSC_OBSERVATIONS];uint64_t sample_failed;
    uint64_t exchange_active,exchange_ready,exchange_request,exchange_ack,exchange_remote,exchange_failed,exchange_previous,exchange_count;
    struct tsc_exchange exchanges[TSC_OBSERVATIONS];struct tsc_offset_pair offsets[TSC_ATTEMPTS];};
static struct {const struct kernel_surfaces *hub;uintptr_t heap;struct limine_mp_response *mp;struct cpu_record *cpus;struct startup *startup;struct apic_info apic;kernel_surface_call ticks,control,idt,timer,threads,now,controller,sample,thread_control,arm,stop;struct tsc_report report;struct tsc_diagnostics clock_status;uint64_t observations,attempts,sample_hz,sample_epoch,exchange_last_bsp;int validating;size_t count;int prepared,started;} state;
extern void smp_entry(struct limine_mp_info *) __attribute__((visibility("hidden")));
static struct kernel_surface_result call(kernel_surface_call fn,uintptr_t a,uintptr_t b,uintptr_t c) {return fn(a,b,c,0,0,0);}
static int ticks_ns(uint64_t ticks,uint64_t *out) {
    if(!state.sample_hz)return 0;
    uint64_t q=ticks/state.sample_hz,r=ticks%state.sample_hz;
    if(q>UINT64_MAX/1000000000 || r>UINT64_MAX/1000000000)return 0;
    uint64_t whole=q*1000000000,fraction=r*1000000000/state.sample_hz;
    if(fraction>UINT64_MAX-whole)return 0;
    *out=whole+fraction;return 1;
}
static struct kernel_surface_result take_sample(struct startup *boot,unsigned round) {
    struct clock_probe probe={0};
    struct kernel_surface_result before=call(state.ticks,0,0,0);
    struct kernel_surface_result r=call(state.sample,(uintptr_t)&boot->samples[round],(uintptr_t)&probe,0);
    struct kernel_surface_result after=call(state.ticks,0,0,0);
    if(r.status)return r;
    if(before.status || after.status || after.value<before.value)return module_result(TSC_REASON_REGRESSION,0);
    struct smp_clock_cost cost={.empty_ns=probe.empty_ns,.bracket_ns=probe.bracket_ns,.hz=state.sample_hz,.epoch=state.sample_epoch};
    if(!ticks_ns(after.value-before.value,&cost.call_ns))return module_result(TSC_REASON_CONVERSION,0);
    boot->costs[round]=cost;return module_result(1,1);
}
/* Called only by the target AP. Release/acquire publishes the timestamp. */
static void exchange_reply(struct startup *boot) {
    uint64_t request=__atomic_load_n(&boot->exchange_request,__ATOMIC_ACQUIRE);
    if(request==__atomic_load_n(&boot->exchange_ack,__ATOMIC_RELAXED))return;
    struct kernel_surface_result ticks=call(state.ticks,0,0,0);
    if(ticks.status || (boot->exchange_previous && ticks.value<boot->exchange_previous))
        __atomic_store_n(&boot->exchange_failed,TSC_REASON_REGRESSION,__ATOMIC_RELEASE);
    else {boot->exchange_remote=ticks.value;boot->exchange_previous=ticks.value;}
    __atomic_store_n(&boot->exchange_ack,request,__ATOMIC_RELEASE);
}
static void exchange_poll(struct startup *boot) {
    struct kernel_surface_result begin=call(state.ticks,0,0,0);
    __asm__ volatile("sti":::"memory");
    __atomic_store_n(&boot->exchange_ready,1,__ATOMIC_RELEASE);
    unsigned polls=0;
    while(__atomic_load_n(&boot->exchange_active,__ATOMIC_ACQUIRE)) {
        exchange_reply(boot);
        if((++polls&4095)==0) {
            struct kernel_surface_result now=call(state.ticks,0,0,0);
            if(begin.status || now.status || now.value<begin.value || now.value-begin.value>state.sample_hz*2 || polls>=2000000) {
                __atomic_store_n(&boot->exchange_failed,TSC_REASON_TIMEOUT,__ATOMIC_RELEASE);break;
            }
        }
        __asm__ volatile("pause");
    }
    __atomic_store_n(&boot->exchange_ready,0,__ATOMIC_RELEASE);
    __asm__ volatile("cli":::"memory");
}
__attribute__((noreturn)) static void fail(void) {
    const char *s="Cloudberry startup: smp AP failed\n";while(*s)__asm__ volatile("outb %0,$0xe9"::"a"((uint8_t)*s++));for(;;)__asm__ volatile("cli;hlt");
}
__attribute__((visibility("hidden"),noreturn)) void smp_ap_main(struct startup *boot) {
    if(call(state.control,CPU_ATTACH,(uintptr_t)boot->cpu,0).status)fail();
    kernel_surface_call xstate=kernel_surface_find_version(state.hub,"xstate.cpu_init",1,0);
    if(!xstate || call(xstate,0,0,0).status)fail();
    if(call(state.idt,boot->tables,(uintptr_t)boot->ist,0).status) fail();
    if(call(state.controller,0,0,0).status)fail();
    if(call(state.timer,(uintptr_t)state.hub,state.heap,state.count).status) fail();
    /* A shared HPET read is CPU-independent; verify forward progress locally
     * before publishing online. No unvalidated per-CPU TSC enters shared time. */
    struct kernel_surface_result first=call(state.now,0,0,0),last=first;
    unsigned poll;for(poll=0;poll<1000000;++poll) {last=call(state.now,0,0,0);if(last.status || last.value<first.value)fail();if(last.value>first.value)break;}
    if(first.status || poll==1000000)fail();
    __atomic_store_n(&boot->cpu->online,1,__ATOMIC_RELEASE);
    for(;;) {
        __asm__ volatile("cli":::"memory");
        uint64_t request=__atomic_load_n(&boot->request,__ATOMIC_ACQUIRE);
        if(request!=__atomic_load_n(&boot->ack,__ATOMIC_RELAXED)) {
            struct kernel_surface_result sampled=take_sample(boot,request-1);
            boot->sample_failed=sampled.status?(sampled.value?sampled.value:TSC_REASON_UNCALIBRATED):0;__atomic_store_n(&boot->ack,request,__ATOMIC_RELEASE);
        }
        if(__atomic_load_n(&boot->exchange_active,__ATOMIC_ACQUIRE))exchange_poll(boot);
        struct kernel_surface_result r=call(state.threads,64,0,0);if(r.status)fail();
        if(r.value) {__asm__ volatile("sti":::"memory");continue;}
        struct kernel_surface_result deadline=call(state.thread_control,THREAD_DEADLINE,0,0);
        if(deadline.status || (deadline.value?call(state.arm,deadline.value,0,0):call(state.stop,0,0,0)).status)fail();
        __asm__ volatile("sti;hlt":::"memory");
    }
}
struct kernel_surface_result smp_surface_prepare(MODULE_ARGS) {
    MODULE_UNUSED();if(state.prepared)return module_result(1,state.hub==(const void *)a && state.heap==b && state.mp==(void *)c);
    const struct kernel_surfaces *hub=(const void *)a;struct limine_mp_response *mp=(void *)c;
    if(!hub || !b || !mp || !mp->cpu_count || !mp->cpus || mp->cpu_count>SIZE_MAX/sizeof(struct startup))return module_result(0,0);
    state.hub=hub;state.heap=b;state.mp=mp;state.count=mp->cpu_count;state.report=(struct tsc_report){.cpus=state.count};
    state.sample=kernel_surface_find_version(hub,"clock.sample",1,0);state.controller=kernel_surface_find_version(hub,"apic.cpu_init",1,0);state.control=kernel_surface_find_version(hub,"cpu.control",1,0);state.idt=kernel_surface_find_version(hub,"inthandle.cpu_init",1,0);state.timer=kernel_surface_find_version(hub,"timer.init",1,0);state.threads=kernel_surface_find_version(hub,"threads.run",1,0);state.now=kernel_surface_find_version(hub,"clock.now_ns",1,0);
    state.thread_control=kernel_surface_find(hub,"threads.control");state.arm=kernel_surface_find(hub,"timer.arm");state.stop=kernel_surface_find(hub,"timer.stop");
    kernel_surface_call alloc=kernel_surface_find_version(hub,"mem.calloc",1,0),stack=kernel_surface_find_version(hub,"mem.stack_alloc",1,0),apic=kernel_surface_find_version(hub,"apic.info",1,0),claim=kernel_surface_find_version(hub,"inthandle.claim",1,0),shared=kernel_surface_find_version(hub,"clock.shared",1,0),publish=kernel_surface_find_version(hub,"hub.publish_table",1,0);
    if(!state.thread_control || !state.arm || !state.stop || !state.sample || !state.controller || !state.control || !state.idt || !state.timer || !state.threads || !state.now || !alloc || !stack || !apic || !claim || !shared || !publish || call(apic,(uintptr_t)&state.apic,0,0).status)return module_result(0,0);
    if(state.count>1 && call(shared,0,0,0).status)return module_result(0,0);
    struct kernel_surface_result records=call(alloc,b,state.count,sizeof(struct cpu_record)),starts=call(alloc,b,state.count,sizeof(struct startup)),bytes=call(state.idt,0,0,0);
    if(records.status || starts.status || bytes.status)return module_result(0,0);
    state.cpus=(void *)records.value;state.startup=(void *)starts.value;
    size_t bsp=state.count;for(size_t i=0;i<state.count;++i)if(mp->cpus[i] && mp->cpus[i]->lapic_id==mp->bsp_lapic_id)bsp=i;
    if(bsp==state.count)return module_result(0,0);
    /* BSP is logical CPU0 independent of firmware enumeration order. */
    struct limine_mp_info *first=mp->cpus[0];mp->cpus[0]=mp->cpus[bsp];mp->cpus[bsp]=first;
    for(size_t i=0;i<state.count;++i) {
        if(!mp->cpus[i])return module_result(0,0);
        for(size_t j=0;j<i;++j)if(mp->cpus[i]->lapic_id==mp->cpus[j]->lapic_id)return module_result(0,0);
        state.cpus[i].index=i;state.cpus[i].lapic_id=mp->cpus[i]->lapic_id;struct startup *s=&state.startup[i];s->cpu=&state.cpus[i];
        struct kernel_surface_result tables=call(alloc,b,1,bytes.value);if(tables.status)return module_result(0,0);s->tables=tables.value;
        for(unsigned k=0;k<4;++k) {struct mem_stack out;if(call(stack,32768,0,(uintptr_t)&out).status)return module_result(0,0);if(!k) {s->top=out.top;state.cpus[i].stack_bottom=out.bottom;}else {s->ist[k-1]=out.top;state.cpus[i].ist_bottom[k-1]=out.bottom;state.cpus[i].ist_top[k-1]=out.top;}}
        __asm__ volatile("mov %%cr3,%0":"=r"(s->cr3));state.cpus[i].stack_top=s->top;
        mp->cpus[i]->extra_argument=(uintptr_t)s;
    }
    struct cpu_config config={.records=state.cpus,.count=state.count,.apic=state.apic.address,.mode=state.apic.mode};
    const struct kernel_hub_table *regions=kernel_hub_find_table(hub,MMIO_TABLE_NAME);
    struct clock_info clock;kernel_surface_call clock_info=kernel_surface_find(hub,"clock.info");
    if(!regions || !clock_info || call(clock_info,(uintptr_t)&clock,0,0).status)return module_result(0,0);
    const struct mmio_table *mmio=(const void *)regions->address;
    for(size_t i=0;i<mmio->count;++i)if(mmio->entries[i].kind==MMIO_HPET && (mmio->entries[i].flags&MMIO_MAPPED)) {
        config.watchdog_hpet=mmio->entries[i].virtual_address+0xf0;config.hpet_period_fs=clock.hpet_period_fs;break;
    }
    if(call(state.control,CPU_CONFIGURE,(uintptr_t)&config,0).status)return module_result(0,0);
    if(call(state.idt,state.startup[0].tables,(uintptr_t)state.startup[0].ist,0).status)return module_result(0,0);
    for(unsigned vector=CPU_TLB_VECTOR;vector<=CPU_WAKE_VECTOR;++vector)if(claim(vector,(uintptr_t)"smp.interrupt",(uintptr_t)&state,(uintptr_t)&state,0,0).status)return module_result(0,0);
    state.cpus[0].online=1;
    struct kernel_hub_table ref={.owner=kernel_surface_owner(hub,"smp.prepare"),.name="smp.cpus",.address=(uintptr_t)state.cpus,.bytes=state.count*sizeof(struct cpu_record),.count=state.count,.entry_bytes=sizeof(struct cpu_record)};
    if(call(publish,(uintptr_t)&ref,0,0).status)return module_result(0,0);
    state.prepared=1;return module_result(state.count,1);
}
struct kernel_surface_result smp_surface_start(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.prepared)return module_result(0,0);if(state.started)return module_result(state.count,1);
    for(size_t i=1;i<state.count;++i) {
        __atomic_store_n(&state.mp->cpus[i]->goto_address,smp_entry,__ATOMIC_RELEASE);
        struct kernel_surface_result begin=call(state.now,0,0,0);if(begin.status)return module_result(0,0);
        while(!__atomic_load_n(&state.cpus[i].online,__ATOMIC_ACQUIRE)) {
            struct kernel_surface_result now=call(state.now,0,0,0);if(now.status || now.value-begin.value>2000000000)return module_result(0,0);__asm__ volatile("pause");
        }
    }
    state.started=1;
    extern struct kernel_surface_result smp_surface_validate_clock(MODULE_ARGS);
    if(state.count>1)(void)smp_surface_validate_clock(0,0,0,0,0,0);
    return module_result(state.count,1);
}
struct kernel_surface_result smp_surface_info(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.prepared || !a)return module_result(0,0);struct smp_info out={.count=state.count,.shared_clock=state.count>1};
    for(size_t i=0;i<state.count;++i) {out.online+=__atomic_load_n(&state.cpus[i].online,__ATOMIC_ACQUIRE)!=0;}
    *(struct smp_info *)a=out;return module_result(1,1);
}
struct kernel_surface_result smp_surface_interrupt(MODULE_ARGS) {
    MODULE_UNUSED();const struct inthandle_frame *frame=(const void *)a;
    if(!frame || (frame->vector!=CPU_TLB_VECTOR && frame->vector!=CPU_WAKE_VECTOR) || b!=(uintptr_t)&state || c!=INTHANDLE_CPU_ENTRY)return module_result(0,0);
    return call(state.control,CPU_ACK,0,0);
}

extern void smp_boot_run(uintptr_t,kernel_surface_call) __attribute__((visibility("hidden"),noreturn));
struct kernel_surface_result smp_surface_run(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.started || call(state.control,CPU_CURRENT,0,0).value)return module_result(0,0);
    kernel_surface_call event=kernel_surface_find_version(state.hub,"event.run",1,0);if(!event)return module_result(0,0);
    smp_boot_run(state.startup[0].top,event);
}

__attribute__((visibility("hidden"),noreturn)) void smp_boot_main(kernel_surface_call event) {
    const char *s="Cloudberry v2: managed BSP event stack active\n";
    while(*s)__asm__ volatile("outb %0,$0xe9"::"a"((uint8_t)*s++));
    call(event,64,0,0);fail();
}

static int collect_round(unsigned round) {
    struct kernel_surface_result sampled=take_sample(&state.startup[0],round);
    if(sampled.status) {state.clock_status.reason=sampled.value;state.clock_status.cpu=0;return 0;}
    ++state.observations;++state.report.samples;
    for(size_t cpu=1;cpu<state.count;++cpu) {
        struct startup *s=&state.startup[cpu];s->sample_failed=0;
        struct kernel_surface_result sent=call(state.ticks,0,0,0);
        __atomic_store_n(&s->request,round+1,__ATOMIC_RELEASE);call(state.control,CPU_WAKE,cpu,0);
        struct kernel_surface_result begin=call(state.now,0,0,0);
        while(__atomic_load_n(&s->ack,__ATOMIC_ACQUIRE)!=round+1) {
            struct kernel_surface_result now=call(state.now,0,0,0);
            if(begin.status || now.status || now.value<begin.value || now.value-begin.value>2000000000) {
                state.report.status=TSC_TIMEOUT;state.clock_status.reason=TSC_REASON_TIMEOUT;state.clock_status.cpu=cpu;return 0;
            }
            __asm__ volatile("pause");
        }
        if(s->sample_failed) {state.clock_status.reason=s->sample_failed;state.clock_status.cpu=cpu;return 0;}
        struct kernel_surface_result received=call(state.ticks,0,0,0);
        if(sent.status || received.status || received.value<sent.value || !ticks_ns(received.value-sent.value,&s->costs[round].relay_ns)) {state.clock_status.reason=TSC_REASON_CONVERSION;state.clock_status.cpu=cpu;return 0;}
        ++state.observations;++state.report.samples;
    }
    return 1;
}

static int exchange_ready(struct startup *s,int wanted) {
    struct kernel_surface_result begin=call(state.now,0,0,0);unsigned polls=0;
    while(__atomic_load_n(&s->exchange_ready,__ATOMIC_ACQUIRE)!=(uint64_t)wanted) {
        struct kernel_surface_result now=call(state.now,0,0,0);
        if(begin.status || now.status || now.value<begin.value || now.value-begin.value>2000000000 || ++polls==2000000)return 0;
        __asm__ volatile("pause");
    }
    return 1;
}
static int collect_exchanges(unsigned start) {
    for(size_t cpu=1;cpu<state.count;++cpu) {
        struct startup *s=&state.startup[cpu];
        if(__atomic_load_n(&s->exchange_ready,__ATOMIC_ACQUIRE)) {state.clock_status.reason=TSC_REASON_SETUP;state.clock_status.cpu=cpu;return 0;}
        s->exchange_failed=0;
        __atomic_store_n(&s->exchange_active,1,__ATOMIC_RELEASE);
        call(state.control,CPU_WAKE,cpu,0);
        int ok=exchange_ready(s,1);uint64_t reason=TSC_REASON_TIMEOUT;
        for(unsigned trial=0;ok && trial<TSC_TRIALS;++trial) {
            struct tsc_exchange exchange={.hz=state.sample_hz,.epoch=state.sample_epoch};
            uint64_t request=__atomic_load_n(&s->exchange_request,__ATOMIC_RELAXED)+1;
            struct kernel_surface_result first=call(state.ticks,0,0,0);
            exchange.before=first.value;
            if(first.status || (state.exchange_last_bsp && first.value<state.exchange_last_bsp)) {reason=TSC_REASON_REGRESSION;ok=0;break;}
            __atomic_store_n(&s->exchange_request,request,__ATOMIC_RELEASE);
            unsigned polls=0;
            while(__atomic_load_n(&s->exchange_ack,__ATOMIC_ACQUIRE)!=request) {
                if(__atomic_load_n(&s->exchange_failed,__ATOMIC_ACQUIRE))break;
                if((++polls&4095)==0) {
                    struct kernel_surface_result now=call(state.ticks,0,0,0);
                    if(now.status || now.value<first.value || now.value-first.value>state.sample_hz*2 || polls>=2000000) {ok=0;break;}
                }
                __asm__ volatile("pause");
            }
            struct kernel_surface_result last=call(state.ticks,0,0,0);
            uint64_t failure=__atomic_load_n(&s->exchange_failed,__ATOMIC_ACQUIRE);
            if(failure) {reason=failure;ok=0;}
            if(__atomic_load_n(&s->exchange_ack,__ATOMIC_ACQUIRE)!=request)ok=0;
            exchange.remote=s->exchange_remote;exchange.after=last.value;
            if(first.status || last.status || (ok && !tsc_exchange_measure(&exchange))) {reason=TSC_REASON_REGRESSION;ok=0;}
            if(ok) {s->exchanges[start+trial]=exchange;s->exchange_count=start+trial+1;state.exchange_last_bsp=last.value;}
        }
        __atomic_store_n(&s->exchange_active,0,__ATOMIC_RELEASE);
        if(!exchange_ready(s,0))ok=0;
        if(!ok) {state.report.status=reason==TSC_REASON_TIMEOUT?TSC_TIMEOUT:TSC_UNAVAILABLE;state.clock_status.reason=reason;state.clock_status.cpu=cpu;return 0;}
    }
    return 1;
}

struct kernel_surface_result smp_surface_validate_clock(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.started || state.validating || call(state.control,CPU_CURRENT,0,0).value || call(state.control,CPU_DEPTH,0,0).value)return module_result(0,0);
    if(__atomic_exchange_n(&state.validating,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    state.report=(struct tsc_report){.status=TSC_UNAVAILABLE,.cpus=state.count};state.observations=0;state.attempts=0;state.exchange_last_bsp=0;
    for(size_t cpu=1;cpu<state.count;++cpu)state.startup[cpu].exchange_count=0;
    state.clock_status=(struct tsc_diagnostics){.reason=TSC_REASON_SETUP,.cpu=0};
    /* Boot-time validation only: runtime recalibration must first select HPET. */
    kernel_surface_call info=kernel_surface_find_version(state.hub,"clock.info",1,0);
    kernel_surface_call shared=kernel_surface_find_version(state.hub,"clock.shared",1,0),diagnostics=kernel_surface_find_version(state.hub,"clock.tsc_status",1,0);
    if(!info || !shared || !diagnostics) {state.clock_status.reason=TSC_REASON_MISSING_METHOD;goto rejected;}
    if(call(shared,0,0,0).status) {struct clock_info current;state.clock_status.reason=!call(info,(uintptr_t)&current,0,0).status && !(current.flags&CLOCK_HPET_AVAILABLE)?TSC_REASON_MISSING_HPET:TSC_REASON_REFERENCE_FAILURE;goto rejected;}
    if(call(diagnostics,(uintptr_t)&state.clock_status,0,0).status) {state.clock_status.reason=TSC_REASON_SETUP;goto rejected;}
    state.clock_status.cpu=0;
    struct clock_info current;if(call(info,(uintptr_t)&current,0,0).status) {state.clock_status.reason=TSC_REASON_SETUP;goto rejected;}
    if(current.source!=CLOCK_HPET) {state.clock_status.reason=TSC_REASON_SOURCE;goto rejected;}
    if(current.flags&CLOCK_TSC_UNSTABLE) {if(!state.clock_status.reason)state.clock_status.reason=TSC_REASON_REGRESSION;goto rejected;}
    if(!(current.flags&CLOCK_TSC_PRESENT)) {state.clock_status.reason=TSC_REASON_MISSING_TSC;goto rejected;}
    if(!(current.flags&CLOCK_TSC_INVARIANT)) {state.clock_status.reason=TSC_REASON_MISSING_INVARIANT;goto rejected;}
    if(!current.tsc_hz)goto rejected;
    state.ticks=kernel_surface_find_version(state.hub,"cpu.ticks",1,0);
    if(!state.ticks) {state.clock_status.reason=TSC_REASON_MISSING_METHOD;goto rejected;}
    unsigned window=0,replacements=0;int refined=0;
    const uint64_t windows[TSC_WINDOWS]={10000000,25000000,50000000,100000000,250000000,500000000};
    for(unsigned attempt=0;attempt<TSC_ATTEMPTS;++attempt) {
        if(call(info,(uintptr_t)&current,0,0).status) {state.clock_status.reason=TSC_REASON_SETUP;goto rejected;}
        state.sample_hz=current.tsc_hz;state.sample_epoch=current.calibrations;state.report.calibration=current.calibrations;
        state.clock_status.hz=current.tsc_hz;state.clock_status.calibrations=current.calibrations;
        unsigned start=attempt*TSC_TRIALS*2;
        for(unsigned phase=0;phase<2;++phase) {
            if(phase) {
                struct kernel_surface_result begin=call(state.now,0,0,0),timestamp;
                do {timestamp=call(state.now,0,0,0);
                    if(begin.status || timestamp.status || timestamp.value<begin.value) {state.clock_status.reason=TSC_REASON_REFERENCE_FAILURE;goto rejected;}
                    __asm__ volatile("pause");
                }while(timestamp.value-begin.value<windows[window]);
            }
            for(unsigned trial=0;trial<TSC_TRIALS;++trial)if(!collect_round(start+phase*TSC_TRIALS+trial))goto rejected;
            if(!collect_exchanges(start+phase*TSC_TRIALS))goto rejected;
        }
        state.report.max_offset_ns=state.report.max_drift_ppm=state.report.max_uncertainty_ns=0;
        int accepted=1,shortage=0,limits_met=1;
        for(size_t cpu=0;cpu<state.count;++cpu) {
            struct tsc_group *group=&state.startup[cpu].groups[attempt];group->target_ns=windows[window];
            tsc_analyze_group(&state.startup[0].samples[start],&state.startup[0].samples[start+TSC_TRIALS],
                &state.startup[cpu].samples[start],&state.startup[cpu].samples[start+TSC_TRIALS],1,group);
            struct tsc_offset_pair *offsets=&state.startup[cpu].offsets[attempt];
            if(cpu) {tsc_analyze_offsets(&state.startup[cpu].exchanges[start],&offsets->start);tsc_analyze_offsets(&state.startup[cpu].exchanges[start+TSC_TRIALS],&offsets->end);}
            else *offsets=(struct tsc_offset_pair){.start={.status=TSC_VALID,.retained=TSC_TRIALS},.end={.status=TSC_VALID,.retained=TSC_TRIALS}};
            uint32_t status=group->status;
            if(group->retained<TSC_MIN_RETAINED)shortage=1;
            if(group->max_drift_ppm>500 || group->max_uncertainty_ns>50000 || group->status==TSC_UNAVAILABLE)limits_met=0;
            for(unsigned phase=0;phase<2;++phase) {struct tsc_offset_group *offset=phase?&offsets->end:&offsets->start;
                if(offset->retained<TSC_MIN_RETAINED)shortage=1;
                if(offset->max_offset_ns>10000 || offset->max_uncertainty_ns>50000)limits_met=0;
                if(offset->max_offset_ns>state.report.max_offset_ns)state.report.max_offset_ns=offset->max_offset_ns;
                if(offset->max_uncertainty_ns>state.report.max_uncertainty_ns)state.report.max_uncertainty_ns=offset->max_uncertainty_ns;
                if(status==TSC_VALID && offset->status!=TSC_VALID)status=offset->status;
            }
            if(group->max_offset_ns>state.report.max_offset_ns)state.report.max_offset_ns=group->max_offset_ns;
            if(group->max_drift_ppm>state.report.max_drift_ppm)state.report.max_drift_ppm=group->max_drift_ppm;
            if(group->max_uncertainty_ns>state.report.max_uncertainty_ns)state.report.max_uncertainty_ns=group->max_uncertainty_ns;
            if(status!=TSC_VALID && accepted) {
                state.report.status=status;state.clock_status.cpu=cpu;
                state.clock_status.reason=status==TSC_OFFSET?TSC_REASON_OFFSET:status==TSC_DRIFT?TSC_REASON_DRIFT:status==TSC_UNAVAILABLE?TSC_REASON_CAPABILITY_MISMATCH:TSC_REASON_UNCERTAINTY;
                accepted=0;
            }
        }
        state.attempts=attempt+1;
        kernel_surface_call refine=kernel_surface_find_version(state.hub,"clock.refine",1,0);
        if(!refined && refine && windows[window]>=100000000 &&
            !call(refine,(uintptr_t)&state.startup[0].groups[attempt],state.sample_epoch,state.sample_hz).status) {
            refined=1;continue; /* Same duration, new epoch, entirely fresh proof. */
        }
        if(accepted)break;
        if(shortage && limits_met && replacements<TSC_REPLACEMENTS) {++replacements;continue;}
        replacements=0;
        if(++window==TSC_WINDOWS)goto rejected;
    }
    state.report.status=TSC_VALID;state.clock_status.reason=TSC_REASON_NONE;state.validating=0;
    kernel_surface_call enable=kernel_surface_find_version(state.hub,"clock.smp_tsc",1,0);
    if(state.count>1 && (!enable || call(enable,state.count,state.heap,(uintptr_t)&state.report).status)) {state.report.status=TSC_UNAVAILABLE;state.clock_status.reason=enable?TSC_REASON_ACTIVATION:TSC_REASON_MISSING_METHOD;return module_result(0,0);}
    if(state.count>1)state.clock_status.source=CLOCK_TSC;
    return module_result(1,1);
rejected:
    state.validating=0;return module_result(0,0);
}
struct kernel_surface_result smp_surface_clock_info(MODULE_ARGS) {
    MODULE_UNUSED();if(!a || !state.prepared || call(state.control,CPU_CURRENT,0,0).value)return module_result(0,0);
    uint64_t flags=call(state.control,CPU_ENTER,0,0).value;
    int ok=!__atomic_load_n(&state.validating,__ATOMIC_ACQUIRE);
    if(ok)*(struct tsc_report *)a=state.report;
    call(state.control,CPU_LEAVE,flags,0);return module_result(ok,ok);
}

struct kernel_surface_result smp_surface_clock_status(MODULE_ARGS) {
    MODULE_UNUSED();if(!a || !state.prepared || call(state.control,CPU_CURRENT,0,0).value)return module_result(0,0);
    uint64_t flags=call(state.control,CPU_ENTER,0,0).value;int ok=!__atomic_load_n(&state.validating,__ATOMIC_ACQUIRE);
    if(ok) {*(struct tsc_diagnostics *)a=state.clock_status;((struct tsc_diagnostics *)a)->samples=state.report.samples;}
    call(state.control,CPU_LEAVE,flags,0);return module_result(ok,ok);
}

/* Adaptive attempts each retain ten starts and ten ends per CPU. */
struct kernel_surface_result smp_surface_clock_sample(MODULE_ARGS) {
    MODULE_UNUSED();if(!a || !state.prepared || b>=state.count || c>=TSC_OBSERVATIONS || call(state.control,CPU_CURRENT,0,0).value)return module_result(0,0);
    uint64_t flags=call(state.control,CPU_ENTER,0,0).value;
    uint64_t count=state.observations;
    int ok=!__atomic_load_n(&state.validating,__ATOMIC_ACQUIRE) && c*state.count+b<count;
    if(ok)*(struct clock_sample *)a=state.startup[b].samples[c];
    call(state.control,CPU_LEAVE,flags,0);return module_result(ok,ok);
}

struct kernel_surface_result smp_surface_clock_group(MODULE_ARGS) {
    MODULE_UNUSED();if(!a || !state.prepared || b>=state.count || c>=TSC_ATTEMPTS || call(state.control,CPU_CURRENT,0,0).value)return module_result(0,0);
    uint64_t flags=call(state.control,CPU_ENTER,0,0).value;
    int ok=!__atomic_load_n(&state.validating,__ATOMIC_ACQUIRE) && c<state.attempts;
    if(ok)*(struct tsc_group *)a=state.startup[b].groups[c];
    call(state.control,CPU_LEAVE,flags,0);return module_result(ok,ok);
}

struct kernel_surface_result smp_surface_clock_cost(MODULE_ARGS) {
    MODULE_UNUSED();if(!a || !state.prepared || b>=state.count || c>=TSC_OBSERVATIONS || call(state.control,CPU_CURRENT,0,0).value)return module_result(0,0);
    uint64_t flags=call(state.control,CPU_ENTER,0,0).value;
    int ok=!__atomic_load_n(&state.validating,__ATOMIC_ACQUIRE) && c*state.count+b<state.observations;
    if(ok)*(struct smp_clock_cost *)a=state.startup[b].costs[c];
    call(state.control,CPU_LEAVE,flags,0);return module_result(ok,ok);
}

struct kernel_surface_result smp_surface_clock_exchange(MODULE_ARGS) {
    MODULE_UNUSED();if(!a || !state.prepared || !b || b>=state.count || c>=TSC_OBSERVATIONS || call(state.control,CPU_CURRENT,0,0).value)return module_result(0,0);
    uint64_t flags=call(state.control,CPU_ENTER,0,0).value;
    int ok=!__atomic_load_n(&state.validating,__ATOMIC_ACQUIRE) && c<state.startup[b].exchange_count;
    if(ok)*(struct tsc_exchange *)a=state.startup[b].exchanges[c];
    call(state.control,CPU_LEAVE,flags,0);return module_result(ok,ok);
}
struct kernel_surface_result smp_surface_clock_offsets(MODULE_ARGS) {
    MODULE_UNUSED();if(!a || !state.prepared || b>=state.count || c>=TSC_ATTEMPTS || call(state.control,CPU_CURRENT,0,0).value)return module_result(0,0);
    uint64_t flags=call(state.control,CPU_ENTER,0,0).value;
    int ok=!__atomic_load_n(&state.validating,__ATOMIC_ACQUIRE) && c<state.attempts;
    if(ok)*(struct tsc_offset_pair *)a=state.startup[b].offsets[c];
    call(state.control,CPU_LEAVE,flags,0);return module_result(ok,ok);
}
