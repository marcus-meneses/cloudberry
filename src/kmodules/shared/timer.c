#include "timer.h"
#include "timer-platform.h"
#include "apic.h"
#include "threads.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
#define MASK (1u<<16)
static struct {
    const struct kernel_surfaces *hub;
    kernel_surface_call now,cpu,tick;
    struct timer_local {volatile struct timer_info info;int initialized;} bootstrap,*locals;
    size_t count;
} state;
static struct timer_local *local(void) {
    if(!state.locals)return &state.bootstrap;
    size_t cpu=CALL(state.cpu,CPU_CURRENT,0,0).value;
    return cpu<state.count?&state.locals[cpu]:&state.bootstrap;
}
struct kernel_surface_result timer_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)a;
    if (local()->initialized) return module_result(1,state.hub==hub);
    if (!hub || hub->magic!=KERNEL_SURFACES_MAGIC || hub->major!=2) return module_result(0,0);
    kernel_surface_call now=kernel_surface_find_version(hub,"clock.now_ns",1,0),
        claim=kernel_surface_find_version(hub,"inthandle.claim",1,0),
        release=kernel_surface_find_version(hub,"inthandle.release",1,0),
        controller=kernel_surface_find_version(hub,"apic.info",1,0);
    struct apic_info platform;
    if (!now || !claim || !release || !controller || CALL(controller,&platform,0,0).status) return module_result(0,0);
    uint64_t flags=timer_irq_save();
    int first=state.hub==NULL;
    if(first)state.cpu=kernel_surface_find(hub,"cpu.control");
    if(first && c>1) {
        kernel_surface_call alloc=kernel_surface_find(hub,"mem.calloc");
        state.cpu=kernel_surface_find(hub,"cpu.control");
        if(!alloc || !state.cpu || !b || c>SIZE_MAX/sizeof(struct timer_local)) {timer_irq_restore(flags);return module_result(0,0);}
        struct kernel_surface_result allocated=CALL(alloc,b,c,sizeof(struct timer_local));
        if(allocated.status) {timer_irq_restore(flags);return module_result(0,0);}
        state.locals=(void *)allocated.value;state.count=c;
    }
    if (first && claim(TIMER_VECTOR,(uintptr_t)"timer.interrupt",(uintptr_t)&state,(uintptr_t)&state,0,0).status) {
        timer_irq_restore(flags);return module_result(0,0);
    }
    if(first) timer_platform_configure(platform.address,platform.mode);
    timer_register_write(0x320,TIMER_VECTOR|MASK);
    timer_register_write(0x380,0);
    timer_register_write(0x3e0,3); /* Divide by 16. */
    struct kernel_surface_result start=CALL(now,0,0,0),end=start;
    if (start.status) goto timer_fail;
    timer_register_write(0x380,UINT32_MAX);
    unsigned poll;
    for (poll=0;poll<1000000;++poll) {
        end=CALL(now,0,0,0);
        if (end.status || end.value<start.value) goto timer_fail;
        if (end.value-start.value>=10000000) break;
        __asm__ volatile("pause");
    }
    uint32_t remaining=timer_register_read(0x390);
    if (poll==1000000 || end.value-start.value>100000000 || remaining==UINT32_MAX || !remaining) goto timer_fail;
    uint64_t hz=(uint64_t)(UINT32_MAX-remaining)*1000000000/(end.value-start.value);
    if (!hz || hz>1000000000) goto timer_fail;
    timer_register_write(0x380,0);
    if(first) {state.hub=hub;state.now=now;state.tick=kernel_surface_find(hub,"threads.tick");}
    local()->info=(struct timer_info){.vector=TIMER_VECTOR,
        .mode=platform.mode,.frequency_hz=hz};local()->initialized=1;
    timer_irq_restore(flags);return module_result(1,1);
timer_fail:
    timer_register_write(0x320,TIMER_VECTOR|MASK);timer_register_write(0x380,0);
    if(first) CALL(release,TIMER_VECTOR,&state,0);
    timer_irq_restore(flags);return module_result(0,0);
}
struct kernel_surface_result timer_surface_arm(MODULE_ARGS) {
    MODULE_UNUSED();if (!local()->initialized) return module_result(0,0);
    uint64_t flags=timer_irq_save();struct kernel_surface_result now=CALL(state.now,0,0,0);
    if (now.status) {timer_irq_restore(flags);return module_result(0,0);}
    uint64_t delta=a>now.value?a-now.value:0,hz=local()->info.frequency_hz,count;
    if (delta/1000000000>UINT32_MAX/hz) count=UINT32_MAX;
    else {
        count=(delta/1000000000)*hz+((delta%1000000000)*hz+999999999)/1000000000;
        if (count>UINT32_MAX) count=UINT32_MAX;
        if (!count) count=1;
    }
    local()->info.deadline_ns=a;local()->info.armed=1;
    timer_register_write(0x320,TIMER_VECTOR);timer_register_write(0x380,(uint32_t)count);
    timer_irq_restore(flags);return module_result(count,1);
}
struct kernel_surface_result timer_surface_stop(MODULE_ARGS) {
    MODULE_UNUSED();if (!local()->initialized) return module_result(0,0);
    uint64_t flags=timer_irq_save();timer_register_write(0x320,TIMER_VECTOR|MASK);timer_register_write(0x380,0);
    local()->info.armed=0;local()->info.deadline_ns=0;timer_irq_restore(flags);return module_result(1,1);
}
struct kernel_surface_result timer_surface_take(MODULE_ARGS) {
    MODULE_UNUSED();if (!local()->initialized) return module_result(0,0);
    uint64_t flags=timer_irq_save(),pending=local()->info.pending;local()->info.pending=0;timer_irq_restore(flags);return module_result(pending,1);
}
struct kernel_surface_result timer_surface_info(MODULE_ARGS) {
    MODULE_UNUSED();if (!local()->initialized || !a) return module_result(0,0);
    uint64_t flags=timer_irq_save();*(struct timer_info *)a=local()->info;timer_irq_restore(flags);return module_result(1,1);
}
struct kernel_surface_result timer_surface_interrupt(MODULE_ARGS) {
    MODULE_UNUSED();const struct inthandle_frame *frame=(const void *)a;
    if (!local()->initialized || !frame || frame->vector!=TIMER_VECTOR || b!=(uintptr_t)&state || c!=INTHANDLE_CPU_ENTRY) return module_result(0,0);
    if (local()->info.pending!=UINT64_MAX) ++local()->info.pending;
    if (local()->info.interrupts!=UINT64_MAX) ++local()->info.interrupts;
    local()->info.armed=0;timer_register_write(0xb0,0);
    if(state.cpu) CALL(state.cpu,CPU_TIMER,0,0);
    if(state.tick) {
        struct kernel_surface_result tick=CALL(state.tick,frame,0,0);
        if(!tick.status && tick.value==THREAD_TICK_DEFERRED) {
            /* A one-shot consumed while switching is inhibited must not lose
             * the process's only preemption source. CPU-local relative retry;
             * no allocator, foreground lock or clock call from this ISR. */
            uint64_t count=(local()->info.frequency_hz+499)/500;if(!count)count=1;
            local()->info.armed=1;timer_register_write(0x320,TIMER_VECTOR);timer_register_write(0x380,(uint32_t)count);
        }
    }
    return module_result(1,1);
}
