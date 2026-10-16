# timer

`timer.kmod` provides per-CPU local APIC one-shot timers. Boot initializes it
through `timer.init(hub,heap,cpu_count)` after clock and apic initialization. It uses the APIC mode
already selected by firmware: xAPIC MMIO or x2APIC MSRs. SMP calls init on each AP after controller setup; thread contexts remain owned
by the separate threads module.

Public surfaces and structures are in [timer.h](timer.h):

- init(hub,heap,cpu_count): acquire the stable APIC descriptor and claim timer vector 224 through inthandle;
  configure the controller and calibrate the divide-by-16 count rate against
  clock.now_ns over approximately 10 ms. Retry of the same successful init is
  harmless. Polling is bounded and stalls/invalid measurements fail.
- arm(deadline_ns): arm a one-shot for an absolute monotonic clock deadline.
  Past deadlines use count 1; conversion rounds up. Long deadlines are capped
  at UINT32_MAX counts and require rechecking/rearming after the early wake.
- stop(): mask/stop the timer without releasing its lifetime vector claims.
- take(): atomically consume the preallocated pending-notification counter.
- info(out): report mode, vectors, measured count frequency, arm request,
  interrupts and pending notifications. This structure uses version 2.0;
  spurious fields belong to apic.info.
- interrupt: claimed CPU-entry callback; context and delivery are
  checked. Manual inthandle.dispatch rejects claimed vectors.

The timer callback increments saturating counters, clears the arm flag and
writes EOI once. It allocates nothing and never calls clock or callback scheduler. Its thread preemption hook takes no foreground lock and does not poll. Notifications coalesce in one fixed counter; they are wake
signals, not queued jobs or a periodic tick count. Spurious delivery and its
no-EOI rule belong to apic. Foreground operations save/disable/restore IF around
timer/pending-state access. State and calibration are CPU-local. BSP allocates the per-CPU array before APs
start. After EOI, threads.tick may suspend an active thread outside a critical
section; the interrupted frame stays on that thread stack until resumption.
This hook allocates nothing and takes no foreground lock.

The separate apic module owns PIC/IOAPIC masks, unrelated LVT sources, NMI-route
preservation, TPR, global enable and spurious vector ownership. Timer only writes
its LVT/divider/initial-count registers, reads its current count and sends its EOI.
No external routing, APIC mode transition, periodic tick or deep idle management
is introduced by timer.

Failed timer setup stops/masks its timer and releases its claim when this was the initial BSP attempt. An AP failure leaves
the established shared claim intact. It never
restores or changes the controller owner's global/spurious configuration.
Production startup identifies timer.init if calibration/initialization fails.

Validation:

```sh
make -C src test-timer
make -C src test-events-boot
```

Host UBSan checks calibration math, exclusive timer-register access, count saturation,
coalescing, EOI behavior and IF preservation. The disposable modern-bus
TCG boot receives repeated real local APIC timer IRQs through inthandle, checks
vector ownership, and runs deadline jobs via event.step. The xAPIC path is
validated natively; x2APIC and physical Intel/AMD hardware remain unvalidated.

References: [Intel system programming manual](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf),
[AMD64 system programming manual](https://docs.amd.com/v/u/en-US/24593_3.44_APM_Vol2).

If threads.tick reports deferred preemption during an IRQ-enabled critical wait,
the consumed one-shot is rearmed for a relative 2 ms retry using its CPU-local
calibration. The ISR uses no allocation, locks or clock calls; EOI still occurs
once. Without an active thread no retry is armed. The reported deadline remains
the original expired deadline until foreground arm/stop. This prevents permanent
loss of preemption after a deferred quantum without introducing periodic idle ticks.
