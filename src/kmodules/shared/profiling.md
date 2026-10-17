# Execution measurements

cpu.control exposes METRICS(out,cpu_index), METRICS_RESET(), IRQ_BEGIN(flags) and
IRQ_END(flags). Structures and opcodes are in kernel/cpu.h. smp.cpus also contains
live per-CPU counters. Nanosecond peak records and max-hold attribution are sequence-protected; cumulative fields
are individually atomic and may reflect adjacent instants. METRICS can fail during
an attribution/reset update;
retry it. Reset affects only the calling CPU, outside a lock with IRQs enabled.

Counters cover outer acquisitions per lock, contention, total/maximum wait and hold,
managed IRQ-off intervals and counter regressions. Lock domains are 0 (memory,
registry and queues) and 1 (clock). Nested lock hold durations can overlap and must not be summed as CPU usage.
Domain counters separate their acquisitions,
contention and time. max_hold_method is hub method index + 1; zero denotes an
explicit critical section with no mediated method tag. max_hold_domain identifies
the lock. Hub entries are immutable once published, and later growth preserves their indexes, so this attribution is
stable for the boot. Counts include instrumentation overhead.

Recursive entries into an already held domain with IF clear start no new sample
and perform no timestamp/HPET reads. The outer hold still covers their execution.
An IF-enabled lock entry reuses its initial ordered sample to start IRQ-off
tracking; acquisition and release retain independent hold/wait measurements.
Every outer acquisition remains measured; this is not sampled instrumentation.

Clock policy publishes a statistics rate through CPU_PROFILE_CLOCK only while
holding clock domain 1. Successful local TSC selection or shared-clock qualification
enables local ordered-TSC conversion. Calibration invalidates the rate first;
shared HPET selection, regression/drift fallback and failed clock reads disable
it. Per-CPU samples carry rate/epoch; mixed-epoch/rate or regressed TSC durations
are not converted. unavailable_ns_samples makes this lost coverage explicit.
Raw tick counters/attribution remain available. A profiling-observed regression
quarantines that epoch on all CPUs; the next clock read sees the fault and falls
back to HPET. Resetting counters does not clear this quarantine. A fresh qualified
epoch is required to resume fast statistics.

Without a qualified rate, or during a clock update, durations use direct HPET.
tsc_ns_samples/hpet_ns_samples record which conversion actually succeeded. The
fast path performs no HPET MMIO for statistics on an uncontended lock. Independent
HPET watchdog references start on actual contention and remain in lock/IPI/TLB
wait loops, with the existing five-second deadline, iteration fallback and IRQ
policy. Clock health still uses HPET independently. Faster statistics do not
replace watchdog timekeeping or weaken acknowledgement ordering.

wait_peak/hold_peak/irq_peak carry nanoseconds, local ticks, CPU index, hub method
index+1, lock domain, caller PC, profiling epoch, source and address-space ID.
Domain 2 denotes an explicitly instrumented IRQ-off transition rather than a
lock; method zero denotes an untagged path. Direct kernel lock callers retain
their call-site PC. Dispatcher/event/timer adapters supply transition attribution.
The caller identifies the measured path, not proof that it caused the delay.
These raw PCs are trusted diagnostics, not application error-trace fields.

The isolated address-space benchmark separately brackets batches of 16 ordered
CPU reads, the old double-CPUID sequence, clock calls, ordinary locks, recursive
locks and IF-enabled locks with direct HPET reads. It records 64 interleaved
samples per case, including an empty HPET bracket. Raw batch times and signed
paired per-operation estimates are retained; no automatic correction is applied
to address-space timings. See [profiling method](../../tests/address-space-profiling.md).

Durations are ordered local TSC ticks, not nanoseconds or guest CPU execution
cycles. They include vCPU host descheduling and other elapsed delays. Do not
compare raw timestamps between CPUs, infer CPU utilization from them, or convert
to time without a validated frequency. Guest profiling cannot distinguish a
host pause from a long hardware operation. This matters especially under TCG.

Managed IRQ-off coverage includes mediated critical sections, thread dispatcher
transitions, event idle preparation and foreground timer programming. It excludes
firmware/early boot, uninstrumented assembly, and arbitrary CLI in future modules.
Clock-validation brackets report their uncertainty separately. Sleeping time and
time spent running another thread are excluded. This is scoped
instrumentation, not a claim to trace every hardware IF transition.

threads.control(TIMING,0,out) reports the calling CPU's runnable-wait samples,
total/maximum wait in clock nanoseconds and failed/missing clock samples. Creation
and wake timestamps use clock; resumed runnable work is timestamped after leaving
its previous context. This measures dispatch waiting, not hard real-time latency
or exact IRQ-entry latency. IRQ notifications separately report deferred wait
in local TSC ticks through irq.info.

The isolated test submits a 20 ms callback and measures the largest lock hold.
Before narrowing, the four-CPU TCG sample was 42,170,289 ticks; after releasing the
callback-wide lock it was 185,724 ticks in the corresponding sample. Subsequent
runs vary. This demonstrates removal of that hold, not a throughput claim.
Full workload measurements still show contention and large host-pause-sensitive
elapsed maxima; those are retained in the isolated boot logs, not hidden.

Changes informed by measurement:

- scheduler.run protects selection, lifecycle and result publication, but releases
  the lock before invoking a callback. A running record cannot be reaped; process
  removal marks it for cleanup after return. Reentrant/concurrent executors fail.
- threads.create constructs private allocations/payload outside the thread-list
  lock, then briefly acquires it to assign an ID and publish the record.
- clock reads use a separate recursive lock and the validated UC HPET counter
  mapping directly, avoiding direct global MMIO/allocator locking on each ordinary read.

Lock order is global domain 0 before clock domain 1. Clock-only operations never
acquire the global lock; init, RTC and TSC activation acquire both in that order.
Inversion is fatal. Total critical depth still prevents thread preemption inside
either lock. Allocator/page mapping and shootdowns retain their existing protection.
Direct callbacks remain synchronous; normal event dispatch now runs them on a
preemptible worker separate from the IRQ worker. AP workers
and interrupts can progress. Callback continuations/budgets remain later work.

Run make -C src test-threads-smp for isolated one/four-CPU measurements and
make -C src BUILD=../bin/tests/isolated test-scheduler for lifecycle checks.
No profiling application, candidate test job or recurring idle polling was added.

The subsequent latency milestone moved event callbacks/IRQ drains onto separate
preemptible workers, moved stack construction/teardown outside an allocation-wide
lock, detached thread reaping from the list lock, and batched stack invalidations
in groups of 16. These are structural reductions, not a new measured throughput
claim. The subsequent policy below moves bulk physical sanitization outside
metadata locks and adds finite managed wait watchdogs; other critical work still
has no hard real-time guarantee.

Current managed latency policy adds source-labelled nanosecond wait/hold/IRQ-off maxima,
IRQ-enabled waiting-window counts, explicit IPI IRQ-off intervals and notices for managed IRQ-off spans above
2 ms. cpu.control(CPU_LATENCY,out) reports the five-second lock/IPI/TLB wait
watchdog and notice threshold. Waits also retain a finite iteration fallback for
a stalled/missing hardware clock. Exceeding the watchdog faults through inthandle
with a register dump; an isolated deliberately stuck-owner boot verifies it.

An outer lock waiter with IF enabled services hardware IRQs while waiting;
thread switching remains inhibited so it cannot move the wait into a dispatcher
which needs the same lock. Nested/IF-disabled callers preserve their policy.
A controlled two-CPU test verifies timer delivery during contention. Heap growth,
calloc zeroing/realloc copying and owned-page sanitization now run outside their
metadata locks. Caller-held outer critical sections still constrain preemption.

These are managed watchdog bounds and measured notices, not a hard real-time
IRQ response guarantee. Ownership searches, unrelated critical sections, hardware
SMIs and host vCPU scheduling can exceed a desired response time. A 2 ms notice
does not forcibly interrupt protected mutations. No host/guest wall-clock promise
is made; the acceptance report separates intentional contention from stress data.
