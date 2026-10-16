#include "threads.h"
#include "xstate.h"
#include "mem-pages.h"
#include "timer.h"
#include "../../kernel/cpu.h"
struct context {uintptr_t rsp,xstate;};
struct thread {struct thread *next;struct context context;struct mem_stack stack;uint64_t ready_ns,wake_ns;int ready_valid,wake_pending;struct thread_info info;kernel_surface_call entry;uintptr_t args[6];};
struct local {struct context dispatcher;struct thread *current;uint64_t dispatching;struct thread_timing timing;};
static struct {const struct kernel_surfaces *hub;uintptr_t heap;size_t count;uint64_t serial;struct thread *head,*tail;struct local *cpus;struct xstate_info xstate;kernel_surface_call fp_init,fp_save,fp_restore,aligned,cpu,alloc,free,stack_alloc,stack_free,arm,now;} state;
extern void threads_switch(struct context *,const struct context *) __attribute__((visibility("hidden")));
static struct kernel_surface_result call(kernel_surface_call fn,uintptr_t a,uintptr_t b,uintptr_t c) {return fn(a,b,c,0,0,0);}
static void switch_context(struct context *from,const struct context *to) {
    if(call(state.fp_save,from->xstate,state.xstate.bytes,0).status || call(state.fp_restore,to->xstate,state.xstate.bytes,0).status)__asm__ volatile("ud2");
    threads_switch(from,to);
}
static size_t current_cpu(void) {return call(state.cpu,CPU_CURRENT,0,0).value;}
static uint64_t enter(void) {return call(state.cpu,CPU_ENTER,0,0).value;}
static void leave(uint64_t f) {(void)call(state.cpu,CPU_LEAVE,f,0);}
static uint32_t status(const struct thread *t) {return __atomic_load_n(&t->info.state,__ATOMIC_ACQUIRE);}
static void set_status(struct thread *t,uint32_t value) {__atomic_store_n(&t->info.state,value,__ATOMIC_RELEASE);}
static void suspend(uint32_t next,uint64_t deadline) {
    struct local *cpu=&state.cpus[current_cpu()];struct thread *t=cpu->current;
    uint64_t saved;__asm__ volatile("pushfq;pop %0;cli":"=r"(saved)::"memory");
    uint64_t lock_flags=next==THREAD_BLOCKED?enter():0;
    if(next==THREAD_BLOCKED && t->wake_pending) {t->wake_pending=0;leave(lock_flags);__asm__ volatile("push %0;popfq"::"r"(saved):"memory","cc");return;}
    t->wake_ns=deadline;set_status(t,next);if(next==THREAD_BLOCKED)leave(lock_flags);
    switch_context(&t->context,&cpu->dispatcher);
    __asm__ volatile("push %0;popfq"::"r"(saved):"memory","cc");
}
__attribute__((noreturn)) static void start(void) {
    struct local *cpu=&state.cpus[current_cpu()];struct thread *t=cpu->current;
    struct kernel_surface_result r=t->entry(t->args[0],t->args[1],t->args[2],t->args[3],t->args[4],t->args[5]);
    uint64_t flags=enter();t->info.result=r;leave(flags);suspend(THREAD_EXITING,0);
    for(;;) __asm__ volatile("cli;hlt");
}
struct kernel_surface_result threads_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();if(state.hub) return module_result(1,state.hub==(const void *)a && state.count==c);
    const struct kernel_surfaces *hub=(const void *)a;if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC || hub->major!=2 || !b || !c || c>SIZE_MAX/sizeof(struct local)) return module_result(0,0);
    state.cpu=kernel_surface_find_version(hub,"cpu.control",1,0);state.alloc=kernel_surface_find_version(hub,"mem.calloc",1,0);state.free=kernel_surface_find_version(hub,"mem.free",1,0);
    state.stack_alloc=kernel_surface_find_version(hub,"mem.stack_alloc",1,0);state.stack_free=kernel_surface_find_version(hub,"mem.stack_free",1,0);state.arm=kernel_surface_find_version(hub,"timer.arm",1,0);state.now=kernel_surface_find_version(hub,"clock.now_ns",1,0);
    if(!state.cpu || !state.alloc || !state.free || !state.stack_alloc || !state.stack_free || !state.arm || !state.now) return module_result(0,0);
    kernel_surface_call info=kernel_surface_find(hub,"xstate.info");state.fp_init=kernel_surface_find(hub,"xstate.init");state.fp_save=kernel_surface_find(hub,"xstate.save");state.fp_restore=kernel_surface_find(hub,"xstate.restore");state.aligned=kernel_surface_find(hub,"mem.aligned_alloc");
    if(!info || !state.fp_init || !state.fp_save || !state.fp_restore || !state.aligned || call(info,(uintptr_t)&state.xstate,0,0).status)return module_result(0,0);
    struct kernel_surface_result r=call(state.alloc,b,c,sizeof(struct local));if(r.status) return r;
    state.cpus=(void *)r.value;
    for(size_t i=0;i<c;++i) {struct kernel_surface_result fp=call(state.aligned,b,64,state.xstate.bytes);
        if(fp.status) {for(size_t j=0;j<i;++j)call(state.free,b,state.cpus[j].dispatcher.xstate,0);call(state.free,b,r.value,0);state.cpus=NULL;return module_result(0,0);}
        state.cpus[i].dispatcher.xstate=fp.value;if(call(state.fp_init,fp.value,state.xstate.bytes,0).status)return module_result(0,0);
    }
    state.heap=b;state.count=c;state.hub=hub;return module_result(1,1);
}
struct kernel_surface_result threads_surface_create(MODULE_ARGS) {
    MODULE_UNUSED();const struct thread_request *r=(const void *)a;
    if(!state.hub || !r || r->abi!=1 || r->reserved || r->cpu>=state.count || !r->stack_bytes || r->stack_bytes<4096 || r->payload.reserved ||
        (r->payload.bytes && (!r->payload.data || r->payload.data_register>5)) || r->payload.bytes>SIZE_MAX-sizeof(struct thread)-128-state.xstate.bytes) return module_result(0,0);
    int found=0;const struct kernel_surface *methods=kernel_surface_entries(state.hub);
    for(size_t i=0;i<state.hub->count;++i) if((uintptr_t)methods[i].call==r->entry && methods[i].major==r->major && methods[i].minor>=r->minor) {found=1;break;}
    if(!found) return module_result(0,0);
    struct kernel_surface_result allocated=call(state.alloc,state.heap,1,sizeof(struct thread)+128+r->payload.bytes+state.xstate.bytes);
    if(allocated.status) return allocated;
    struct thread *t=(void *)allocated.value;
    struct kernel_surface_result stack=call(state.stack_alloc,r->stack_bytes,r->process_id,(uintptr_t)&t->stack);
    if(stack.status) {call(state.free,state.heap,(uintptr_t)t,0);return stack;}
    t->context.xstate=((uintptr_t)(t+1)+15+r->payload.bytes+63)&~(uintptr_t)63;
    if(call(state.fp_init,t->context.xstate,state.xstate.bytes,0).status) {call(state.stack_free,t->stack.id,r->process_id,0);call(state.free,state.heap,(uintptr_t)t,0);return module_result(0,0);}
    t->info=(struct thread_info){.id=0,.process_id=r->process_id,.cpu=r->cpu,.state=THREAD_RUNNABLE};t->entry=(kernel_surface_call)r->entry;
    for(size_t i=0;i<6;++i)t->args[i]=r->payload.arguments[i];
    if(r->payload.bytes) {unsigned char *dst=(void *)(((uintptr_t)(t+1)+15)&~(uintptr_t)15);const unsigned char *src=r->payload.data;
        for(size_t i=0;i<r->payload.bytes;++i) {dst[i]=src[i];}
        t->args[r->payload.data_register]=(uintptr_t)dst;}
    uintptr_t *sp=(void *)(t->stack.top-8);*sp=0;*--sp=(uintptr_t)start;*--sp=0x202;
    for(unsigned i=0;i<6;++i) {*--sp=0;}
    t->context.rsp=(uintptr_t)sp;
    struct kernel_surface_result queued=call(state.now,0,0,0);t->ready_ns=queued.value;t->ready_valid=!queued.status;
    uint64_t flags=enter();
    if(state.serial==UINT64_MAX) {leave(flags);struct kernel_surface_result released=call(state.stack_free,t->stack.id,r->process_id,0);uint64_t cleanup=released.status?t->stack.id:0;call(state.free,state.heap,(uintptr_t)t,0);return module_result(cleanup,0);}
    t->info.id=++state.serial;
    if(state.tail)state.tail->next=t;else state.head=t;state.tail=t;uint64_t id=t->info.id;
    leave(flags);call(state.cpu,CPU_WAKE,r->cpu,0);return module_result(id,1);
}
static void wake_due(uint64_t now) {
    for(struct thread *t=state.head;t;t=t->next)if(t->info.cpu==current_cpu() && status(t)==THREAD_BLOCKED && t->wake_ns && now>=t->wake_ns) {
        t->wake_ns=0;t->ready_ns=now;t->ready_valid=1;set_status(t,THREAD_RUNNABLE);
    }
}
struct kernel_surface_result threads_surface_run(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.hub || !a || call(state.cpu,CPU_DEPTH,0,0).value) return module_result(0,0);
    struct local *cpu=&state.cpus[current_cpu()];if(cpu->dispatching) return module_result(0,0);cpu->dispatching=1;
    size_t done=0;while(done<a) {
        uint64_t saved;__asm__ volatile("pushfq;pop %0;cli":"=r"(saved)::"memory");
        call(state.cpu,CPU_IRQ_BEGIN,512,0);
        uint64_t flags=enter();struct kernel_surface_result time=call(state.now,0,0,0);
        if(time.status) {leave(flags);call(state.cpu,CPU_IRQ_END,512,0);__asm__ volatile("push %0;popfq"::"r"(saved):"memory","cc");cpu->dispatching=0;return module_result(0,0);}
        wake_due(time.value);struct thread *t=state.head,*prev=NULL;
        while(t && (t->info.cpu!=current_cpu() || status(t)!=THREAD_RUNNABLE)) {prev=t;t=t->next;}
        if(!t) {leave(flags);call(state.cpu,CPU_IRQ_END,512,0);__asm__ volatile("push %0;popfq"::"r"(saved):"memory","cc");break;}
        /* Rotate the selected record to the tail for round-robin fairness. */
        if(t!=state.tail) {if(prev)prev->next=t->next;else state.head=t->next;state.tail->next=t;state.tail=t;t->next=NULL;}
        __atomic_store_n(&cpu->current,t,__ATOMIC_RELEASE);call(state.cpu,CPU_ACTIVE,t->stack.bottom,t->stack.top);set_status(t,THREAD_RUNNING);++t->info.switches;leave(flags);
        struct kernel_surface_result now=call(state.now,0,0,0);
        if(now.status || now.value>UINT64_MAX-2000000 || call(state.arm,now.value+2000000,0,0).status) {__atomic_store_n(&cpu->current,NULL,__ATOMIC_RELEASE);call(state.cpu,CPU_ACTIVE,0,0);set_status(t,THREAD_RUNNABLE);cpu->dispatching=0;call(state.cpu,CPU_IRQ_END,512,0);__asm__ volatile("push %0;popfq"::"r"(saved):"memory","cc");return module_result(0,0);}
        if(t->ready_valid && now.value>=t->ready_ns) {uint64_t wait=now.value-t->ready_ns;++cpu->timing.samples;cpu->timing.total_wait_ns+=wait;if(wait>cpu->timing.max_wait_ns)cpu->timing.max_wait_ns=wait;}
        else ++cpu->timing.clock_failures;
        call(state.cpu,CPU_IRQ_END,512,0);
        switch_context(&cpu->dispatcher,&t->context);
        call(state.cpu,CPU_IRQ_BEGIN,512,0);
        struct kernel_surface_result ready_time=call(state.now,0,0,0);
        t->ready_ns=ready_time.value;t->ready_valid=!ready_time.status;
        uint64_t transition=enter();
        __atomic_store_n(&cpu->current,NULL,__ATOMIC_RELEASE);call(state.cpu,CPU_ACTIVE,0,0);
        if(status(t)==THREAD_EXITING)set_status(t,THREAD_EXITED);
        leave(transition);call(state.cpu,CPU_IRQ_END,512,0);__asm__ volatile("push %0;popfq"::"r"(saved):"memory","cc");++done;
    }
    cpu->dispatching=0;return module_result(done,1);
}
struct kernel_surface_result threads_surface_control(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.hub) return module_result(0,0);
    if(a==THREAD_YIELD || a==THREAD_BLOCK || a==THREAD_BLOCK_UNTIL) {
        uint64_t flags;__asm__ volatile("pushfq;pop %0":"=r"(flags)::"memory");
        if(!(flags&512))return module_result(0,0);
        if(call(state.cpu,CPU_DEPTH,0,0).value || !state.cpus[current_cpu()].current) return module_result(0,0);
        if(a==THREAD_BLOCK_UNTIL && !b)return module_result(0,0);
        suspend(a==THREAD_YIELD?THREAD_RUNNABLE:THREAD_BLOCKED,a==THREAD_BLOCK_UNTIL?b:0);return module_result(1,1);
    }
    uint64_t flags=enter();
    if(a==THREAD_CURRENT) {struct thread *current=state.cpus[current_cpu()].current;uint64_t id=current?current->info.id:0;leave(flags);return module_result(id,1);}
    if(a==THREAD_TIMING && c) {*(struct thread_timing *)c=state.cpus[current_cpu()].timing;leave(flags);return module_result(1,1);}
    if(a==THREAD_DEADLINE) {uint64_t deadline=0;
        for(struct thread *t=state.head;t;t=t->next)if(t->info.cpu==current_cpu() && status(t)==THREAD_BLOCKED && t->wake_ns && (!deadline || t->wake_ns<deadline))deadline=t->wake_ns;
        leave(flags);return module_result(deadline,1);
    }
    if(a==THREAD_READY) {struct kernel_surface_result time=call(state.now,0,0,0);if(time.status){leave(flags);return time;}wake_due(time.value);size_t count=0;for(struct thread *t=state.head;t;t=t->next)if(t->info.cpu==current_cpu() && status(t)==THREAD_RUNNABLE)++count;leave(flags);return module_result(count,1);}
    struct thread *t=state.head,*prev=NULL;while(t && t->info.id!=b){prev=t;t=t->next;}
    if(!t) {leave(flags);return module_result(0,0);}int ok=0;
    if(a==THREAD_INFO && c) {*(struct thread_info *)c=(struct thread_info){t->info.id,t->info.process_id,t->info.cpu,status(t),t->info.switches,t->info.result};ok=1;}
    if(a==THREAD_WAKE && (status(t)==THREAD_RUNNING || status(t)==THREAD_RUNNABLE)) {t->wake_pending=1;ok=1;}
    if(a==THREAD_WAKE && status(t)==THREAD_BLOCKED) {struct kernel_surface_result time=call(state.now,0,0,0);t->ready_ns=time.value;t->ready_valid=!time.status;t->wake_ns=0;set_status(t,THREAD_RUNNABLE);call(state.cpu,CPU_WAKE,t->info.cpu,0);ok=1;}
    if(a==THREAD_REAP && status(t)==THREAD_EXITED && __atomic_load_n(&state.cpus[t->info.cpu].current,__ATOMIC_ACQUIRE)!=t) {
        /* Keep the record discoverable, but exclusively owned by this reaper.
         * Stack invalidation and sanitization must not hold the thread-list lock. */
        set_status(t,THREAD_REAPING);leave(flags);
        struct kernel_surface_result released=call(state.stack_free,t->stack.id,t->info.process_id,0);
        flags=enter();
        if(released.status) {set_status(t,THREAD_EXITED);leave(flags);return released;}
        prev=NULL;struct thread *at=state.head;while(at!=t) {prev=at;at=at->next;}
        if(prev)prev->next=t->next;else state.head=t->next;if(state.tail==t)state.tail=prev;
        leave(flags);return call(state.free,state.heap,(uintptr_t)t,0);
    }
    leave(flags);return module_result(ok,ok);
}
struct kernel_surface_result threads_surface_tick(MODULE_ARGS) {
    MODULE_UNUSED();if(!a || ((const struct inthandle_frame *)a)->vector!=TIMER_VECTOR)return module_result(0,0);
    if(state.hub && state.cpus[current_cpu()].current) {
        if(call(state.cpu,CPU_DEPTH,0,0).value)return module_result(THREAD_TICK_DEFERRED,1);
        suspend(THREAD_RUNNABLE,0);
    }
    return module_result(1,1);
}
