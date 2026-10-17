#ifndef CLOUDBERRY_THREADS_H
#define CLOUDBERRY_THREADS_H
#include "module.h"
#include "scheduler.h"
enum thread_tick_result {THREAD_TICK_HANDLED=1,THREAD_TICK_DEFERRED=2};
enum thread_state {THREAD_RUNNABLE=1,THREAD_RUNNING,THREAD_BLOCKED,THREAD_EXITED,THREAD_EXITING,THREAD_REAPING,THREAD_BLOCKING};
enum thread_control_op {THREAD_YIELD,THREAD_BLOCK,THREAD_WAKE,THREAD_REAP,THREAD_INFO,THREAD_READY,THREAD_TIMING,THREAD_CURRENT,THREAD_BLOCK_UNTIL,THREAD_DEADLINE};
struct thread_timing {uint64_t samples,total_wait_ns,max_wait_ns,clock_failures;};
struct thread_request {uint32_t abi,cpu;uint64_t process_id;size_t stack_bytes;uintptr_t entry;uint16_t major,minor;uint32_t reserved;struct scheduler_payload payload;uint64_t address_space;};
struct thread_info {uint64_t id,process_id;uint32_t cpu,state;uint64_t switches;struct kernel_surface_result result;};
/* init(hub,heap,cpu_count); create(request) -> ID; run(budget) -> dispatches on
 * current CPU; control(op,id,out); tick() is IRQ-only safe preemption notification.
 * ABI1 retains the existing request prefix and uses the kernel root; ABI2 adds
 * address_space (0 kernel). A nonzero space must belong to process_id and is
 * leased through REAP. The scheduler selects/restores roots on every switch.
 * create copies optional payload, pins CPU, validates entry/version against hub.
 * block/yield only from a running thread outside critical sections; wake by ID.
 * BLOCKING means the context is still switching away; BLOCKED is published
 * only after its dispatcher restores the kernel root and clears current/stack.
 * wake also credits RUNNING/RUNNABLE/BLOCKING to prevent lost wakes.
 * BLOCK_UNTIL uses b=absolute clock deadline; DEADLINE returns earliest blocked
 * deadline on current CPU (0 none); CURRENT returns current ID (0 dispatcher).
 * tick returns DEFERRED when a running thread cannot switch inside a critical
 * section; timer must rearm a short CPU-local retry without foreground calls.
 * reap only EXITED; executing stacks are never freed. Owner 0 is kernel work. */
struct kernel_surface_result threads_surface_init(MODULE_ARGS);
struct kernel_surface_result threads_surface_create(MODULE_ARGS);
struct kernel_surface_result threads_surface_run(MODULE_ARGS);
struct kernel_surface_result threads_surface_control(MODULE_ARGS);
struct kernel_surface_result threads_surface_tick(MODULE_ARGS);
#endif
