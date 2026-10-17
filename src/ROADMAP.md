# Cloudberry v2 roadmap

Target: modern (2016+) Intel/AMD x86-64, UEFI, PCIe, NVMe, AHCI and xHCI.
Kmodules are coupled kernel mechanisms; handlers/handles are a future layer.

Completed foundation: dynamic ABI3 hub and provider ownership/draining, heap, hardware/MMIO inventory, clock/RTC,
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
    HID and physical USB acceptance remain open; BOT storage is provided by item 17.

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
    is applied; driver directory remains caller-owned; USB storage now consumes timeout metadata.
    Image preparation supplies a real GPT partition GUID. Host tests and isolated one/four-CPU TCG acceptance
    validate ownership, failures and the handoff; missing/duplicate/invalid-file
    boots fail explicitly. Runtime reconfiguration remains outside this milestone.

16. Driver-partition image preparation: the build appends a 512 MiB ext4 GPT
    partition to the existing UEFI hybrid ISO. Its real partition GUID is stored
    in `boot/cloudberry.cnf` and applied to both GPT copies; the configured driver
    directory is created in ext4. Host checks validate GPT CRCs, filesystem size,
    packaged identity, e2fsck cleanliness and directory contents. Isolated UEFI
    boot validates the unchanged Limine FAT/ISO9660 path and typed GUID handoff.
    Guest ext4 access follows in item 17; disk loading remains pending. Boot performs no formatting.

17. Boot storage readers: separate Limine-loaded usb-storage, partition and
    ext4-fs modules. USB BOT/SCSI exposes a retained, versioned read-only block
    descriptor; GPT validates both header/array CRCs and matches the configured
    unique GUID. Ext4 validates supported clean filesystem geometry and metadata
    checksums and provides stat/list/read, extent trees and sparse/unwritten data.
    Focused UBSan fixtures cover 1/2/4 KiB blocks, checksum seeds, fragmented files,
    metadata damage, OOM rollback and lifetime pins. Isolated USB→GPT→ext4 boots
    cover actual file reads and four-CPU AMD-IOMMU unplug/replug. HTree directories,
    symlink traversal, journal replay, writes, UAS and physical USB remain open.
    Runtime UNIT ATTENTION invalidates storage; initial readiness alone may consume
    it before publication. Poisoned BOT transport may require core rebind.

18. Writable ext4: USB BOT WRITE(10)/(16), MODE SENSE write-protection checks,
    qualified SYNCHRONIZE CACHE and block ABI1.1 write/flush propagate through GPT
    slices. Explicit writable mounts use a pinned, privately linked lwext4 engine,
    journal replay, bounded full-data transactions and conservative write barriers.
    Create/grow/append/truncate/remove, directories and no-replace rename are exposed.
    Mount/device registries are dynamic; overlapping writable views are refused.
    Normal mutations, independent fsck, abrupt journal commit/checkpoint cuts and
    injected write/barrier failures validate acknowledged durability and fail-closed
    ownership. One/four-CPU Intel/AMD-IOMMU QEMU boots validate real USB writes.
    Normal boot remains read-only; only disposable test images are made writable.
    Seeded-checksum writes, orphan-list recovery, physical durability, forced
    abandonment of poisoned mounts, sparse extension and barrier batching remain open.

19. Closed: common-block NVMe write/flush/lifetime adapter and AHCI SATA backend.
    SATA uses owned PCI MSI, deferred IRQ completion, tracked IOMMU DMA, LBA48
    read/write and flush. Explicit controller/namespace/port selection feeds the
    existing configured-GUID GPT selector and ext4. Current modules stay Limine-
    loaded. No automatic persistent-disk claiming or disk-module loading is added.
    Focused disposable-media tests boot through each transport and exercise
    journaled ext4, pins/bounds, IRQ loss/reset and stale identities. Automatic
    boot provenance/duplicate-GUID selection and physical qualification remain open.

20. Closed: dynamic ABI3 hub and provider lifecycle. Separate page-backed method,
    table/provider and invocation/stub arrays keep published addresses stable;
    writer serialization, complete-batch publication and OOM/mapping-failure retry
    replace the fixed 16 KiB ceiling. Format3 rejects old hub-layout binaries.
    Providers expose live/draining/retired states; scheduler records, threads and
    deferred IRQ routes lease callbacks through reclamation. Active/leased calls
    drain without forced unwinding; retired stubs fail. Current boot modules and
    hard-ISR providers stay pinned. Published-table owners cannot retire yet;
    images/stubs remain mapped pending comprehensive reclamation.
    Focused host and four-CPU guest checks pass. Active documentation was reviewed
    for current storage, hub capacity/layout and lifetime claims.

21. Closed: module images use an independent 1 GiB virtual arena at
    0xffff920000000000, with demand-allocated physical extents, RX/RW-NX
    permissions and per-image guards. Retired/failed extents remain reserved.
    Four-CPU AMD-Vi isolated validation loaded a 20 MiB BSS-heavy image, checked
    guards/permissions and rejected oversized and unavailable-physical-memory
    requests without publishing partial exports.

22. Closed: dynamic NVMe controllers/namespaces and AHCI controllers/ports,
    with controller-specific ownership, IRQ routes and DMA state. Namespace/port
    cookies and references have independent lifetimes; sibling open/close never
    resets a live controller. Commands serialize per controller. NVMe failure
    poisons its namespaces; SATA stops the affected engine, escalating to a
    controller-wide failure if quiescence cannot be proven. Four-CPU isolated
    tests cover two controllers and two namespaces/ports, data isolation,
    close/reopen, pins, stale cookies and an unrelated surviving controller.
    Journaled ext4 regression passes through both backends. Inactive namespace
    cleanup is explicit/retryable. Early splash ensures its framebuffer mapping
    before drawing, covering the NVMe/OVMF direct-map gap found by acceptance.

23. Closed: USB removal propagates root identity invalidation through ext4 mounts
    and partition views. A boot-lifetime subscribed storage worker stops new I/O,
    retries busy drains, discards mount cache/journal allocations without writing
    and releases unretained slices/BOT records. Retained dead views reject I/O and
    close on final release; replacement devices require fresh identities/mounts.
    Driver images/controller registries remain boot-lifetime. Existing USB core
    owns transfer cancellation and xHCI retains uncertain DMA safely. Three host
    in-flight journal disconnect cycles reclaim every tracked allocation and
    reference; journal crash/recovery checks still pass. Four-CPU AMD-Vi guest
    acceptance covers writable removal/reinsertion and nested-hub subtree loss
    with stale-reference rejection, automatic cleanup and a fresh explicit mount.

24. Closed: the boot-loaded bootstrap coordinator waits for initial USB/hub
    discovery and scans supported NVMe namespaces, connected AHCI ports and USB
    BOT LUN0 interfaces. A unique configured GUID mounts read-only; duplicate,
    corrupt GPT, transport failure and topology changes fail selection. Other
    instances close; uncertain cleanup retains reachable ownership. Copied Limine
    executable metadata is published as boot.origin; GPT disk correlation reports
    match/different/unknown separately from configured-volume selection. No boot
    formatting, migrations, journal replay or writes. Four-CPU USB/NVMe/SATA,
    duplicate-GUID and multi-controller/multi-namespace guest acceptance pass.
    USB multi-LUN multiplexing/UAS and unsupported hardware remain explicit limits.

25. Closed: explicit `drivers.modules` basenames select `.driver` manifests and
    format3 images from the readonly ext4 driver volume. Bootstrap validates the
    whole opt-in list, matches PCI inventory classes and preserves dependency
    order; the kernel mediates image validation, dependency-version leases,
    hidden initialization and optional live startup. Failures preserve owner/
    error details, reject new calls and retain active/leased/table-backed state;
    safe retirement releases dependency leases. All existing modules remain
    Limine-loaded. Four-CPU AMD-Vi failure-path and Intel VT-d success-path guest
    tests load disk-only fixtures, reject version/image/init/start failures,
    prove unselected images remain unloaded and check retirement/stale gates.
    Focused manifest/config/hub host checks pass. General unload/executable
    reclamation, signature installation policy and hardware activation rollback
    remain outside this milestone.

26. Closed: runtime modules remain hidden through INITIALIZING and STARTING.
    Public discovery, captured stubs, callback leases and table publication reject
    both states. ABI2 initialization contexts provide loading-thread-only private
    lookup. LIVE is published only after both hooks succeed; failed activation
    remains non-resumable. Host concurrent gate checks and four-CPU AMD-Vi
    disk-module init/start failure acceptance pass. Hardware rollback and code
    reclamation remain deferred.

27. Closed: private address-space roots share supervisor-only upper kernel
    mappings and own lower-half user tables. mem.space_* checks owner/bounds/W^X,
    retains mapped data, tracks active CPUs/leases and reclaims private tables in
    bounded steps. ABI2 kernel threads select their leased roots on every switch
    and restore the kernel dispatcher root. Targeted TLB invalidation synchronizes
    with root transitions and physical reuse; BSP/AP write protection is enforced.
    Host ownership/OOM/teardown checks and isolated four-CPU AMD-Vi/Intel VT-d
    tests pass for same-VA private backing, timer/yield/block/exit, remote active
    translations and lease-gated destruction. All execution remains kernel-mode;
    this is not completed user-process isolation.

28. Closed: eight isolated address-space cycles exercise exact read-only write,
    NX fetch and unmapped-read hardware faults, including active remote-CPU
    revocation. Test-only CPU-local gates match CR2/CR3/RIP/error and restore the
    original IDT entry per operation. CPU1 receives IPIs while deferring root
    switches, so context reloads cannot conceal stale translations. Owned page,
    table and stack reservations return to baseline each cycle. Production fault
    handling remains unchanged; ring-3 supervisor protection awaits user-mode
    acceptance. Four-CPU AMD-Vi and Intel VT-d guest acceptance pass.

29. Closed: isolated probes record interleaved empty-clock/same-root/root-roundtrip
    timings, worker CPU/space/owner, yield elapsed time and paired active/inactive
    protection changes. Ten trials each measure dense 32/512-page and sparse
    128-page construction/teardown, table backing and managed BSP hold/IRQ-off
    maxima. Host reports retain all raw samples, median/p95/max and paired
    estimates; malformed/incomplete data fails acceptance. Single-CPU and both
    four-CPU IOMMU emulations pass, along with seven parser checks. Results use
    HPET/PGE-off/TCG; native timing and the PGE-toggle path remain unmeasured.
    Sparse population is costly in this environment; allocation/ledger attribution
    precedes optimization. Production scheduling/invalidation policy is unchanged.
    See tests/address-space-profiling.md for methodology, observations and run IDs.

## Next priorities

Documentation review: bootstrap's automatic read-only volume selection,
Limine-origin correlation, selected runtime module loading/retirement and private
space metadata descriptions now agree with their production implementations.

Review update: 2026-10-17. The resized dynamic hub has four-CPU guest validation.
Multi-instance storage and USB storage-disconnection cleanup are implemented.
Bootstrap storage selection and selected future disk-module loading are implemented.
The five requested storage/hub priorities are closed.

Activation hardening and private address-space infrastructure are implemented.
The initial application-facing C contract is defined in interfaces/README.md and
compile-tested headers. The first loaded driver/handler will be a privileged
keyboard process publishing /@keyboard. It follows foundational kmodules, uses
scoped OS I/O and memory handles and has no direct application access to the hub.
Protected process milestone implemented: static ext4 ELF loading, ring-3 entry,
CPU-local kernel entry stacks/GS transitions, preemption, user fault containment
and explicit quiescent cleanup. Isolated one/four-CPU boots and malformed-ELF
UBSan checks pass; native user-mode acceptance remains open. Copied/admitted handle transport and the C system library are now implemented,
with immutable boot JSON grants, owner-local tokens, filtered reflection, retained
results, blocked request/reply and scoped memory/I/O managers. UBSan grammar/broker
checks and isolated one/four-CPU user-process tests pass, including cross-CPU
provider/client delivery and scoped clock access. Next:
keyboard publication and an isolated authorized client. Boot manifests are enforced; installed-app authorization and actual keyboard
HID execution remain separate work.
HID/input and native display follow the
storage/driver-loading foundation rather than preceding it. CPU pinning can remain.

Completed performance sequence (native acceptance 2026-10-17):

1. Reduce timestamp/lock instrumentation overhead without weakening ordering or
   watchdogs. Implemented capability-selected AMD LFENCE/RDTSC/LFENCE or
   MFENCE/RDTSCP/CPUID, conservative fallback and AP agreement; recursive locks
   avoid unused HPET reads and IF-enabled entry reuses timing. Independent HPET
   attribution probes measure raw timestamps, clock calls and lock variants.
   Nine user-host KVM boots passed; AMD selected guaranteed serializing LFENCE.
   Four-CPU empty clock brackets fell from about 64 us to 35 us. Outer lock/HPET
   instrumentation still costs about 34 us; it remains enabled and measurable.
2. Establish a fresh KVM baseline. The original user-host one/four-CPU batch
   passed with TSC; its four-CPU empty clock bracket was about 64 us. Updated
   scripts/bench-spaces-kvm.sh preserves it and supports repeated native-to-native
   comparisons alongside the historical TCG comparison, with source/clock/host
   identity checks. Fresh baseline accepted: 20261017T165114.408723Z, three boots
   per configuration, unchanged host and stable TSC throughout. Raw timestamps
   are about 35 ns, sparse population about 74 ms and dense 512-page teardown
   about 25 ms on four CPUs. The agent still has no /dev/kvm.
3. Optimize sparse population and dense/sparse teardown using the cleaner native
   evidence. Implemented advisory next-fit physical search with exhaustive
   fallback and sanitized-hole reuse; private teardown cursors retain progress
   and amortize lock overhead over batches of at most 16 removals/4096 entry
   examinations. Ownership, inactive-root checks, sanitization, watchdogs and
   acknowledged invalidation remain enforced. Host and isolated guest checks pass.
   Repeated KVM acceptance passed: 20261017T202426.499405Z, three boots per case,
   unchanged host and stable TSC. Four-CPU AMD-Vi/VT-d sparse population is
   6.37/6.35 ms versus 73.85/73.58 ms; dense teardown is 1.94/1.94 ms versus
   25.17/25.14 ms; sparse teardown is 1.60/1.60 ms versus 12.95/12.96 ms.
   Batching increases individual sparse holds: typical managed IRQ-off maxima
   are about 78 us versus 50 us; the largest sparse-trial observation is 90 us.
   Broader stress cycles still contain multi-ms outliers (up to 4.06 ms in this
   batch); no hard latency bound or attribution to host pauses is established.
   Per-boot raw results and both previous reference sessions remain preserved.

See tests/address-space-profiling.md for collection commands and interpretation.

Follow-up requested after that acceptance:

1. Closed documentation drift for automatic bootstrap storage selection, boot
   provenance, runtime module loading and private-space metadata.
2. Implemented qualified local-TSC statistics, epoch/rate invalidation, global
   regression quarantine, explicit unavailable-sample accounting and HPET fallback.
   Independent HPET lock/IPI/TLB watchdogs remain active. Reports retain CPU,
   method/name, lock domain, caller PC, clock source/epoch and address-space identity
   for wait/hold/IRQ peaks. Focused host and guest validation is recorded in tests;
   native acceptance is now complete in session 20261017T214448.656303Z. Earlier
   failed sessions below record the races found and corrected during acceptance.
   Native session 20261017T211928.519874Z passes three single-CPU runs but times
   out during the first four-CPU AMD-Vi run. The benchmark's shared-state waits
   now use nonblocking dispatch rather than event-loop idle, with an explicit
   remote-only completion regression. The incomplete session is not accepted;
   native reruns against 20261017T202426.499405Z were required at that stage.
   Session 20261017T212856.886469Z subsequently completes three single-CPU and
   two four-CPU AMD-Vi correctness boots, but its second SMP boot falls back to
   HPET and the comparator rejects mixed-clock timings. The runtime health check
   now brackets its HPET reference to avoid treating vCPU pauses as drift; genuine
   drift/regression still falls back. Reports now retain the TSC rejection reason.
   Native confirmation was pending then; the old log cannot establish its reason.
   The next native session, 20261017T213514.682497Z, completes seven boots with
   stable TSC before the second VT-d boot exposes premature BLOCKED publication:
   its remote thread still uses the private root during switch-out. Threads now
   use BLOCKING until dispatcher/root restoration, preserving wakes across that
   transition. The inactive-root assertion remains intact. This partial batch is
   not an accepted performance baseline.
3. Defined the versioned C handle boundary and keyboard 1.0 event/read/state
   contract under interfaces, plus handlers/keyboard's loading/authorization plan.
   /@keyboard is the first handler milestone; C++ object layouts stay out of the
   ABI. Keyboard runtime implementation follows the process/transport foundation.

Native follow-up closed: session 20261017T214448.656303Z passes all nine KVM
boots, three per topology/IOMMU case, with stable TSC, zero rejection reasons,
TSC-only measured duration statistics and no unavailable samples. Host/toolchain
metadata match the previous accepted session. Four-CPU clock brackets are now
0.31-0.35 us, root round trips 3.97-4.54 us, sparse population 0.43-0.50 ms and
dense teardown 0.28-0.32 ms. These elapsed gains include instrumentation changes.
Managed stress outliers still reach 5.35 ms in memory.unmap_batch and 6.07 ms
waiting for clock.now_ns; caller attribution does not distinguish host pauses.
This accepted session becomes the next baseline. Further native reruns need a
relevant change; native Intel CPU and physical hardware qualification remain open.

Clock boot-cost reporting remains a backlog item; retain per-boot calibration,
qualification and HPET fallback. SCI reconciliation, EC/platform drivers and PCI
INTx routing also remain deferred until required by a consumer.

Outstanding acceptance: native Intel CPU (including shared TSC), physical
Intel/AMD firmware/device DMA takeover and posted-write drain. Both emulated
IOMMU models on an AMD host do not establish those results. HPET-less SMP,
hard latency bounds, parallel storage command queues,
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


Historical milestone, superseded by ABI3 dynamic growth (item 20): hub capacity expanded to 16 KiB (four contiguous pages), with mediation backing
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
