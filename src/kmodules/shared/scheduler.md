# scheduler

`scheduler.kmod` is loaded alongside inthandle, mem and hwenum. Its named/versioned
methods register through the generic loader. Boot initializes it with the hub
and the shared bootstrap heap after configuring mem. No work is submitted by
normal boot; event.run enters the live BSP execution loop after diagnostics.

Hub surfaces: scheduler.init, scheduler.submit, scheduler.run, scheduler.cancel,
scheduler.result, scheduler.stats, scheduler.remove_process and scheduler.next_deadline. Exact structures/call arguments are in
`scheduler.h`; the hub contains names, versions, entries and provider ownership.

A scheduler_request declares its format ABI (3), process_id, deadline_ns, target kind, entry vector, required
entry major/minor and payload structure. Process ID zero identifies kernel-owned
work; other IDs identify the owning process. IDs are supplied by trusted kernel
callers, not inferred or authorized by this module. submit uses version 3.0,
result stays 2.0, and stats uses 2.0 for its added waiting count. Two target kinds are supported:

- METHOD: entry_vector is the address of a published mediated hub method, with
  matching major and sufficient minor version. Six payload arguments map directly
  to RDI/RSI/RDX/RCX/R8/R9. Arbitrary native addresses outside the hub are rejected.
- INTERRUPT: entry_vector is a bound vector32..255. Payload must contain a copied
  normalized inthandle_frame with matching vector; six arguments must be zero.
  Dispatch goes through inthandle.dispatch. Exceptions cannot be queued.

Optional payload data is copied into the task's 16-byte-aligned storage, with its
owned pointer substituted into a selected argument register. Request and input
buffer can be released after successful submission. Other register pointers are
borrowed and must remain valid through execution; pointers nested inside copied
data are not deep-copied. Copying six arguments by itself does not extend the
lifetime of any pointed-to object. Each target's header defines its own argument
or data format; the scheduler does not infer that contract.

Submission allocates one variable-size record via mem.calloc and returns a
nonzero monotonic task ID. FIFO queues have no fixed entry-count or payload-size
ceiling beyond representable sizes and available backing. mem can grow the shared
heap. Allocation/validation failure publishes no task and consumes no ID.

run(budget) synchronously executes at most budget jobs on the caller stack.
Zero budget runs nothing. A target runs to completion; its returned value/status
is retained even on failure, and subsequent work can run. Nested run is rejected;
callbacks may submit work, inspect state or cancel other queued tasks. There is
no independent callback stack or callback preemption. Independent preemptible
contexts belong to the threads module. Timer/event coordinate idle wakeups outside this module. Budget bounds the number of
invocations, not the duration of one invocation.

A deadline_ns of zero means immediately ready. Otherwise it is an absolute
clock.now_ns timestamp; already-due submissions are ready immediately. Future
jobs retain their copied payloads in WAITING. An intrusive list orders waiting
jobs by deadline, with FIFO ordering for ties. Insertion is O(n); removing a
known pending record and ready-queue insertion are O(1). There is no fixed count
ceiling or polling allocation. Existing ready jobs precede newly promoted jobs.
run checks time before dispatch and never executes waiting work early; a busy
callback may delay it. Deadlines are eligibility times, not completion guarantees.

next_deadline(out) promotes due work and returns IDLE (0), READY (1), or DEADLINE
(2); out contains the earliest future deadline for DEADLINE. UINT64_MAX remains
a valid deadline, not an empty-queue sentinel. Clock failure propagates without
publishing a new submission. A run failure returns its already-dispatched count
with failed status; retained completions remain available.

Task states: WAITING -> QUEUED -> RUNNING -> COMPLETE, or queued/waiting ->
CANCELLED. Cancel is
idempotent on cancelled tasks and cannot cancel running/completed work. result
can peek any state, but can reap only complete/cancelled records. Reaping releases
the task and copied payload through mem.free. Results or borrowed references into
that storage expire at reaping; copy such data before reaping. No automatic result
retention timeout exists. stats reports submitted and current queue/running/
complete/cancelled/retained counts plus waiting; queued includes ready and waiting.

remove_process(process_id) removes all matching waiting, queued, cancelled and completed
records, freeing copied payloads as well. It returns the number removed or marked
for removal; an empty process succeeds with zero. Other owners are unaffected,
including kernel-owned tasks unless process_id is zero. Completions include the
owner process_id. Removal does not roll back effects of already executed calls.

A matching running callback is marked for removal on return: its record and
payload cannot be freed while its method still uses them. Same-process submissions
are rejected until it returns, then its record is freed without retaining a
completion. Repeated removal during that interval counts the still-running record.
This is deferred cleanup, not forced termination of kernel code; a hanging callback
still blocks cleanup. After cleanup, the scheduler permits that ID to be reused.
Process lifecycle enforcement belongs to a future process manager. Ordinary
reaping and process removal invalidate references into task-owned storage.

Interrupt jobs are deferred software delivery, not a CPU int instruction or a
synthesized physical IRQ. inthandle tells callbacks INTHANDLE_DEFERRED in the
third argument; real CPU entries use INTHANDLE_CPU_ENTRY. Changes to a copied
frame do not restore the original CPU context. Hardware acknowledgement is not
performed by scheduler, and callbacks must not acknowledge an already handled
hardware event again. The current vector binding is used at dispatch; an unbound
vector or failed manual callback yields a failed completion instead of halting.
Real unhandled CPU entries retain fatal diagnostics.

Queue mutations use the recursive global lock; run takes it only for selection
and lifecycle/result publication. Invocation is synchronous on the caller's
stack, outside that lock, and cannot begin inside a caller-held critical section.
Hardware ISRs must not call these allocating surfaces. The event module invokes
run(1) on a dedicated preemptible worker; direct run callers retain synchronous
semantics. A kernel callback can still fault or hang.

Validation:

```sh
make -C src test-scheduler
make -C src test-scheduler-boot
```

The UBSan host test covers FIFO/budget, all register arguments, copied/aligned
payload, cancellation/reaping, callback failure, recursive-run rejection,
submission during a callback, zero/kernel ownership, mixed-state process removal,
self-removal with live copied payload, isolation between owners, IRQ frame copying/unbinding, invalid targets/ABI/
lengths, deadline ordering/ties/cancellation/owner removal, clock failure,
allocation failure, queue exhaustion and exact cleanup. One modern-bus
TCG boot invokes actual module exports, copies a stack payload, queues vector128,
checks deferred delivery tagging, handles unbinding/callback failure, validates
process removal (including ID zero) and owner reporting, and reaps all
records. The probe handlers and assertions are test-only and excluded from the
production ISO. The separate clock module supplies timestamps; scheduler does
uses it to promote deadlines, while event/timer provide interrupt-driven wakes.

`scheduler.info(out scheduler_diagnostics)` exposes cumulative completed/timed
calls, total/max callback elapsed time, timing failures/regressions, last task
and owner, late jobs, total/max deadline lateness and last late task. Totals
saturate and survive result reaping/process cleanup. Measurement surrounds the
actual callback, separate from live queue statistics. An unreadable/regressing
clock records a timing failure without overwriting the callback's result.
These are elapsed durations, including interruptions, not per-process CPU usage.
Host checks include exact simulated duration/lateness and measurement failures.


Mediated queue operations use the global recursive lock. run protects selection,
lifecycle and result publication, but releases the lock across the callback.
Concurrent/nested executors are rejected under that lock; entry with a
caller-held critical section is rejected so it cannot leak a parent lock across
the callback. Running records remain
retained, and process removal defers cleanup until callback return. A callback
executes synchronously on its caller: on a thread it may use that thread's
blocking API. The event module uses a dedicated preemptible worker, so long
callbacks no longer delay BSP event dispatch or the IRQ worker. They still delay
later callbacks in their own serialized queue. Direct calls on the BSP dispatcher
remain synchronous and cannot block. Long/blocking jobs belong in independent
threads. See profiling.md for the measured change.

Submissions from APs send a wake IPI to the BSP after queue publication, so its
idle event loop cannot sleep indefinitely with newly submitted callbacks. The
isolated SMP test publishes from an AP while the BSP enters idle.

METHOD records lease their provider at submission until record destruction, including
retained completed/cancelled results. Failed construction releases the lease; new
submissions targeting draining providers fail. Existing queued leases may dispatch
while draining. Result reclamation or process removal releases the lease after any
running callback returns. See [hub lifetime](../../kernel/hub.md).
