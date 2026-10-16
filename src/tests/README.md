# Isolated acceptance

Boot tests rebuild below `bin/tests/isolated`, use test-only kernels/probes and
create disposable storage. NVMe media contain known data and are attached read-only.
No test attaches the persistent candidate disks or changes host permissions.

```sh
make -C src test-threads-smp
make -C src test-iommu
make -C src test-iommu QEMU_CPUS=4
make -C src test-lock-watchdog
make -C src BUILD=../bin/tests/isolated test-iommu-host test-pci test-pages
```

The watchdog test deliberately leaves a lock owner stuck on another CPU. It must
produce a bounded acquisition timeout, owned exception/register dump and halt.
It takes about five seconds after boot and never runs in the candidate.

For native CPU/KVM acceptance on a host with accessible `/dev/kvm`:

```sh
make -C src test-native QEMU_CPUS=4 QEMU_IOMMU=intel X2APIC=1
# Or exercise the AMD-Vi device model:
make -C src test-native QEMU_CPUS=4 QEMU_IOMMU=amd X2APIC=1
```

This explicitly uses `-accel kvm -cpu host,migratable=off,+invtsc`; failure to open KVM stops before
building/running. It never falls back silently to TCG. This validates native guest
CPU execution with emulated devices, not physical NVMe/IOMMU hardware. Without
IOMMU emulation, omit QEMU_IOMMU or use `none`.

Each successful normal acceptance boot writes `report.json` beside `boot.log`:
`bin/tests/isolated/tests/mem-boot-<N>cpu[-intel|-amd][-native]/`. Reports include the
exact VM arguments, raw measurements, TSC validation status/offset/drift/uncertainty
and explicit emulator limitations. TSC status 1 is accepted; other statuses retain
HPET and must not be described as native TSC acceptance. Tick counts include host
pauses; HPET nanosecond measurements are supplied separately. The intentional
200 ms lock-owner test is labelled separately from normal stress measurements.

Physical acceptance remains pending. These probe images assume disposable test
geometry and must not be used to diagnose real disks. No bare-metal installer,
USB writing or host boot changes are performed. Wider firmware topologies and
native TSC/x2APIC/device acceptance require evidence from the target environment.

## Vendor and extended-context acceptance

`make -C src test-portability` runs focused topology/coherence/firmware checks
and isolated boots for four-CPU Intel/VT-d and AMD/AMD-Vi, Intel CPU + AMD-Vi,
XSAVE/SSE without AVX, and FXSAVE fallback. CPU/IOMMU backend selection is asserted,
not inferred from filenames. New state probes verify fresh x87/SSE/AVX contexts,
control words, yields and timer preemption on each CPU.

Report directory suffixes include -intelcpu/-amdcpu, -sse-xsave and -fxsave.
Reports include selected CPU backend, negotiated state mask/size/mode, physical
address bits, active IOVA width, APIC mode and explicit firmware takeover status.
Native reports additionally record the host CPU vendor/model/flags. Neither
emulated IOMMU acceptance nor KVM qualifies physical DMA handoff or a motherboard.
Backend register tests validate AMD coherent fetch control and preserved firmware
policy; firmware tests cover bridged Intel scopes and malformed/pending PCIe caps.

`test-xstate-host` uses the production XSAVE/XRSTOR code with the host OS's
existing enabled state; it never executes privileged CPU initialization or changes
host CPU configuration. Native host AVX round trips pass here. QEMU 6.2 cannot
emulate AVX, so guest AVX preemption is validated separately by the user-host KVM run below. Use newer
QEMU or `test-native` to exercise that path. `test-native X2APIC=1` requests
x2APIC through Limine when available; inspect the saved APIC mode instead of
assuming it was enabled. Contract comparison verifies identical versioned exports
for both CPU and IOMMU alternatives.

The timer host fixture also verifies deferred-preemption retry counts, one EOI
per delivery and absence of ISR clock calls; thread probes require actual timer
preemption after voluntary yields.

## Current native status (2026-10-15)

Both latest four-CPU AMD KVM runs pass with actual x2APIC mode 2, zero AVX probe
errors and qualified, active shared TSC at about 2.0963 GHz. All CPUs qualify at
250 ms with the original safety limits; HPET remains the health reference and
fallback. See [latest evidence](#shared-tsc-accepted-on-amd-kvm-2026-10-15) for
run IDs, counts and bounds. Both IOMMU models are emulated on an AMD CPU host;
native Intel CPU and physical devices remain unqualified.

Clock calibration and multi-CPU qualification execute on every relevant boot.
Host reports are analysis artifacts; they neither configure compilation nor
provide a persisted guest calibration. Test-only probes remain isolated.

## Historical validation and profiling evolution

The following dated evidence and intermediate implementation notes describe
prior revisions. Pending, rejected and HPET-only results below are historical;
the current status above and the final acceptance section supersede them.
Use the [SMP contract](../kmodules/shared/smp.md) for the current algorithm.

### Native evidence (2026-10-14)

An earlier successful JSON report from Ryzen 5 5500U recorded four KVM CPUs,
AMD CPU backend, x2APIC mode 2, XSAVE mask 7 / 832 bytes, eight error-free AVX
preemption probes, and emulated VT-d/NVMe DMA revocation. It is preserved as
`bin/tests/isolated/tests/mem-boot-4cpu-intel-native/history/success-1792000728733716314.json`.
The original matching log was overwritten by a later run; this JSON is historical
evidence, not a pass for the latest execution.

Both corrected native runs at that stage passed on Ryzen 5 5500U:

| Device model | Historical run ID | Event callbacks / idle wakes | XSAVE / errors |
| --- | --- | --- | --- |
| Intel VT-d | e88bb4faf7fb46949a08f56df8ec2b1e | 4 / 2 | mask 7, 832 bytes / 0 |
| AMD-Vi | 3700e594b60f4ff19022acb4134e8d43 | 4 / 2 | mask 7, 832 bytes / 0 |

Each result.json says passed and matches its report.json. Both verify guest
x87/SSE/AVX preemption on all four CPUs, NVMe reads/timeout/reset/reopen, and
revoked DMA canary protection. Intel permission-error messages are expected from
the deliberate denied-write probe. AMD QEMU 6.2 lacks missing-PTE fault events,
which remains explicitly noted. SIGTERM after BOOT OK is harness cleanup.

These runs report APIC mode 1 (xAPIC), not x2APIC. The earlier mode-2 report is
historical evidence. TSC status 2 remains UNAVAILABLE; shared timestamps use HPET.
Maximum measured managed IRQ-off is 2.078 ms with one 2 ms notice in the Intel
model run, and 1.189 ms with none in AMD-Vi; these are observations, not bounds.
Native Intel CPU and physical Intel/AMD device qualification remain open.

Each started boot attempt now writes result.json with a unique run ID and status
(running/passed/failed). An older report.json is archived under history/ before
launch; report.json exists only for the current successful attempt. Reports and
results carry matching run IDs. Harness/cleanup failures remove the current
success report. Inspect result.json and boot.log together after Make exits.

The event test separates callback completion from idle coverage. Already-overdue
jobs must dispatch without a mandatory HLT. Future jobs must demonstrate an
actual idle/wake cycle and consumed timer notification; coverage gets bounded
additional attempts if host pauses consume deadlines. Completion diagnostics are
read after runnable worker bookkeeping catches up. Tests compare counter deltas
and print them, rather than requiring four sleeps for four jobs or nonzero
runtime on a finite-resolution clock. Failures still fail the run.

## Invariant TSC rerun

The commands above now expose invariant TSC using a nonmigratable host CPU.
`QEMU_CPU=host` explicitly restores the old baseline for comparison. No host CPU
or KVM settings are modified. A guest feature bit alone does not qualify TSC:
calibration and cross-CPU checks must pass before clock source becomes TSC.

Reports now include `tsc_rejection_reason`, raw guest capability bits, enumerated
and calibrated Hz, sample count, current clock source, and requested versus actual
x2APIC mode. Test kernels always relink when configuration switches, preventing an
older xAPIC-linked test image from surviving an X2APIC=1 rebuild. The earlier native reports listed above predate these changes.

Historical invariant-TSC reruns passed (2026-10-14): Intel model run
`44032295f83247679100e4b578409fd1`, AMD model run
`ea5748de57504c6e87aab953e7031051`. Both verify actual x2APIC mode 2, invariant TSC
recognition, approximately 2.096 GHz calibration, and zero AVX preservation errors.
Shared clock source remains HPET: all 12 samples were collected, but validation
reports CPU offset on CPU 0. The uncertainty-inclusive bound is 30.564 / 26.864 us,
exceeding 10 us even for BSP self-comparison. This does not establish physical CPU
skew. TSC SMP acceptance remains pending a better measurement method.

## Historical fixed-window clock profiles

Previous reruns included a nominal 0/50/100 ms diagnostic phase followed by the
existing 0/25/50 ms qualification phase. Per-CPU raw samples and derived windows
are saved under `tsc_profile` in report.json. Actual intervals may exceed targets
under host scheduling or expensive virtualized reads. Signed drift, endpoint
uncertainty, BSP-relative observed offset and conservative bounds are reported
independently, including explicit BSP self-comparison labels. All complete windows
are analyzed even if acceptance fails its first offset check. This is diagnostic
data, not a new acceptance proof; no samples are discarded yet.

`make -C src test-tsc-profile` checks analysis against synthetic frequency error,
constant cross-CPU offset and uncertainty, including incomplete-data rejection.
Use the same `test-native` commands above to collect the new measurements.

Historical profile evidence: native Intel-model run f8fd76ff81294693af28596f406aeaa2
and AMD-model run 1f77fee8e022424d8004de714093d414 both pass, with 24 observations,
x2APIC and zero AVX state errors. Actual short windows span 2.08–2.50 ms despite
300 us nominal targets; bracket uncertainties span 11.741–114.884 us. Full
qualification-window drift bounds range approximately 845–3285 ppm, so TSC remains
unqualified and shared time stays on HPET. Raw drift alone is not acceptance.

The diagnostic window was extended to 100 ms after the 300 us experiment above.
New reports label it `profile-100ms`; the 50 ms qualification phase and acceptance
bounds remain unchanged. Native measurements of the new window are now available.

100 ms reruns: db7c3375c2aa4aba9becce887cf6aead (Intel model) and
3bea22785daa4f689a949176598620e7 (AMD model) both pass with matching reports,
x2APIC and zero AVX errors. Actual profile windows span 99.863–100.120 ms. BSP
100 ms observed drift is -408.2 / -117.7 ppm, with conservative bounds 676.6 /
352.9 ppm. AP bounds remain above 500 ppm. Qualification still rejects the BSP
self-offset uncertainty; HPET remains selected. These are AMD CPU/KVM observations
with two emulated IOMMU models, not native Intel CPU or physical hardware evidence.

## Adaptive clock profiling

The current sampler replaces the fixed phases above with 10, 25, 50, 100, 250 and
500 ms attempts. Each collects ten paired trials per CPU (twenty observations),
filters wide brackets and three-MAD outliers per CPU, and requires seven retained
trials. Starting/ending bursts overlap trials, avoiding ten full waits per window.
Actual intervals, all samples and rejection masks remain in report.json under
`tsc_profile`. Median estimates are reported; worst retained uncertainty-inclusive
bounds govern acceptance. Persistent per-CPU skew is not classified as an outlier.

Qualification stops as soon as every CPU passes in one window; otherwise the last
failure is reported and HPET stays active. BSP self-offset now cancels correctly,
while its drift and uncertainty remain checked. Prior fixed-window results do not
validate the adaptive flow. Rerun the usual test-native commands for new evidence.

Focused checks: `test-tsc-validation`, `test-smp-clock`, `test-tsc-profile` and
`test-clock`. Deterministic sampler tests cover first-window acceptance, exhaustion,
all-CPU requirements and snapshot bounds; TCG exercises missing-invariant fallback.

Adaptive native reruns now pass: f9005447c07044b5968bfdcd8bf8b809 (Intel model),
6e8a590c95224fcb94c5d3bf5fa743e8 (AMD model). Each collected 480 raw observations
through all six attempts, with x2APIC and zero AVX state errors. At 500 ms median
drift is approximately -247..-270 ppm across CPUs; conservative drift bounds are
320..407 ppm. Shared TSC remains unqualified because some groups retain only six
trials and non-BSP offset bounds remain 61.9..94.8 us. Observed median offsets
alone (-4.8..+6.2 us) do not overcome the uncertainty. HPET remains selected.

## Refined calibration and overhead evidence

The current sampler can apply one qualified >=100 ms BSP median frequency
correction, then repeat that duration with a fresh epoch. Up to nineteen attempts
and 380 observations per CPU are retained, including bounded replacement batches. Raw samples now also carry empty/read
bracket, full call and cross-CPU relay durations, plus input Hz and epoch.
`tsc_profile.overhead` summarizes min/upper-median/max costs per CPU/epoch;
`tsc_profile.calibrations` records frequency history. Group estimates show their
input frequency and epoch. Cross-epoch trial pairs are rejected by the reporter.
No measured overhead is subtracted from safety bounds.

Host tests cover stale/invalid calibration rejection, HPET time continuity,
new-epoch repetition, one-correction limit, persistent CPU skew rejection, cost
snapshots and report auditing. TCG fallback boots pass; native KVM reruns are
needed for real overhead and corrected-frequency evidence. Existing fixed/adaptive
native results above predate this refinement change.

Refinement native reruns now pass: 2cf045dd23464db093d323f146ef2a8b (Intel model),
e419cc91cabe4bc1ad3eada9b866021b (AMD model). Both applied one correction at
250 ms, then sampled that duration again under epoch 3. Fresh BSP drift is -3.484 /
-5.900 ppm; final 500 ms drift bounds are 75..167 ppm across CPUs. Shared time
remains HPET because some groups fall below seven trials and AP offset bounds
exceed 10 us. Post-refinement median overhead ranges: empty reads 1.76..2.01 us,
HPET bracket 16.57..26.37 us, full call 30.36..40.44 us, relay 148.79..219.45 us.
Frequency correction and overhead telemetry are now exercised on AMD KVM; this
does not qualify native Intel CPU behavior or physical hardware.

## TSC-only offset exchange

Current native runs additionally collect ten TSC-only exchanges per AP at both
ends of each attempted window. APs are woken once for a short shared-memory batch;
HPET/IPI setup remains outside its timing brackets. Half-round-trip uncertainty
plus conservative conversion rounding bounds each midpoint offset without
assuming symmetric delays. Counter regressions abort; watchdog/iteration limits
bound waiting and AP polling exits during cleanup.

`tsc_profile.groups` now qualify frequency only. `offset_groups` qualify each
CPU's starting/ending offset batches separately; every CPU must pass both with
seven retained trials, and every retained conservative offset bound must be
within 10 us. `exchanges` stores raw BSP-before/AP/BSP-after timestamps, Hz/epoch,
offset, uncertainty, round-trip time and rejection decisions. The older offsets
in HPET `windows` are diagnostic-only. Reporter checks prevent missing exchange
evidence, invalid rounding bounds or cross-epoch data from appearing accepted.

Focused UBSan tests cover clean exchange acceptance, persistent skew rejection,
asymmetric slow-response uncertainty, regressions and bounded timeout cleanup.
Isolated TCG boots exercise fallback; user-host test-native reruns are needed for
real AP polling behavior and precision. Earlier native reports predate this path.

The clock boot test deliberately invalidates calibration proofs. If startup had
accepted shared TSC, the test now revalidates after that recalibration before
thread/device checks. This preserves honest active-clock coverage; reports describe
the final validation. Accepted proofs require frequency and both offset groups
for every CPU, and retained exchanges must match the reported conservative bounds.

TSC-only native reruns pass: 1abf70cfd63e44d2808116e4fbc6247d (Intel model) and
02e989eb6521452d88726f411683d881 (AMD model). Conservative offset bounds improve
to 1.697–3.858 us and median RTT to 3.606–4.057 us. Final frequency drift bounds
are 99–203 ppm. Some groups still retain fewer than seven trials, so shared time
remains HPET. Zero-MAD filtering can incorrectly reject 5–10 ns variations against
~1.7 us uncertainty; these results motivate uncertainty-aware filtering and
replacement samples without weakening qualification limits.

## Uncertainty-aware filtering and replacements

The current filter preserves small estimate differences within overlapping
measurement uncertainties, including zero-MAD quantization patterns. Width
outliers must exceed median + max(three MADs, median), and still obey 50 us.
Drift/offset rejection compares against the larger of three MADs and combined
trial/representative uncertainty. Acceptance still uses the full conservative
bounds, minimum seven retained trials, 10 us offset and 500 ppm drift budgets.

A shortage can request two fresh ten-trial batches at the same duration only
when the measured bounds otherwise fit. Persistently bad bounds are not retried
to seek a favorable subset. Old observations remain recorded; groups do not pool
trials from different windows/epochs. `tsc_profile.attempts` distinguishes these
replacements from longer durations and calibration verification.

Regression checks cover the native 128/133/138/143 ns zero-MAD pattern, small
frequency variations inside uncertainty, genuine outliers, persistent bad clocks,
replacement recovery, bounded exhaustion and refusal to retry bad bounds. Offline
replay of the previous native datasets qualifies all groups at 250 ms under the
new filter; this is analysis evidence, not a new native execution or build input.
Native KVM reruns now qualify shared TSC; see the latest evidence below.


## Shared TSC accepted on AMD KVM (2026-10-15)

Latest report/result pairs both pass:

| Emulated IOMMU | Run ID | TSC Hz | Max offset bound | Max drift bound | Max HPET uncertainty |
| --- | --- | ---: | ---: | ---: | ---: |
| Intel | e57e0cf740d44dbb9058b9ecc16f086b | 2,096,296,396 | 3,168 ns | 407 ppm | 47,193 ns |
| AMD | 7a0ff1412aed4eea8aef36dc5cc7a090 | 2,096,294,219 | 3,676 ns | 348 ppm | 49,900 ns |

Both report VALID shared TSC, active TSC source, no rejection reason, x2APIC mode
2 and zero AVX probe errors. Each records six attempts, 480 raw HPET samples
and 360 TSC-only exchanges. All final start/end offset batches retain 10/10;
final frequency groups retain 9/9/8/9 trials (Intel model) and 9/9/7/9 (AMD
model). The final window is 250 ms in both: Intel verifies a refined calibration
epoch, AMD replaces a quality-short batch without pooling observations. Existing
minimum counts and uncertainty-inclusive safety limits remain unchanged.
These are Ryzen 5 5500U / AMD CPU backend observations with two emulated IOMMU
models, not native Intel CPU or physical DMA qualification. HPET health checks
and fallback remain enabled. Reports remain diagnostic artifacts, not build
inputs or persisted guest clock configuration.


## xHCI transport acceptance

```sh
make -C src test-xhci
make -C src test-xhci-boot
make -C src test-xhci-boot QEMU_CPUS=4 QEMU_IOMMU=intel
make -C src test-xhci-boot QEMU_CPUS=4 QEMU_IOMMU=amd
```

Host UBSan fixtures use the production driver with modeled MMIO/PCI/DMA/IRQ
adapters. They cover ring cycles, short transfers, blocking completion/deadline,
cancellation, stale tokens, restart reuse, malformed capabilities, firmware
refusal, command timeout and retained DMA on failed halt. The boot probe runs in
a test-only module, sends raw control requests and cancels an interrupt transfer.
It checks EP0 evaluation, 100 descriptor reads, transfer-ring wrap and real MSI-X
completion. QMP over process pipes removes/reinserts a disposable test keyboard;
port generations advance and stale device tokens stay invalid. It then disables
the slot and closes/reopens the controller. USB core/HID are not implemented by
these test fixtures. The tests pass on one CPU without IOMMU and four CPUs with
both emulated IOMMU models. Report/result IDs identify each fresh run.

Hotplug reports use `mem-boot-<N>cpu[-intel|-amd]-xhci-hotplug` beneath the isolated
test directory. `boot.log` holds guest diagnostics; `qemu.log` separately holds
emulator stderr so DMA fault probes cannot overwrite guest output. Optional
`--xhci-trace` on the Python harness enables controller trace events there.
Candidate media contains no probe module and persistent images are not attached.
Physical USB3/controller quirks and native KVM xHCI acceptance remain pending.

Latest isolated acceptance IDs (2026-10-15): one CPU / no IOMMU
`9ba70b417ca64d09931e4634838b3af4`, four CPUs / Intel IOMMU
`5993d29f8a734e189fd2b0670d41f1bd`, four CPUs / AMD IOMMU
`a4335812b6ae417e9e193624b0fdee23`. Each matching result/report pair passes and
records 100 control transfers, 111 deferred interrupt notifications and 113
controller events. Candidate build and relocation checks pass; persistent
NVMe/SATA image identity, size and modification timestamps remain unchanged.
