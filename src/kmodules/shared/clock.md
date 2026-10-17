# clock

`clock.kmod` loads independently and publishes twelve version 1.0 hub surfaces.
Boot calls `clock.init(hub)` after mem, scheduler, hwenum and mmio initialization.
The module owns its small static state and uses MMIO surfaces for HPET register
access. Public structures and call arguments are in [clock.h](clock.h).

- `clock.init(hub)` discovers/configures available sources, probes the RTC, calls
  calibration and returns CLOCK_TSC or CLOCK_HPET. Repeating the same successful
  configuration is harmless. It fails if no usable monotonic source exists.
- `clock.calibrate()` validates the reference and selects/recalibrates the source.
  It returns the selected source. It can be called again after initialization.
- `clock.now_ns()`, `clock.now_us()` and `clock.now_ms()` return monotonic elapsed
  time since successful initialization, in the specified units. Zero is valid;
  check result.status for failure. Units do not imply matching hardware accuracy.
- `clock.info(out)` reports source, capability/availability flags, selected TSC
  frequency, HPET period and number of successful calibrations.
- `clock.rtc(out)` reads a stable, validated hardware calendar, when available.
- `clock.shared()` selects/reanchors to HPET before SMP qualification.
- `clock.sample(out, optional clock_probe)` captures an IRQ-masked HPET/TSC
  bracket and optional read-cost diagnostics for qualification.
- `clock.smp_tsc(cpu_count,heap,report)` activates a qualified current-epoch SMP
  TSC source and initializes per-CPU regression tracking.
- `clock.tsc_status(out)` reports capability/calibration and rejection diagnostics.
- `clock.refine(group,expected_epoch,expected_hz)` applies a qualifying longer-window
  frequency correction; fresh SMP proof must follow before activation.

HPET discovery uses `mmio.regions`. The module requires a 64-bit main counter and
a valid advertised period (at most 100 ns). A 32-bit-only HPET is not used because
this bootstrap has no periodic rollover maintenance. Initialization enables the
main counter if necessary, preserving its value and other configuration bits.
It does not reset the counter, program comparators or change interrupt routing.
HPET mappings and the running counter remain for the lifetime of this boot.

TSC selection requires CPUID to advertise both TSC and invariant-TSC support.
With HPET available, calibration measures TSC over three approximately 5 ms
windows, rejects non-progressing counters, implausible frequencies (outside
1 MHz–10 GHz), or samples differing by more than 2%. If CPUID leaf 0x15 supplies
an independent frequency, the measured frequency must also agree within 2%.
A rejected TSC leaves HPET selected. Calibration uses bounded polling and rejects
an individual sample taking more than 100 ms; it does not wait indefinitely for
a stalled reference. These are bootstrap validation tolerances, not precision
or long-term drift guarantees.

If HPET is absent, unsuitable or stalls during initial calibration, invariant
TSC may instead use a complete CPUID leaf 0x15 ratio/crystal frequency. It is
marked enumerated, not HPET-calibrated. An ordinary CPU core-frequency value
is not substituted for a TSC frequency. Timestamps use ordered reads from the selected CPU backend: Intel uses
LFENCE/RDTSC/LFENCE. AMD selects that sequence only when CPUID
0x80000021 EAX[2] guarantees always-serializing LFENCE; otherwise RDTSCP-capable
CPUs use MFENCE/RDTSCP followed by CPUID. The latter drains prior stores and
orders preceding execution, while trailing CPUID orders subsequent execution.
Without either capability, CPUID/RDTSC/CPUID remains the fallback. Selection is
immutable after cpu.init and each AP must confirm the same mode. No optional
DE_CFG MSR is modified. IPI memory fencing remains a separate surface.

Recalibration reanchors the selected counter without resetting elapsed time.
Old/new conversions use one shared counter observation: HPET is bracketed by
ordered TSC reads, whose midpoint supplies the matching TSC anchor. A TSC-only
clock likewise uses the same observation for old elapsed time and its new anchor.
This avoids dropping the interval between separate old/new timestamp reads.
Timestamp reads are nondecreasing; integer conversions check overflow. Failed
recalibration does not publish new anchors or increment the successful count.
A TSC reading below the last accepted TSC reading marks CLOCK_TSC_UNSTABLE and
causes fallback to an available HPET reference; without a reference it fails.
This detects backward movement even when still above the initialization anchor.
Clock reads/calibration/info use the separate recursive clock lock. Init, RTC
and validated TSC activation take global then clock locks. Counter reads use the
already validated UC HPET mapping directly; they no longer call the global MMIO
adapter on each timestamp. See profiling.md for lock ordering and measurements.

clock.shared() requires healthy HPET and reanchors continuously. It selects HPET
before SMP validation or revalidation. The calibrated BSP TSC frequency remains
available in info even while HPET is selected. clock.sample(out) takes a short
IRQ-masked TSC/HPET bracket and reports absolute nanoseconds, a conservative
uncertainty bound and local CPU capabilities; it uses immutable boot mappings
without the foreground lock and is intended for the SMP validator.

SMP adaptively collects ten paired trials per CPU/window, applies per-CPU
median/MAD filtering with a seven-trial minimum, and checks invariant-TSC/ordered-read capability,
upper offset/drift bounds including uncertainty, and calibration epoch. Only a
successful current proof permits clock.smp_tsc(cpu_count,heap,report) to select
TSC. Rejection keeps HPET. Per-CPU previous readings detect regressions/stalls;
the shared last timestamp clamps small cross-CPU differences monotonically.
An AP initially just below the BSP activation anchor clamps to the base time
rather than underflowing an unsigned elapsed calculation.
An HPET health comparison runs after 100 ms of TSC progress or 64 timestamp
reads and falls back if divergence exceeds 1 ms. Failure to read the reference
returns failure. No recurring idle timer or thread migration is introduced.
Explicit recalibration selects HPET again and revokes the old calibration proof.
A counter marked unstable cannot be reenabled using a stale proof this boot.

These are finite validation/health checks, not a guarantee of perfect oscillator
accuracy or arbitrary hypervisor migration safety. Native acceptance still needs
target-machine validation. HPET remains required for SMP and retained for fallback.
clock.info reports the actual source; smp.clock_info is the last validation report,
whose calibration epoch and current clock flags must also be inspected.

RTC discovery consults the ACPI FADT, honors the CMOS-RTC-not-present flag and
uses its century register index when provided. A missing FADT or invalid device
leaves RTC unavailable without preventing monotonic-clock initialization. The
x86 adapter delegates CMOS reads to inthandle.cmos_read. Inthandle owns the
write-only selector/NMI-mask shadow: it explicitly masks NMI before installing
its IDT/IST, then enables NMI after the fatal path is active. Each CMOS read
preserves that selector/mask and the caller's IF; RTC code never writes port 0x70
itself. inthandle.nmi_mask changes or queries this policy explicitly. No CMOS
data is written, register C is rejected, and no RTC interrupt is enabled or
acknowledged. Future CMOS users must go through the same owner.

RTC snapshots require the valid-time/battery bit, no update-in-progress, no SET
freeze, and two identical samples. Both BCD/binary and 12/24-hour modes are
decoded; invalid dates, including impossible leap days, fail without overwriting
the caller's output. UIP/stability retries are bounded. Missing century metadata
uses 2000–2099 explicitly flagged CLOCK_RTC_CENTURY_ASSUMED. The returned calendar
has the RTC's configured timezone convention; the module does not claim UTC or
set the date. Availability in clock.info reflects initialization/the latest RTC
read and can change after a successful retry.

This module provides time reads, not interrupt timers, scheduled wakeups,
preemption, sleep, alarms or wall-clock synchronization. The separate timer/event modules provide APIC wakes and deadline execution.

```sh
make -C src test-clock
make -C src test-clock-boot
```

Host UBSan tests inject sources to exercise calibrated TSC, HPET fallback,
enumerated TSC without HPET, stalled-reference fallback, recalibration continuity,
bounded failures, previous-reading regression, slow-read anchoring, units, BCD/binary/12-hour/leap-date decoding, absent RTC and
missing-century behavior. A disposable modern-bus TCG boot checks actual module
exports, HPET timestamp progress, recalibration, units and the Q35 CMOS RTC, including mask preservation while NMI is disabled.
TCG exercises HPET fallback; user-host AMD KVM runs now validate TSC calibration
and shared-clock activation (2026-10-15). Test fixtures never enter the production ISO.

Hardware references: [Intel HPET specification](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/software-developers-hpet-spec-1-0a.pdf),
[ACPI FADT](https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/05_ACPI_Software_Programming_Model/ACPI_Software_Programming_Model.html),
[Intel software developer manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).
AMD ordering references: [AMD64 system programming manual](https://docs.amd.com/api/khub/documents/sD1_QL~h4Afq2_tvzxqqSQ/content)
(CPUID 0x80000021 LFenceAlwaysSerializing) and
[AMD timestamp guidance](https://www.amd.com/content/dam/amd/en/documents/archived-tech-docs/software-optimization-guides/40546.pdf)
(RDTSCP preceding-execution ordering); the Intel instruction reference also
documents MFENCE before RDTSCP when prior stores must be globally visible.

## CPU discovery and rejection diagnostics

Clock policy remains shared: both vendors use the architectural TSC and HPET.
`clock-x86.c` obtains capabilities through the selected CPU backend's `cpu.tsc`
surface instead of interpreting CPUID itself. Intel/AMD adapters supply ordered
reads through `cpu.ticks`; their common capability decoder lives in `cpu-tsc.c`.
There is no vendor-specific clock policy requiring separate clock images yet.

`cpu.tsc(out cpu_tsc_info)` reports ABI version 1, vendor, TSC/invariant/RDTSCP
flags, raw CPUID 0x80000007 EDX and optional enumerated frequency. A complete
CPUID 0x15 denominator, numerator and crystal frequency is required; absent or
incomplete data uses HPET calibration. Core/base frequencies, model names and
hypervisor presence never establish invariant TSC or its frequency.

`clock.tsc_status(out tsc_diagnostics)` reports current source/capabilities,
calibrated/enumerated frequency, calibration count and the local rejection reason.
`smp.clock_status(out tsc_diagnostics)` reports the last cross-CPU attempt,
failing CPU and sample count. Existing clock_info/tsc_report layouts are unchanged.
A failed `clock.sample` returns its reason in result.value without mutating shared
state from the AP. Diagnostic reason codes are defined in tsc-validation.h.
Accepted cross-CPU reports are snapshots; inspect the current source/flags too,
since runtime health checks can subsequently fall back to HPET.

KVM default CPU configuration is `host,migratable=off,+invtsc`. QEMU 6.2 filters
unmigratable features such as invariant TSC from automatic host expansion when
migration compatibility is enabled. Explicit exposure does not bypass calibration,
10 us offset / 500 ppm drift / 50 us uncertainty checks, or runtime safeguards.
Latest user-host AMD KVM reruns recognize invariant TSC, calibrate about
2.0963 GHz and qualify shared TSC on all four CPUs at 250 ms. HPET remains the
runtime health reference and fallback. Earlier HPET-bracket offset measurements
failed qualification; current offset proof uses bounded TSC-only exchanges. Override
`QEMU_CPU=host` to reproduce the previous baseline. `test-cpu-tsc` verifies both
vendor contracts and missing/incomplete CPUID data under UBSan.

For current adaptive profiling, see [SMP raw clock profiling](smp.md#raw-clock-profiling).
Adaptive windows span 10–500 ms, with all raw observations, filter masks, medians
and conservative bounds retained in native reports. Every CPU must qualify in
the same window; shared-clock activation retains runtime safeguards.

`clock.sample(out, optional clock_probe)` can additionally measure an empty pair
of ordered timestamp reads before its actual HPET bracket. The existing
clock_sample layout is unchanged. `clock.refine(group,expected_epoch,expected_hz)`
accepts sufficiently stable >=100 ms BSP calibration evidence only on CPU0 in
shared HPET mode. It changes frequency/calibration epoch without changing HPET
anchors or selecting TSC. Sampling under the new epoch and all-CPU qualification
must follow before activation. See SMP documentation for the qualification
criteria, repeated-window rule, overhead telemetry and host-report epoch checks.

SMP offset qualification now uses a separate TSC-only request/ack exchange;
HPET brackets continue to qualify frequency/drift. The combined proof requires
both frequency groups and starting/ending exchange batches on every CPU. Offset
uncertainty includes the complete possible causal message delay; no assumed
symmetry or measured-overhead subtraction is used. See SMP documentation.


Calibration is performed at every boot; multi-CPU startup additionally invokes
adaptive SMP qualification. No stored calibration or report.json is consumed.
Latest AMD KVM evidence qualifies TSC with both IOMMU models; native Intel CPU
and physical hardware qualification remain separate outstanding work.
