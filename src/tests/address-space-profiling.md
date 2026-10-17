# Address-space profiling

The isolated address-space test measures the production implementation. All probes,
fault fixups, measurement loops and report processing are test-only. The dated
follow-up sections describe production timestamp, allocation and teardown changes;
root-switch, scheduling and TLB-invalidation policy are unchanged by this work.

## KVM collection and automatic comparison

From the project root, run:

```sh
./scripts/bench-spaces-kvm.sh
# Optional independent repetitions; the default already samples extensively:
./scripts/bench-spaces-kvm.sh --runs 3
# Explicit before/after reference, including after a failed intervening run:
./scripts/bench-spaces-kvm.sh --runs 3 --reference-session bin/tests/kvm-spaces/20261017T152711.432088Z
```

The script checks `/dev/kvm` before building, then compiles once into the reusable
`bin/tests/kvm-spaces-build` directory. It runs one CPU without IOMMU, then four
CPUs with AMD-Vi and Intel VT-d, always using `-accel kvm -cpu
host,migratable=off,+invtsc`. There is no TCG fallback, sudo or host reconfiguration.
It uses the existing isolated runner and disposable image paths only. Candidate
artifacts, persistent disks and the original TCG report directories are untouched.

A timestamped directory under `bin/tests/kvm-spaces/` contains per-boot reports,
boot/runner/build logs, host CPU/kernel/affinity/QEMU/compiler information, guest
binary hashes and source identity. `comparison.json` and `comparison.md` compare
pooled native distributions against the checked-in historical summary at
`baselines/address-spaces-tcg.json`; that file preserves the three run IDs below
and their counts/minimum/median/p95/maximum. Native raw samples and per-boot medians
remain available. Ratios describe elapsed TCG/KVM measurements, not code speedups.
Clock-source/paging differences and changed guest sources are explicitly flagged;
clock changes during measurement or across repetitions prevent a pooled result.

When a previous accepted session is available through latest.json (or explicitly
selected with --reference-session), native-reference.json snapshots its pooled
raw evidence and host identity. native-comparison.json/.md then compare the new
KVM runs against that KVM reference. Failed/incomplete references, mixed clock/
paging modes within repetitions and reused run identities are rejected. Host
CPU/microcode, kernel, QEMU, compiler and affinity differences are reported.
Changed guest sources are expected for an optimization but still flagged. This
comparison is separate from comparison.json/.md, which retain the historical
TCG comparison. Neither reference supplies build or guest configuration.

`bin/tests/kvm-spaces/latest.json` points to the latest session and its status.
A small adjacent `.tar.gz` contains reports/logs and the reference summary, never
VM disks or guest binaries. Failed runs return nonzero and retain diagnostics;
they are not accepted performance results. After running, reply “done” so the
saved host-side reports can be inspected; no guest file export is required.
The script serializes access to its build directory and detects source/binary
changes during a batch. Run it while the host is otherwise quiet; it does not
change host scheduling, CPU frequency or affinity settings.

Host-only verification: `python3 src/tests/test-space-compare.py` covers ratios,
raw samples, repetitions, invalid/stale identities, source changes, clock/mode
warnings, preflight failure and a simulated full collection/archive. The actual
KVM run remains user-host acceptance because this agent environment lacks KVM.

## Run and report

```sh
make -C src test-spaces-boot QEMU_CPUS=1
make -C src test-spaces-boot QEMU_CPUS=4 QEMU_IOMMU=amd
make -C src test-spaces-boot QEMU_CPUS=4 QEMU_IOMMU=intel
# On a host with accessible KVM; no silent TCG fallback:
make -C src test-spaces-boot QEMU_ACCEL=kvm QEMU_CPUS=4 QEMU_IOMMU=amd
python3 src/tests/test-space-report.py
```

Each boot uses separate images and disposable media below `bin/tests/isolated`.
No candidate VM or persistent developer disk is attached. `report.json` under
`tests/spaces-<N>cpu-<iommu>/` contains `address_spaces`: raw nanosecond samples,
per-worker CPU/space/owner identities, counts, minimum/median/nearest-rank p95/
maximum, clock metadata and paired estimates. All outliers and negative paired
differences remain visible. The parser rejects malformed/truncated samples,
missing fault acceptance, incomplete workers and unexpected table geometry;
Host checks exercise those failure paths, instrumentation metadata and preservation of noisy data.
Reports are analysis artifacts, never compiler or guest configuration inputs.

## What the numbers mean

Each of eight stress cycles records 32 interleaved samples per worker:

- An empty clock bracket establishes measurement overhead.
- Two mediated same-root selections provide a control with no CR3 change.
- A kernel/private round trip performs two root changes through the same API,
  restores the thread root before restoring IF, and excludes FP/stack switching.
- A voluntary yield measures elapsed scheduler/peer execution through resumption;
  it is not exclusive context-switch or root cost.

CPU1 holds its private root while remaining interruptible. The coordinator collects
32 paired inactive/remote protection timings per cycle, checks acknowledgements,
and later verifies actual revoked accesses fault. The inactive root has the same
backing data and API path. Clock reads inside the measurement and any contention,
IPI waits or vCPU pauses contribute to elapsed nanoseconds. The difference of
paired root samples estimates additional round-trip cost; dividing its median by
two is only an estimate per root, not a hardware-cycle measurement.

Ten trials each populate/destroy 32 and 512 contiguous user pages and 128 pages
at 2 MiB spacing. Dense geometries use four private page-table pages; sparse uses
131. Teardown timing excludes subsequent data-frame release. Mapping construction,
private table counts, and managed BSP hold/IRQ-off maxima during destruction are
recorded separately. Every trial restores owned reservations to baseline. Cycle
maxima include other operations such as stack construction/reaping; they do not
attribute time solely to protection or root selection. Raw test fault guards are
outside managed IRQ-off instrumentation. No reported maximum is a latency bound.

## Independent instrumentation attribution

The BSP also collects 64 interleaved direct-HPET brackets for an empty control
and batches of 16 operations: mediated cpu.ticks, the original inlined double-
CPUID timestamp sequence, mediated clock.now_ns, IF-disabled ordinary locks,
recursive locks under an already held domain, and IF-enabled ordinary locks.
The last case deliberately allows interrupts; the others preserve IF clear
inside each bracket and restore the incoming state afterward. The recursive
case excludes its containing acquisition. None changes watchdog configuration.

instrumentation records CPU vendor/capabilities and selected timestamp ordering.
instrument_* distributions retain raw elapsed batch nanoseconds, including HPET
read overhead. instrumentation.estimated_ns_per_operation preserves signed
(batch minus paired empty bracket)/16 estimates. Outliers remain visible; these
estimates are not subtracted from other benchmark measurements. The CPUID control
is inlined whereas cpu.ticks includes mediation: their difference alone is not
an isolated instruction-speed measurement. On TCG, emulated instruction costs
also cannot predict the native cost of VM exits.

## Native observations and follow-up: 2026-10-17

User-host KVM session 20261017T152711.432088Z passed all three configurations on
Ryzen 5 5500U, with shared TSC selected throughout. Four-CPU empty clock brackets
were approximately 64 us, root round trips 152–154 us, sparse population 79 ms and
dense 512-page teardown 36 ms. The expensive clock control prevents interpreting
the entire root round trip as CR3 cost. Code inspection identified double-CPUID
AMD timestamp reads and repeated lock-instrumentation HPET reads as candidates.
These are attribution leads, not measured fractions of the total.

Implemented: capability-selected AMD timestamps (guaranteed serializing LFENCE,
otherwise MFENCE/RDTSCP/CPUID, then the original conservative fallback), AP mode
agreement, removal of unused recursive-lock HPET reads, and reuse of entry timing
for managed IRQ-off tracking. Outer HPET maxima, clock regression/health fallback,
IPI fences and wait watchdogs remain enabled. Independent attribution probes and
native-to-native comparisons are available. Isolated guest/host correctness is
checked locally. That implementation introduced no allocator, invalidation or
reaper change.

Fresh native baseline accepted: session 20261017T165114.408723Z, three boots per
configuration (nine total), identical host metadata, matching guest source and
stable TSC throughout. AMD selected LFENCE ordering. Pooled four-CPU clock brackets
are 34.76/34.97 us (AMD-Vi/VT-d), dense 512-page teardown 25.17/25.14 ms and sparse
population 73.85/73.58 ms. Compared with the original native batch, these are
approximately 45%, 30% and 6% reductions respectively; the original batch had
one boot per case, so repeatability evidence is stronger for the new baseline.
Independent probes estimate about 35 ns per mediated raw timestamp, 1.58 us for
the old inlined double-CPUID sequence, 34 us for an ordinary IF-disabled lock and
46 us for an IF-enabled lock. Recursive acquisition costs are near the HPET-bracket
noise floor. All raw samples, per-boot distributions and native comparisons remain
in the session archive. No blanket overhead subtraction is applied. This evidence
motivates allocation-search and bounded teardown improvements next. The agent
environment still has no /dev/kvm.

## Allocation and teardown follow-up

The fresh KVM baseline separates cheap ordered timestamps from costly lock/HPET
instrumentation. Sparse population also creates 131 table pages and exposed an
allocation search that repeatedly rediscovered the occupied prefix. Physical
claims now use an advisory next-fit hint, with a complete-map fallback before
failure and lower-hole reconsideration after sanitization/removal. The reservation
ledger remains authoritative; this adds no capacity limit or second ownership map.

Teardown now owns a depth-first continuation per DESTROYING space. Locked batches
remove at most 16 leaves/tables and examine at most 4096 entries; lock drops no
longer restart traversal or require one acquisition for every leaf. Inactive-root,
owner/reference checks, table sanitization and all active-root TLB policy remain
unchanged. Failures retain the walk position for retry. These are work bounds,
not elapsed-time guarantees: ledger searches and host pauses still contribute.

Focused host checks cover allocation wrap/reuse/non-overlap, guarded stacks,
ownership/OOM, dense/sparse continuation, wrong-owner rejection, retained failure
position and the removal cap. Initial isolated one/four-CPU AMD/Intel TCG boots
passed with a 512-entry scan cap. Raising that cap to 4096 retains the removal cap
and passes the final four-CPU AMD-Vi boot, run
5d467738312a4d9cb1ad352221b38afe. Reports are under
bin/tests/space-optimization/acceptance; final source identity is saved beside
the final AMD report. No candidate image or persistent disk was used.

On the final four-CPU TCG run, sparse population changed from 266.10 to 3.47 ms,
dense 512-page teardown from 12.10 to 1.57 ms, and sparse teardown from 7.23 to
5.29 ms, compared with the prior copied AMD RDTSCP-path run. These are diagnostic
emulated observations, not native speedup claims. Batching trades fewer lock
acquisitions for longer individual holds: sparse teardown's median managed
maximum hold rose from about 59 to 314 us; the largest observed managed IRQ-off
span over its ten trials was 329 us. Native validation follows below; these TCG
observations remain historical evidence rather than native predictions.

### Native acceptance: allocation and teardown

Session 20261017T202426.499405Z passed all nine KVM boots (three per case), with
matching current guest source, unchanged host metadata and stable TSC throughout.
It compares against 20261017T165114.408723Z. Both references and the full raw
reports remain preserved; native-comparison.json/.md records per-boot medians,
pooled distributions and the expected changed-source warning.

Pooled medians below are milliseconds; AMD-Vi and VT-d are emulated IOMMU models
on the same AMD host, not native Intel CPU qualification.

| Measurement | 1 CPU before/after | 4 CPUs AMD-Vi before/after | 4 CPUs VT-d before/after |
| --- | ---: | ---: | ---: |
| Sparse 128-page population | 56.74 / 6.32 | 73.85 / 6.37 | 73.58 / 6.35 |
| Dense 512-page teardown | 24.96 / 1.91 | 25.17 / 1.94 | 25.14 / 1.94 |
| Sparse 128-page teardown | 12.86 / 1.57 | 12.95 / 1.60 | 12.96 / 1.60 |

Per-boot four-CPU sparse population medians stay within 6.34–6.38 ms; dense
teardown within 1.93–1.94 ms and sparse teardown within 1.60–1.61 ms. Empty clock
brackets remain approximately 35 us; root round-trip medians remain approximately
124 us, with visible noise/outliers. These measurements still include the real
cost of enabled lock/HPET instrumentation; no overhead is subtracted.

The latency tradeoff is recorded explicitly: typical sparse-teardown
managed IRQ-off maxima increase from about 50 us to 78 us; the largest observation
across its 90 trials is 91.56 us (90.09 us for the four-CPU cases). Broader cycle
median maxima decrease, but individual stress-cycle observations reach 4.06 ms.
Those lack caller/host-pause attribution in this report and establish no hard
latency bound. Correctness checks, exact hardware faults, active-root revocation,
reference/lease checks and owned-reservation baseline restoration all pass.
The requested performance sequence is complete; further instrumentation tuning
or architectural work requires a separate decision.

## Qualified statistics and peak attribution follow-up

The next authorized follow-up separates statistics from watchdog timekeeping.
Clock policy now publishes a qualified profiling rate only after local/shared
TSC selection and invalidates it before recalibration and on fallback/error.
Uncontended statistics use ordered local TSC rather than repeated HPET MMIO;
HPET is retained when no qualified rate exists. Samples crossing epoch/rate
changes are counted as unavailable rather than converted incorrectly. A regression
seen by profiling quarantines that epoch globally and reaches clock policy on its
next read, selecting HPET. Watchdog deadlines still use independent HPET on actual
lock contention and every existing IPI/TLB wait path.

Every outer acquisition remains measured. wait_peak/hold_peak/irq_peak retain
CPU, method ID/name, domain, caller PC, profiling epoch/source, address-space ID
and both duration units. Explicit dispatcher/event/timer transitions provide
caller attribution. These identify where a peak was recorded, not whether its
cause was a host pause or another owner. Existing multi-ms observations are not
retroactively attributed. Reports retain 114 peak records plus 38 sample-quality
records for BSP cycles/teardown, and fail if attribution is missing, duplicated
or disagrees with the scalar maximum. Historical references without profiling
sources receive an explicit comparison warning. No blanket correction is applied.

Host checks cover conversion overflow, regression and mixed epochs, and clock
tests check both clock-observed and profiling-observed fallback. The isolated
guest forces a synthetic rate solely to exercise the fast mechanism, local
fallback and mixed-epoch rejection, then restores real policy before all timings.
The deliberate stuck-owner probe forces fast statistics and must still reach the
HPET timeout. This test rate is neither calibration evidence nor a candidate setting.
Fresh KVM performance confirmation remains pending; use the unchanged collection
entry point with --runs 3 to compare against 20261017T202426.499405Z.

Local acceptance lives under bin/tests/profile-boundary/acceptance: one-CPU run
d59ffeee6a4040a48cfec27b0c37f653, four-CPU AMD-Vi run
f45232f23786495293890ff6965601f6 and four-CPU Intel VT-d run
f8f2b7eaad7d41a68f32210a41ab073b. All pass hardware fault/revocation/owned-reservation
checks and contain 114 matching peak records. The forced-fast watchdog reaches
its independent HPET timeout and stack dump. Host conversion/clock checks and
nine reporter/eleven comparison tests pass. These TCG checks validate mechanics
and fallback, not the native performance of qualified statistics.

Focused local acceptance: AMD RDTSCP four-CPU run
7bdedaf162144d50a7caeca271c632d4; AMD double-CPUID fallback one-CPU run
b1c4787248294dba9bcab8b6675453ae; Intel LFENCE four-CPU run
81284ccb35ce4efabc7ae4e2b7d6fcf6. These use TCG, with copied reports under
bin/tests/clock-overhead-validation. The deliberate stuck-owner watchdog also
passes. Capability/AP-mode host checks and eight report/ten comparison checks
pass. These results establish guest correctness, not native performance gains.

## Native timeout and rendezvous correction: 2026-10-17

Session `20261017T211928.519874Z` passes all three single-CPU native runs but
fails its first four-CPU AMD-Vi run at the 90-second runner timeout, after four
complete cycles and the fifth cycle's protection samples. No fatal guest fault or
explicit correctness failure is logged; this incomplete session is not accepted
as a performance baseline.

The test used `event.step` while polling shared AP phase/thread state. Once BSP
workers block, that call can stop the BSP timer and sleep indefinitely: remote
block/exit is not a scheduler submission or a wake notification for the BSP.
Consequently even the test's absolute deadline could not be checked. Rendezvous
now use nonblocking `threads.run`, keeping both state and deadline observable.
Every four-CPU cycle also checks that empty dispatch returns with all workers
blocked, then finishes the AP worker with all BSP workers blocked, explicitly
exercising remote-only progress. Candidate idle policy, clock policy,
watchdogs and the runner timeout are unchanged. Microbenchmark operations retain
the same brackets; scheduling/lock distributions can change with the corrected
test driver and remote-first completion order, so compare them with that caveat.

Focused TCG validation passes one CPU (run
`67849e01d6974e0f8c5658f6027a3056`, eight cycles/80 expected faults) and four
CPUs with AMD-Vi (run `207d8ff57fe74c748cd823375745a94b`, eight cycles/120 expected
faults, including empty-dispatch and remote-only completion). Both restore all
owned reservations and pass image integrity checks. Reports live under
`bin/tests/profile-boundary/tests/spaces-{1cpu-none,4cpu-amd}`. These checks do not
replace the native rerun.

Fresh native acceptance remains required. Preserve the incomplete logs and use
an explicitly accepted reference rather than the failed session:

```sh
./scripts/bench-spaces-kvm.sh --runs 3 --reference-session bin/tests/kvm-spaces/20261017T202426.499405Z
```

## Native clock-source transition follow-up: 2026-10-17

Session `20261017T212856.886469Z` passes all three single-CPU correctness runs and
two four-CPU AMD-Vi correctness runs. In the second four-CPU run, clock source
changes from TSC to HPET during cycle 2 (the third cycle); all eight cycles,
120 expected faults and teardown checks still pass. The comparator rejects the
mixed-source timings, so the batch stops before completing the nine-run series.
This is not an accepted performance baseline. The original test wait timeout did
not recur in either completed SMP boot.

The old runtime health check compared TSC with a later HPET sample as though
both were simultaneous. A deterministic simulated vCPU pause reproduces a false
fallback in that implementation. The corrected check brackets HPET with ordered
TSC reads, retaining the 1 ms divergence threshold outside the whole interval.
Regression, genuine leading/lagging TSC drift and profiling quarantine continue
to select HPET. The historical log lacks a fallback reason, so the precise trigger
in that boot cannot be established retrospectively. Future reports preserve
`clock_reason` from `clock.tsc_status`, and comparison failures include the source
transition, reason and run ID. Historical reports without that field still load.
No mixed-source measurements are silently accepted or automatically retried.

Host tests cover 10 ms pauses on either side of the reference instant, genuine
drift in both directions, regression inside the bracket, and the previous clock
and profiling fallback cases. The pause-before test fails with the uncorrected
clock implementation and passes with this change. All 13 clock modes, ten
reporter checks and eleven comparison checks pass. Isolated four-CPU AMD-Vi/TCG
run `2758a4300c8640209f676aeefbe20138` passes eight cycles, 120 expected faults,
owned-reservation restoration and image checks, with the new reason recorded.
TCG verifies fallback mechanics; native confirmation of this correction still
requires the explicit accepted reference command above.

## Observations: 2026-10-17

These acceptance runs used Q35, 512 MiB, `-cpu max`, TCG and the indicated emulated
IOMMU. All used HPET (10 ns nominal counter period), PGE off and PCID off. A
nominal timer period does not imply that measurement precision under virtualization.
Native timing could not be collected here because `/dev/kvm` is absent. Emulating
Intel VT-d does not qualify an Intel CPU or physical Intel hardware.

Medians below are in **microseconds**, rounded for readability; full distributions
and raw nanoseconds are preserved in each report.

| Measurement | 1 CPU, no IOMMU | 4 CPUs, AMD-Vi | 4 CPUs, VT-d |
| --- | ---: | ---: | ---: |
| Clock bracket | 13.41 | 14.81 | 14.81 |
| Two same-root calls | 35.75 | 100.61 | 117.09 |
| Kernel/private root round trip | 53.43 | 257.12 | 306.95 |
| Inactive protection change | 17.32 | 18.61 | 17.81 |
| Remote protection change | — | 191.12 | 187.74 |
| Dense 32-page teardown | 1,000.47 | 1,030.24 | 1,025.80 |
| Dense 512-page teardown | 13,305.26 | 13,624.60 | 13,703.70 |
| Sparse 128-page teardown | 7,878.32 | 8,058.57 | 8,202.07 |
| Sparse 128-page population | 206,151.55 | 268,408.06 | 266,486.62 |

The same-root control itself grows under SMP, so the larger round-trip timings
cannot all be attributed to CR3 reloads. Sparse population is substantially more
expensive than dense population in these runs. Its additional table allocations,
zeroing and reservation searches are candidates for further attribution; this
experiment does not isolate their individual contributions. Dense 512-page
teardown takes roughly 13–14 ms, but the observed per-step managed hold maxima
are much shorter, as the implementation drops the lock between disposal steps.

PGE was disabled, so these results do not measure the PGE-toggle/global-flush
branch. Retain conservative invalidation and defer PCID/global retention changes
until equivalent native measurements and correctness acceptance exist. Handler/
handle design and user-mode supervisor protection remain separate work.

Run identities:

- `spaces-1cpu-none`: `3131359e42ff412bab46af4d57128e9a`.
- `spaces-4cpu-amd`: `6bef1848d8494883a938b219639d2491`.
- `spaces-4cpu-intel`: `ab7590d0f0914e62b2583d32632356de`.
