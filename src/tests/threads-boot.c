#include "../kmodules/shared/threads.h"
#include "../kmodules/shared/smp.h"
#include "../kmodules/shared/mmio.h"
#include "../kmodules/shared/clock.h"
#include "../kmodules/shared/tsc-validation.h"
#include "../kmodules/shared/mem-pages.h"
#include "probe.h"
#include "../kmodules/shared/xstate.h"
#include "../kmodules/shared/apic.h"
#include "../kmodules/shared/cpu-platform.h"
#include <stdbool.h>
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static kernel_surface_call create,run,control,now,entry;
static void print_number(const char *s,uint64_t value) {
    while(*s) {__asm__ volatile("outb %0,$0xe9"::"a"(*s++));}
    char digits[24];unsigned n=0;
    do{digits[n++]='0'+value%10;value/=10;}while(value);while(n)__asm__ volatile("outb %0,$0xe9"::"a"(digits[--n]));
    __asm__ volatile("outb %0,$0xe9"::"a"((unsigned char)'\n'));
}
static void print_signed(const char *s,int64_t value) {
    while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));
    if(value<0)__asm__ volatile("outb %0,$0xe9"::"a"((unsigned char)'-'));
    print_number("",value<0?UINT64_C(0)-(uint64_t)value:(uint64_t)value);
}
static void print_exchange(unsigned cpu,unsigned round,const struct tsc_exchange *x) {
    const char *s="TSCX ";while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));
    uint64_t values[]={cpu,round,x->before,x->remote,x->after,x->hz,x->epoch,
        x->offset_ns<0?UINT64_C(0)-(uint64_t)x->offset_ns:(uint64_t)x->offset_ns,x->uncertainty_ns};
    for(unsigned i=0;i<9;++i) {
        if(i==7 && x->offset_ns<0)__asm__ volatile("outb %0,$0xe9"::"a"((unsigned char)'-'));
        char digits[24];unsigned n=0;uint64_t value=values[i];do{digits[n++]='0'+value%10;value/=10;}while(value);
        while(n)__asm__ volatile("outb %0,$0xe9"::"a"(digits[--n]));
        __asm__ volatile("outb %0,$0xe9"::"a"((unsigned char)(i==8?'\n':' ')));
    }
}
static bool failure(unsigned line) {
    const char *s="FAIL threads-boot.c line ";while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));
    char digits[10];unsigned n=0;do{digits[n++]='0'+line%10;line/=10;}while(line);
    while(n) {__asm__ volatile("outb %0,$0xe9"::"a"(digits[--n]));}
    __asm__ volatile("outb %0,$0xe9"::"a"((unsigned char)'\n'));return false;
}
#define CHECK(x) do{if(!(x))return failure(__LINE__);}while(0)
static uint64_t launch(struct probe_thread *p,unsigned cpu,uintptr_t other) {
    struct thread_request request={.abi=1,.cpu=cpu,.process_id=0,.stack_bytes=32768,.entry=(uintptr_t)entry,.major=1,.payload={.arguments={(uintptr_t)p,other}}};
    struct kernel_surface_result r=CALL(create,&request,0,0);return r.status?0:r.value;
}
static bool wait_state(uint64_t id,unsigned state,unsigned cpu) {
    struct kernel_surface_result begin=CALL(now,0,0,0);if(begin.status)return false;
    for(;;) {struct thread_info info;
        if(CALL(control,THREAD_INFO,id,&info).status)return false;
        if(info.state==state)return true;
        if(!cpu && CALL(run,1,0,0).status)return false;
        struct kernel_surface_result time=CALL(now,0,0,0);if(time.status || time.value-begin.value>2000000000)return false;__asm__ volatile("pause");
    }
}
bool kernel_threads_test(const struct kernel_surfaces *hub) {
    create=kernel_surface_find(hub,"threads.create");run=kernel_surface_find(hub,"threads.run");control=kernel_surface_find(hub,"threads.control");now=kernel_surface_find(hub,"clock.now_ns");entry=kernel_surface_find(hub,"probe.thread");
    kernel_surface_call smp=kernel_surface_find(hub,"smp.info"),pages_info=kernel_surface_find(hub,"mem.pages_info");
    CHECK(create && run && control && now && entry && smp && pages_info);
    struct apic_info apic_info;CHECK(!CALL(kernel_surface_find(hub,"apic.info"),&apic_info,0,0).status);print_number("MEASURE APIC mode: ",apic_info.mode);
    struct cpu_features cpu_features;struct xstate_info xstate;
    CHECK(!CALL(kernel_surface_find(hub,"cpu.features"),&cpu_features,0,0).status && !CALL(kernel_surface_find(hub,"xstate.info"),&xstate,0,0).status);
    if(!(xstate.mask&4)) {const char *note="NOTE xstate: AVX guest preservation untested (AVX unavailable)\n";while(*note)__asm__ volatile("outb %0,$0xe9"::"a"(*note++));}
    print_number("MEASURE CPU physical address bits: ",cpu_features.physical_bits);print_number("MEASURE xstate mask: ",xstate.mask);print_number("MEASURE xstate bytes: ",xstate.bytes);print_number("MEASURE xstate mode: ",xstate.mode);
    struct smp_info topology;CHECK(!CALL(smp,&topology,0,0).status && topology.online==topology.count);
    struct mem_pages_info before,after;CHECK(!CALL(pages_info,&before,0,0).status);
    CHECK(CALL(control,THREAD_YIELD,0,0).status && CALL(control,THREAD_BLOCK,0,0).status);
    struct thread_request invalid={.abi=1,.cpu=topology.count,.stack_bytes=32768,.entry=(uintptr_t)entry,.major=1};CHECK(CALL(create,&invalid,0,0).status);
    invalid.cpu=0;invalid.entry=123;CHECK(CALL(create,&invalid,0,0).status);
    __asm__ volatile("sti":::"memory");
    kernel_surface_call cpu_control=kernel_surface_find(hub,"cpu.control"),schedule=kernel_surface_find(hub,"scheduler.submit"),execute=kernel_surface_find(hub,"scheduler.run"),reap=kernel_surface_find(hub,"scheduler.result");
    CHECK(!CALL(cpu_control,CPU_METRICS_RESET,0,0).status);
    struct cpu_latency_policy policy;CHECK(!CALL(cpu_control,CPU_LATENCY,&policy,0).status && policy.hardware_clock && policy.wait_budget_ns==5000000000 && policy.critical_notice_ns==2000000);
    if(topology.count>1) {
        const struct kernel_hub_table *regions=kernel_hub_find_table(hub,MMIO_TABLE_NAME),*records=kernel_hub_find_table(hub,"smp.cpus");CHECK(regions && records);
        const struct mmio_table *mmio=(const void *)regions->address;uintptr_t counter=0;
        for(size_t i=0;i<mmio->count;++i)if(mmio->entries[i].kind==MMIO_HPET)counter=mmio->entries[i].virtual_address+0xf0;
        struct clock_info clock;CHECK(counter && !CALL(kernel_surface_find(hub,"clock.info"),&clock,0,0).status);
        struct probe_thread holder={.mode=9,.address=counter,.iterations=UINT64_C(200000000000000)/clock.hpet_period_fs};
        uint64_t id=launch(&holder,1,0);CHECK(id);__atomic_store_n(&holder.peer,1,__ATOMIC_RELEASE);
        while(!__atomic_load_n(&holder.phase,__ATOMIC_ACQUIRE))__asm__ volatile("pause");
        struct cpu_record *cpus=(void *)records->address;uint64_t before=__atomic_load_n(&cpus[0].timer_interrupts,__ATOMIC_ACQUIRE);
        uint64_t time=CALL(now,0,0,0).value;CHECK(!CALL(kernel_surface_find(hub,"timer.arm"),time+1000000,0,0).status);
        uint64_t flags=CALL(cpu_control,CPU_ENTER,0,0).value;uint64_t after=__atomic_load_n(&cpus[0].timer_interrupts,__ATOMIC_ACQUIRE);CALL(cpu_control,CPU_LEAVE,flags,0);
        struct cpu_metrics metric;CHECK(!CALL(cpu_control,CPU_METRICS,&metric,0).status);print_number("MEASURE controlled wait timer IRQs: ",after-before);print_number("MEASURE controlled wait windows: ",metric.irq_enabled_waits);CHECK(after>before && metric.irq_enabled_waits && metric.max_wait_ns);
        CHECK(wait_state(id,THREAD_EXITED,1) && !CALL(control,THREAD_REAP,id,0).status);
        print_number("MEASURE IRQ-enabled lock wait windows: ",metric.irq_enabled_waits);print_number("MEASURE intentional test lock hold ns: ",holder.clock);
        CHECK(!CALL(cpu_control,CPU_METRICS_RESET,0,0).status);
    }
    uint64_t guarded=CALL(cpu_control,CPU_ENTER,0,0).value;CHECK(CALL(execute,1,0,0).status);CALL(cpu_control,CPU_LEAVE,guarded,0);
    struct scheduler_request delayed={.abi=SCHEDULER_REQUEST_ABI,.kind=SCHEDULER_METHOD,.entry_vector=(uintptr_t)kernel_surface_find(hub,"probe.delay"),.entry_major=1,.payload={.arguments={20000000}}};
    struct kernel_surface_result job=CALL(schedule,&delayed,0,0);CHECK(!job.status && !CALL(execute,1,0,0).status);
    struct cpu_metrics measurement;CHECK(!CALL(cpu_control,CPU_METRICS,&measurement,0).status && measurement.acquisitions && measurement.max_hold_ticks);
    const char *label="MEASURE callback max lock hold ticks: ";while(*label)__asm__ volatile("outb %0,$0xe9"::"a"(*label++));
    char measured[24];unsigned used=0;uint64_t number=measurement.max_hold_ticks;do{measured[used++]='0'+number%10;number/=10;}while(number);while(used)__asm__ volatile("outb %0,$0xe9"::"a"(measured[--used]));__asm__ volatile("outb %0,$0xe9"::"a"((unsigned char)'\n'));
    struct scheduler_completion finished;CHECK(!CALL(reap,job.value,&finished,1).status && !finished.result.status);
    __asm__ volatile("sti":::"memory");
    struct probe_thread busy={.mode=0},peer={.mode=1};uint64_t one=launch(&busy,0,0),two=launch(&peer,0,(uintptr_t)&busy);CHECK(one && two);
    CHECK(CALL(control,THREAD_REAP,one,0).status);
    kernel_surface_call submit=kernel_surface_find(hub,"scheduler.submit"),result=kernel_surface_find(hub,"scheduler.result"),step=kernel_surface_find(hub,"event.step");
    struct scheduler_request callback={.abi=SCHEDULER_REQUEST_ABI,.kind=SCHEDULER_METHOD,.entry_vector=(uintptr_t)kernel_surface_find(hub,"probe.add"),.entry_major=1,.payload={.arguments={20,22}}};
    struct scheduler_completion completion;struct kernel_surface_result queued=CALL(submit,&callback,0,0);CHECK(!queued.status);
    for(unsigned tries=0;tries<64;++tries) {CHECK(!CALL(step,1,0,0).status);CHECK(!CALL(result,queued.value,&completion,0).status);if(completion.state==SCHEDULER_COMPLETE)break;}
    CHECK(!CALL(result,queued.value,&completion,1).status && completion.state==SCHEDULER_COMPLETE && completion.result.value==42 && !busy.done);

    CHECK(wait_state(one,THREAD_EXITED,0) && wait_state(two,THREAD_EXITED,0));
    struct thread_info info;CHECK(!CALL(control,THREAD_INFO,one,&info).status && info.switches>=2 && !info.result.status && info.result.value==77);
    CHECK(busy.done && peer.done && !busy.errors && !peer.errors);
    CHECK(!CALL(control,THREAD_REAP,one,0).status && !CALL(control,THREAD_REAP,two,0).status && CALL(control,THREAD_REAP,one,0).status);
    for(unsigned cpu=1;cpu<topology.count;++cpu) {
        struct probe_thread spinning={.mode=0},partner={.mode=1};one=launch(&spinning,cpu,0);two=launch(&partner,cpu,(uintptr_t)&spinning);
        CHECK(one && two && wait_state(one,THREAD_EXITED,cpu) && wait_state(two,THREAD_EXITED,cpu));
        CHECK(!CALL(control,THREAD_INFO,one,&info).status && info.switches>=2 && !spinning.errors && !partner.errors);
        CHECK(!CALL(control,THREAD_REAP,one,0).status && !CALL(control,THREAD_REAP,two,0).status);
    }
    struct probe_thread blocked={.mode=2};one=launch(&blocked,0,0);CHECK(one && wait_state(one,THREAD_BLOCKED,0));
    CHECK(!blocked.done && CALL(control,THREAD_REAP,one,0).status && !CALL(control,THREAD_WAKE,one,0).status);
    CHECK(wait_state(one,THREAD_EXITED,0) && blocked.done && !blocked.errors && !CALL(control,THREAD_REAP,one,0).status);
    struct probe_thread critical={.mode=5};one=launch(&critical,0,0);CHECK(one && wait_state(one,THREAD_EXITED,0) && !critical.errors && !CALL(control,THREAD_REAP,one,0).status);
    for(unsigned cpu=0;cpu<topology.count;++cpu) {
        struct probe_thread sleeper={.mode=7};one=launch(&sleeper,cpu,0);CHECK(one && wait_state(one,THREAD_EXITED,cpu) && !sleeper.errors && !CALL(control,THREAD_REAP,one,0).status);
    }
    struct probe_thread credited={.mode=8};one=launch(&credited,0,0);
    CHECK(one && !CALL(control,THREAD_WAKE,one,0).status && wait_state(one,THREAD_EXITED,0) && !credited.errors && !CALL(control,THREAD_REAP,one,0).status);
    /* Parallel pinned workers exercise shared allocation/zeroing/release and clock. */
    struct probe_thread workers[64];uint64_t ids[64];CHECK(topology.count<=64);
    for(unsigned i=0;i<topology.count;++i) {workers[i]=(struct probe_thread){.mode=3};ids[i]=launch(&workers[i],i,0);CHECK(ids[i]);}
    for(unsigned i=0;i<topology.count;++i) {CHECK(wait_state(ids[i],THREAD_EXITED,i));CHECK(workers[i].done && !workers[i].errors && workers[i].cpu==i && workers[i].clock);CHECK(!CALL(control,THREAD_REAP,ids[i],0).status);}
    if(topology.count>1) {
        kernel_surface_call alloc=kernel_surface_find(hub,"mem.pages_alloc"),free=kernel_surface_find(hub,"mem.pages_free"),map=kernel_surface_find(hub,"memory.map_owned"),unmap=kernel_surface_find(hub,"memory.unmap_owned");
        struct mem_pages first,second;CHECK(!CALL(alloc,1,90,&first).status && !CALL(alloc,1,90,&second).status);
        *(uint64_t *)first.address=0x1111;*(uint64_t *)second.address=0x2222;uintptr_t alias=MEM_STACK_BASE-8192;
        CHECK(!CALL(map,alias,first.physical,90).status);struct probe_thread reader={.mode=4,.address=alias};one=launch(&reader,1,0);CHECK(one && wait_state(one,THREAD_BLOCKED,1));
        CHECK(!CALL(unmap,alias,first.physical,90).status && !CALL(map,alias,second.physical,90).status);
        CHECK(!CALL(control,THREAD_WAKE,one,0).status && wait_state(one,THREAD_EXITED,1) && !reader.errors);
        CHECK(!CALL(control,THREAD_REAP,one,0).status && !CALL(unmap,alias,second.physical,90).status && !CALL(free,first.id,90,0).status && !CALL(free,second.id,90,0).status);
    }
    if(topology.count>1) {
        struct probe_thread publisher={.mode=6};one=launch(&publisher,1,0);CHECK(one);__atomic_store_n(&publisher.peer,1,__ATOMIC_RELEASE);
        CHECK(!CALL(step,4,0,0).status && wait_state(one,THREAD_EXITED,1) && !publisher.errors && publisher.address);
        CHECK(!CALL(result,publisher.address,&completion,0).status);
        if(completion.state!=SCHEDULER_COMPLETE)CHECK(!CALL(step,4,0,0).status);
        CHECK(!CALL(result,publisher.address,&completion,1).status && completion.state==SCHEDULER_COMPLETE && completion.result.value==9);
        CHECK(!CALL(control,THREAD_REAP,one,0).status);
    }
    kernel_surface_call fp_entry=kernel_surface_find(hub,"probe.xstate");CHECK(fp_entry);
    for(unsigned cpu=0;cpu<topology.count;++cpu) {
        struct probe_thread fp[2]={{.mode=1},{.mode=2}};uint64_t ids[2];
        for(unsigned i=0;i<2;++i) {struct thread_request request={.abi=1,.cpu=cpu,.stack_bytes=32768,.entry=(uintptr_t)fp_entry,.major=1,.payload={.arguments={(uintptr_t)&fp[i]}}};
            struct kernel_surface_result created=CALL(create,&request,0,0);CHECK(!created.status);ids[i]=created.value;}
        for(unsigned i=0;i<2;++i) {CHECK(wait_state(ids[i],THREAD_EXITED,cpu));struct thread_info info;
            CHECK(!CALL(control,THREAD_INFO,ids[i],&info).status);print_number("MEASURE xstate errors: ",fp[i].errors);print_number("MEASURE xstate iterations: ",fp[i].iterations);print_number("MEASURE xstate switches: ",info.switches);
            CHECK(fp[i].done && !fp[i].errors && fp[i].iterations>3 && !CALL(control,THREAD_INFO,ids[i],&info).status && info.switches>4);
            CHECK(!CALL(control,THREAD_REAP,ids[i],0).status);}
    }
    const char *fp_pass="PASS xstate: fresh contexts, x87/SSE/available AVX, MXCSR/control word, yield and timer preemption on every CPU\n";
    while(*fp_pass)__asm__ volatile("outb %0,$0xe9"::"a"(*fp_pass++));
    CHECK(!CALL(pages_info,&after,0,0).status && before.allocations==after.allocations && before.physical_pages==after.physical_pages && before.stacks==after.stacks);
    struct thread_timing timing;CHECK(!CALL(control,THREAD_TIMING,0,&timing).status && timing.samples && timing.max_wait_ns);
    print_number("MEASURE thread max runnable wait ns: ",timing.max_wait_ns);
    uint64_t contention=0;
    for(unsigned cpu=0;cpu<topology.count;++cpu) {struct cpu_metrics metrics;CHECK(!CALL(cpu_control,CPU_METRICS,&metrics,cpu).status);contention+=metrics.contended;
        print_number("MEASURE CPU index: ",cpu);print_number("MEASURE max lock hold ticks: ",metrics.max_hold_ticks);print_number("MEASURE max lock wait ticks: ",metrics.max_wait_ticks);print_number("MEASURE longest lock domain: ",metrics.max_hold_domain);print_number("MEASURE max managed IRQ-off ticks: ",metrics.max_irq_off_ticks);print_number("MEASURE max managed IRQ-off ns: ",metrics.max_irq_off_ns);print_number("MEASURE max lock hold ns: ",metrics.max_hold_ns);print_number("MEASURE max lock wait ns: ",metrics.max_wait_ns);print_number("MEASURE critical budget notices: ",metrics.critical_overruns);
        if(metrics.max_hold_method) {CHECK(metrics.max_hold_method<=hub->count);const char *method=kernel_surface_entries(hub)[metrics.max_hold_method-1].name;const char *label="MEASURE longest lock method: ";while(*label)__asm__ volatile("outb %0,$0xe9"::"a"(*label++));while(*method)__asm__ volatile("outb %0,$0xe9"::"a"(*method++));__asm__ volatile("outb %0,$0xe9"::"a"((unsigned char)'\n'));}
    }
    print_number("MEASURE total contended acquisitions: ",contention);
    kernel_surface_call clock_info=kernel_surface_find(hub,"smp.clock_info");struct tsc_report validation;CHECK(!CALL(clock_info,&validation,0,0).status);print_number("MEASURE TSC validation status: ",validation.status);print_number("MEASURE TSC maximum offset ns: ",validation.max_offset_ns);print_number("MEASURE TSC maximum drift ppm: ",validation.max_drift_ppm);print_number("MEASURE TSC maximum uncertainty ns: ",validation.max_uncertainty_ns);
    struct cpu_tsc_info capabilities;struct tsc_diagnostics diagnostic,current;
    CHECK(!CALL(kernel_surface_find(hub,"cpu.tsc"),&capabilities,0,0).status);
    CHECK(!CALL(kernel_surface_find(hub,"smp.clock_status"),&diagnostic,0,0).status);
    CHECK(!CALL(kernel_surface_find(hub,"clock.tsc_status"),&current,0,0).status);
    print_number("MEASURE TSC capability flags: ",capabilities.flags);
    print_number("MEASURE TSC extended power EDX: ",capabilities.extended_power_edx);
    print_number("MEASURE TSC frequency source: ",capabilities.frequency_source);
    print_number("MEASURE TSC enumerated Hz: ",capabilities.hz);
    print_number("MEASURE TSC calibrated Hz: ",current.hz);
    print_number("MEASURE clock source: ",current.source);
    print_number("MEASURE TSC current reason: ",current.reason);
    print_number("MEASURE TSC rejection reason: ",diagnostic.reason);
    print_number("MEASURE TSC rejection CPU: ",diagnostic.cpu);
    print_number("MEASURE TSC validation samples: ",diagnostic.samples);
    kernel_surface_call snapshot=kernel_surface_find(hub,"smp.clock_sample");CHECK(snapshot);
    for(unsigned round=0;round<TSC_OBSERVATIONS;++round)for(unsigned cpu=0;cpu<topology.count;++cpu) {
        struct clock_sample observation;
        if(CALL(snapshot,&observation,cpu,round).status)continue;
        print_number("MEASURE TSC sample CPU: ",cpu);
        print_number("MEASURE TSC sample round: ",round);
        print_number("MEASURE TSC sample ticks ns: ",observation.tsc_ns);
        print_number("MEASURE TSC sample HPET ns: ",observation.hpet_ns);
        print_number("MEASURE TSC sample uncertainty ns: ",observation.uncertainty_ns);
        struct smp_clock_cost cost;CHECK(!CALL(kernel_surface_find(hub,"smp.clock_cost"),&cost,cpu,round).status);
        print_number("MEASURE TSC sample empty ns: ",cost.empty_ns);print_number("MEASURE TSC sample bracket ns: ",cost.bracket_ns);
        print_number("MEASURE TSC sample call ns: ",cost.call_ns);print_number("MEASURE TSC sample relay ns: ",cost.relay_ns);
        print_number("MEASURE TSC sample Hz: ",cost.hz);print_number("MEASURE TSC sample epoch: ",cost.epoch);
    }
    kernel_surface_call grouped=kernel_surface_find(hub,"smp.clock_group");CHECK(grouped);
    for(unsigned attempt=0;attempt<TSC_ATTEMPTS;++attempt)for(unsigned cpu=0;cpu<topology.count;++cpu) {
        struct tsc_group group;if(CALL(grouped,&group,cpu,attempt).status)continue;
        print_number("MEASURE TSC group CPU: ",cpu);print_number("MEASURE TSC group attempt: ",attempt);
        print_number("MEASURE TSC group status: ",group.status);print_number("MEASURE TSC group retained: ",group.retained);
        print_number("MEASURE TSC group bracket mask: ",group.bracket_mask);print_number("MEASURE TSC group outlier mask: ",group.outlier_mask);
        print_number("MEASURE TSC group target ns: ",group.target_ns);print_number("MEASURE TSC group interval median ns: ",group.interval_median_ns);
        print_number("MEASURE TSC group uncertainty median ns: ",group.uncertainty_median_ns);
        print_signed("MEASURE TSC group drift median ppb: ",group.drift_median_ppb);print_signed("MEASURE TSC group offset median ns: ",group.offset_median_ns);
        print_number("MEASURE TSC group drift MAD ppb: ",group.drift_mad_ppb);print_number("MEASURE TSC group offset MAD ns: ",group.offset_mad_ns);
        print_number("MEASURE TSC group offset bound ns: ",group.max_offset_ns);print_number("MEASURE TSC group drift bound ppm: ",group.max_drift_ppm);
        print_number("MEASURE TSC group uncertainty bound ns: ",group.max_uncertainty_ns);
        CHECK(group.retained<=TSC_TRIALS);
        CHECK(group.status!=TSC_VALID || (group.retained>=TSC_MIN_RETAINED && group.max_offset_ns<=10000 && group.max_drift_ppm<=500 && group.max_uncertainty_ns<=50000));
    }
    kernel_surface_call exchange=kernel_surface_find(hub,"smp.clock_exchange"),offset_info=kernel_surface_find(hub,"smp.clock_offsets");CHECK(exchange && offset_info);
    for(unsigned round=0;round<TSC_OBSERVATIONS;++round)for(unsigned cpu=1;cpu<topology.count;++cpu) {
        struct tsc_exchange raw;if(!CALL(exchange,&raw,cpu,round).status)print_exchange(cpu,round,&raw);
    }
    for(unsigned attempt=0;attempt<TSC_ATTEMPTS;++attempt)for(unsigned cpu=0;cpu<topology.count;++cpu) {
        struct tsc_offset_pair pair;if(CALL(offset_info,&pair,cpu,attempt).status)continue;
        for(unsigned phase=0;phase<2;++phase) {
            struct tsc_offset_group *g=phase?&pair.end:&pair.start;
            print_number("MEASURE TSC offset CPU: ",cpu);print_number("MEASURE TSC offset attempt: ",attempt);print_number("MEASURE TSC offset phase: ",phase);
            print_number("MEASURE TSC offset status: ",g->status);print_number("MEASURE TSC offset retained: ",g->retained);
            print_number("MEASURE TSC offset wide mask: ",g->wide_mask);print_number("MEASURE TSC offset outlier mask: ",g->outlier_mask);
            print_signed("MEASURE TSC offset median ns: ",g->median_ns);print_number("MEASURE TSC offset MAD ns: ",g->mad_ns);
            print_number("MEASURE TSC offset bound ns: ",g->max_offset_ns);print_number("MEASURE TSC offset uncertainty ns: ",g->max_uncertainty_ns);
            CHECK(g->status!=TSC_VALID || (g->retained>=TSC_MIN_RETAINED && g->max_offset_ns<=10000));
        }
    }
    CHECK(validation.status!=TSC_VALID || (!diagnostic.reason && diagnostic.samples>=topology.count*TSC_TRIALS*2));
    if(validation.status==TSC_VALID) {
        CHECK(validation.samples%(topology.count*TSC_TRIALS*2)==0);
        unsigned last_attempt=validation.samples/(topology.count*TSC_TRIALS*2)-1;
        CHECK(last_attempt<TSC_ATTEMPTS);
        for(unsigned cpu=0;cpu<topology.count;++cpu) {
            struct tsc_group frequency;struct tsc_offset_pair offsets;
            CHECK(!CALL(grouped,&frequency,cpu,last_attempt).status && frequency.status==TSC_VALID);
            CHECK(!CALL(offset_info,&offsets,cpu,last_attempt).status && offsets.start.status==TSC_VALID && offsets.end.status==TSC_VALID);
        }
    }

    const char *s="PASS threads/SMP: timer preemption, block/wake, critical sections, pinned workers, TLB invalidation and teardown; CPUs online: ";while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));
    char digits[20];unsigned n=0;uint64_t count=topology.online;do{digits[n++]='0'+count%10;count/=10;}while(count);while(n) {__asm__ volatile("outb %0,$0xe9"::"a"(digits[--n]));}
    __asm__ volatile("outb %0,$0xe9"::"a"((unsigned char)'\n'));
    return true;
}
