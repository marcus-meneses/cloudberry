#include "../kmodules/shared/module.h"
#include "../kmodules/shared/inthandle.h"
struct kernel_surface_result probe_arguments(MODULE_ARGS) {
    return module_result(a+3*b+5*c+7*d+11*e+13*f, 1);
}
struct kernel_surface_result probe_add(MODULE_ARGS) {
    MODULE_UNUSED(); return module_result(a+b,1);
}

struct kernel_surface_result probe_interrupt(MODULE_ARGS) {
    MODULE_UNUSED(); struct inthandle_frame *frame=(void *)a;
    if (!frame || frame->vector!=129 || frame->error || frame->cs!=8 || b!=42 || c!=INTHANDLE_CPU_ENTRY) return module_result(0,0);
    frame->rax=UINT64_C(0x123456789abcdef0); return module_result(1,1);
}

#include "probe.h"
struct kernel_surface_result probe_payload_call(MODULE_ARGS) {
    MODULE_UNUSED(); const struct probe_payload *payload=(const void *)a;
    if (!payload || b!=sizeof(*payload)) return module_result(0,0);
    return module_result(payload->left+payload->right+c,1);
}
struct kernel_surface_result probe_failed(MODULE_ARGS) {
    MODULE_UNUSED(); return module_result(99,0);
}

struct kernel_surface_result probe_deferred(MODULE_ARGS) {
    MODULE_UNUSED(); const struct inthandle_frame *frame=(const void *)a;
    return module_result(1,frame && frame->vector==129 && frame->error==0 && frame->cs==8 &&
                           b==42 && c==INTHANDLE_DEFERRED);
}

struct kernel_surface_result probe_timestamp(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    kernel_surface_call now=kernel_surface_find_version(hub,"clock.now_ns",1,0);
    kernel_surface_call step=kernel_surface_find_version(hub,"event.step",1,0),
        run=kernel_surface_find_version(hub,"event.run",1,0);
    if (!step || !run || step(1,0,0,0,0,0).status!=KERNEL_SURFACE_FAILED ||
        run(1,0,0,0,0,0).status!=KERNEL_SURFACE_FAILED) return module_result(0,0);
    return now?now(0,0,0,0,0,0):module_result(0,0);
}

#include "../kmodules/shared/threads.h"
#include "../kmodules/shared/mem-pages.h"
#include "../kernel/cpu.h"
struct kernel_surface_result probe_thread_call(MODULE_ARGS) {
    MODULE_UNUSED();struct probe_thread *p=(void *)a;
    const struct kernel_surfaces *hub=(const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    kernel_surface_call control=kernel_surface_find(hub,"threads.control"),cpu=kernel_surface_find(hub,"cpu.control"),
        now=kernel_surface_find(hub,"clock.now_ns"),alloc=kernel_surface_find(hub,"mem.pages_alloc"),free=kernel_surface_find(hub,"mem.pages_free");
    if(!p || !control || !cpu || !now || !alloc || !free)return module_result(0,0);
    volatile uint64_t canary[16];for(unsigned i=0;i<16;++i)canary[i]=UINT64_C(0x123456789abcdef0)+i;
    if(p->mode==0) {__atomic_store_n(&p->phase,1,__ATOMIC_RELEASE);while(!__atomic_load_n(&p->peer,__ATOMIC_ACQUIRE))__asm__ volatile("pause");}
    if(p->mode==1) {struct probe_thread *peer=(void *)b;__atomic_store_n(&peer->peer,1,__ATOMIC_RELEASE);}
    if(p->mode==2) {__atomic_store_n(&p->phase,1,__ATOMIC_RELEASE);if(control(THREAD_BLOCK,0,0,0,0,0).status)++p->errors;}
    if(p->mode==3) {
        p->cpu=cpu(CPU_CURRENT,0,0,0,0,0).value;
        for(unsigned i=0;i<8;++i) {struct mem_pages pages;
            if(alloc(1,p->cpu+100,(uintptr_t)&pages,0,0,0).status) {++p->errors;break;}
            volatile uint64_t *word=(void *)pages.address;if(*word)++p->errors;*word=i+1;
            if(free(pages.id,p->cpu+100,0,0,0,0).status)++p->errors;
            struct kernel_surface_result time=now(0,0,0,0,0,0);if(time.status || time.value<p->clock)++p->errors;p->clock=time.value;
            if(control(THREAD_YIELD,0,0,0,0,0).status)++p->errors;
        }
    }
    if(p->mode==4) {
        volatile uint64_t *word=(void *)p->address;
        if(*word!=0x1111)++p->errors;
        __atomic_store_n(&p->phase,1,__ATOMIC_RELEASE);
        if(control(THREAD_BLOCK,0,0,0,0,0).status)++p->errors;
        if(*word!=0x2222)++p->errors;
    }
    if(p->mode==6) {
        while(!__atomic_load_n(&p->peer,__ATOMIC_ACQUIRE))__asm__ volatile("pause");
        struct kernel_surface_result begin=now(0,0,0,0,0,0),time=begin;
        do{time=now(0,0,0,0,0,0);if(time.status || begin.status){++p->errors;break;}}while(time.value-begin.value<20000000);
        kernel_surface_call submit=kernel_surface_find_version(hub,"scheduler.submit",3,0);
        struct scheduler_request request={.abi=SCHEDULER_REQUEST_ABI,.kind=SCHEDULER_METHOD,.entry_vector=(uintptr_t)kernel_surface_find(hub,"probe.add"),.entry_major=1,.payload={.arguments={4,5}}};
        struct kernel_surface_result queued=submit((uintptr_t)&request,0,0,0,0,0);if(queued.status)++p->errors;p->address=queued.value;
    }
    if(p->mode==7) {
        struct kernel_surface_result start=now(0,0,0,0,0,0);
        if(start.status || control(THREAD_BLOCK_UNTIL,start.value+10000000,0,0,0,0).status || now(0,0,0,0,0,0).value<start.value+10000000)++p->errors;
    }
    if(p->mode==8 && control(THREAD_BLOCK,0,0,0,0,0).status)++p->errors;
    if(p->mode==10) {cpu(CPU_ENTER,0,0,0,0,0);__atomic_store_n(&p->phase,1,__ATOMIC_RELEASE);for(;;)__asm__ volatile("pause");}
    if(p->mode==9) {
        while(!__atomic_load_n(&p->peer,__ATOMIC_ACQUIRE))__asm__ volatile("pause");
        uint64_t flags=cpu(CPU_ENTER,0,0,0,0,0).value;volatile uint64_t *counter=(void *)p->address;uint64_t begin=*counter;
        __atomic_store_n(&p->phase,1,__ATOMIC_RELEASE);
        while(*counter-begin<p->iterations)__asm__ volatile("pause");
        cpu(CPU_LEAVE,flags,0,0,0,0);
        struct cpu_metrics sample;uint64_t index=cpu(CPU_CURRENT,0,0,0,0,0).value;
        if(!cpu(CPU_METRICS,(uintptr_t)&sample,index,0,0,0).status)p->clock=sample.max_irq_off_ns;
        cpu(CPU_METRICS_RESET,0,0,0,0,0);
    }
    if(p->mode==5) {
        uint64_t flags=cpu(CPU_ENTER,0,0,0,0,0).value;
        if(!control(THREAD_YIELD,0,0,0,0,0).status)++p->errors;
        cpu(CPU_LEAVE,flags,0,0,0,0);
    }
    for(unsigned i=0;i<16;++i)if(canary[i]!=UINT64_C(0x123456789abcdef0)+i)++p->errors;
    __atomic_store_n(&p->done,1,__ATOMIC_RELEASE);return module_result(77,p->errors==0);
}

struct kernel_surface_result probe_delay(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    kernel_surface_call now=kernel_surface_find(hub,"clock.now_ns");
    struct kernel_surface_result start=now(0,0,0,0,0,0),end=start;
    if(start.status)return start;
    do {end=now(0,0,0,0,0,0);if(end.status)return end;}while(end.value-start.value<a);
    return module_result(end.value-start.value,1);
}

#include "../kmodules/shared/irq.h"
struct kernel_surface_result probe_irq_call(MODULE_ARGS) {
    MODULE_UNUSED();const struct irq_notification *n=(const void *)a;struct probe_irq *p=(void *)b;
    const struct kernel_surfaces *hub=(const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    kernel_surface_call write=kernel_surface_find(hub,"mmio.write"),enable=kernel_surface_find(hub,"irq.enable"),cpu=kernel_surface_find(hub,"cpu.control");
    uint64_t flags;__asm__ volatile("pushfq;pop %0":"=r"(flags));
    if(!n || !p || !write || !enable || !cpu)return module_result(0,0);
    if(!(flags&512) || cpu(CPU_DEPTH,0,0,0,0,0).value)++p->errors;
    if(write(p->index,0x20,8,1,0,0).status || enable(n->id,n->owner,1,0,0,0).status)++p->errors;
    ++p->callbacks;p->notifications+=n->count;return module_result(1,!p->errors);
}

struct kernel_surface_result probe_nvme_call(MODULE_ARGS) {
    MODULE_UNUSED();struct probe_nvme *p=(void *)a;
    const struct kernel_surfaces *hub=(const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    kernel_surface_call read=kernel_surface_find(hub,"nvme.read");
    struct kernel_surface_result r=read(p->lba,p->blocks,p->buffer,0,0,0);
    p->value=r.value;p->status=r.status;__atomic_store_n(&p->done,1,__ATOMIC_RELEASE);return r;
}

#include "../kmodules/shared/xstate.h"
/* Assembly intentionally owns vector/x87 registers; the surrounding C and all
 * ISR infrastructure compile with -mgeneral-regs-only. */
struct kernel_surface_result probe_xstate(MODULE_ARGS) {
    MODULE_UNUSED();struct probe_thread *p=(void *)a;if(!p)return module_result(0,0);
    const struct kernel_surfaces *hub=(const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    kernel_surface_call now=kernel_surface_find(hub,"clock.now_ns"),control=kernel_surface_find(hub,"threads.control"),info=kernel_surface_find(hub,"xstate.info");
    struct xstate_info layout;if(!info || info((uintptr_t)&layout,0,0,0,0,0).status)return module_result(0,0);
    uint64_t thread_id=control(THREAD_CURRENT,0,0,0,0,0).value;
    uint64_t pattern[4]={p->mode,p->mode+1,p->mode+2,p->mode+3},got[4];
    uint64_t fresh[2];__asm__ volatile("movdqu %%xmm0,%0":"=m"(fresh)::"memory");
    if(fresh[0] || fresh[1])p->errors|=1;
    uint32_t mxcsr=0x1f80|((p->mode&3)<<13),read_mxcsr;
    uint16_t cw=0x37f|((p->mode&3)<<10),read_cw;
    uint64_t fp=UINT64_C(0x3ff0000000000000)+(p->mode<<40),fp_got;
    __asm__ volatile("ldmxcsr %0;fldcw %1;fldl %2"::"m"(mxcsr),"m"(cw),"m"(fp):"memory");
    if(layout.mask&4)__asm__ volatile("vmovdqu %0,%%ymm0;vmovdqu %0,%%ymm15"::"m"(pattern):"memory");
    else __asm__ volatile("movdqu %0,%%xmm0;movdqu %0,%%xmm15"::"m"(pattern):"memory");
    uint64_t start=now(0,0,0,0,0,0).value;unsigned loops=0;
    for(;;) {
        if(loops<3 && control(THREAD_YIELD,0,0,0,0,0).status)++p->errors;
        uint64_t time=now(0,0,0,0,0,0).value;
        if(layout.mask&4)__asm__ volatile("vmovdqu %%ymm0,%0":"=m"(got)::"memory");
        else __asm__ volatile("movdqu %%xmm0,%0":"=m"(got)::"memory");
        for(unsigned i=0;i<((layout.mask&4)?4u:2u);++i)if(got[i]!=pattern[i])p->errors|=2;
        if(layout.mask&4)__asm__ volatile("vmovdqu %%ymm15,%0":"=m"(got)::"memory");
        else __asm__ volatile("movdqu %%xmm15,%0":"=m"(got)::"memory");
        for(unsigned i=0;i<((layout.mask&4)?4u:2u);++i)if(got[i]!=pattern[i])p->errors|=4;
        __asm__ volatile("stmxcsr %0;fnstcw %1;fstl %2":"=m"(read_mxcsr),"=m"(read_cw),"=m"(fp_got)::"memory");
        if(read_mxcsr!=mxcsr)p->errors|=8;
        if(read_cw!=cw)p->errors|=16;
        if(fp_got!=fp)p->errors|=32;
        if(loops==3)start=time;
        ++loops;if(loops>4 && time-start>=30000000) {
            struct thread_info current;if(control(THREAD_INFO,thread_id,(uintptr_t)&current,0,0,0).status)p->errors|=64;
            if(current.switches>4)break;
            if(time-start>500000000) {p->errors|=64;break;}
        }
    }
    __asm__ volatile("fstp %%st(0)":::"memory");p->done=1;p->iterations=loops;return module_result(loops,!p->errors);
}
