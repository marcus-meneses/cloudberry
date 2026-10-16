#include "cpu.h"
static struct cpu_record bootstrap;
static struct cpu_config config;
static uint32_t lock_owner[2];
static void irq_begin(struct cpu_record *,uint64_t);
static void irq_end(struct cpu_record *,uint64_t);
static kernel_surface_call backend_id,backend_ticks,backend_fence,backend_init;
bool kernel_cpu_backend(const struct kernel_surfaces *hub) {
    backend_id=kernel_surface_find_version(hub,"cpu.id",1,0);backend_ticks=kernel_surface_find_version(hub,"cpu.ticks",1,0);
    backend_fence=kernel_surface_find_version(hub,"cpu.ipi_fence",1,0);backend_init=kernel_surface_find_version(hub,"cpu.init",1,0);
    return backend_id && backend_ticks && backend_fence && backend_init && !backend_init(0,0,0,0,0,0).status;
}
static uint32_t local_id(void) {return backend_id(0,0,0,0,0,0).value;}
static struct cpu_record *current(void) {
    if (!config.records) return &bootstrap;
    struct cpu_record *cpu;__asm__ volatile("movq %%gs:0,%0":"=r"(cpu));return cpu;
}
static int attach(struct cpu_record *cpu) {
    if(!backend_init || backend_init(0,0,0,0,0,0).status)return 0;
    uintptr_t at=(uintptr_t)cpu,base=(uintptr_t)config.records;
    if(at<base || (at-base)%sizeof(*cpu) || (at-base)/sizeof(*cpu)>=config.count || cpu->lapic_id!=local_id())return 0;
    cpu->self=at;
    __asm__ volatile("wrmsr"::"c"(0xc0000101u),"a"((uint32_t)at),"d"((uint32_t)(at>>32)):"memory");return 1;
}
static void flush_local(struct cpu_record *cpu) {
    uint64_t request=__atomic_load_n(&cpu->tlb_request,__ATOMIC_ACQUIRE);
    if(request!=__atomic_load_n(&cpu->tlb_ack,__ATOMIC_RELAXED)) {
        uintptr_t cr3;__asm__ volatile("mov %%cr3,%0;mov %0,%%cr3":"=r"(cr3)::"memory");
        __atomic_store_n(&cpu->tlb_ack,request,__ATOMIC_RELEASE);
    }
}
static void write_reg(uint32_t off,uint32_t value) {
    if(config.mode==2) __asm__ volatile("wrmsr"::"c"(0x800+off/16),"a"(value),"d"(0):"memory");
    else *(volatile uint32_t *)(config.apic+off)=value;
}
__attribute__((noreturn)) static void panic(const char *s) {
    while(*s)__asm__ volatile("outb %0,$0xe9"::"a"((uint8_t)*s++));
    __asm__ volatile("ud2");__builtin_unreachable();
}
#define WAIT_BUDGET_NS UINT64_C(5000000000)
#define CRITICAL_NOTICE_NS UINT64_C(2000000)
static uint64_t watchdog_now(void) {return config.watchdog_hpet?*(volatile uint64_t *)config.watchdog_hpet:0;}
static uint64_t watchdog_ns(uint64_t start,uint64_t end) {
    if(!config.hpet_period_fs || end<start)return 0;
    uint64_t delta=end-start,q=delta/1000000,r=delta%1000000;
    if(q>UINT64_MAX/config.hpet_period_fs)return UINT64_MAX;
    uint64_t whole=q*config.hpet_period_fs,fraction=r*config.hpet_period_fs/1000000;
    return fraction>UINT64_MAX-whole?UINT64_MAX:whole+fraction;
}
static void watchdog_check(uint64_t start,unsigned spins,const char *reason) {
    uint64_t now=watchdog_now();
    if((config.watchdog_hpet && (now<start || watchdog_ns(start,now)>WAIT_BUDGET_NS)) || spins==100000000)panic(reason);
}
static void ipi(uint32_t id,uint32_t vector) {
    uint64_t flags;__asm__ volatile("pushfq;pop %0;cli":"=r"(flags)::"memory");
    struct cpu_record *cpu=current();irq_begin(cpu,flags);
    if(config.mode==2)backend_fence(0,0,0,0,0,0);
    if(config.mode==2) __asm__ volatile("wrmsr"::"c"(0x830),"a"(vector),"d"(id):"memory");
    else {
        unsigned spins=0;uint64_t start=watchdog_now();
        while(*(volatile uint32_t *)(config.apic+0x300)&(1u<<12)) {
            watchdog_check(start,++spins,"Cloudberry: fatal IPI delivery timeout\n");
            flush_local(current());__asm__ volatile("pause");
        }
        write_reg(0x310,id<<24);write_reg(0x300,vector);
    }
    irq_end(cpu,flags);
    __asm__ volatile("push %0;popfq"::"r"(flags):"memory","cc");
}
static uint64_t ticks(void) {
    if(backend_ticks)return backend_ticks(0,0,0,0,0,0).value;
    /* Bootstrap only, before a CPU module has been selected. */
    uint32_t a,b,c,d;__asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0),"c"(0):"memory");
    __asm__ volatile("rdtsc":"=a"(a),"=d"(d)::"memory");return ((uint64_t)d<<32)|a;
}
static void metric_add(uint64_t *at,uint64_t value) {__atomic_add_fetch(at,value,__ATOMIC_RELAXED);}
static void metric_max(uint64_t *at,uint64_t value) {uint64_t old=__atomic_load_n(at,__ATOMIC_RELAXED);while(old<value && !__atomic_compare_exchange_n(at,&old,value,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {}}
static uint64_t elapsed(struct cpu_record *cpu,uint64_t begin,uint64_t end) {
    if(end<begin) {metric_add(&cpu->metrics.clock_regressions,1);return 0;}return end-begin;
}
static void irq_begin(struct cpu_record *cpu,uint64_t flags) {if(flags&512) {cpu->irq_begin=ticks();cpu->irq_hpet=watchdog_now();}}
static void irq_end(struct cpu_record *cpu,uint64_t flags) {
    if(flags&512) {uint64_t span=elapsed(cpu,cpu->irq_begin,ticks());metric_add(&cpu->metrics.irq_off_ticks,span);metric_max(&cpu->metrics.max_irq_off_ticks,span);uint64_t ns=watchdog_ns(cpu->irq_hpet,watchdog_now());metric_max(&cpu->metrics.max_irq_off_ns,ns);if(ns>CRITICAL_NOTICE_NS)metric_add(&cpu->metrics.critical_overruns,1);}
}
uint64_t kernel_cpu_enter(void) {return kernel_cpu_enter_tag(0);}
uint64_t kernel_cpu_enter_tag(uint64_t method) {return kernel_cpu_enter_domain(0,method);}
uint64_t kernel_cpu_enter_domain(unsigned domain,uint64_t method) {
    uint64_t flags;__asm__ volatile("pushfq;pop %0;cli":"=r"(flags)::"memory");
    struct cpu_record *cpu=current();if(domain>1 || (domain==0 && cpu->lock_depth[1] && !cpu->lock_depth[0]))panic("Cloudberry: fatal lock-order inversion\n");
    uint32_t owner=cpu->index+1;uint64_t begin=cpu->lock_depth[domain]?0:ticks();
    uint64_t started_hpet=watchdog_now();unsigned spins=0;int contended=0;
    /* IRQ delivery is safe while waiting, but switching into a dispatcher which
     * needs this same lock would simply move the IRQ-disabled wait there. */
    int waiting_guard=(flags&512)!=0;if(waiting_guard)__atomic_add_fetch(&cpu->depth,1,__ATOMIC_RELAXED);
    irq_begin(cpu,flags);
    if(__atomic_load_n(&lock_owner[domain],__ATOMIC_RELAXED)!=owner) {
        for(;;) {uint32_t empty=0;if(__atomic_compare_exchange_n(&lock_owner[domain],&empty,owner,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) break;
            contended=1;flush_local(cpu);
            watchdog_check(started_hpet,++spins,"Cloudberry: fatal lock acquisition timeout\n");
            /* A waiter owns no protected state. Service timers/device IRQs and
             * keep hardware delivery live when the caller allowed interrupts.
             * Thread switching stays inhibited until acquisition completes.
             * Nested acquisitions retain their parent's IF policy. */
            if(flags&512) {
                irq_end(cpu,flags);metric_add(&cpu->metrics.irq_enabled_waits,1);
                __asm__ volatile("sti":::"memory");
                do {
                    watchdog_check(started_hpet,++spins,"Cloudberry: fatal lock acquisition timeout\n");
                    __asm__ volatile("pause");
                } while(__atomic_load_n(&lock_owner[domain],__ATOMIC_RELAXED));
                __asm__ volatile("cli":::"memory");irq_begin(cpu,flags);
            } else __asm__ volatile("pause");
        }
    }
    if(!cpu->lock_depth[domain]) {
        uint64_t acquired=ticks(),wait=elapsed(cpu,begin,acquired);cpu->lock_begin[domain]=acquired;cpu->lock_hpet[domain]=watchdog_now();metric_max(&cpu->metrics.max_wait_ns,watchdog_ns(started_hpet,cpu->lock_hpet[domain]));cpu->lock_method[domain]=method;
        metric_add(&cpu->metrics.domain_acquisitions[domain],1);metric_add(&cpu->metrics.domain_contended[domain],contended);metric_add(&cpu->metrics.domain_wait_ticks[domain],wait);
        metric_add(&cpu->metrics.acquisitions,1);metric_add(&cpu->metrics.contended,contended);metric_add(&cpu->metrics.wait_ticks,wait);metric_max(&cpu->metrics.max_wait_ticks,wait);
    }
    ++cpu->lock_depth[domain];__atomic_add_fetch(&cpu->depth,1,__ATOMIC_RELAXED);
    if(waiting_guard)__atomic_sub_fetch(&cpu->depth,1,__ATOMIC_RELAXED);
    return flags;
}
void kernel_cpu_leave(uint64_t flags) {kernel_cpu_leave_domain(0,flags);}
void kernel_cpu_leave_domain(unsigned domain,uint64_t flags) {
    struct cpu_record *cpu=current();
    if(domain>1 || !cpu->lock_depth[domain] || !__atomic_load_n(&cpu->depth,__ATOMIC_RELAXED))panic("Cloudberry: fatal critical-section underflow\n");
    int outer=cpu->lock_depth[domain]==1;if(outer)__atomic_add_fetch(&cpu->metrics_sequence,1,__ATOMIC_ACQ_REL);
    __atomic_sub_fetch(&cpu->depth,1,__ATOMIC_RELAXED);
    if(!--cpu->lock_depth[domain]) {
        metric_max(&cpu->metrics.max_hold_ns,watchdog_ns(cpu->lock_hpet[domain],watchdog_now()));
        uint64_t held=elapsed(cpu,cpu->lock_begin[domain],ticks());metric_add(&cpu->metrics.hold_ticks,held);metric_add(&cpu->metrics.domain_hold_ticks[domain],held);
        if(held>__atomic_load_n(&cpu->metrics.max_hold_ticks,__ATOMIC_RELAXED)) {__atomic_store_n(&cpu->metrics.max_hold_ticks,held,__ATOMIC_RELAXED);__atomic_store_n(&cpu->metrics.max_hold_method,cpu->lock_method[domain],__ATOMIC_RELAXED);__atomic_store_n(&cpu->metrics.max_hold_domain,domain,__ATOMIC_RELAXED);}
        __atomic_store_n(&lock_owner[domain],0,__ATOMIC_RELEASE);
    }
    irq_end(cpu,flags);
    if(outer)__atomic_add_fetch(&cpu->metrics_sequence,1,__ATOMIC_RELEASE);
    __asm__ volatile("push %0;popfq"::"r"(flags):"memory","cc");
}
void kernel_cpu_flush(void) {
    if(!config.records) return;
    struct cpu_record *self=current();
    for(size_t i=0;i<config.count;++i) {struct cpu_record *cpu=&config.records[i];
        if(cpu==self || !__atomic_load_n(&cpu->online,__ATOMIC_ACQUIRE)) continue;
        uint64_t request=__atomic_add_fetch(&cpu->tlb_request,1,__ATOMIC_RELEASE);ipi(cpu->lapic_id,CPU_TLB_VECTOR);
        uint64_t started=watchdog_now();unsigned wait;for(wait=0;wait<100000000;++wait) {if(__atomic_load_n(&cpu->tlb_ack,__ATOMIC_ACQUIRE)==request) break;watchdog_check(started,wait+1,"Cloudberry: fatal TLB acknowledgement timeout\n");flush_local(self);__asm__ volatile("pause");}
        if(wait==100000000)panic("Cloudberry: fatal TLB acknowledgement timeout\n");
    }
}
struct kernel_surface_result kernel_cpu_control(MODULE_ARGS) {
    MODULE_UNUSED();
    if(a==CPU_LATENCY && b) {*(struct cpu_latency_policy *)b=(struct cpu_latency_policy){WAIT_BUDGET_NS,CRITICAL_NOTICE_NS,config.watchdog_hpet!=0,0};return module_result(1,1);}
    if(a==CPU_METRICS && b && c<config.count) {
        struct cpu_record *target=&config.records[c];const uint64_t *source=(const void *)&target->metrics;
        struct cpu_metrics snapshot;uint64_t *out=(void *)&snapshot;
        for(unsigned attempt=0;attempt<8;++attempt) {
            uint64_t sequence=__atomic_load_n(&target->metrics_sequence,__ATOMIC_ACQUIRE);if(sequence&1)continue;
            for(size_t i=0;i<sizeof(struct cpu_metrics)/8;++i)out[i]=__atomic_load_n(&source[i],__ATOMIC_RELAXED);
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            if(sequence==__atomic_load_n(&target->metrics_sequence,__ATOMIC_RELAXED)) {*(struct cpu_metrics *)b=snapshot;return module_result(1,1);}
        }return module_result(0,0);
    }
    if(a==CPU_METRICS_RESET) {
        uint64_t flags;__asm__ volatile("pushfq;pop %0;cli":"=r"(flags)::"memory");
        struct cpu_record *cpu=current();if(cpu->depth || !(flags&512)) {__asm__ volatile("push %0;popfq"::"r"(flags):"memory","cc");return module_result(0,0);}
        __atomic_add_fetch(&cpu->metrics_sequence,1,__ATOMIC_ACQ_REL);
        uint64_t *data=(void *)&cpu->metrics;for(size_t i=0;i<sizeof(cpu->metrics)/8;++i)__atomic_store_n(&data[i],0,__ATOMIC_RELAXED);
        __atomic_add_fetch(&cpu->metrics_sequence,1,__ATOMIC_RELEASE);
        __asm__ volatile("push %0;popfq"::"r"(flags):"memory","cc");return module_result(1,1);
    }
    if(a==CPU_IRQ_BEGIN) {irq_begin(current(),b);return module_result(1,1);}
    if(a==CPU_IRQ_END) {irq_end(current(),b);return module_result(1,1);}
    if(a==CPU_CURRENT) return module_result(current()->index,1);
    if(a==CPU_ENTER) return module_result(kernel_cpu_enter(),1);
    if(a==CPU_LEAVE) {kernel_cpu_leave(b);return module_result(1,1);}
    if(a==CPU_DEPTH) return module_result(__atomic_load_n(&current()->depth,__ATOMIC_RELAXED),1);
    if(a==CPU_CONFIGURE) {
        const struct cpu_config *in=(const void *)b;
        if(config.records || !in || !in->records || !in->count || in->count>UINT32_MAX || (in->mode==1 && !in->apic) || bootstrap.depth || (in->mode!=1 && in->mode!=2)) return module_result(0,0);
        if((in->watchdog_hpet && (!in->hpet_period_fs || in->hpet_period_fs>1000000000 || (in->watchdog_hpet&7))) || (!in->watchdog_hpet && in->hpet_period_fs))return module_result(0,0);
        config=*in;
        uint32_t id=local_id();for(size_t i=0;i<config.count;++i)if(config.records[i].lapic_id==id)return module_result(1,attach(&config.records[i]));
        return module_result(0,0);
    }
    if(a==CPU_ATTACH) {int ok=config.records && attach((void *)b);return module_result(ok,ok);}
    if(a==CPU_WAKE && b<config.count && __atomic_load_n(&config.records[b].online,__ATOMIC_ACQUIRE)) {ipi(config.records[b].lapic_id,CPU_WAKE_VECTOR);return module_result(1,1);}
    if(a==CPU_TIMER) {__atomic_add_fetch(&current()->timer_interrupts,1,__ATOMIC_RELAXED);return module_result(1,1);}
    if(a==CPU_ACTIVE) {struct cpu_record *cpu=current();__atomic_store_n(&cpu->active_bottom,b,__ATOMIC_RELEASE);__atomic_store_n(&cpu->active_top,c,__ATOMIC_RELEASE);return module_result(1,1);}
    if(a==CPU_STACK_BUSY) {
        for(size_t i=0;i<config.count;++i) {struct cpu_record *cpu=&config.records[i];
            if((b<cpu->stack_top && cpu->stack_bottom<c) || (b<__atomic_load_n(&cpu->active_top,__ATOMIC_ACQUIRE) && __atomic_load_n(&cpu->active_bottom,__ATOMIC_ACQUIRE)<c))return module_result(1,1);
            for(unsigned k=0;k<3;++k)if(b<cpu->ist_top[k] && cpu->ist_bottom[k]<c)return module_result(1,1);
        }return module_result(0,1);
    }
    if(a==CPU_ACK && config.records) {flush_local(current());write_reg(0xb0,0);return module_result(1,1);}
    return module_result(0,0);
}
