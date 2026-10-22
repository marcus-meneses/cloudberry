# Kernel threads

`threads.kmod` adds independent kernel execution contexts. A thread entry is a
versioned hub method; ordinary calls to that method remain synchronous calls.
The callback scheduler remains a separate run-to-completion queue.

`threads.init(hub,heap,cpu_count)` allocates CPU-local dispatcher state before AP
startup. `threads.create(request)` validates the exact mediated entry/major/minor,
copies register arguments and optional payload, allocates a mem-managed guarded
stack and returns a monotonically increasing ID. Embedded payload pointers stay
borrowed. Requests specify a CPU, process ID (zero is kernel-owned) and stack
body size. Bodies are page-rounded, committed immediately, and must be at least
one page; there is no fixed stack-size or thread-count ceiling.

Request ABI1 uses the kernel root. ABI2 appends an optional `address_space` ID,
which must belong to `process_id`. Creation leases it through REAP. All context
switches, including timer preemption and yield/block/exit, select the saved root;
dispatchers restore the kernel root. These are still kernel-mode threads, not
user processes. See [private address spaces](mem-spaces.md).

`threads.run(budget)` dispatches at most budget contexts assigned to the calling
CPU (including eligible user contexts stolen from an overloaded CPU). Runnable records rotate to the list tail. Each dispatch arms a 2 ms local
APIC one-shot. Normal C contexts save RSP, callee-saved registers and flags;
hardware entry also saves the interrupted general-register frame. An IRQ can
suspend a thread and return to its dispatcher; the retained interrupt frame is
restored when the context resumes. Kernel continuations stay on their original CPU;
clean user-return continuations may migrate as described below. CPU-local current/dispatcher transitions
run with IRQs disabled. No stack is freed while still executing.

`threads.control(op,id,out)` supports:

- YIELD: current running thread becomes runnable and suspends.
- BLOCK: current running thread passes through BLOCKING, then blocks until WAKE(id).
- WAKE: blocked becomes runnable and sends a wake IPI; RUNNING/RUNNABLE receives
  one coalesced wake credit, consumed by its next block to avoid a lost wake.
  BLOCKING also receives a credit, consumed by dispatcher completion to make the
  saved context runnable without exposing it before the switch finishes.
- BLOCK_UNTIL: block until WAKE or absolute nanosecond deadline passed as id.
- DEADLINE: earliest blocked-thread deadline on the calling CPU, zero if none.
- CURRENT: current thread ID, zero on the dispatcher.
- INFO: copy ID, process owner, CPU, lifecycle state, dispatch count and result.
- REAP: free an exited thread's stack, copied payload and record; stale IDs fail.
- READY: local runnable work plus eligible remote work; used before idle.
- SELECT_CPU: choose runnable/running pressure first, with live occupancy as tie-breaker.
- LOAD(cpu,out): runnable/running/blocked/live counts, migrations and wait metrics.

Yield/block fail outside a thread, with IRQs disabled or inside a kernel critical
section. BLOCKING is published before switching away; BLOCKED is committed under
the thread-list lock only after the dispatcher restores its root/context and
clears the current thread and active-stack markers. Thus a remote BLOCKED
observation no longer precedes context detachment. Other threads can still use
the same address space; BLOCKED does not imply process-wide inactivity. A wake
during BLOCKING commits RUNNABLE instead, clearing the sleep deadline and credit.
Existing state values stay unchanged; BLOCKING is appended to the enum. Exiting
passes through EXITING while switching away, then EXITED on the dispatcher stack;
only EXITED can be reaped. REAPING claims exclusive cleanup ownership while
stack unmapping/sanitization runs outside the thread-list lock; failed cleanup
returns the record to EXITED for retry. If mem reports a failed allocation rollback, create
propagates its failure/cleanup ID for retry through mem.stack_free with the same
owner; a failed result is never a thread ID. There is no arbitrary kernel-thread cancellation. THREAD_WAIT is a kernel join;
user-process termination and eligible user-context migration are described below. Process IDs are ownership
metadata; scheduler.remove_process removes callbacks, not threads.

`threads.tick(frame)` is timer's allocation-free IRQ hook, after EOI. It suspends
only an active thread outside a critical section. Lifecycle publication is
atomic; the IRQ hook takes no foreground lock. Context code and all current
kmodules compile with general registers only. The [xstate](xstate.md) module
eagerly saves/restores x87/SSE/available AVX for threads and dispatchers; interrupt
infrastructure must continue to avoid floating-point/vector instructions. IRQ callbacks otherwise return directly and must never
use allocating or potentially blocking foreground methods.

Short mutations use recursive `cpu.control(ENTER/LEAVE)` with IF preservation.
Do not suspend while holding a lock, or call a thread executor from an ISR.
Existing mem/memory, clock, mmio, hwenum and callback scheduler methods are
synchronized through kernel mediation. The clock has its own ordered lock.
A scheduler callback runs outside its queue lock, synchronously on its caller;
the event module invokes callbacks on its dedicated preemptible worker. Thread
execution itself does not hold either lock. New kmodules must protect their own
shared mutable state and declare their IRQ/reentrancy contract.

The BSP event loop dispatches one thread context per step. Separate workers
execute scheduler callbacks and IRQ notifications; long callbacks do not retain
the event stack. Both BSP and AP idle paths arm blocked-thread deadlines. AP loops
run assigned threads (including eligible stolen user work) and idle with adjacent STI/HLT when empty. READY is
checked under disabled IRQs to avoid losing a wake before BSP idle. Empty loops
have no recurring polling timer.

`make -C src test-threads-smp` uses isolated one/four-CPU TCG boots. A spinning
thread waits for a peer on each CPU, proving timer preemption without voluntary
yields. Checks include stack canaries, callback progress alongside busy threads,
block/wake, timed blocking on every CPU, wake-before-block credits, rejection of yield inside a critical section, stale IDs, concurrent
page allocation and complete stack/payload teardown. Probes never enter candidate
media. Later AMD-host four-CPU KVM acceptance validates guest x87/SSE/AVX
preemption and x2APIC; see ../../tests/README.md. Native Intel CPU and physical
execution remain unqualified.


Private construction in create occurs outside the thread-list lock; a short
publication step assigns the ID and links the completed record. TIMING reports
per-CPU runnable waiting in clock nanoseconds, including missing/regressing
sample counts. See profiling.md for coverage and elapsed-time limitations.

A tick received while a running thread's critical depth inhibits switching returns
THREAD_TICK_DEFERRED. Timer rearms a CPU-local relative retry, so consuming a
one-shot during an IRQ-enabled lock wait cannot permanently remove preemption.
No-active-thread ticks remain one-shot; idle behavior is unchanged.

Thread records lease their entry provider from successful creation through reap,
including blocked/running/exited states. A failed create releases its lease; an
unsuccessful reap retains it. New threads for a draining provider fail. The mediation
gate counts an active call across thread preemption/blocking until it returns.
See [hub lifetime](../../kernel/hub.md); this does not add forced cancellation or
unmapping of executable providers.

ACCOUNTING (appended control operation 12) copies cumulative scheduled runtime,
current timestamp, state, assigned CPU, CPU count and validity. ID zero queries
only clock/CPU count. Dispatch begins an interval and dispatcher return closes
it; blocked time is excluded, active slices can be sampled, and clock regression
or failure invalidates the cumulative measurement. Totals saturate on overflow.
No new timestamp reads are added to the dispatch path: accounting uses its
existing dispatch/return clock samples. This includes the thread's syscall time,
not CPU-local dispatcher work or other kernel threads.


## Placement and conservative user migration

Request ABI3 appends `flags`; `THREAD_MIGRATABLE` requires a process-owned private
address space. ABI1/2 and zero flags preserve affinity. Applications opts ordinary
apps into process ABI3; boot providers, the lifecycle manager and kernel workers
remain pinned. Each process still has one user thread.

Timer delivery from CPL3 marks preemption pending, acknowledges the timer and
returns through the device callback. Inthandle then invokes the boot-pinned
`process.user_return` hook with IF clear. `THREAD_USER_RETURN` suspends at this
clean boundary, where no device callback remains on the saved stack. A dispatcher
may steal only a runnable, opted-in boundary context detached from **every** CPU.
RUNNING/BLOCKING/EXITING, blocked waits and arbitrary kernel continuations never
move. Source root restoration, xstate save, accounting closure and current/active
stack clearing precede visibility to another dispatcher. Target dispatch restores
xstate, address-space ownership and TSS RSP0 before the user return completes.

Idle CPUs pull from donors with more than one runnable/running context. Publication
of an eligible detached continuation wakes an idle CPU; no periodic idle polling
is added. Pressure is snapshotted once per decision rather than rescanning the
whole list for each candidate. Explicit affinity remains unchanged. Monotonic
`clock.now_ns` supplies accounting/deadlines through either qualified shared TSC
or guarded HPET; migration does not require activating an unqualified TSC.
`THREAD_LOAD` exposes per-CPU counts, incoming/outgoing migrations, wait samples,
total wait and maximum wait. These are diagnostics, not hard latency guarantees.
