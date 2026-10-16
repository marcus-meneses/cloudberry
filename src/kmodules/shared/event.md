# Event dispatcher and deferred workers

The event kmodule coordinates BSP execution through the hub. `smp.run` moves
onto the managed BSP dispatcher stack and calls `event.run(64)`. This remains
coupled kernel code, distinct from future resource handlers.

`event.init(hub)` caches dependencies. On the first valid step, it creates two
persistent CPU0 kernel workers with guarded 16 KiB stacks: one invokes
`scheduler.run(1)`, the other `irq.drain(1)`. `event.worker(kind)` is their entry
surface and rejects calls from any other context. No test/demo jobs are created.

`event.step(budget)` requires a positive budget; it dispatches at most one thread
context, a tighter limit than the supplied budget. It wakes the appropriate
worker when a callback or device notification is ready. Callback invocation no
longer occurs on the event stack. The normal 2 ms thread quantum can preempt a
long callback, letting the event loop and the other worker make progress.
`event.run(budget)` repeats this indefinitely. Nested entry is rejected through
an atomic ownership guard, including entry from a worker. Rejected calls preserve
IF; successful steps leave IF enabled and execution failures disable it.

Workers block after each invocation. Wake credits prevent loss of a notification
between the final condition check and blocking. Deadline-based thread waits are
independent of the callback queue: the event loop arms the earliest callback or
blocked-thread deadline. With no ready work or deadline, it stops its timer and
uses adjacent STI/HLT after checking readiness and pending IRQ notifications with
interrupts disabled. There is no periodic polling tick.

This bounds event dispatch to a thread quantum outside critical sections, not a
hard real-time latency guarantee. Interrupt-disabled locks can still delay it.
Callbacks within each worker remain serialized. Device callbacks must perform
short, bounded completion/cause-clearing work and hand longer work to independent
threads; they must not block waiting on their own IRQ worker. `irq.context()`
allows such blocking APIs to reject that context. A failed worker is an essential
execution failure, rather than an automatic provider restart.

`event.info(out)` reports cumulative steps, completed callback batches/jobs,
idle entries, wakes, consumed timer notifications and failures. Its snapshot is
BSP-only. Counts of dispatched contexts differ from completed callback counts.

Isolated tests cover real timer deadlines/idle wake, nested-entry rejection and
HPET device completion while a slow scheduler callback is still running.
