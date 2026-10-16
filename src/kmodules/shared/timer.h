#ifndef CLOUDBERRY_KMODULE_TIMER_H
#define CLOUDBERRY_KMODULE_TIMER_H
#include "module.h"
#include "inthandle.h"
#define TIMER_VECTOR 224u
enum timer_mode { TIMER_XAPIC=1, TIMER_X2APIC=2 };
struct timer_info {
    uint32_t vector,mode,armed,reserved;
    uint64_t frequency_hz,interrupts,deadline_ns,pending;
};
/* Per-CPU local APIC one-shots; init(hub,heap,cpu_count) calibrates the current
 * CPU against clock.now_ns. BSP allocates notification records before AP start.
 * arm(absolute_deadline_ns) -> programmed count; overdue deadlines use count 1.
 * Long deadlines use UINT32_MAX chunks; scheduler checks time again on wake.
 * stop() -> 1; take() -> number of pending, coalesced wake notifications.
 * info(out timer_info) -> 1. All foreground surfaces preserve the caller's IF.
 * interrupt is a claimed CPU-entry callback, not a schedulable method.
 * The IRQ callback allocates nothing, invokes no scheduler/clock surfaces, and
 * acknowledges the local APIC exactly once, then invokes threads.tick for safe
 * context preemption (allocation-free, uses only CPU-local context and atomic lifecycle state).
 * Deferred preemption rearms a relative 2 ms retry without a clock call; this
 * retains the original expired deadline_ns until the next foreground arm/stop.
 * Idle CPUs do not acquire periodic ticks. Spurious delivery belongs to apic.
 * APIC owns controller-wide setup and spurious delivery. This module only
 * programs timer registers and acknowledges its own timer interrupt. */
struct kernel_surface_result timer_surface_init(MODULE_ARGS);
struct kernel_surface_result timer_surface_arm(MODULE_ARGS);
struct kernel_surface_result timer_surface_stop(MODULE_ARGS);
struct kernel_surface_result timer_surface_take(MODULE_ARGS);
struct kernel_surface_result timer_surface_info(MODULE_ARGS);
struct kernel_surface_result timer_surface_interrupt(MODULE_ARGS);
#endif
