# Cloudberry v2 roadmap

Target: modern (2016+) Intel/AMD x86-64, UEFI, PCIe, NVMe, AHCI and xHCI.
Kmodules are coupled kernel mechanisms; handlers/handles are a future layer.

Completed foundation: versioned hub, heap, hardware/MMIO inventory, clock/RTC,
vector and CMOS/NMI ownership, APIC timers, deadline callbacks, owned page reuse,
guarded stacks, CPU-pinned preemptible threads, AP startup, per-CPU descriptor
tables/stacks/timers, wake IPIs and acknowledged shared-address-space shootdown.

Closed priorities:

1. Execution measurements: per-CPU/domain lock wait/hold/attribution, managed
   IRQ-off intervals, runnable waiting and deferred IRQ latency. Measurements
   removed the callback-wide lock, shortened thread publication and separated
   clock synchronization from allocator/queue synchronization. Remaining elapsed
   maxima include host vCPU pauses; critical sections are not globally bounded.
2. Cross-CPU TSC validation: adaptive 10/25/50/100/250/500 ms windows, ten paired
   HPET frequency trials and separate TSC-only offset exchanges per CPU.
   Uncertainty-aware median/MAD filtering requires at least seven retained trials,
   with conservative bounds and calibration-epoch checks before activation.
   Quality shortages permit two fresh batches per duration; one calibration
   refinement requires a fresh proof. At most 19 attempts are retained.
   Accepted SMP timestamps use per-CPU regression checks plus a shared monotonic
   result and HPET health guard. Rejection or later instability retains/restores
   HPET. Thread migration and balancing remain disabled.
3. Device interrupt foundation: owned IOAPIC GSI routes, MSI/MSI-X messages,
   short allocation-free ISRs, preallocated/coalesced notifications, foreground
   drain/rearm, safe release checks and event-loop wake integration. Real HPET
   level delivery is validated; device configuration belongs to its owner.

4. Deferred execution: the BSP event dispatcher runs at most one thread context
   per step. Separate preemptible scheduler/IRQ workers block between invocations;
   timed thread blocking and wake credits prevent polling and lost completion
   wakes. HPET completion progresses while a slow scheduler callback runs.
   Each worker still serializes callbacks; long device work belongs in threads.
5. Memory critical sections: stack construction reserves its gap before unlocked
   work; stack teardown and thread reaping no longer retain an operation-wide
   list/global lock. Maps/unmaps batch at most 16 pages per acknowledged shootdown.
   Bulk zero/copy/growth work subsequently moved outside metadata locks; remaining
   managed waits have a watchdog, as described below.
6. PCI/DMA and read-only NVMe: exclusive type-0 endpoint ownership, safe BAR
   probe/restore, bounded capability traversal, MSI-X table programming and tracked
   identity DMA. NVMe identifies one namespace and reads through real completion
   interrupts, blocks until completion/deadline, and resets on error/timeout.
   Uncertain quiescence retains DMA ownership. Boot only caches dependencies.

Validation uses isolated one/four-CPU TCG boots and focused UBSan host tests.
NVMe tests attach disposable read-only media, wrap queues, force a lost-completion
500 ms timeout and verify close/reopen. User-host four-CPU KVM acceptance on
Ryzen 5 5500U passed AMD CPU/x2APIC/AVX preemption and emulated VT-d/NVMe.
Both latest native IOMMU-model runs pass on AMD KVM with guest AVX and x2APIC.
Shared TSC is qualified and active at approximately 2.0963 GHz; all four CPUs
qualify at 250 ms. HPET remains the health reference and fallback.
Native Intel CPU and physical NVMe/IOMMU remain pending.

7. Managed latency policy: outer waiters service IRQs, while preserving thread
   switching/lock ownership rules; lock/IPI/TLB waits have a five-second HPET
   watchdog plus iteration fallback and owned fatal dump. Bulk memory sanitization,
   heap growth and large zero/copy work run outside metadata locks. Profiling adds
   HPET nanosecond maxima and 2 ms notices. Stuck-owner and IRQ-delivery tests pass.
   Hard real-time latency is not established by these watchdogs/notices.
8. Initial IOMMU DMA: Intel VT-d legacy and AMD-Vi v1 backends, supported firmware
   scopes, requester domains, page pinning, invalidation acknowledgement and fault
   health. Intel/AMD models validate NVMe DMA and blocked unmapped canary writes.
   QEMU 6.2 AMD missing-PTE fault logging is absent; decoding is tested separately.
   Unsupported firmware/ATS/alias layouts fail closed; wider support remains open.
9. Native acceptance harness: explicit KVM/host CPU, disposable media and saved
   JSON reports. User-host AMD KVM/x2APIC/guest AVX acceptance passed; no /dev/kvm
   is available in the agent sandbox. Both native IOMMU-model reruns now pass;
   shared TSC is qualified on AMD KVM. Native Intel CPU and physical-device
   qualification remain pending.

10. Vendor portability: shared/intel/amd source segregation, boot-selected CPU
    and independently ACPI-selected IOMMU images behind identical frontend names.
    AMD extended IDs/ordered timestamps, fenced x2APIC notifications and coherent
    AMD device tables are covered. Intel bridge scopes, 39/48-bit geometry,
    PCIe pending-transaction checks and fault-overflow refusal tighten coverage.
    Native reports retain capability/host metadata; physical takeover remains
    explicitly unverified, not certified by emulated devices.
11. Extended thread state: eager x87/SSE/available AVX for threads/dispatchers,
    common XSAVE or FXSAVE capability selection, clean new contexts and per-AP
    layout validation. Isolated tests cover per-CPU yield/timer preservation,
    XSAVE/SSE-only and FXSAVE fallback. Native host AVX save/restore passes; guest
    AVX preemption passed on all four KVM vCPUs (Ryzen 5 5500U). TCG 6.2 lacks AVX.
    ISR infrastructure remains general-register
    only. Deferred quanta rearm a CPU-local timer retry instead of losing the
    preemption source during IRQ-enabled lock waits. AVX-512/AMX, lazy ownership and thread migration remain disabled.

12. ACPI/platform inventory: pinned uACPI runtime, DSDT/SSDT namespace initialization,
    HID/CID/UID/STA/ADR/SEG/BBN/CRS/DEP descriptions, generation-qualified firmware
    companions, segmented/bridged PCI matching with duplicate refusal, and owned
    platform.devices publication. Checked firmware/port/PCI adapters retain kernel
    ownership restrictions. SCI is reserved masked; enabling it, namespace/hotplug
    reconciliation, EC support, power management and platform drivers are deferred.
    Isolated SSDT tests and Intel/AMD TCG boots validate inventory and failure flags;
    native/physical ACPI acceptance remains open.

13. USB core: owned root-device/configuration/interface registry, speed/descriptor
    validation, interface claims, scoped synchronous class/endpoint I/O and
    core-owned bounce buffers. BSP workers consume xHCI port-change wake credits
    instead of periodic polling; class-driver threads can subscribe to USB-level
    device/worker-state wakes. Controlled detach/rebind and stop/restart retain
    uncertain ownership, invalidate old identities and reuse connector records.
    Isolated tests cover keyboard/mouse/storage enumeration, class control,
    pending-I/O shutdown and hotplug with one/four CPUs and both IOMMU models.
    HID/storage class drivers and physical USB acceptance remain open.

14. USB hubs: separate hub-class module and owned `usb.hubs` registry; dynamically
    sized port state, power/reset, interrupt change bitmaps and nested attachment.
    xHCI derives route/TT contexts; USB core serializes topology and invalidates
    subtrees before postorder teardown. Generation-qualified references reject
    old attachments; malformed children fail locally. Isolated one/four-CPU TCG
    tests cover nested QEMU hubs, child hotplug, active grandchild disconnect,
    reattachment and shutdown with Intel/AMD CPU and IOMMU models. Physical,
    native KVM and real high-speed multi-TT/SuperSpeed hub acceptance remain open.
    Lifecycle/descriptor closure (2026-10-16): class-only restart preserves the
    root attachment while rebuilding descendants; blocked reads disconnect and
    manager/worker stacks are reclaimed. Two repeated cycles are tested. Full
    descriptors must match their validated prefix; malformed/short second reads
    fail before configuration. Failed-hub automatic recovery stays deferred.

15. Boot configuration: Limine-loaded `boot/cloudberry.cnf` is identified as data;
    the config kmodule owns strict schema parsing, copied typed values and
    `config.<key>` references. Batch hub publication is atomic. Inventory visibility
    is applied; driver directory/timeout are future-consumer metadata, with no
    fabricated partition GUID. Host tests and isolated one/four-CPU TCG acceptance
    validate ownership, failures and the handoff; missing/duplicate/invalid-file
    boots fail explicitly. Runtime reconfiguration remains outside this milestone.

## Next priorities

Review update: 2026-10-16. Hub lifecycle/descriptor closure is complete; retain
the existing storage-first sequence.

1. Storage before HID: implement USB mass-storage Bulk-Only Transport/SCSI reads
   on USB core, exposing a common read-only block contract alongside NVMe. Then
   add bounded partition discovery and an ext4 reader. Retain device transport
   and correlate available Limine disk/partition identities with the boot volume;
   absent or ambiguous matches remain explicit. Build-time boot-image preparation
   may append the planned >=512 MiB ext4 driver partition; no boot formatting or
   migrations. SATA/AHCI and UAS are separate later backends, not prerequisites
   for reaching the current USB boot medium.
2. Load driver modules from disk through the kernel: reuse image/contract checks,
   resolve hub dependencies and select drivers for discovered devices. Foundational
   USB/xHCI/mass-storage/partition/filesystem modules must initially arrive through
   Limine so the kernel can reach that driver partition. Loading and safe hot
   executable replacement are separate milestones.

After disk loading: establish process address spaces, user/kernel privilege
separation, safe boundary copying and teardown/ownership cleanup before
application-facing handlers/handles. HID/input and native display follow the
storage/driver-loading foundation rather than preceding it. CPU pinning can remain.

Clock boot-cost reporting remains a backlog item; retain per-boot calibration,
qualification and HPET fallback. SCI reconciliation, EC/platform drivers and PCI
INTx routing also remain deferred until required by a consumer.

Outstanding acceptance: native Intel CPU (including shared TSC), physical
Intel/AMD firmware/device DMA takeover and posted-write drain. Both emulated
IOMMU models on an AMD host do not establish those results. HPET-less SMP,
hard latency bounds, NVMe writes/multiple controllers/namespaces/parallel queues,
interrupt remapping and unsupported IOMMU firmware policies remain open.

Tests stay on isolated builds/disposable media and never run candidate VMs or
attach persistent disks automatically. No boot migrations or host OS changes.

## Historical implementation and validation log

The entries below describe successive revisions and their results at the time.
Their pending/rejected/HPET-only statements are historical, not current status.
The current summary and priorities above, and the final shared-TSC acceptance
entry below, supersede them. No historical report is used as a build input.

Native acceptance correction (2026-10-14): the shared event test now observes
idle coverage independently of callback count, waits for worker bookkeeping and
prints diagnostics. Stale reports are archived, and result.json records each run.
Both corrected four-CPU native runs pass with matching run IDs: Intel VT-d and
AMD-Vi models on Ryzen 5 5500U, x87/SSE/AVX preservation, NVMe timeout/reset and
DMA revocation. Current runs report xAPIC; the earlier x2APIC JSON is historical.
TSC remains UNAVAILABLE/HPET. Physical device qualification is still open.

### Invariant TSC discovery and VM exposure

Implemented selected Intel/AMD `cpu.tsc` capability/frequency discovery, shared
clock consumption, explicit rejection diagnostics and KVM defaults enabling
invariant TSC with migration disabled. Complete CPUID 0x15 ratios are optional;
HPET calibration and cross-CPU acceptance remain mandatory for SMP TSC selection.
Focused host tests cover both vendors and missing/incomplete capability leaves.
Native user-host reruns pass and recognize invariant TSC, but SMP activation
remains rejected by the conservative sampling uncertainty bound. Physical Intel/AMD
and native Intel CPU acceptance are still open. Clock policy remains shared;
architecture-specific discovery and ordered reads reside in CPU backends.

Validation for this change passed: focused UBSan TSC discovery/clock tests,
identical backend exports, all five isolated portability boots, and final one/four-
CPU thread boots. The candidate ISO was rebuilt; persistent NVMe/SATA image inode,
size and nanosecond modification times were unchanged. TCG reports explicitly
identify missing invariant-TSC support rather than claiming TSC qualification.

Native rerun evidence (2026-10-14): both new runs pass, with matching result/report
IDs 44032295f83247679100e4b578409fd1 (Intel IOMMU model) and
ea5748de57504c6e87aab953e7031051 (AMD IOMMU model). Both use the AMD CPU backend,
actual x2APIC mode 2 and XSAVE mask 7 with zero state errors. Invariant TSC is
exposed (power EDX=256), calibrated at 2,096,207,037 / 2,096,385,072 Hz.
All 12 samples were collected. Validation rejects on BSP CPU 0 with conservative
offset bounds 30,564 / 26,864 ns against the 10,000 ns budget; comparing the BSP
with itself still adds its sampling uncertainty twice. This is insufficient proof
of synchrony, not evidence of actual inter-CPU skew. Improve reference sampling
and self-comparison diagnostics before revisiting acceptance; retain HPET fallback.

Clock profiling: added a nominal 300 us diagnostic phase (0/150/300 us), retaining
existing 50 ms qualification and all safety budgets. Snapshot surfaces and native
reports expose actual intervals, raw samples, observed drift/offset and uncertainty
bounds independently, including BSP self-comparison attribution. No outlier
filtering or overhead normalization is assumed before inspecting these results.
User-host KVM measurements for this profile are now available (see below).

300 us profile results (2026-10-14 local / 2026-10-15 UTC): both KVM acceptance
runs pass with matching result/report IDs: f8fd76ff81294693af28596f406aeaa2
(Intel IOMMU model), 1f77fee8e022424d8004de714093d414 (AMD IOMMU model). Both use
the AMD CPU backend, x2APIC and error-free AVX preservation, collecting 24 raw
observations each. Nominal 300 us per-CPU windows actually span 2.08–2.50 ms;
sequential sampling, mailbox/IPI work, reads and host scheduling are included.
Individual bracket uncertainties range 11.741–114.884 us. At approximately 50 ms,
observed drift ranges -511.9 to +30.7 ppm, but conservative upper bounds remain
845–3285 ppm, above 500 ppm. Intel-model validation rejects its BSP self-offset
bound; AMD-model validation rejects uncertainty. HPET remains selected. These
measurements do not establish stable TSC drift or actual CPU skew. Next diagnostic
step: characterize local repeated bracket costs separately from cross-CPU relay
costs before selecting a filtering or normalization policy.

Clock profiling window increased to 100 ms (0/50/100 ms observations) at user
request. The separate 50 ms qualification phase and safety budgets are unchanged.
Reports now use profile-100ms; historical 300 us results above remain historical.
Native measurements of the new window are available below.

100 ms profile results (2026-10-14 local / 2026-10-15 UTC): both native runs pass
with matching result/report IDs db7c3375c2aa4aba9becce887cf6aead (Intel model) and
3bea22785daa4f689a949176598620e7 (AMD model). Both use the AMD CPU backend, x2APIC,
24 observations and zero AVX errors. Actual full profile intervals span
99.863–100.120 ms. BSP observed drift at 50/100 ms is -409.7/-408.2 ppm for the
Intel-model run, -135.0/-117.7 ppm for AMD-model. This suggests a repeatable
within-run frequency mismatch; variation between runs is consistent with
calibration variability, not proof of physical clock drift. At 100 ms the BSP
uncertainty-inclusive drift bound improves to 676.6 / 352.9 ppm respectively.
Only the latter BSP diagnostic meets 500 ppm; AP bounds still range 745.9–1453.5
ppm. Both qualification attempts reject on BSP self-offset uncertainty, retaining
HPET. Next step remains repeated local brackets and calibration characterization;
do not treat a single acceptable BSP drift bound as cross-CPU qualification.

Adaptive clock qualification now replaces fixed 100/50 ms phases. Attempts use
10/25/50/100/250/500 ms windows and ten paired trials per CPU, with bracket and
three-MAD filtering, upper medians, and at least seven retained trials. All CPUs
must pass the same attempt. Conservative worst-retained bounds remain 10 us /
500 ppm / 50 us; BSP self-offset correlation is corrected without dropping its
drift checks. Raw data/filter decisions persist in host reports; no compilation
input or guest-persistent clock profile is added. Native KVM reruns remain pending.

Adaptive validation checks passed: UBSan group tests, deterministic production
sampler/control-flow tests, report parsing/audit tests, clock regression/fallback
tests, and isolated one/four-CPU TCG boots. TCG cannot expose invariant TSC here,
so its boots verify fallback; synthetic adapters exercise adaptive acceptance and
all six failed windows. Native KVM measurements remain pending. Candidate rebuilt;
persistent NVMe/SATA images were not attached to tests or modified.

Adaptive native evidence (2026-10-14 local / 2026-10-15 UTC): both runs pass,
matching result/report IDs f9005447c07044b5968bfdcd8bf8b809 (Intel IOMMU model)
and 6e8a590c95224fcb94c5d3bf5fa743e8 (AMD model). Both AMD CPU/KVM, x2APIC,
error-free AVX preservation. All six attempts ran, retaining all 480 raw
observations per run; bracket/MAD exclusions total 57/21 and 75/17 trial groups.
At 500 ms, per-CPU median TSC-versus-HPET drift is -270.1..-247.1 ppm and
uncertainty-inclusive drift bounds are 320..407 ppm. Some groups retain only six
trials, below seven required. Non-BSP median offsets are -4.8..+6.2 us, but
conservative offset bounds remain 61.9..94.8 us against 10 us. Shared time stays
HPET. Similar drift across CPUs and both runs suggests a shared calibration or
reference-frequency mismatch, not established inter-CPU desynchronization.
Next: evaluate longer-window median calibration and characterize bracket/relay
costs without subtracting variable uncertainty or weakening offset bounds.

Longer-window calibration and overhead instrumentation implemented: stable BSP
median evidence from >=100 ms can refine frequency once, increasing the epoch
without changing HPET time. That duration is repeated with fresh samples; only a
new all-CPU proof can activate TSC. Storage allows seven attempts/140 observations
per CPU. Probes measure empty ordered reads, HPET bracket, full sampling call and
BSP/AP round trip; report statistics retain input Hz/epoch and do not subtract
variable overhead. Native KVM results for this change remain pending.

Refinement validation passed: UBSan clock tests for both correction signs, stale
and invalid evidence rejection and HPET continuity; deterministic sampler tests
for corrected-frequency acceptance only after fresh-epoch verification, persistent
skew remaining rejected and the one-update limit; report overhead/epoch-history
and cross-epoch rejection tests. Isolated one/four-CPU fallback boots and candidate
build passed. Persistent NVMe/SATA image inode/size/mtime remain unchanged. Native
KVM overhead and corrected-frequency measurements still need user-host execution.

Refinement native evidence (2026-10-14 local / 2026-10-15 UTC): both runs pass,
matching result/report IDs 2cf045dd23464db093d323f146ef2a8b (Intel IOMMU model)
and e419cc91cabe4bc1ad3eada9b866021b (AMD model). Both AMD CPU/KVM, x2APIC,
zero AVX errors, seven attempts and 560 observations. At 250 ms BSP medians
refined frequency from 2,096,024,728 to 2,096,155,243 Hz and from 2,096,446,012
to 2,096,169,149 Hz. Fresh 250 ms BSP medians are -3.484 / -5.900 ppm. Final
500 ms per-CPU medians span -17.615..+1.680 ppm; conservative drift bounds are
75..167 ppm. Some groups retain only 4..6 trials, below seven required, and
non-BSP offset bounds remain 61.9..85.5 us, so HPET remains selected.
Post-refinement cost medians: empty ordered reads 1.76..2.01 us, HPET bracket
16.57..26.37 us, full sample call 30.36..40.44 us, AP round trip 148.79..219.45 us.
These costs include virtualization/pauses; subtraction is not justified. Frequency
refinement is evidenced, shared TSC synchronization is still unqualified. Next:
separate HPET-based frequency calibration from TSC-only cross-CPU offset exchange
with explicit round-trip uncertainty, rather than weaken the 10 us offset budget.

Implemented independent TSC-only cross-CPU offset qualification. Preallocated
request/ack batches wake APs once, keep interrupts enabled, and exclude HPET/IPI
setup from the measured path. Each raw AP timestamp is bracketed by ordered BSP
reads; full causal round-trip uncertainty is retained without symmetry assumptions.
Frequency groups use HPET independently. Every CPU's start/end offset batches
must retain seven of ten trials and fit 10 us conservative bounds, alongside the
existing frequency proof. Regressions abort; polling/timeouts and cleanup are
bounded. Raw exchanges and per-batch masks/statistics persist in native reports.
Native KVM evidence for this path remains pending; no compile-time or guest-disk
profile persistence is added.

Validation passed for TSC-only exchange: focused UBSan clock/group/sampler and
report tests, including causal RTT bounds, asymmetric delay, persistent skew,
minimum retained counts, regressions and timeout cleanup. Final one/four-CPU TCG
fallback boots and candidate build pass; persistent disk metadata is unchanged.
The clock acceptance test refreshes any previously accepted SMP proof after its
deliberate recalibration, so subsequent thread tests can exercise the accepted
TSC path rather than silently returning to HPET. Accepted reports also require
complete frequency and start/end offset groups for every CPU. Actual AP polling
and offset precision still need user-host KVM acceptance.

TSC-only native evidence (2026-10-15): both runs pass, matching report/result IDs
1abf70cfd63e44d2808116e4fbc6247d (Intel IOMMU model) and
02e989eb6521452d88726f411683d881 (AMD model), AMD CPU/KVM, x2APIC, zero AVX errors.
Offset batch conservative bounds now span 1.697–3.858 us against the unchanged
10 us limit; per-CPU median round trips are 3.606–4.057 us. Frequency bounds at
the final 500 ms windows are 99–203 ppm, but some frequency/offset groups retain
only six or fewer trials. HPET therefore remains active. The AMD run applied
frequency refinement and repeated its 250 ms window under a fresh epoch.
A concrete filter defect is now evident: Intel-model CPU2's first offset batch
has six identical 138 ns estimates (zero MAD), causing legitimate 128/133/143 ns
observations to be rejected by the 3 ns floor despite ~1680 ns uncertainty.
Next: uncertainty-aware outlier thresholds and replacement trials while retaining
minimum sample counts and all conservative acceptance budgets. TSC offset
measurement precision is evidenced; full shared-clock activation remains pending.

Implemented uncertainty-aware rejection and bounded replacement trials. Offset
and drift outliers must exceed both the three-MAD spread and combined sample /
representative uncertainty; widths use a relative median floor plus the unchanged
50 us absolute ceiling. Small native quantized differences are retained. Worst
retained safety bounds, seven-trial minimum, 10 us offset and 500 ppm drift limits
are unchanged. Shortages with otherwise acceptable bounds permit two fresh
batches at the same duration; persistent bad bounds are not retried. All data and
attempt reasons remain logged (up to 19 attempts / 380 observations per CPU).
Offline replay of both previous native datasets now qualifies at 250 ms; this
neither changes the production build from saved data nor proves live acceptance.
Native KVM measurements of this change remain pending.

Validation for uncertainty-aware filtering passed: UBSan filter and sampler tests,
including the native zero-MAD regression pattern, uncertainty-overlap frequency
variations, recovery via a fresh batch, 18-attempt exhaustion without calibration,
and refusal to replace persistently bad bounds. Report tests distinguish same-epoch
replacements from fresh-epoch calibration verification. One/four-CPU TCG fallback
boots and candidate build pass; persistent disk inode/size/mtime unchanged.
Offline native-data replay qualifies both earlier datasets at 250 ms; live KVM
execution of the revised policy remains pending.


Shared TSC native acceptance (2026-10-15): both current four-CPU runs pass,
with matching report/result IDs e57e0cf740d44dbb9058b9ecc16f086b (Intel IOMMU
model) and 7a0ff1412aed4eea8aef36dc5cc7a090 (AMD IOMMU model). Both execute the
AMD CPU backend on Ryzen 5 5500U under KVM, report x2APIC mode 2, and record
zero AVX probe errors. Shared TSC is VALID and active in both runs, with no
rejection reason. All frequency and start/end offset groups qualify at 250 ms.
Worst conservative offset/drift bounds are 3,168 ns / 407 ppm (Intel model)
and 3,676 ns / 348 ppm (AMD model); maximum retained HPET uncertainty is
47,193 ns / 49,900 ns respectively. The Intel-model run verifies a refined
calibration epoch; the AMD-model run succeeds after a same-duration replacement
batch. Neither lowers the minimum seven retained trials or the 10 us / 500 ppm /
50 us limits. This closes the pending live validation of uncertainty-aware
filtering and bounded replacements. HPET remains the runtime health reference
and fallback. Physical hardware and native Intel CPU acceptance remain open.


Hub capacity expanded to 16 KiB (four contiguous pages), with mediation backing
computed from method capacity (three pages / 12 KiB). All pages retain RO/NX hub
and RX stub permissions. Current 148 methods and four table references use
45.36% of the hub; up to 334 methods fit with those references. This is fixed
boot allocation, not runtime growth or module unloading support.


xHCI controller milestone: shared kernel-loaded module, owned published controller
registry, checked capabilities/firmware handoff, root-port/protocol snapshots,
command/event rings, scratchpads, MSI-X deferred completion, slot/address and
endpoint configuration/EP0 evaluation, asynchronous 4KiB transfer submissions,
wake-driven waiting, cancellation and quiescent close/reopen. Stable records reuse
fresh generations/IDs; stale device tokens cannot bind to a replacement attachment.
Existing hub facilities supply PCI/DMA/IOMMU/IRQ/mem/clock/thread mechanisms.
Production init claims no controller; tests alone issue raw USB requests.
UBSan lifecycle/ring/failure tests and isolated one/four-CPU QEMU hotplug probes
pass, including Intel VT-d and AMD-Vi DMA paths. At this transport-only revision,
USB core/HID, driver-manager and image-unload work remained deferred; native/physical controller acceptance is open.
That transport-only revision exported 162 methods and five table references
(49.85% hub). Current ACPI/USB inventories and usage are documented in maps/startup.md.
