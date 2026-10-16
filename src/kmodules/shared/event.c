#include "event.h"
#include "diagnostics.h"
#include "scheduler.h"
#include "threads.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static struct {
    const struct kernel_surfaces *hub;
    kernel_surface_call run,next,arm,stop,take,threads,thread_control,cpu,irq_drain,irq_pending,create;
    uint64_t workers[2];int worker_failed;
    struct event_diagnostics info;
    int initialized,stepping;
} state;
struct kernel_surface_result event_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)a;
    if (state.initialized) return module_result(1,state.hub==hub);
    if (!hub || hub->magic!=KERNEL_SURFACES_MAGIC || hub->major!=2) return module_result(0,0);
    state.run=kernel_surface_find_version(hub,"scheduler.run",1,0);
    state.next=kernel_surface_find_version(hub,"scheduler.next_deadline",1,0);
    state.arm=kernel_surface_find_version(hub,"timer.arm",1,0);
    state.stop=kernel_surface_find_version(hub,"timer.stop",1,0);
    state.take=kernel_surface_find_version(hub,"timer.take",1,0);
    if (!state.run || !state.next || !state.arm || !state.stop || !state.take) return module_result(0,0);
    state.irq_drain=kernel_surface_find_version(hub,"irq.drain",1,0);state.irq_pending=kernel_surface_find_version(hub,"irq.pending",1,0);
    state.thread_control=kernel_surface_find_version(hub,"threads.control",1,0);state.threads=kernel_surface_find_version(hub,"threads.run",1,0);state.cpu=kernel_surface_find_version(hub,"cpu.control",1,0);
    state.create=kernel_surface_find_version(hub,"threads.create",1,0);
    if(!state.irq_drain || !state.irq_pending || !state.create || !state.threads || !state.thread_control || !state.cpu)return module_result(0,0);
    state.hub=hub;state.initialized=1;return module_result(1,1);
}
/* Two independent preemptible contexts keep a slow device completion from
 * monopolizing the event dispatcher or the deadline callback queue. */
struct kernel_surface_result event_surface_worker(MODULE_ARGS) {
    MODULE_UNUSED();if(a>1 || !state.initialized || !state.workers[a] || CALL(state.thread_control,THREAD_CURRENT,0,0).value!=state.workers[a])return module_result(0,0);
    for(;;) {
        struct kernel_surface_result r=CALL(a?state.irq_drain:state.run,1,0,0);
        if(r.status) {__atomic_store_n(&state.worker_failed,1,__ATOMIC_RELEASE);return r;}
        if(!a && r.value) {diagnostic_add(&state.info.batches,1);diagnostic_add(&state.info.jobs,r.value);}
        if(CALL(state.thread_control,THREAD_BLOCK,0,0).status) {__atomic_store_n(&state.worker_failed,1,__ATOMIC_RELEASE);return module_result(0,0);}
    }
}
static int workers(void) {
    kernel_surface_call entry=kernel_surface_find_version(state.hub,"event.worker",1,0);
    if(!entry)return 0;
    for(unsigned i=0;i<2;++i)if(!state.workers[i]) {
        struct thread_request r={.abi=1,.cpu=0,.stack_bytes=16384,.entry=(uintptr_t)entry,.major=1};
        r.payload.arguments[0]=i;
        struct kernel_surface_result created=CALL(state.create,&r,0,0);
        if(created.status)return 0;
        state.workers[i]=created.value;
    }
    return 1;
}
static struct kernel_surface_result step(size_t budget) {
    (void)budget;uint64_t irq_flags=0;
    diagnostic_add(&state.info.steps,1);
    __asm__ volatile("sti" ::: "memory");
    if(!workers() || __atomic_load_n(&state.worker_failed,__ATOMIC_ACQUIRE))goto fail;
    uint64_t due=0;
    struct kernel_surface_result ready=CALL(state.next,&due,0,0);
    if(ready.status)goto fail;
    if(ready.value==SCHEDULER_READY && CALL(state.thread_control,THREAD_WAKE,state.workers[0],0).status)goto fail;
    struct kernel_surface_result irq=CALL(state.irq_pending,0,0,0);
    if(irq.status)goto fail;
    if(irq.value && CALL(state.thread_control,THREAD_WAKE,state.workers[1],0).status)goto fail;
    struct kernel_surface_result dispatched=CALL(state.threads,1,0,0);
    if(dispatched.status)goto fail;
    if(dispatched.value)return dispatched;
    /* IF stays clear from pending consumption through queue inspection and arm.
     * STI's interrupt shadow covers HLT, avoiding a lost interrupt before idle. */
    __asm__ volatile("pushfq;pop %0;cli":"=r"(irq_flags)::"memory");
    if(state.cpu)CALL(state.cpu,CPU_IRQ_BEGIN,irq_flags,0);
    struct kernel_surface_result pending=CALL(state.take,0,0,0);
    if (pending.status) goto fail;
    diagnostic_add(&state.info.notifications,pending.value);
    uint64_t deadline=0;
    struct kernel_surface_result next=CALL(state.next,&deadline,0,0);
    if (next.status) goto fail;
    if (next.value==SCHEDULER_READY) {if(state.cpu)CALL(state.cpu,CPU_IRQ_END,irq_flags,0);__asm__ volatile("sti" ::: "memory");return module_result(0,1);}
    struct kernel_surface_result sleep=CALL(state.thread_control,THREAD_DEADLINE,0,0);
    if(sleep.status)goto fail;
    if(sleep.value && (next.value==SCHEDULER_IDLE || sleep.value<deadline)) {deadline=sleep.value;next.value=SCHEDULER_DEADLINE;}
    if (next.value==SCHEDULER_DEADLINE) {
        if (CALL(state.arm,deadline,0,0).status) goto fail;
    } else if (next.value==SCHEDULER_IDLE) {
        if (CALL(state.stop,0,0,0).status) goto fail;
    } else goto fail;
    if(state.thread_control) {struct kernel_surface_result ready=CALL(state.thread_control,THREAD_READY,0,0);
        if(ready.status)goto fail;
        if(ready.value){if(state.cpu)CALL(state.cpu,CPU_IRQ_END,irq_flags,0);__asm__ volatile("sti":::"memory");return module_result(0,1);}}
    if(state.irq_pending) {struct kernel_surface_result pending=CALL(state.irq_pending,0,0,0);if(pending.status)goto fail;
        if(pending.value) {if(state.cpu)CALL(state.cpu,CPU_IRQ_END,irq_flags,0);__asm__ volatile("sti":::"memory");return module_result(0,1);}}
    diagnostic_add(&state.info.idle_entries,1);
    if(state.cpu)CALL(state.cpu,CPU_IRQ_END,irq_flags,0);
    __asm__ volatile("sti; hlt" ::: "memory");
    diagnostic_add(&state.info.wakes,1);
    return module_result(0,1);
fail:
    if(irq_flags && state.cpu)CALL(state.cpu,CPU_IRQ_END,irq_flags,0);
    diagnostic_add(&state.info.failures,1);
    __asm__ volatile("cli" ::: "memory");return module_result(0,0);
}
struct kernel_surface_result event_surface_step(MODULE_ARGS) {
    MODULE_UNUSED();if ((state.cpu && CALL(state.cpu,0,0,0).value) || !state.initialized || !a || __atomic_exchange_n(&state.stepping,1,__ATOMIC_ACQ_REL)) return module_result(0,0);
    struct kernel_surface_result r=step(a);__atomic_store_n(&state.stepping,0,__ATOMIC_RELEASE);return r;
}
struct kernel_surface_result event_surface_run(MODULE_ARGS) {
    MODULE_UNUSED();if ((state.cpu && CALL(state.cpu,0,0,0).value) || !state.initialized || !a || __atomic_exchange_n(&state.stepping,1,__ATOMIC_ACQ_REL)) return module_result(0,0);
    for (;;) {struct kernel_surface_result r=step(a);if (r.status) break;}
    __atomic_store_n(&state.stepping,0,__ATOMIC_RELEASE);return module_result(0,0);
}

struct kernel_surface_result event_surface_info(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.initialized || !a) return module_result(0,0);
    if(state.cpu && CALL(state.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);
    uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;
    *(struct event_diagnostics *)a=state.info;CALL(state.cpu,CPU_LEAVE,flags,0);return module_result(1,1);
}
