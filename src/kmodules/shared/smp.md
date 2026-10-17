# SMP

`smp.kmod` starts the CPUs supplied by Limine's MP response. Bootstrap passes
that response through, rather than interpreting the CPU inventory in main.
Logical CPU0 is the BSP; other indices follow firmware order. No hotplug, thread
migration or load balancing is implemented.

`prepare(hub,heap,response)` allocates the CPU table, startup records, private
GDT/TSS/IDT storage and four guarded 32 KiB stack bodies per CPU: dispatcher,
NMI, double fault and machine check. These sizes are policy defaults, not mem
limits. It publishes `smp.cpus` and claims vectors 225 (TLB) and 226 (wake).
`inthandle.cpu_init` installs each CPU's private tables. The early BSP static IST
stacks remain as retained module backing, but are superseded by guarded stacks.
`apic.cpu_init` owns each AP's local controller setup; global PIC/IOAPIC masking
is done once by apic.init. `timer.init` independently calibrates each CPU.

`start()` releases APs through Limine goto_address with release ordering. AP entry
loads the shared CR3 and switches immediately to its managed dispatcher stack.
An AP attaches its CPU-local GS base, installs interrupt tables/controller/timer,
checks shared-clock progress and publishes online with release ordering. BSP
waits for each AP with a two-second clock deadline. `info(out)` reports expected
and online counts and whether shared clock mode is selected. `run()` transfers
the BSP to its managed dispatcher stack and enters event.run; the Limine stack
and responses remain reserved. Initial bring-up failures are fatal boot errors.

SMP deliberately selects `clock.shared()` before releasing APs: a healthy shared
64-bit HPET supplies CPU-independent timestamps, and recalibration cannot
silently select an unvalidated TSC afterward. Each AP verifies forward progress;
shared clock state is serialized. SMP boot fails if that reference is unavailable.
Single-CPU boot retains validated TSC selection/fallback behavior. After AP startup,
validate_clock tries 10, 25, 50, 100, 250 and 500 ms observation windows. Each
attempt collects ten starting and ten ending observations per CPU through
preallocated request/ack mailboxes and wake IPIs. The requested interval separates
the two bursts; actual per-trial elapsed intervals include read/relay costs. It
stops at the first window where every CPU qualifies, otherwise retains HPET.
A qualified longer-window calibration may be applied once, repeating that window
with fresh observations and a new epoch before any activation.
clock_info returns the last report: status, sample/CPU count, uncertainty,
offset/drift bounds and calibration epoch. Unavailable invariant TSC, uncertainty
above 50 us, offset bounds above 10 us or drift bounds above 500 ppm reject it.
Uncertainty is added to the upper error bound, never subtracted to make a sample
pass. A successful current-epoch report permits clock.smp_tsc activation; otherwise
HPET remains selected. Runtime validation first returns to HPET. Recalibration or
later instability invalidates use of the old proof. Migration remains disabled.

The thin kernel CPU mechanism owns GS:0 identity, recursive critical sections,
IPI delivery and TLB acknowledgement mailboxes. CPUID is used to validate CPU
identity during attachment, not on every kernel call for identity lookup.
Timestamp backends may still execute CPUID for ordering when required by the
advertised capabilities; [clock](clock.md) describes their selection. GS is reserved to the
kernel; no swapgs/userland context exists yet. Short foreground mutations use
a global lock and a separate clock lock with local IRQs disabled, always global
before clock. This establishes correctness but
serializes allocator/registry/queue operations; it is not a scalable locking
implementation. Callbacks execute outside their queue lock. Raw mem primitives bypass mediation and require caller locking.
Interrupt bindings use atomic sequence snapshots; counts are atomic. Owners
must quiesce hardware and in-flight callbacks on all CPUs before reclaiming a
binding's context.

Managed map/unmap and MMIO permission changes invalidate remote CPUs before
returning. TLB IPI handlers invalidate nonglobal/global translations, publish acknowledgement and send EOI.
A CPU spinning for the kernel lock services its preallocated mailbox even with
IF disabled, preventing a shootdown/lock deadlock. The issuer waits for every
online CPU before physical reuse; a bounded delivery/acknowledgement failure
enters the owned exception dump. Shared-kernel changes cover every CPU; private-root changes target CPUs
currently using that root, serialized with scheduler root transitions. Root
switches flush inherited global entries too. PCID remains disabled. See
[private address spaces](mem-spaces.md).
Per-CPU boot/emergency stacks and active thread stacks cannot be released.

Wake IPIs interrupt idle CPUs after runnable publication. APs execute pinned
threads independently; memory and clock calls can serialize, while ordinary
thread computation runs concurrently. `smp.cpus` is a live diagnostic table;
its mutable counters/fields require atomic snapshots when inspected.

Focused validation uses one/four-CPU TCG boots with disposable media. Tests prove
preemption on every CPU, parallel pinned workers, allocator sanitization,
monotonic reads, and an AP observing a new physical frame through a previously
cached virtual address after unmap/remap. The candidate VM and persistent disks
are never used by these tests. Four-CPU AMD KVM/x2APIC acceptance passes;
physical hardware and larger CPU topologies remain unvalidated.

Protocol reference: [Limine MP definitions](https://github.com/limine-bootloader/limine-protocol/blob/trunk/include/limine.h).

AP idle paths now query the earliest blocked-thread deadline and arm their local
one-shot before adjacent STI/HLT, stopping it when no deadline exists. Timed
blocking is independent of the BSP callback queue and is tested on every CPU.

## Raw clock profiling

`smp.clock_sample(out clock_sample,cpu,round)` copies a retained raw observation
on the BSP after validation ends. Attempt a occupies rounds a*20 through a*20+19:
first ten starts, then ten ends, paired by trial index. Up to nineteen attempts are
retained (380 observations per CPU, including bounded replacements and one calibration verification); snapshots of incomplete/uncollected rounds
fail without writing the output. Storage is reserved during SMP preparation, not
allocated during sampling. `smp.clock_group(out tsc_group,cpu,attempt)` reports a
completed group's filtering masks, retained count, medians, MADs and final bounds.
Existing clock_info/tsc_report layouts remain unchanged; report.samples counts all
collected observations and its bounds describe the final attempted window.

Each CPU is analyzed independently. Widths above 50 us are rejected. The relative
width cutoff is median(width) + max(3*MAD(width), median(width)), retaining ordinary
variation around a near-zero MAD without relaxing the absolute sampling limit.
For drift/offset estimates, three-MAD thresholds are enlarged when necessary to
the trial's uncertainty plus the representative group's median uncertainty. Drift
uncertainty is propagated from both endpoints into ppb; TSC-only offset uncertainty
comes from its causal round trip. Small differences inside overlapping uncertainty
intervals are not statistical outliers. Numeric floors remain 1 ns / 1 ppm only
when measured uncertainty is smaller. Actual uncertainties are never subtracted
or reduced for acceptance. TSC-only offset batches are filtered independently.
Medians use the upper middle observation for even counts. At least seven of ten
trials must remain; a consistently different CPU or frequency is never excluded
by comparison with other CPUs. Invalid capabilities/regressions fail closed.

Median drift/offset and scatter describe the retained observations, but acceptance
uses the worst uncertainty-inclusive drift bounds of those trials plus independent
TSC-only offset bounds, not the
median alone. Limits remain 10 us offset, 500 ppm drift, 50 us bracket uncertainty.
BSP self-offset cancels exactly because it is the same observation; its temporal
drift and read uncertainty still qualify independently. All CPUs must pass within
the same attempt and calibration epoch before clock.smp_tsc activation.

Test kernels export every raw observation, including discarded trials, plus group
summaries. report.json stores samples, per-trial windows, filter reasons and group
statistics under tsc_profile. Wide-bracket and MAD rejection counts are separate.
Host analysis audits kernel decisions; it is not used for compilation or as an
activation proof. Median/MAD filtering is a robust estimator, not a formal
confidence interval. No guest-persistent calibration, RTC reference or overhead subtraction is
introduced. Runtime HPET health guards remain.

`test-tsc-validation` exercises robust filtering, too few observations, persistent
skew/drift, and BSP correlation. `test-smp-clock` runs the production adaptive
control flow with deterministic host adapters, checking early acceptance,
six-window exhaustion, all-CPU requirements and snapshot bounds. `test-tsc-profile`
checks report analysis. Native AMD KVM profiling/refinement evidence is recorded in tests/README.md;
shared TSC offset qualification now passes on AMD KVM; native Intel CPU evidence
remains pending.

## Longer-window calibration and overhead

After a BSP group of at least 100 ms retains seven trials, `clock.refine(group,
expected_epoch,expected_hz)` may update the frequency once during this profiling
run. It requires invariant/shared HPET mode, no instability, zero BSP self-offset,
MAD at most 100 ppm, median correction 10 ppm..2%, and the worst drift bound at
most 250 ppm above the median magnitude. Enumerated frequency must still agree
within 2%. HPET time anchors remain unchanged; calibration epoch increments.
The same window is then repeated with new samples. Previous observations are
retained for diagnosis, but cannot form the new activation proof. No correction
is applied from a group below the minimum count or with excessive uncertainty.

`smp.clock_cost(out smp_clock_cost,cpu,round)` copies per-observation costs:
empty ordered-read bracket, HPET bracket without counter quantization, full sample
call, BSP-to-AP request/ack round trip, input Hz and calibration epoch. CPU0 has
no relay measurement. Empty brackets include selected backend/mediation costs;
full sample calls include capability discovery, probe work and timestamp reads.
Relay duration also includes IPI/wakeup/waiting and possible host pauses. These
are observed durations, not pure CPU execution time or quantities to subtract
from uncertainty. Samples from different epochs are never combined in a trial.
Native reports include per-CPU/epoch min/upper-median/max costs and frequency
history. Clock policy and hardware ordering remain behind existing backends.

## TSC-only cross-CPU offset qualification

HPET brackets now qualify per-CPU frequency/drift only. Their derived cross-CPU
positions remain diagnostic; `clock_group.max_offset_ns` is zero because offset
qualification comes from separate timestamp exchanges. No frequency or offset
limits are relaxed and no overhead is subtracted.

After each starting/ending HPET burst, each AP is woken once into a bounded
shared-memory request/ack batch. The AP keeps interrupts enabled and polls for
ten requests; no IPI or HPET read occurs in the timed exchange. BSP reads its
ordered TSC before publishing a request. The AP captures its ordered TSC after
acquiring that request, stores the timestamp and releases the acknowledgement.
BSP acquires the acknowledgement, then reads its TSC again. All ordering uses the
selected Intel/AMD CPU backend. CPU-local elapsed-time watchdogs and finite poll
limits bound the session; cleanup clears the active flag and waits for AP exit.
HPET is used only outside the exchange for readiness/exit timeout checks.

The AP's capture happened somewhere between the BSP endpoints. The midpoint
estimates offset; half the entire round-trip span (rounded upward, plus counter
conversion rounding) bounds its uncertainty. This does not assume symmetric
message delays. Interrupts, host pauses and slow responses widen that interval.
BSP/AP counter regressions abort qualification rather than being discarded.

Start and end offset batches are filtered per CPU with upper medians and the
uncertainty-aware MAD thresholds described above, first on uncertainty widths,
then on observed offsets. Each batch must retain
at least seven of ten trials, and every retained |offset| + uncertainty must fit
10 us. A CPU with persistent skew fails; it cannot be excluded as an outlier
relative to other CPUs. The frequency group and both offset batches must pass
for every CPU in the same calibration epoch. Runtime HPET safeguards remain.

`smp.clock_exchange(out tsc_exchange,cpu,round)` copies the BSP-before/AP/BSP-after
raw ticks, input Hz, epoch and derived offset/uncertainty (APs only).
`smp.clock_offsets(out tsc_offset_pair,cpu,attempt)` exposes starting/ending batch
statistics and rejection masks. Completed individual exchanges survive a later
batch failure for diagnostics. BSP self-offset is exactly zero and requires no
message exchange. Storage is allocated during SMP preparation, not in an ISR.

Test kernels emit compact TSCX records; native report analysis verifies their
causal bounds, epochs and accepted-group evidence. report.json stores independent
frequency groups, raw exchanges and offset_groups. Old HPET-derived offsets are
labelled diagnostic-only. Host tests cover actual sampler early/exhausted paths,
persistent skew, asymmetric slow replies, bounded timeout cleanup and parsing.
Native KVM execution of the AP polling/timing path now passes on four AMD-host
vCPUs, with all final offset batches retaining 10/10 trials (2026-10-15).

## Bounded replacement trials

If any frequency/offset batch retains fewer than seven trials and no measured
bounds exceed the acceptance budgets, profiling takes up to two fresh batches
of ten paired trials at the same duration. Both old and replacement observations
remain logged; trials are not pooled across windows or calibration epochs. This
keeps each proof internally consistent rather than selecting individual favorable
points from many separate runs. Persistent bad offset/drift bounds do not trigger
these shortage retries. Exhaustion advances to the next duration, or retains HPET
at the end. A frequency correction's mandatory verification is separate from the
two-replacement budget, which is shared across epochs at that duration.

At most 19 attempts are stored: three per each of six durations, plus one fresh
calibration verification. `tsc_profile.attempts` identifies initial/longer windows,
replacement batches and calibration verification. All rejection masks, minimum
counts and worst-retained uncertainty-inclusive safety budgets are preserved.


Qualification runs on every multi-CPU boot; retained reports are diagnostic and
are not persisted/reused for subsequent boots. Single-CPU boot performs clock
initialization/calibration without cross-CPU qualification. The latest two native
runs each requested 10+25+50+100+250+250 ms intervals (685 ms total); actual elapsed
qualification also includes collection, relay and analysis overhead. Dedicated
end-to-end boot-cost reporting remains a roadmap item.
