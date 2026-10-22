#ifndef CLOUDBERRY_THREADS_H
#define CLOUDBERRY_THREADS_H
#include "module.h"
#include "scheduler.h"
enum thread_tick_result {THREAD_TICK_HANDLED=1,THREAD_TICK_DEFERRED=2};
enum thread_state {THREAD_RUNNABLE=1,THREAD_RUNNING,THREAD_BLOCKED,THREAD_EXITED,THREAD_EXITING,THREAD_REAPING,THREAD_BLOCKING};
enum thread_control_op {THREAD_YIELD,THREAD_BLOCK,THREAD_WAKE,THREAD_REAP,THREAD_INFO,THREAD_READY,THREAD_TIMING,THREAD_CURRENT,THREAD_BLOCK_UNTIL,THREAD_DEADLINE,THREAD_WAIT,THREAD_SELECT_CPU,THREAD_ACCOUNTING,THREAD_LOAD,THREAD_USER_RETURN};
enum thread_flags {THREAD_MIGRATABLE=1};
struct thread_load {uint64_t runnable,running,blocked,live,migrations_in,migrations_out,wait_samples,total_wait_ns,max_wait_ns;};
struct thread_account_info {uint64_t runtime_ns,timestamp_ns;uint32_t state,cpu,cpus,flags;};
/* ACCOUNTING(thread ID,out); ID 0 queries CPU count/clock only. flags bit0
 * means cumulative timing is valid. Runtime includes execution inside syscalls. */
struct thread_timing {uint64_t samples,total_wait_ns,max_wait_ns,clock_failures;};
struct thread_request {uint32_t abi,cpu;uint64_t process_id;size_t stack_bytes;uintptr_t entry;uint16_t major,minor;uint32_t reserved;struct scheduler_payload payload;uint64_t address_space;uint64_t flags;};
struct thread_info {uint64_t id,process_id;uint32_t cpu,state;uint64_t switches;struct kernel_surface_result result;};
/* init(hub,heap,cpu_count); create(request) -> ID; run(budget) -> dispatches on
 * current CPU; control(op,id,out); tick() is IRQ-only safe preemption notification.
 * ABI1 retains the existing request prefix and uses the kernel root; ABI2 adds
 * address_space (0 kernel). A nonzero space must belong to process_id and is
 * leased through REAP. The scheduler selects/restores roots on every switch.
 * ABI3 appends flags: THREAD_MIGRATABLE requires nonzero owner/address_space.
 * ABI1/2 and ABI3 flags=0 preserve explicit affinity. Only detached clean
 * user-return continuations may be stolen; kernel continuations stay pinned.
 * create copies optional payload and validates entry/version against hub.
 * block/yield only from a running thread outside critical sections; wake by ID.
 * BLOCKING means the context is still switching away; BLOCKED is published
 * only after its dispatcher restores the kernel root and clears current/stack.
 * wake also credits RUNNING/RUNNABLE/BLOCKING to prevent lost wakes.
 * BLOCK_UNTIL uses b=absolute clock deadline; DEADLINE returns earliest blocked
 * deadline on current CPU (0 none); CURRENT returns current ID (0 dispatcher).
 * tick returns DEFERRED when a running thread cannot switch inside a critical
 * section; timer must rearm a short CPU-local retry without foreground calls.
 * LOAD(cpu,out thread_load) reports runnable/running/blocked/live pressure,
 * migrations and cumulative runnable wait metrics. SELECT_CPU chooses pressure
 * first, then live occupancy. USER_RETURN is trusted IF-clear CPL3-return work.
 * WAIT(id) blocks until that thread is fully EXITED, outside locks with IF set.
 * Embedded join registrations prevent target reap until joiners observe exit.
 * reap only EXITED; executing stacks are never freed. Owner 0 is kernel work. */
/* entry_stack(top): trusted current-thread RSP0 binding, with IF clear.
 * Scheduler restores it with the saved context; zero on kernel dispatchers. */
struct kernel_surface_result threads_surface_entry_stack(MODULE_ARGS);
struct kernel_surface_result threads_surface_init(MODULE_ARGS);
struct kernel_surface_result threads_surface_create(MODULE_ARGS);
struct kernel_surface_result threads_surface_run(MODULE_ARGS);
struct kernel_surface_result threads_surface_control(MODULE_ARGS);
struct kernel_surface_result threads_surface_tick(MODULE_ARGS);
#endif
