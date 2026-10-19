# Isolated acceptance

Boot tests rebuild below `bin/tests/isolated`, use test-only kernels/probes and
create disposable storage. Original NVMe read tests attach known data read-only;
focused block-write tests make only their disposable boot image writable.
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

## Address spaces and module activation

For native collection with automatic comparison, run
`./scripts/bench-spaces-kvm.sh` from the project root. It builds once, retains
independent per-boot outputs and a small report archive, and records the latest
session in `bin/tests/kvm-spaces/latest.json`. No guest export is needed. See
[collection and comparison details](address-space-profiling.md#kvm-collection-and-automatic-comparison).

`test-spaces` runs production page-table construction/teardown on host-backed
memory with UBSan, including partial-construction OOM. `test-spaces-boot
QEMU_CPUS=4 QEMU_IOMMU=amd` (or intel) uses kernel-only probes and disposable media
to exercise same-VA private mappings, CR3 selection across timer/yield/block/exit,
active remote TLB invalidation, owner checks, leases, teardown and stale IDs.
Eight cycles check exact hardware #PFs for read-only writes (error 3), NX fetches
(error 17) and unmapped reads (error 0), including revocation while CPU1 cannot
switch roots but can receive IPIs. The original CPU-local IDT gate is restored
after each probe; unexpected faults halt. The fault code/assembly is linked only
into the isolated probe module, never production inthandle. Each cycle checks
that owned page/table/stack reservations return to baseline.
Reports live under `bin/tests/isolated/tests/spaces-<N>cpu-<iommu>/`.
The same isolated target now records paired clock/same-root/root-change controls,
active versus inactive protection changes, yield elapsed time, and ten teardown
trials per dense/sparse geometry. Raw samples, median/p95/max, worker identity,
clock and PGE/PCID metadata are saved in `report.json.address_spaces`; missing or
malformed groups fail acceptance. See [method and dated measurements](address-space-profiling.md).
Independent HPET probes also measure 64 batches of timestamp, clock and ordinary/
recursive/IF-enabled lock calls. Reports expose the selected AMD/Intel timestamp
ordering and retain raw overhead measurements. The KVM collection script supports
`--runs 3` and `--reference-session PATH`, or automatically uses the last passed
session for a separate native-comparison.md/.json. Host checks for timestamp
selection/AP agreement are available through `test-cpu-tsc test-cpu-ordering`.
User-mode execution and a handler invocation boundary are not tested or supplied.

The profiling follow-up adds test-cpu-profile host checks for rate/epoch changes,
regression, HPET fallback and saturating conversion. test-clock verifies a
profiling-observed fault disables TSC statistics and selects HPET. The isolated
space boot briefly forces a synthetic statistics rate solely to test TSC-only
samples, local fallback and mixed-epoch rejection, then restores actual clock
policy before measuring. The watchdog failure probe forces fast statistics while
verifying the independent HPET timeout still fires. Synthetic rates never become
calibration data, candidate configuration or benchmark samples.

Address-space reports now retain 114 peak-attribution records (wait/hold/IRQ for
eight cycles and thirty geometry trials), with CPU, method/name, domain, caller,
source/epoch and address-space identity. Thirty-eight quality records expose TSC,
HPET and unavailable sample counts. Missing, duplicate or inconsistent peak
records fail reporting. Native comparisons warn if the reference used different
or unrecorded profiling sources. Coverage remains managed BSP instrumentation;
raw CLI fault guards and unrelated AP intervals are not included.

`make -C src BUILD=../bin/tests/isolated test-interfaces` checks the proposed
handle and `/@keyboard` C/C++20 layouts, framing bounds and structured key/Unicode
validation. These are contract tests, not runtime permission, HID or userland tests.

Runtime module initialization and startup now both reject discovery, captured
stubs, callback leases and table publication. `test-hub` checks concurrent gate
rejection; `test-module-disk-boot` checks ABI2 private lookup, hidden hooks,
init/start failure, success and retirement. All existing modules stay Limine-loaded.

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

## ACPI namespace and platform inventory

`make -C src test-acpi` runs UBSan tests for segmented/bridged PCI associations,
duplicate roots/BDFs, descendant invalidation, platform filtering/parentage and
publication rollback. `make -C src test-pci` also checks firmware config access
restrictions, companion ownership and initialization rollback.

`make -C src test-acpi-boot` rebuilds only below bin/tests/isolated and injects a
small test-only SSDT using QEMU `-acpitable`. It checks known memory/I/O/IRQ resources,
_DEP resolution, AML integer/buffer evaluation, unchanged output on failure,
malformed _CRS flags and non-PCI _ADR classification. No fixture enters candidate
media. The ordinary boot suite also checks ACPI/PCI/platform consistency.

After building the isolated images, vendor/model variants can be checked with:

```sh
python3 src/tests/mem-boot.py bin/tests/isolated --cpus=4 --iommu=intel --cpu=max,vendor=GenuineIntel --acpi-fixture
python3 src/tests/mem-boot.py bin/tests/isolated --cpus=4 --iommu=amd --cpu=max,vendor=AuthenticAMD --acpi-fixture
```

Successful runs store reports under mem-boot-1cpu-acpi,
mem-boot-4cpu-intelcpu-intel-acpi and mem-boot-4cpu-amdcpu-amd-acpi. These are
emulated-firmware acceptance, not native KVM or physical ACPI qualification.

## USB core

```sh
make -C src test-usb test-xhci
make -C src test-usb-boot
make -C src test-usb-boot QEMU_CPUS=4 QEMU_IOMMU=intel
make -C src test-usb-boot QEMU_CPUS=4 QEMU_IOMMU=amd
```

Host tests exercise production parsing/core code with modeled transport under
UBSan, including malformed descriptors, OOM rollback, interface scoping and
connector reuse after detach and controller restart. xHCI watcher tests assert
port/failure wake ownership and unsubscribe-before-close.

Boot acceptance uses isolated images/disposable disks and QMP unplug/replug of
the test keyboard. USB core enumerates the real QEMU keyboard, mouse and boot
storage, retrieves/validates configurations, claims storage endpoints and issues
GetMaxLUN through scoped class control. It performs no storage reads or writes.
The test observer uses USB watch/block notifications rather than polling for
attach/configuration. Pending keyboard interrupt I/O must return on shutdown, followed by a clean bus
restart without growing the connector table. HID report interpretation remains
outside this test and outside USB core.

Successful reports live under mem-boot-<N>cpu[-intel|-amd]-usb-hotplug. USB3 parser
rules are modeled; native/physical USB acceptance remains pending. Test probes
never enter the candidate ISO. The build also checks new USB objects for absolute
runtime pointer relocations, which the format3 loader cannot relocate.

## External USB hubs

```sh
make -C src test-usb-hub test-usb test-xhci
make -C src test-usb-hub-boot
make -C src test-usb-hub-boot QEMU_CPUS=4 QEMU_IOMMU=intel QEMU_CPU=max,vendor=GenuineIntel
make -C src test-usb-hub-boot QEMU_CPUS=4 QEMU_IOMMU=amd QEMU_CPU=max,vendor=AuthenticAMD
```

The host hub test exercises production descriptor and port policy under UBSan:
descriptor bounds and the narrow QEMU legacy quirk, reset/debounce, connection
edges arriving during enumeration, detach, SuperSpeed warm-reset selection and
overcurrent. The core model checks an unauthorized downstream request fails and
a malformed child remains a device-local failure. The xHCI model additionally
checks route saturation, multi-TT inheritance and postorder slot removal.

Isolated boot tests add an external hub, a nested hub and child devices to the
disposable VM. QMP removes/re-adds a child, then removes the upstream hub while a
grandchild interrupt read is blocked. The guest verifies terminal disconnect,
stale references, record reuse after topology reattachment and class/core shutdown.
Only test images include these devices/probes; no persistent development disk is
attached. QEMU 6.2 emulates a full-speed hub, so actual high-speed multi-TT and
SuperSpeed hub transfers, native KVM and physical hardware remain unqualified.

Latest passing result/report pairs share these run IDs:

| Configuration | Run ID |
| --- | --- |
| 1 CPU, TCG | `93bfacde1eb141d0afe063dfde381eb1` |
| 4 CPUs, Intel CPU/IOMMU model | `e3b929c5717f41a3b5c3a27192461386` |
| 4 CPUs, AMD CPU/IOMMU model | `0a248cf5c18245acafdeea9edebe20d4` |

Reports are under `bin/tests/isolated/tests/mem-boot-<N>cpu[-intelcpu-intel|-amdcpu-amd]-usb-hubs/`.
The existing root USB hotplug/restart regression also passes, run
`01be06e4f9ff47e386ef12a9183cc073`.

Lifecycle/descriptor closure (2026-10-16): the probe now performs two class-only
stop/start cycles with USB core still running and the same root attachment ID.
Each cycle disconnects a blocked grandchild interrupt read, rejects stale child
references and restores fresh descendants. Page-ledger stack counts verify that
all three manager/worker stacks are reclaimed on stop and restored without growth
on restart. Host tests reject inconsistent second-read length, type, port count,
characteristics and power fields, invalid prefixes and short responses, preserving
the narrow QEMU legacy quirk. SuperSpeed's complete first read is checked too.

## Boot configuration

```sh
make -C src test-config
make -C src test-config-boot
make -C src test-config-boot QEMU_CPUS=4 QEMU_IOMMU=intel QEMU_CPU=max,vendor=GenuineIntel
```

Host tests use production config code under UBSan: typed values/defaults,
UTF-8/GUID/decimal/range validation, syntax, duplicate/unknown/missing fields,
owned lifetime, allocation rollback and publication failure. The real boot probe
checks config hub pointers and batch name-collision rollback. Isolated negative
boots remove/duplicate the Limine data-module tag or corrupt the config version;
all must fail before startup completion, with a line diagnostic for invalid text.

Positive result/report pairs: `mem-boot-1cpu`, run
`9202620014eb4e8c93aa4128aebd0259`; `mem-boot-4cpu-intelcpu-intel`, run
`c8f1405484bd48888598829786ce9458`. Negative result/log directories are
`config-missing`, `config-duplicate` and `config-invalid` under the same isolated
tests directory. These fixtures never alter the candidate configuration or
attach persistent development disks. See [config contract](../kmodules/shared/config.md).

## Appended ext4 boot partition

```sh
make -C src test-boot-image
make -C src test-mem-boot
```

The host image check extracts the packaged cloudberry.cnf, validates primary and
backup GPT CRCs, confirms that its drivers.partition_guid selects exactly one
Linux filesystem partition of at least 512 MiB and preserves an EFI partition.
It extracts that partition to a disposable sparse regular file, runs e2fsck -fn
and verifies the configured /boot/kmodules/drivers directory through debugfs.
Normal and fault boot fixtures use the shared image builder, with all formatting
at build time. The real boot probe now validates the typed partition-GUID reference
and five config entries. Latest one-CPU UEFI acceptance: 99222c353e564ba1b12adc9f87cbce3d.

The image remains read-only in QEMU. These host image checks alone do not establish guest storage access;
the separate storage tests below validate USB block reads and guest ext4 access. Persistent
NVMe/SATA images are unchanged, and candidate images are never booted automatically.


## USB storage and ext4 readers

```sh
make -C src test-usb-storage test-storage-fs
make -C src test-storage-boot
make -C src test-storage-boot QEMU_CPUS=4 QEMU_IOMMU=amd USB_HOTPLUG=1
```

Host tests exercise production BOT validation, command sense, transport failure,
read bounds, retained-parent lifetime and GPT CRC/identity/OOM failures. Independently
formatted ext4 fixtures cover 1/2/4 KiB blocks, explicit checksum seeds, 512-byte
inodes, regular/fragmented extent trees, sparse data, EOF and metadata corruption.
All host filesystem fixtures are disposable regular files under bin/tests/isolated.

Storage boot probes remain test-only kmodule code. The harness populates the isolated
ext4 driver directory with a deterministic 16 KiB probe file using the build-time
`--drivers-source` option. Candidate images do not receive that fixture. The guest
reads the actual USB boot device via BOT/SCSI, selects config.drivers.partition_guid,
mounts ext4, lists and reads the file, checks lifetime pins/stale cookies, and closes
all layers. Four-CPU AMD-IOMMU/QMP validation removes and re-adds USB storage, verifies
failed access through the mounted filesystem, teardown, fresh USB identity and
reopening. QEMU reattachment uses a fresh read-only block node for the same isolated
image. No candidate VM or persistent developer disks are used.

Acceptance evidence (2026-10-16): isolated one-CPU storage boot
`4e5ee22d226e47be8e04161ce3b1d66f` and final four-CPU AMD-IOMMU storage
unplug/replug boot `8326cddee63140c3a718bf69dc646fb7` passed with matching
result/report IDs. These use TCG and do not establish native or physical-device
acceptance. The final candidate image passed host GPT/ext4/configuration checks
and contains no isolated probe file.


## Writable ext4 and journal durability

```sh
make -C src test-usb-storage test-storage-fs test-ext4-runtime test-ext4-write
make -C src test-storage-write-boot QEMU_CPUS=1 QEMU_IOMMU=intel
make -C src test-storage-write-boot QEMU_CPUS=4 QEMU_IOMMU=amd
```

`test-ext4-write` creates ordinary disposable GPT/ext4 image files under
bin/tests/isolated/tests/ext4-write. It exercises create/grow/append/overwrite,
truncate/zero extension, remove, directories and rename; read-only remount checks
persisted data and indexed directories. Independent e2fsck validates allocation,
checksums, links and clean journal state. Trace-guided process termination tests
journal descriptors, commits and checkpoints for creation/write/append/truncate/
remove/rename. Six injected write/barrier failures check poison/parent retention
and fresh-process recovery. Reports include a run ID and running/failed/passed state;
failed reruns cannot leave an older passing report current.

Crash tests model whole block writes followed by flush, not torn sectors or lying
physical write caches. Kernel byte/sort primitives are checked separately against
libc behavior; no host mount, loop device or persistent developer disk is used.

Writable boot tests alone remove read-only attachment from the isolated USB test
image. The guest executes USB WRITE and SYNCHRONIZE CACHE through the common block
contract and GPT, mutates ext4, syncs/unmounts and verifies read-only remount data.
After QEMU exits, host GPT/ext4 checks run again; any failure changes result.json
and removes the successful report. The ordinary candidate run stays read-only and
receives no probe file. These kmodules continue to arrive through Limine.

Writable acceptance (2026-10-16): mem-boot-1cpu-intel-storage-write: `cc1eb884b5684c17821cccbae5fc8630`; mem-boot-4cpu-amd-storage-write: `7dcb91ebb7d74dcab6f81dab13759754`.
The host journal run `b57ce593cdd04c7cba30cd8c712638b7` passes 29 crash cuts and six
write/barrier failure cases, with independent fsck and no sanitizer errors.
All guests use TCG; physical/torn-sector durability is still unqualified.

## Focused NVMe/AHCI boot-device block tests

```
make -C src test-block-boot BLOCK_BACKEND=nvme QEMU_CPUS=4 QEMU_IOMMU=amd
make -C src test-block-boot BLOCK_BACKEND=sata QEMU_CPUS=4 QEMU_IOMMU=intel
```

`block-boot.py` runs a dedicated test kernel and `probe.block` on disposable
media below `bin/tests/isolated/tests/block-<backend>-<cpus>cpu-<iommu>`.
The hybrid image itself boots through the selected NVMe or AHCI transport;
no USB-storage boot shortcut and no developer disk image is attached.
The probe uses the configured GPT GUID and existing partition/ext4 modules,
reads a patterned fixture, creates/appends/renames/deletes a journaled file,
and remounts readonly to check persisted bytes. It checks sector bounds, capacity,
zero-length I/O, slice bounds, retain/close refusal, actual MSI/MSI-X delivery,
lost-notification timeout/reset, fresh cookies and stale-descriptor refusal.
NVMe additionally rejects invalid/inactive namespace IDs and explicitly reopens
namespace 1. After VM termination, independent GPT/backup CRC and `e2fsck -fn`
checks qualify the final image. `result.json` is always replaced per run; only
passing runs get `report.json`. Failure removes any earlier passing report.

PCI's focused host suite covers 32/64-bit single-vector MSI programming, MME
clearing, masking, owner/entry rejection and truncated capability bounds, alongside
existing MSI-X and DMA/IOMMU cases. Physical hardware, torn-sector durability,
simultaneous storage instances, NCQ and SATA hotplug are not qualified.

Latest isolated TCG qualification (2026-10-16 local date):

- sata, 1 CPU(s), none IOMMU model: `f07052b863014a79a6a818d39b11b330`.
- sata, 4 CPU(s), intel IOMMU model: `12251496e312483d93c83d3fb8027ba0`.
- nvme, 4 CPU(s), amd IOMMU model: `a9c71348fc9a44e1ac0ed6200914b615`.

## Dynamic hub and provider draining

```
make -C src test-hub
make -C src test-hub-boot QEMU_CPUS=4 QEMU_IOMMU=amd
```

The host test uses shared page aliases and real generated RX mediation stubs,
three concurrent registry readers and a blocked caller. It grows to 602 methods
and 200 owned tables, injects physical-allocation and writable-alias mapping
failure, verifies retry without partial publication, stable pointers, boot/table
pinning, active and leased draining, resume, irreversible retirement and stale-call
failure. No fixed giant physical registry is allocated.

The dedicated isolated guest test late-loads format3 synthetic modules, rejects
format2 before allocation/publication, grows by 600 methods/200 tables, checks
actual RO/NX and RX page flags and executes a late stub on CPU1. It exercises blocked-call accounting plus thread,
scheduler-result and IRQ-route leases. It drains and retires only synthetic test
providers. All production boot modules remain pinned. Four-CPU AMD-Vi TCG passes;
physical image unmapping/table withdrawal are deliberately not implemented.
The storage regression boots from disposable AHCI with four CPUs and Intel VT-d,
then exercises configured-GUID ext4 writes/remount and independent fsck checks.
Results live under `bin/tests/isolated/tests/hub-4cpu-amd`; no candidate VM or
persistent developer disk is attached. See [the current hub contract](../kernel/hub.md).

Latest dynamic-hub qualification (2026-10-16 local date):

- `hub-4cpu-amd`: `91e42dc936974637bb5c6bd3f2283b84`, passed.
- `block-sata-4cpu-intel`: `a77daa62958c440e809186dfb54b0383`, passed.

Resized-layout qualification (2026-10-17): the isolated four-CPU AMD-Vi TCG
boot rebuilt the kernel and hub probe against 16 MiB windows and passed late
registration, stable RO/RX mappings, CPU1 stub execution, caller/lease draining,
retirement and stale-call rejection. GPT/ext4 host checks passed afterward.
Run ID: `8ad19743be294d31aeefcb8b0d6c1209`. Candidate VM and persistent developer disks were not used.

Module-image arena acceptance (2026-10-17): isolated four-CPU AMD-Vi boot
loaded a 20 MiB BSS-heavy image beyond the former aggregate 16 MiB boundary,
verified RX/RW-NX mappings and an unmapped guard, rejected an oversized virtual
request and a 600 MiB physical request on a 512 MiB VM without new exports.

## Protected processes and first isolated handler (2026-10-17)

```sh
make -j4 -C src BUILD=../bin/tests/process TEST_BUILD=../bin/tests/process test-process-elf test-manifest test-handles test-keyboard test-interfaces
make -j4 -C src TEST_BUILD=../bin/tests/process test-process-boot QEMU_CPUS=1 QEMU_IOMMU=none
make -j4 -C src TEST_BUILD=../bin/tests/process test-process-boot QEMU_CPUS=4 QEMU_IOMMU=amd
python3 src/tests/keyboard-boot.py bin/tests/process
```

The process probe checks same-VA isolation, preemption, user fault containment,
owned cleanup, copied handle calls, boot manifest authorization, filtered
reflection and cross-CPU request/reply. It then loads the production keyboard
ELF/manifest/layout bundle from ext4, admits independent full/readonly/denied
clients, injects actual QMP USB key events, removes/replaces the keyboard and
checks structured arrows and empty replacement state. Cooperative decommission
removes discovery, closes the I/O worker and reaps owned process/stack state.

Host UBSan checks cover layouts/Unicode/modifiers/repeat, replayable event cursors,
grant-time filtering, retention/deadlines, resynchronization anchors and preserved
error payload/results. General HID, multisession policy, automatic provider rebind,
installer/signatures and native KVM/physical user-mode input remain open.

Accepted integrated TCG runs: one CPU/no IOMMU
`4b52f2ae41564258972e34143507c949`; four CPUs/VT-d
`5416fd1d535b47899f1eeafd75001032`; four CPUs/AMD-Vi
`a6fcf05306744aa3b13f59b96e329531`. JSON reports/boot logs live below
`bin/tests/process/tests/process-*`.

`keyboard-boot.py` separately boots the ordinary production image, without probe
modules or fixture applications, using readonly disposable USB media. It waits
for automatic `/@keyboard` publication. The explicit USB BOT/SCSI hard-disk
attachment preserves the hybrid image's 512-byte GPT sectors; automatic readonly
optical selection had hidden that partition during the first check. Candidate
VMs and persistent developer disks are never attached by these tests.

Final production startup acceptance: four-CPU AMD-Vi TCG run
`5d0ff656a58646828234075c05d1d731`. The one/four-CPU thread/SMP regression
also passes, including xstate, block/wake, remote TLB reuse, deferred IRQ delivery
and NVMe timeout/reset. Its interrupt fixture uses vector 129 because vector 128
is now owned by the process gate; boot config acceptance expects all eight items.

Lifecycle hardening acceptance (2026-10-18): four-CPU AMD-Vi TCG run `1d9d53aaf1d54f45a377f4fac70bf815`
adds two fault/restart cycles. A test-only keyboard image faults after dispatch
while USB input is active; its independent client receives the original payload,
trace and UNKNOWN outcome. Explicit reap releases I/O ownership; a fresh real
provider reacquires the device, stops cooperatively and returns stack ownership
to baseline. No fault trigger enters the production bundle. Host status checks
cover publication, faults, stop requests, withdrawal and reaped processes.

## Process/handle measurement harness

`./scripts/bench-handles-kvm.sh --runs 3` collects native one/four-CPU acceptance
and handle measurements with both IOMMU models. See scripts/README.md. Every
case retains 48 samples after eight warmups, separate clock brackets, raw
median/p95/max, kernel lock counts/peaks and caller/method/source attribution.
The client validates results and releases retained request records. Timings
include these costs. Empty-bracket subtraction is only an estimate. Metrics reset
on each participating CPU before client launch; peaks still include process
launch and owner waits. This is a baseline, not a saturation/load test.

The collection/comparison/archive flow passed under single-CPU TCG in session
`20261018T053801.212505Z`; separate four-CPU AMD-Vi guest collection passed.
TCG empty brackets were about 0.42 ms (one CPU) and 2.17 ms (four CPUs); these
validate collection, not native performance. Parser tests reject incomplete,
duplicate, malformed and wrong-topology data. No /dev/kvm is available here;
native user-mode/input timing acceptance remains user-host work. The user process
fixture now also preserves SSE and available AVX values across its gate and timer
preemption; AVX execution requires guest CPUID/OSXSAVE/XCR0 support.

Generic provider startup acceptance uses the production loader for keyboard and
a separate startup-check bundle that reads arbitrary named data. Bootstrap has
no keyboard-specific header or layout parser. Host tests cover descriptor paths,
identity/version/unknown/duplicate rejection, copied packet framing and startup
allocation rollback at each failing allocation. Status snapshots enumerate copied
process/state values, and live/unreaped metadata release is refused. Layout
configuration now belongs to keyboard/config.cnf; boot.providers selects bundles.

Final generic-startup acceptance (2026-10-18): one-CPU TCG
`1be5eb78706e49a28da981c9206e7b02`, four-CPU AMD-Vi TCG
`6c8a1af5a72d4655b94ac7e98f2c638b`, and ordinary four-CPU AMD-Vi production boot
`0a9842560b1a4e4ebbc53d015c467372` all pass. The final one-CPU collector/archive session
is `20261018T055222.122944Z`. Host C/C++20 interface, provider-definition, config,
keyboard, startup/OOM and status checks pass; parser/host-identity/hash checks
pass after excluding volatile MHz from reference identity. Candidate bundle
contents include only the real keyboard executable, descriptor, manifest, config
and two layouts. Fault/startup-check/benchmark fixtures remain isolated.

KVM session `20261018T060145.736826Z` passes all three one-CPU correctness/timing
boots with TSC. Its first four-CPU AMD-Vi correctness boot also passes, but uses
HPET (source 2, calibration 3), with zero unavailable profiling samples. The
collector correctly rejects this mixed-clock baseline. The original log omitted
SMP rejection/runtime fallback reasons, so it cannot establish why TSC was not
active. This session remains failed and is not promoted to a complete baseline.

The process probe now records clock source/reason at origin, measurement begin
and end, plus SMP qualification reason/CPU/status/sample counts and bounds. Raw
frequency samples/call costs, signed groups, offset groups and TSC exchanges are
retained under handles.clock_profile. These are observations of existing records;
no calibration, acceptance limit, health guard or production clock behavior changes.
The raw qualification dump precedes the metrics reset; phase snapshots remain
outside the timed operation loop.
The host parser checks framing/indexes/duplicates and decodes signed quantities;
reported rejection distinguishes fallback from unavailable profiling samples.
Each boot gets timing.json and completed correctness boots remain visible in a
failed session summary. Seven host checks and four-CPU TCG boot
`dba210e7ea9b461a8b2b3ce0f603fe0c` pass. TCG rejects invariant-TSC availability
as expected; native qualification records require the user-host rerun.

Targeted native follow-up `20261018T061208.303777Z` passes four-CPU AMD-Vi,
run `6f139d9f897e43c9a7aadcc10a02abf6`. TSC stays selected at origin, begin
and end (~2.096172 GHz, calibration 3), with zero current/qualification rejection
reasons and zero unavailable/HPET profiling samples. Eight adaptive attempts
produce 640 frequency samples, 480 offset exchanges and 32 CPU/attempt groups.
All CPUs pass the final 500 ms window after one frequency refinement; the final
offset bound is 680 ns, drift bound 211 ppm and uncertainty bound 49,702 ns.
The least-retained final CPU group has seven samples, meeting the existing
minimum. No limit was relaxed. Earlier HPET selection is not reproduced and
its cause remains unknown because its original diagnostics were absent.

Raw median timings are 10.239 us clock bracket, 17.207 us acquisition,
21.324 us reflection, 20.628 us clock-handle case, 132.673/157.601/237.846 us
for 0/128/4096-byte cross-CPU echo and 54.705 us allocation/free. These include
measurement/validation/release costs; they are not pure IPC durations. Peak
managed hold/IRQ-off is 257/258 us in memory.unmap_batch, and BSP lock wait
reaches 432 us. The scope includes reset-thread teardown/start/owner waits, so
these peaks do not yet identify a handle-only bottleneck. A complete repeated
one/four-CPU/both-IOMMU baseline remains pending before performance changes.


Handle phase profiling and simultaneous-client/resource acceptance are described
in [handle-profiling.md](handle-profiling.md). `--profile-handles` selects a
separate disposable broker; ordinary candidate builds contain no probes.


`make -C src test-installations` checks installation framing, identity/path binding,
privilege ceilings, C/C++ startup contracts and load/admission allocation rollback.
The process boot acceptance also runs a disposable registered lifecycle launcher:
concurrent ordinary apps, argv/cwd, manifest/owner denial, cooperative stop,
deferred wait timeout/replay, child fault, caller abandonment and service cleanup.
The candidate contains only `launch` and `true`, with no automatic test launch.


## Console and software graphics

`make -C src test-console-host` runs UBSan raster/quoting and stream ownership,
backpressure, EOF/delegation/cleanup tests without booting. `test-interfaces`
checks new rendering/stream/filesystem/launch C layouts in C and C++20.

`make -C src test-console-boot QEMU_CPUS=4 QEMU_ACCEL=tcg` builds an isolated
fixture image under `bin/tests/console/<run-id>`. The original candidate remains
unchanged. The fixture registers graphics-check and a passive initial supervisor,
plus a binary file larger than the pipe buffer. Real USB keyboard injection drives
the production terminal; screenshot decoding verifies visible text. The test checks
recreate/append, piping, cwd, protected paths, graphics primitives and damage,
foreign-owner denial, abandoned-surface cleanup missing-file error delivery, independent stderr and interactive stdin/EOF.
Type-ahead spanning idle/foreground transitions is retained in one event queue.
After stopping QEMU it independently extracts and compares the large copied file.
Only disposable boot media is attached; no developer NVMe/SATA disk is opened.
Screenshots, decoded text, logs and result JSON remain in the run directory.

For native correctness acceptance, use the same target with `QEMU_ACCEL=kvm`.
This is not a replacement for repeated uninstrumented performance measurements.
No /dev/kvm is exposed in the agent environment.


`make -C src test-filesystem-tree` validates the generated production namespace,
flat executable/manifest/resources paths, catalog-derived identity indexes,
root-private state, shared keyboard definitions and every Limine module path.
The isolated console test additionally writes/reads terminal's own root app-data
and exercises `ls -a` for hidden underscore metadata. All test writes use a copied
disposable image.


For a stalled console, run the disposable harness with `--diagnostics`:

```sh
python3 src/tests/console-boot.py bin --cpus=4 --accel=tcg --fixtures --diagnostics
```

On failure it attaches GDB through a test-only Unix socket and saves
`diagnostics.log`: relocated module symbols, vCPU stacks/registers, lock/watchdog
state, scheduler threads, processes, handle requests/publications/channels,
keyboard capture state and xHCI endpoints. Reads do not call guest methods or
modify guest state; debugger attachment pauses/resumes the guest. No debug server
is added to normal candidate startup. Fatal kernel messages are detected before
waiting for a screen timeout. Keyboard injection uses explicit press/release
edges, avoiding dependence on QMP `send-key`'s guest-time delay queue. The final
idle-and-resume step checks that an empty interrupt-IN receive stays armed and
resumes input without recurring idle cancellation/rearming.

`test-hpet-counter` forces high-half rollovers, transient/persistent zero reads
and bounded failure. Keyboard host tests cover pending-node reuse and failed
worker notification; xHCI's model checks real endpoint states and intermediate
Stopped transfer events.

HPET zero-read diagnosis uses a separate kernel CPU object; the candidate kernel
is never replaced:

```sh
make -C src ../bin/tests/hpet-zero-kernel.elf ../bin/tests/apps/graphics-check.elf
python3 src/tests/console-boot.py bin --fixtures --cpus=4 --diagnostics \
    --kernel bin/tests/hpet-zero-kernel.elf --mmio-trace
```

This deliberately traps on the first rejected all-zero sample and reports a
failed console run when captured. Inspect `hpet_zero_probe`, its guest page-table
walk and `mmio.log`; the probe is not an acceptance test. Tracing is expensive
and can itself exhaust provider startup deadlines. Run
`python3 src/tests/console-boot.py bin --cpus=4 --accel=kvm --storage-devices --diagnostics`
for an isolated native reproduction with fresh 1 GiB NVMe/SATA disks. It attaches
neither persistent development disk. Provider startup logs include bundle,
launch/readiness phase, and available process exit/fault information; debugger
snapshots include bootstrap records. See [the HPET investigation](../kmodules/shared/clock.md#zero-read-cause-established-2026-10-19)
for the established QEMU wrong-device dispatch and the remaining qualifications.

Focused console performance probes remain fixture-only:

```sh
python3 src/tests/console-boot.py bin --fixtures --profile-only --cpus=4 --diagnostics
python3 src/tests/console-boot.py bin --fixtures --scroll-only --cpus=4 --diagnostics
```

`profile.json` records guest-clock nanoseconds for 1/32/128-character text writes,
filesystem stat and create/write/remove of a temporary file on the disposable
image. It also records host wall time and accelerator/CPU count. These are
diagnostic samples, not a statistically qualified native baseline. `scroll.json`
records wall time for 60 numbered lines, checking the final output/prompt after
wrapping and scrolling. The complete console fixture also checks this case.
Host raster checks compare batch output with individual glyphs, reject an entire
malformed batch before drawing, and compare overlapping copies with the general
snapshot path in both directions with clipping and damage/generation checks.

Startup timing uses the production app catalog on copied media and exits after
the first prompt visibly echoes an injected key:

```sh
python3 src/tests/console-boot.py bin --startup-only --storage-devices --cpus=4 --diagnostics
python3 src/tests/console-boot.py bin --startup-only --startup-trace --storage-devices --cpus=4 --accel=kvm --diagnostics
```

`startup.json` records host-monotonic seconds from QEMU launch, the first observed
prompt, the verified input echo and timestamped boot-log milestones. Image
preparation is excluded. Framebuffer polling/decoding introduces detection delay;
key-to-echo includes explicit press/release injection and is not a precise input
latency benchmark. Runs are sequential diagnostic samples, not a statistical
baseline. `--startup-trace` builds separate kernel/bootstrap images under
`bin/tests`, with `CB_BOOT_TIMING` marking module initialization and provider
load/readiness phases. Only copies in the disposable image are replaced; tracing
is absent from normal candidate builds. It adds observable logging overhead.
The diagnostic loader uses the matching bootstrap symbols on failure. No test
apps are installed in startup-only images and persistent disks are never used.

Startup repair checks: `test-ext4-write` also checks that twenty warmed deep-path
metadata lookups issue zero physical reads. `test-console-host` compares uniform
RGB detection for mixed, transparent, clipped-subregion and invalid bounds.
`--startup-trace` includes per-file and process-load spans in addition to provider
readiness; those messages are compiled only into the separate diagnostic module.

The HPET profiler now samples 1/64 entries separately per CPU/domain when TSC
is unqualified; qualified-TSC durations remain fully measured. `test-cpu-profile`
checks exact selection counts, paired-reference retention, omitted-reference
rejection, valid zero references and existing rate/epoch/regression contracts.
Deliberately omitted durations are `cpu_metrics.sampled_out_ns`; they are not
failed samples or measured zero durations. Historical full-HPET peak comparisons
need this coverage difference considered before interpreting maxima.

`--fixtures --profile-only --diagnostics` captures a read-only GDB snapshot after
timing completes, including `performance-state.json`: clock/SMP qualification,
profiler rate and per-CPU counters. The same companion is written on failures.
No guest surface is invoked by the debugger. Native replay remains:

```sh
make -C src build
python3 src/tests/console-boot.py bin --fixtures --profile-only --storage-devices --cpus=4 --accel=kvm --diagnostics
```

Focused native replay `7166875b0eec446c93158021ffe2a5c0` passes: four CPUs,
6.994 s to observed prompt, qualified shared TSC at 2.096 GHz and zero unavailable
profiling samples. `profile.json` records all seven probes and
`performance-state.json` retains qualification/counter evidence. This profile
mode is not the full console or long-duration acceptance suite.

Clear/scroll checks: `test-console-host` includes production graphics decoder and
presentation tests against a memory display, plus framebuffer-I/O tests for all
overlap directions, pitch guards, channel layout, owner denial and malformed
rectangles. Raster checks compare identity fills/self-copies against the general
renderer, including clips, unchanged pixels and saturated generation counters.

Dedicated guest timing uses five clears of a nonempty full canvas and five text
newlines over full-width alternating color stripes. Preparation and result output
are outside the measured operations. Results and medians are in `render.json`:

```sh
python3 src/tests/console-boot.py bin --fixtures --render-only --storage-devices --cpus=4 --accel=kvm --diagnostics
```

The fixture requires `bin/tests/apps/graphics-check.elf`, built with
`make -C src ../bin/tests/apps/graphics-check.elf`. It is never installed into the
candidate catalog. Use `--accel=tcg` for emulator checks. Native performance must
be measured independently; a TCG speedup does not establish framebuffer-read
cost on KVM or physical hardware.

Native clear/scroll replay `c6a5770963b64e3688873034b5168393` passed operation
checks but measured 5.337 ms clear and 311.815 ms dense scroll. This exposed the
framebuffer-read cost that the TCG comparison did not establish. The backend now
uses a private RAM shadow. Its host test counts framebuffer reads and verifies
zero readback after full paint, coherence after RGB blits and overlap-safe copies,
pitch guards and copy fallback without shadow allocation. Native shadow replay
`9ffd0a0a32aa496ca2ef66031d09d082` passed with four KVM CPUs and disposable
disks: five-sample median clear 6.521 ms and dense scroll 8.322 ms. Compared with
the native pre-shadow run, scrolling is 37.5× faster; clearing did not improve.
Both runs used HPET after shared-TSC uncertainty rejection. This closes the
focused native rendering check; physical hardware and stable-TSC acceptance
remain separate. Raw comparisons are in `bin/tests/render-comparison.json`.
