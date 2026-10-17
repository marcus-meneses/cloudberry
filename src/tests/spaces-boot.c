#include "../kernel/modules.h"
#include "../kernel/cpu.h"
#include "../kmodules/shared/mem-spaces.h"
#include "../kmodules/shared/mem-pages.h"
#include "../kmodules/shared/threads.h"
#include "probe.h"
#include "space-fault.h"
#include "../kmodules/shared/clock.h"
#include "../kmodules/shared/mmio.h"
#include "../kmodules/shared/cpu-platform.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void print(const char *p){while(*p)__asm__ volatile("outb %0,$0xe9"::"a"(*p++));}
static void number(uint64_t value){char d[24];unsigned n=0;do{d[n++]='0'+value%10;value/=10;}while(value);while(n)__asm__ volatile("outb %0,$0xe9"::"a"(d[--n]));}
static void samples(const char *name,const uint64_t *values,unsigned n){print("MEASURE spaces ");print(name);for(unsigned i=0;i<n;++i){print(" ");number(values[i]);}print("\n");}
static void attribution(const struct kernel_surfaces *hub,const struct cpu_metrics *metrics,unsigned scope,unsigned trial){
    const struct cpu_profile_peak *peaks[]={&metrics->wait_peak,&metrics->hold_peak,&metrics->irq_peak};
    const char *kinds[]={"wait","hold","irq"};
    for(unsigned i=0;i<3;++i){
        const struct cpu_profile_peak *p=peaks[i];const char *method="kernel";
        if(p->method && p->method<=kernel_surface_count(hub))method=kernel_surface_entries(hub)[p->method-1].name;
        print("ATTR spaces ");print(kinds[i]);
        print(" ");number(scope);print(" ");number(trial);
        const uint64_t *fields=(const void *)p;for(unsigned k=0;k<sizeof(*p)/8;++k){print(" ");number(fields[k]);}
        print(" ");print(method);print("\n");
    }
    uint64_t quality[]={scope,trial,metrics->tsc_ns_samples,metrics->hpet_ns_samples,metrics->unavailable_ns_samples};samples("profile_quality",quality,5);
}
static bool failure(unsigned line){print("FAIL spaces-boot line ");char d[12];unsigned n=0;do{d[n++]='0'+line%10;line/=10;}while(line);while(n)__asm__ volatile("outb %0,$0xe9"::"a"(d[--n]));print("\n");return false;}
#define CHECK(x) do{if(!(x))return failure(__LINE__);}while(0)
static bool profile_contract_test(const struct kernel_surfaces *hub){
    CHECK(kernel_cpu_control(CPU_PROFILE_CLOCK,1000000000,0,0,0,0).status);
    uint64_t held=kernel_cpu_enter_domain(1,0);CHECK(!kernel_cpu_control(CPU_PROFILE_CLOCK,1000000000,0,0,0,0).status);kernel_cpu_leave_domain(1,held);
    CHECK(!kernel_cpu_control(CPU_METRICS_RESET,0,0,0,0,0).status);
    uint64_t flags;__asm__ volatile("pushfq;pop %0;cli":"=r"(flags)::"memory");
    held=kernel_cpu_enter();kernel_cpu_leave(held);
    struct cpu_metrics metrics;CHECK(!kernel_cpu_control(CPU_METRICS,(uintptr_t)&metrics,0,0,0,0).status);
    CHECK(metrics.tsc_ns_samples==2 && !metrics.hpet_ns_samples && metrics.hold_peak.source==CPU_PROFILE_TSC);
    const struct kernel_hub_table *cpus=kernel_hub_find_table(hub,"smp.cpus");CHECK(cpus);
    struct cpu_record *records=(void *)cpus->address;records[0].profile_fault_epoch=metrics.hold_peak.clock_epoch;
    held=kernel_cpu_enter();kernel_cpu_leave(held);
    CHECK(!kernel_cpu_control(CPU_METRICS,(uintptr_t)&metrics,0,0,0,0).status && metrics.hpet_ns_samples==2);
    /* A rate/epoch transition invalidates in-flight TSC conversion. */
    held=kernel_cpu_enter_domain(1,0);CHECK(!kernel_cpu_control(CPU_PROFILE_CLOCK,1000000000,0,0,0,0).status);kernel_cpu_leave_domain(1,held);
    held=kernel_cpu_enter();uint64_t clock_lock=kernel_cpu_enter_domain(1,0);CHECK(!kernel_cpu_control(CPU_PROFILE_CLOCK,0,0,0,0,0).status);kernel_cpu_leave_domain(1,clock_lock);kernel_cpu_leave(held);
    CHECK(!kernel_cpu_control(CPU_METRICS,(uintptr_t)&metrics,0,0,0,0).status && metrics.unavailable_ns_samples>=2);
    /* This is a synthetic rate for mechanism testing, never calibration data.
     * Restore actual clock policy while holding its serialization domain. */
    clock_lock=kernel_cpu_enter_domain(1,0);struct clock_info current;
    CHECK(!CALL(kernel_surface_find(hub,"clock.info"),&current,0,0).status);
    CHECK(!kernel_cpu_control(CPU_PROFILE_CLOCK,current.source==CLOCK_TSC?current.tsc_hz:0,0,0,0,0).status);kernel_cpu_leave_domain(1,clock_lock);
    __asm__ volatile("push %0;popfq"::"r"(flags):"memory","cc");
    print("PASS profiling mechanism: serialized rate publication, TSC-only samples, local HPET fallback and mixed-epoch rejection; real clock policy restored\n");return true;
}
static bool cycle(const struct kernel_surfaces *hub,struct boot_pages *pages,unsigned trial){
    kernel_surface_call create=kernel_surface_find(hub,"mem.space_create"),map=kernel_surface_find(hub,"mem.space_map"),unmap=kernel_surface_find(hub,"mem.space_unmap"),protect=kernel_surface_find(hub,"mem.space_protect"),query=kernel_surface_find(hub,"mem.space_query"),info=kernel_surface_find(hub,"mem.space_info"),destroy=kernel_surface_find(hub,"mem.space_destroy");
    kernel_surface_call alloc=kernel_surface_find(hub,"mem.pages_alloc"),free=kernel_surface_find(hub,"mem.pages_free"),thread=kernel_surface_find(hub,"threads.create"),control=kernel_surface_find(hub,"threads.control"),dispatch=kernel_surface_find(hub,"threads.run"),clock=kernel_surface_find(hub,"clock.now_ns"),entry=kernel_surface_find(hub,"probe.space");
    /* These rendezvous observe shared probe state, not a scheduler deadline or
     * a wakeable BSP thread. event.step may stop the local timer and HLT once
     * only an AP is running; an AP blocking/exiting does not wake the BSP.
     * Nonblocking dispatch keeps the condition and absolute timeout observable.
     * Production event-loop idle policy and the measured operations are unchanged. */
    CHECK(create && map && unmap && protect && query && info && destroy && entry && dispatch && control && clock);
    CHECK(!kernel_cpu_control(CPU_METRICS_RESET,0,0,0,0,0).status);
    struct mem_space_info a,b,idle,snapshot;struct mem_pages first,second,replacement,code_a,code_b,hole_a,hole_b,canary;
    CHECK(!CALL(create,101,&a,0).status && !CALL(create,102,&b,0).status && a.id!=b.id);
    CHECK(!CALL(alloc,1,101,&first).status && !CALL(alloc,1,102,&second).status && !CALL(alloc,1,101,&replacement).status);
    *(uint64_t *)first.address=0x1111;*(uint64_t *)second.address=0x2222;*(uint64_t *)replacement.address=0x3333;
    CHECK(!CALL(alloc,1,101,&code_a).status && !CALL(alloc,1,102,&code_b).status && !CALL(alloc,1,101,&hole_a).status && !CALL(alloc,1,102,&hole_b).status);
    const unsigned char code[]={0xb8,42,0,0,0,0xc3};for(unsigned i=0;i<sizeof(code);++i){((unsigned char *)code_a.address)[i]=code[i];((unsigned char *)code_b.address)[i]=code[i];}
    *(uint64_t *)hole_a.address=0x7777;*(uint64_t *)hole_b.address=0x7777;
    const uint64_t va=0x400000;
    CHECK(!map(a.id,101,va+4096,code_a.physical,0,0).status && !map(b.id,102,va+4096,code_b.physical,0,0).status && !map(a.id,101,va+8192,hole_a.physical,0,0).status && !map(b.id,102,va+8192,hole_b.physical,0,0).status);
    CHECK(!map(a.id,101,va,first.physical,MEM_SPACE_WRITE,0).status && !map(b.id,102,va,second.physical,MEM_SPACE_WRITE,0).status);
    CHECK(!CALL(create,101,&idle,0).status && !map(idle.id,101,va,first.physical,0,0).status);
    CHECK(map(a.id,102,va+4096,second.physical,1,0).status && map(a.id,101,0,first.physical,1,0).status && map(a.id,101,va+4096,first.physical,3,0).status && map(a.id,101,KERNEL_SURFACES_ADDRESS,first.physical,1,0).status);
    struct mem_space_mapping mapping;CHECK(!query(a.id,101,va,(uintptr_t)&mapping,0,0).status && mapping.physical==first.physical && mapping.flags==1);
    CHECK(CALL(free,first.id,101,0).status && CALL(destroy,a.id,102,0).status);
    uint64_t *at=boot_physical_pointer(pages,a.root),*bt=boot_physical_pointer(pages,b.root),*master=boot_physical_pointer(pages,pages->kernel_root);
    for(unsigned i=256;i<512;++i)CHECK(at[i]==master[i] && bt[i]==master[i] && !(at[i]&4));
    const struct kernel_hub_table *cpus=kernel_hub_find_table(hub,"smp.cpus");CHECK(cpus);
    unsigned count=cpus->count>1?3:2;uint64_t ids[3];struct probe_space probes[3]={{.root=a.root,.address=va,.expected=0x1111,.next=0x3333},{.root=b.root,.address=va,.expected=0x2222,.next=0x2222},{.root=a.root,.address=va,.expected=0x1111,.next=0x3333,.hold_preemption=1}};
    for(unsigned i=0;i<count;++i){probes[i].id=i==1?b.id:a.id;probes[i].owner=i==1?102:101;struct thread_request r={.abi=2,.cpu=i==2?1:0,.process_id=i==1?102:101,.address_space=i==1?b.id:a.id,.stack_bytes=32768,.entry=(uintptr_t)entry,.major=1};r.payload.arguments[0]=(uintptr_t)&probes[i];struct kernel_surface_result created=CALL(thread,&r,0,0);CHECK(!created.status);ids[i]=created.value;}
    struct thread_request wrong={.abi=2,.cpu=0,.process_id=102,.address_space=a.id,.stack_bytes=32768,.entry=(uintptr_t)entry,.major=1};CHECK(CALL(thread,&wrong,0,0).status);
    __asm__ volatile("sti":::"memory");CHECK(CALL(kernel_surface_find(hub,"mem.space_switch"),a.id,101,0).status);
    uint64_t deadline=CALL(clock,0,0,0).value+UINT64_C(15000000000);
    for(;;){unsigned ready=0;for(unsigned i=0;i<count;++i)ready+=__atomic_load_n(&probes[i].phase,__ATOMIC_ACQUIRE)==1;if(ready==count)break;CHECK(!CALL(dispatch,1,0,0).status && CALL(clock,0,0,0).value<deadline);}
    CHECK(kernel_cpu_space()==0);uintptr_t cr3;__asm__ volatile("mov %%cr3,%0":"=r"(cr3));CHECK(cr3==pages->kernel_root);
    CHECK(!CALL(info,a.id,101,&snapshot).status && snapshot.leases==count-1 && snapshot.active<=count-1);
    struct cpu_record *cpu=(void *)cpus->address;
    uint64_t remote[SPACE_SAMPLES],inactive[SPACE_SAMPLES],bracket[SPACE_SAMPLES];
    for(unsigned i=0;i<SPACE_SAMPLES;++i){
        uint64_t held=kernel_cpu_enter();CHECK(count!=3 || __atomic_load_n(&cpu[1].address_space,__ATOMIC_ACQUIRE)==a.id);
        uint64_t ack=count==3?__atomic_load_n(&cpu[1].tlb_ack,__ATOMIC_ACQUIRE):0;
        struct kernel_surface_result begin=CALL(clock,0,0,0),end=CALL(clock,0,0,0);CHECK(!begin.status && !end.status && end.value>=begin.value);bracket[i]=end.value-begin.value;
        begin=CALL(clock,0,0,0);CHECK(!protect(idle.id,101,va,i&1,0,0).status);end=CALL(clock,0,0,0);CHECK(!begin.status && !end.status && end.value>=begin.value);inactive[i]=end.value-begin.value;
        begin=CALL(clock,0,0,0);CHECK(!protect(a.id,101,va,(i&1)?0:1,0,0).status);end=CALL(clock,0,0,0);CHECK(!begin.status && !end.status && end.value>=begin.value);remote[i]=end.value-begin.value;
        CHECK(count!=3 || __atomic_load_n(&cpu[1].tlb_ack,__ATOMIC_ACQUIRE)>ack);kernel_cpu_leave(held);
    }
    CHECK(!CALL(destroy,idle.id,101,0).status);
    samples("protect_clock_bracket_ns",bracket,SPACE_SAMPLES);samples("inactive_protect_ns",inactive,SPACE_SAMPLES);samples(count==3?"remote_protect_ns":"no_active_protect_ns",remote,SPACE_SAMPLES);
    CHECK(!query(a.id,101,va,(uintptr_t)&mapping,0,0).status && !mapping.flags && CALL(destroy,a.id,101,0).status);
    CHECK(!protect(b.id,102,va,0,0,0).status && !protect(a.id,101,va+4096,MEM_SPACE_EXEC,0,0).status && !protect(b.id,102,va+4096,MEM_SPACE_EXEC,0,0).status);
    for(unsigned i=0;i<count;++i)__atomic_store_n(&probes[i].command,1,__ATOMIC_RELEASE);
    for(;;){unsigned ready=0;for(unsigned i=0;i<count;++i)ready+=__atomic_load_n(&probes[i].phase,__ATOMIC_ACQUIRE)==2;if(ready==count)break;CHECK(!CALL(dispatch,1,0,0).status && CALL(clock,0,0,0).value<deadline);}
    /* Revoke an already executed page and remove an already read page while
     * CPU1 still uses this root. Post-change guarded accesses must really fault. */
    if(count==3){uint64_t held;for(;;){held=kernel_cpu_enter();if(__atomic_load_n(&cpu[1].address_space,__ATOMIC_ACQUIRE)==a.id)break;kernel_cpu_leave(held);CHECK(!CALL(dispatch,1,0,0).status && CALL(clock,0,0,0).value<deadline);}
        CHECK(!protect(a.id,101,va+4096,0,0,0).status && !CALL(unmap,a.id,101,va+8192).status);kernel_cpu_leave(held);
    }else CHECK(!protect(a.id,101,va+4096,0,0,0).status && !CALL(unmap,a.id,101,va+8192).status);
    CHECK(!protect(b.id,102,va+4096,0,0,0).status && !CALL(unmap,b.id,102,va+8192).status);
    CHECK(!CALL(free,hole_a.id,101,0).status && !CALL(free,hole_b.id,102,0).status && !CALL(alloc,1,999,&canary).status);*(uint64_t *)canary.address=0x8888;
    for(unsigned i=0;i<count;++i)__atomic_store_n(&probes[i].command,2,__ATOMIC_RELEASE);
    for(;;){unsigned blocked=0;for(unsigned i=0;i<count;++i){struct thread_info t;CHECK(!CALL(control,THREAD_INFO,ids[i],&t).status);blocked+=t.state==THREAD_BLOCKED;}if(blocked==count)break;CHECK(!CALL(dispatch,1,0,0).status && CALL(clock,0,0,0).value<deadline);}
    CHECK(!CALL(info,a.id,101,&snapshot).status && !snapshot.active && CALL(destroy,a.id,101,0).status);
    CHECK(!CALL(unmap,a.id,101,va).status && !CALL(free,first.id,101,0).status && !map(a.id,101,va,replacement.physical,0,0).status);
    if(count==3){
        /* Regression: finish remote work while every BSP worker is blocked.
         * There is deliberately no local work/deadline or completion wake IPI.
         * Dispatch must return even if the AP has not finished yet. */
        struct kernel_surface_result ready=CALL(control,THREAD_READY,0,0);CHECK(!ready.status && !ready.value);
        struct kernel_surface_result empty=CALL(dispatch,1,0,0);CHECK(!empty.status && !empty.value);
        CHECK(!CALL(control,THREAD_WAKE,ids[2],0).status);
        for(;;){struct thread_info t;CHECK(!CALL(control,THREAD_INFO,ids[2],&t).status);if(t.state==THREAD_EXITED)break;
            CHECK(!CALL(dispatch,1,0,0).status && CALL(clock,0,0,0).value<deadline);}
    }
    for(unsigned i=0;i<(count==3?2:count);++i)CHECK(!CALL(control,THREAD_WAKE,ids[i],0).status);
    for(;;){unsigned exited=0;for(unsigned i=0;i<count;++i){struct thread_info t;CHECK(!CALL(control,THREAD_INFO,ids[i],&t).status);exited+=t.state==THREAD_EXITED;}if(exited==count)break;CHECK(!CALL(dispatch,1,0,0).status && CALL(clock,0,0,0).value<deadline);}
    CHECK(CALL(destroy,a.id,101,0).status);
    for(unsigned i=0;i<count;++i){struct thread_info t;CHECK(!CALL(control,THREAD_INFO,ids[i],&t).status && !t.result.status && !probes[i].errors && probes[i].phase==4 && t.switches>20);CHECK(!CALL(control,THREAD_REAP,ids[i],0).status);}
    CHECK(!CALL(info,a.id,101,&snapshot).status && !snapshot.active && !snapshot.leases);
    CHECK(!CALL(destroy,a.id,101,0).status && !CALL(destroy,b.id,102,0).status && CALL(info,a.id,101,&snapshot).status);
    CHECK(!CALL(free,second.id,102,0).status && !CALL(free,replacement.id,101,0).status && kernel_cpu_space()==0);
    CHECK(!CALL(free,code_a.id,101,0).status && !CALL(free,code_b.id,102,0).status && !CALL(free,canary.id,999,0).status);
    struct mem_space_info fresh;CHECK(!CALL(create,101,&fresh,0).status && fresh.id>a.id && !CALL(destroy,fresh.id,101,0).status);
    for(unsigned i=0;i<count;++i){uint64_t worker[]={i==2?1:0,probes[i].id,probes[i].owner};samples("worker",worker,3);samples("switch_clock_bracket_ns",probes[i].timings[0],SPACE_SAMPLES);samples("same_root_pair_ns",probes[i].timings[1],SPACE_SAMPLES);samples("root_roundtrip_ns",probes[i].timings[2],SPACE_SAMPLES);samples("yield_roundtrip_ns",probes[i].timings[3],SPACE_SAMPLES);}
    struct cpu_metrics metrics;CHECK(!kernel_cpu_control(CPU_METRICS,(uintptr_t)&metrics,0,0,0,0).status);samples("cycle_max_hold_ns",&metrics.max_hold_ns,1);samples("cycle_max_irq_off_ns",&metrics.max_irq_off_ns,1);
    attribution(hub,&metrics,0,trial);
    /* Completed a cycle; the outer test checks allocation and fault totals. */
    return true;
}

/* Independent reference: these brackets read the already mapped HPET directly,
 * avoiding clock.now_ns and the locks whose overhead is being measured. Keep
 * raw elapsed totals (16 operations), including the HPET bracket and all noise. */
static uint64_t profile_hpet(volatile uint64_t *counter){__asm__ volatile("":::"memory");uint64_t value=*counter;__asm__ volatile("":::"memory");return value;}
static bool instrumentation_profile(const struct kernel_surfaces *hub,uint64_t period){
    const struct kernel_hub_table *ref=kernel_hub_find_table(hub,MMIO_TABLE_NAME);CHECK(ref && ref->bytes>=sizeof(struct mmio_table));
    const struct mmio_table *table=(const void *)ref->address;volatile uint64_t *counter=NULL;
    CHECK(table->magic==MMIO_TABLE_MAGIC && table->major==1 && table->entry_bytes==sizeof(struct mmio_region) && table->count<=(ref->bytes-sizeof(*table))/sizeof(struct mmio_region));
    /* Match clock's first usable mapped HPET, rather than pairing a second
     * counter with the first counter's period on a multi-HPET platform. */
    for(uint64_t i=0;i<table->count;++i)if(table->entries[i].kind==MMIO_HPET && (table->entries[i].flags&MMIO_MAPPED) && table->entries[i].virtual_address && table->entries[i].bytes>=0xf8){
        uint64_t caps=*(volatile uint64_t *)table->entries[i].virtual_address;
        if((caps&(1u<<13)) && (caps>>32)==period){counter=(void *)(table->entries[i].virtual_address+0xf0);break;}
    }
    CHECK(counter && period && period<=100000000);
    kernel_surface_call tick=kernel_surface_find(hub,"cpu.ticks"),clock=kernel_surface_find(hub,"clock.now_ns"),info=kernel_surface_find(hub,"cpu.tsc");
    struct cpu_tsc_info cpu;CHECK(tick && clock && info && !CALL(info,&cpu,0,0).status);
    uint64_t mode[]={cpu.vendor,cpu.flags,16,64};samples("instrumentation",mode,4);
    const char *names[]={"instrument_hpet_bracket_ns","instrument_ticks_16_ns","instrument_cpuid_ticks_16_ns","instrument_clock_16_ns","instrument_lock_16_ns","instrument_nested_lock_16_ns","instrument_irq_lock_16_ns"};
    uint64_t elapsed[7][64];
    for(unsigned sample=0;sample<64;++sample)for(unsigned kind=0;kind<7;++kind){
        uint64_t flags;__asm__ volatile("pushfq;pop %0;cli":"=r"(flags)::"memory");CHECK(flags&512);
        uint64_t held=kind==5?kernel_cpu_enter():0;
        if(kind==6)__asm__ volatile("sti":::"memory");
        uint64_t begin=profile_hpet(counter);
        for(unsigned i=0;i<(kind?16u:0u);++i){
            if(kind==1){CHECK(!CALL(tick,0,0,0).status);}
            else if(kind==2){
                uint32_t a,b,c,d;__asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0),"c"(0):"memory");
                __asm__ volatile("rdtsc":"=a"(a),"=d"(d)::"memory");
                __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0),"c"(0):"memory");
            }else if(kind==3){CHECK(!CALL(clock,0,0,0).status);}
            else {uint64_t lock=kernel_cpu_enter();kernel_cpu_leave(lock);}
        }
        uint64_t end=profile_hpet(counter);if(kind==6)__asm__ volatile("cli":::"memory");
        if(kind==5)kernel_cpu_leave(held);
        __asm__ volatile("push %0;popfq"::"r"(flags):"memory","cc");
        CHECK(end>=begin && end-begin<=UINT64_MAX/period);elapsed[kind][sample]=(end-begin)*period/1000000;
    }
    for(unsigned i=0;i<7;++i)samples(names[i],elapsed[i],64);
    print("PASS instrumentation profiling: independent HPET brackets, ordered ticks, conservative CPUID control and recursive/IRQ-enabled locks\n");return true;
}
static uint64_t owned_count(struct boot_pages *pages){uint64_t n=0;for(struct boot_reservation_chunk *c=pages->reservations.first;c;c=c->next)for(size_t i=0;i<c->count;++i){unsigned kind=c->entries[i].kind;if(kind==BOOT_RES_SPACE_TABLE || kind==BOOT_RES_PAGES || kind==BOOT_RES_STACK)++n;}return n;}
static uint64_t private_tables(struct boot_pages *pages){uint64_t n=0;for(struct boot_reservation_chunk *c=pages->reservations.first;c;c=c->next)for(size_t i=0;i<c->count;++i)n+=c->entries[i].kind==BOOT_RES_SPACE_TABLE;return n;}
static bool teardown_profile(const struct kernel_surfaces *hub,struct boot_pages *pages){
    kernel_surface_call create=kernel_surface_find(hub,"mem.space_create"),map=kernel_surface_find(hub,"mem.space_map"),destroy=kernel_surface_find(hub,"mem.space_destroy"),alloc=kernel_surface_find(hub,"mem.pages_alloc"),free=kernel_surface_find(hub,"mem.pages_free"),clock=kernel_surface_find(hub,"clock.now_ns");
    const unsigned sizes[]={32,512,128};
    for(unsigned shape=0;shape<3;++shape){
        uint64_t elapsed[10],populate[10],tables[10],hold[10],irq_off[10];
        for(unsigned trial=0;trial<10;++trial){uint64_t baseline=owned_count(pages),before_tables=private_tables(pages);struct mem_space_info space;struct mem_pages data;
            CHECK(!CALL(create,700,&space,0).status && !CALL(alloc,sizes[shape],700,&data).status);
            struct kernel_surface_result begin=CALL(clock,0,0,0),end;
            for(unsigned i=0;i<sizes[shape];++i)CHECK(!map(space.id,700,UINT64_C(0x400000)+(uint64_t)i*(shape==2?UINT64_C(0x200000):4096),data.physical+(uint64_t)i*4096,0,0).status);
            end=CALL(clock,0,0,0);CHECK(!begin.status && !end.status && end.value>=begin.value);populate[trial]=end.value-begin.value;tables[trial]=private_tables(pages)-before_tables;
            CHECK(!kernel_cpu_control(CPU_METRICS_RESET,0,0,0,0,0).status);
            begin=CALL(clock,0,0,0);CHECK(!CALL(destroy,space.id,700,0).status);end=CALL(clock,0,0,0);
            CHECK(!begin.status && !end.status && end.value>=begin.value);elapsed[trial]=end.value-begin.value;
            struct cpu_metrics metrics;CHECK(!kernel_cpu_control(CPU_METRICS,(uintptr_t)&metrics,0,0,0,0).status);hold[trial]=metrics.max_hold_ns;irq_off[trial]=metrics.max_irq_off_ns;
            attribution(hub,&metrics,shape+1,trial);
            CHECK(!CALL(free,data.id,700,0).status && owned_count(pages)==baseline);
        }
        const char *total[]={"teardown_dense_32_ns","teardown_dense_512_ns","teardown_sparse_128_ns"},*build[]={"populate_dense_32_ns","populate_dense_512_ns","populate_sparse_128_ns"},*table_names[]={"dense_32_table_pages","dense_512_table_pages","sparse_128_table_pages"},*hold_names[]={"teardown_dense_32_hold_ns","teardown_dense_512_hold_ns","teardown_sparse_128_hold_ns"},*irq_names[]={"teardown_dense_32_irq_off_ns","teardown_dense_512_irq_off_ns","teardown_sparse_128_irq_off_ns"};
        samples(total[shape],elapsed,10);samples(build[shape],populate,10);samples(table_names[shape],tables,10);samples(hold_names[shape],hold,10);samples(irq_names[shape],irq_off,10);
    }
    return true;
}
bool kernel_spaces_test(const struct kernel_surfaces *hub,struct boot_pages *pages){
    const struct kernel_hub_table *cpus=kernel_hub_find_table(hub,"smp.cpus");CHECK(cpus && cpus->count);
    kernel_surface_call configure=kernel_surface_find(hub,"probe.space_fault_config");CHECK(configure);
    kernel_surface_call page_alloc=kernel_surface_find(hub,"mem.pages_alloc"),page_free=kernel_surface_find(hub,"mem.pages_free");
    struct mem_pages storage;CHECK(cpus->count<=SIZE_MAX/sizeof(struct space_fault_slot) && !CALL(page_alloc,(cpus->count*sizeof(struct space_fault_slot)+4095)/4096,998,&storage).status);
    struct space_fault_slot *slots=(void *)storage.address;CHECK(!CALL(configure,slots,cpus->count,0).status);
    /* event.step lazily creates two permanent deferred-worker stacks. Warm it
     * before the test allocation baseline, rather than misclassifying them. */
    CHECK(!CALL(kernel_surface_find(hub,"event.step"),1,0,0).status);
    struct clock_info initial,final;kernel_surface_call clock_info=kernel_surface_find(hub,"clock.info");CHECK(clock_info && !CALL(clock_info,&initial,0,0).status);
    CHECK(profile_contract_test(hub));
    uint64_t source_begin[]={initial.source,initial.flags,initial.hpet_period_fs,initial.tsc_hz};samples("clock_begin",source_begin,4);uint64_t cr4;__asm__ volatile("mov %%cr4,%0":"=r"(cr4));uint64_t root_mode[]={!!(cr4&128),!!(cr4&(1u<<17))};samples("root_mode",root_mode,2);
    CHECK(instrumentation_profile(hub,initial.hpet_period_fs));
    uint64_t baseline=owned_count(pages);
    for(unsigned i=0;i<8;++i){CHECK(cycle(hub,pages,i));CHECK(owned_count(pages)==baseline);}
    uint64_t faults=0;for(uint64_t i=0;i<cpus->count;++i){CHECK(!slots[i].armed);faults+=slots[i].seen;}
    CHECK(faults==8*5*(cpus->count>1?3:2) && !CALL(configure,0,0,0).status && !CALL(page_free,storage.id,998,0).status);
    samples("fault_events",&faults,1);uint64_t cycles=8;samples("cycles",&cycles,1);
    CHECK(teardown_profile(hub,pages) && !CALL(clock_info,&final,0,0).status);
    uint64_t source_end[]={final.source,final.flags,final.hpet_period_fs,final.tsc_hz};samples("clock_end",source_end,4);
    print("PASS address-space profiling: paired root controls, active/inactive protection, scheduling and 30 dense/sparse teardown trials\n");
    print("PASS address spaces: 8 cycles; exact RO/NX/unmapped faults, restored CPU-local gates, active SMP revocation, reuse, preemption and zero owned-page/table/stack leaks\n");return true;
}
