#include "../kmodules/shared/event.h"
#include <stdbool.h>
#include "../kmodules/shared/scheduler.h"
#include "../kmodules/shared/timer.h"
#include "../kmodules/shared/apic.h"
#include "../kmodules/shared/threads.h"
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static bool failure(unsigned line) {
    const char *s="FAIL events-boot.c line ";while (*s) __asm__ volatile("outb %0,$0xe9" :: "a"(*s++));
    char digits[10];unsigned n=0;do {digits[n++]=(char)('0'+line%10);line/=10;} while (line);
    while (n) __asm__ volatile("outb %0,$0xe9" :: "a"(digits[--n]));
    __asm__ volatile("outb %0,$0xe9" :: "a"((unsigned char)'\n'));return false;
}
static void measure(const char *label,uint64_t value) {
    while(*label)__asm__ volatile("outb %0,$0xe9"::"a"(*label++));
    char digits[24];unsigned n=0;do{digits[n++]='0'+value%10;value/=10;}while(value);
    while(n)__asm__ volatile("outb %0,$0xe9"::"a"(digits[--n]));
    __asm__ volatile("outb %0,$0xe9"::"a"((unsigned char)'\n'));
}
bool kernel_events_test(const struct kernel_surfaces *hub) {
    kernel_surface_call submit=kernel_surface_find_version(hub,"scheduler.submit",3,0),
        result=kernel_surface_find_version(hub,"scheduler.result",2,0),
        next=kernel_surface_find_version(hub,"scheduler.next_deadline",1,0),
        now=kernel_surface_find_version(hub,"clock.now_ns",1,0),
        timestamp=kernel_surface_find_version(hub,"probe.timestamp",1,0),
        info_call=kernel_surface_find_version(hub,"timer.info",2,0),
        take=kernel_surface_find_version(hub,"timer.take",1,0),
        stop=kernel_surface_find_version(hub,"timer.stop",1,0),
        bind=kernel_surface_find_version(hub,"inthandle.bind",1,0),
        claim=kernel_surface_find_version(hub,"inthandle.claim",1,0),
        release=kernel_surface_find_version(hub,"inthandle.release",1,0),
        dispatch=kernel_surface_find_version(hub,"inthandle.dispatch",1,1);
    if (!submit || !result || !next || !now || !timestamp || !info_call || !take || !stop || !bind || !claim || !release || !dispatch) return failure(__LINE__);
    kernel_surface_call init=kernel_surface_find_version(hub,"event.init",1,0),
        step=kernel_surface_find_version(hub,"event.step",1,0);
    if (!init || !step || CALL(init,hub,0,0).status || CALL(step,0,0,0).status!=KERNEL_SURFACE_FAILED) return failure(__LINE__);
    struct timer_info before,after;
    if (CALL(info_call,&before,0,0).status || !before.frequency_hz || before.vector!=TIMER_VECTOR) return failure(__LINE__);
    struct inthandle_frame frame={.vector=TIMER_VECTOR};
    if (CALL(bind,TIMER_VECTOR,"probe.failed",0).status!=KERNEL_SURFACE_FAILED ||
        CALL(release,TIMER_VECTOR,1,0).status!=KERNEL_SURFACE_FAILED ||
        claim(TIMER_VECTOR,(uintptr_t)"probe.failed",0,1,0,0).status!=KERNEL_SURFACE_FAILED ||
        CALL(dispatch,&frame,0,0).status!=KERNEL_SURFACE_FAILED) return failure(__LINE__);
    frame.vector=APIC_SPURIOUS_VECTOR;
    if (CALL(bind,APIC_SPURIOUS_VECTOR,"probe.failed",0).status!=KERNEL_SURFACE_FAILED ||
        CALL(release,APIC_SPURIOUS_VECTOR,1,0).status!=KERNEL_SURFACE_FAILED ||
        CALL(dispatch,&frame,0,0).status!=KERNEL_SURFACE_FAILED) return failure(__LINE__);
    kernel_surface_call scheduler_info=kernel_surface_find_version(hub,"scheduler.info",1,0),
        event_info=kernel_surface_find_version(hub,"event.info",1,0),
        threads=kernel_surface_find_version(hub,"threads.run",1,0);
    struct scheduler_diagnostics scheduler_before,scheduler;struct event_diagnostics event_before,event;
    if(!scheduler_info || !event_info || !threads || CALL(scheduler_info,&scheduler_before,0,0).status || CALL(event_info,&event_before,0,0).status)return failure(__LINE__);
    event=event_before;unsigned submitted=0;
    /* Already-due work must run without requiring an idle transition. Future
     * deadlines exercise actual idle/IRQ wakeup. Cold worker construction or a
     * paused vCPU can consume a deadline before idle, so observe the path rather
     * than assuming one HLT per callback. Extra attempts are coverage-bounded. */
    for(unsigned iteration=0;iteration<8;++iteration) {
        if(submitted>=4 && event.wakes>event_before.wakes && event.notifications>event_before.notifications)break;
        struct kernel_surface_result time=CALL(now,0,0,0);if(time.status)return failure(__LINE__);
        uint64_t deadline=iteration<4 && (iteration&1)?1:time.value+50000000;
        struct scheduler_request request={.abi=SCHEDULER_REQUEST_ABI,.kind=SCHEDULER_METHOD,.process_id=0,
            .deadline_ns=deadline,.entry_vector=(uintptr_t)timestamp,.entry_major=1};
        struct kernel_surface_result queued=CALL(submit,&request,0,0);if(queued.status || !queued.value)return failure(__LINE__);
        struct scheduler_completion completion;unsigned steps;
        for(steps=0;steps<64;++steps) {
            if(CALL(step,16,0,0).status || CALL(result,queued.value,&completion,0).status)return failure(__LINE__);
            if(completion.state==SCHEDULER_COMPLETE)break;
        }
        if(steps==64 || completion.result.status || completion.result.value<deadline || CALL(result,queued.value,&completion,1).status)return failure(__LINE__);
        ++submitted;
        /* Scheduler completion is published before its worker updates event
         * counters. Finish runnable bookkeeping without entering an unbounded
         * event.step/HLT after the final deadline has been reaped. */
        unsigned drain;for(drain=0;drain<64;++drain) {
            if(CALL(event_info,&event,0,0).status)return failure(__LINE__);
            if(event.jobs>=event_before.jobs+submitted)break;
            if(CALL(threads,1,0,0).status)return failure(__LINE__);
        }
        if(drain==64)return failure(__LINE__);
    }
    __asm__ volatile("cli" ::: "memory");
    if(CALL(info_call,&after,0,0).status || CALL(take,0,0,0).status || CALL(stop,0,0,0).status)return failure(__LINE__);
    uint64_t deadline;if(CALL(next,&deadline,0,0).value!=SCHEDULER_IDLE)return failure(__LINE__);
    if(CALL(scheduler_info,&scheduler,0,0).status || CALL(event_info,&event,0,0).status)return failure(__LINE__);
    measure("MEASURE event submitted jobs: ",submitted);
    measure("MEASURE event completed jobs: ",event.jobs-event_before.jobs);
    measure("MEASURE event idle entries: ",event.idle_entries-event_before.idle_entries);
    measure("MEASURE event wakes: ",event.wakes-event_before.wakes);
    measure("MEASURE event notifications: ",event.notifications-event_before.notifications);
    measure("MEASURE event failures: ",event.failures-event_before.failures);
    measure("MEASURE event timer interrupts: ",after.interrupts-before.interrupts);
    measure("MEASURE event timed callbacks: ",scheduler.timed-scheduler_before.timed);
    if(submitted<4 || scheduler.completed<scheduler_before.completed+submitted || scheduler.timed<scheduler_before.timed+submitted ||
        scheduler.timing_failures!=scheduler_before.timing_failures || scheduler.late_jobs<scheduler_before.late_jobs+2 ||
        event.jobs!=event_before.jobs+submitted || event.wakes<=event_before.wakes ||
        event.idle_entries-event_before.idle_entries!=event.wakes-event_before.wakes ||
        event.notifications<=event_before.notifications || event.failures!=event_before.failures || after.interrupts<=before.interrupts)return failure(__LINE__);
    const char *message="PASS event loop: real APIC IRQs/EOI, protected vectors, deadline dispatch and idle wake\n";
    while (*message) __asm__ volatile("outb %0,$0xe9" :: "a"(*message++));
    return true;
}
