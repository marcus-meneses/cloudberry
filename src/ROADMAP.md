# Cloudberry v2 roadmap

Target: modern (2016+) Intel/AMD x86-64, UEFI, PCIe, NVMe, AHCI and xHCI.
Kmodules are coupled kernel mechanisms; isolated handlers use a separate handle layer.

Current closure (2026-10-21): ownership management, explicit account homes,
nested interactive login and separate persistent development media are implemented.
See the closure at the end for validation. Earlier sections record chronological
milestones and their limitations at that time; current contracts live beside code.

Next bounded gaps: installer/update support for existing development media,
app-specific per-user data authorization and live credential/group revocation.
Session stop remains cooperative; forced termination and a secure compositor are
not provided by this milestone. Hardware/native KVM validation remains separate.

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
The application-facing C contract, protected processes and copied handle transport
are implemented. The three execution/handler priorities are closed:

1. Static ext4 ELF loading into private roots, ring-3 entry/return, CPU-local entry
   stacks/GS ownership, preemption, contained user faults and quiescent owned reap.
   One/four-CPU guest tests and malformed-ELF UBSan tests pass.
2. C system-library acquisition/publication/invocation, immutable boot JSON grants,
   owner-local tokens, filtered reflection, checked copies, retained result/error
   records, blocked request/reply and scoped memory/I/O managers. Host and real
   cross-CPU process tests pass. Explicit request release bounds internal caches.
3. The production privileged keyboard ELF/manifest/layout bundle loads from ext4,
   acquires /@memory and /@io and publishes /@keyboard. Independent protected
   clients validate denied/readonly/full grants, input delivery, structured arrows,
   USB removal/reconnection, cleared state and cooperative shutdown. IBM/ABNT2,
   Unicode, repeat, cursor replay/eviction and admission boundaries pass host tests.
   Focused one/four-CPU TCG acceptance uses disposable media; fixture clients never
   enter candidate boot. Repeated AMD-host KVM user-entry/input acceptance now passes; physical HID
   and native Intel CPU acceptance remain open.

Current status (2026-10-18): repeated native AMD-host KVM process/input/handle
acceptance, isolated phase profiling, bounded request-allocation tuning and
simultaneous-client/lifecycle acceptance are closed. The uninstrumented nine-run
session `20261018T153448.340455Z` is the new host baseline. All reported medians
improve against the preceding baseline, with no correctness/clock rejection.
See [the profiling guide](tests/handle-profiling.md).

Application launch/installed identity and lifecycle policy are now implemented:
a privileged userland `/@applications` handler, build-owned installation records,
ordinary `launch`/`true` apps, authenticated ownership, wake-driven completion,
cooperative stop and separate execution/result reclamation. Bootstrap remains a
startup coordinator. Isolated one/four-CPU TCG runtime acceptance and four-CPU
AMD-host KVM correctness acceptance pass. The native lifecycle run is separate
from the earlier handle performance baseline; no new performance campaign is required.

One boot input domain, pinned single-threaded processes, negotiated boot-keyboard
HID and trusted unsigned development bundles remain intentional limits.
Interactive approval, installer/signature enforcement, safe automatic provider
rebind/recovery, general HID/multiple keyboards, networking and graphical apps
remain separate milestones. Physical/native Intel acceptance remains open.

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
   ABI. This contract milestone is now followed by the implemented protected
   keyboard provider and isolated clients described above.

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

## Execution/handler follow-up history (2026-10-18)

The entries below record successive decisions and their then-current status.
The current next steps are summarized above; later acceptance supersedes earlier
"pending" statements.

1. Closed: cooperative process.stop, truthful provider health snapshots, retained
   reply state on allocation failure and isolated active-USB/pending-client fault
   acceptance with two explicit reap/restart cycles. No forced cancellation or
   automatic capability rebind.
2. Next: isolated native user-mode/handle validation and repeatable acquisition,
   invocation, wake/clock and allocation measurements, with raw host/guest reports.
3. Next: remove keyboard-specific bootstrap coupling through generic configured
   provider bundles/startup data; the handler owns its layout interpretation.
   Bootstrap coordinates startup, process owns execution/reap and handle mediation
   owns grants; ongoing supervision must not silently become bootstrap work.

Priority 2 collector implemented: isolated native process/keyboard acceptance,
raw clock/acquisition/reflection/kernel/IPC/allocation timings, participating-CPU
lock attribution, strict parser, source/binary/host identities and archived
explicit-reference comparisons. One-CPU TCG harness and four-CPU collection pass.
Native KVM results remain pending because /dev/kvm is absent here; performance
optimizations await that evidence. SSE/available AVX user-context checks now
exercise the gate and preemption rather than relying only on kernel-thread tests.

Priority 3 implementation: generic provider.cnf descriptors and boot.providers
replace the keyboard factory/configuration keys. The process mechanism loads
ELFs; handle mediation admits manifests; bootstrap only coordinates declared
startup/readiness. Generic readonly named files form the startup packet, with
handler-owned config/layout interpretation. Private startup records are released
only after process reap; aggregate/per-process snapshots expose no reclaimable
metadata pointers. An unrelated startup-data app and the keyboard/fault providers
exercise the same loader. No new installer, runtime filesystem or supervisor was
introduced. Native KVM measurement remains pending user-host execution.

Priority 3 closure: focused host tests, one/four-CPU TCG process/handler boots and
ordinary automatic startup pass; the candidate image is rebuilt. Bootstrap has
no keyboard implementation include or configuration interpretation. Public status
contains only stable aggregate/copy values, with explicit private-record release.
The remaining validation for priority 2 is user-host KVM collection through
scripts/bench-handles-kvm.sh; no performance optimization is claimed from TCG.

Native handle session `20261018T060145.736826Z`: three single-CPU TSC runs pass;
the first four-CPU AMD-Vi correctness run passes on HPET and the timing gate
rejects it. No profiling samples are unavailable. Root cause is unclassified
because that revision omitted qualification/runtime diagnostics. The isolated
probe/parser now preserve both reasons and raw qualification records, with
phase snapshots and explicit rejection messages. A targeted native four-CPU run
is required before clock changes or performance optimization; production clock
selection/health acceptance remains unchanged.

Targeted native handle follow-up `20261018T061208.303777Z` passes four-CPU
AMD-Vi with stable TSC and no diagnostic rejection or unavailable samples. All
CPUs qualify in the final 500 ms window after eight attempts/refinement; final
offset bound is 680 ns. This validates the diagnostic path and targeted native
process/keyboard boundary. It does not explain the previous HPET selection or
replace the full repeated topology/IOMMU baseline. Next native action remains
bench-handles-kvm.sh --runs 3, followed by evidence-based handle-path tuning.


## Native handle baseline acceptance (2026-10-18)

Session `20261018T062204.486040Z` passes all nine KVM boots: three each
with one CPU/no IOMMU, four CPUs/AMD-Vi and four CPUs/VT-d. Origin/begin/end
snapshots retain qualified TSC without runtime fallback or unavailable profiling
samples. This closes the repeated native process/keyboard/handle baseline for
priority 2. Both emulated IOMMU models ran on an AMD host; physical Intel/AMD
device acceptance remains open. The earlier HPET selection remains unexplained.

Median of per-boot medians, raw microseconds:

| Case | Clock bracket | Acquire memory | Reflection | Empty echo | 4096-byte echo | Allocate/free page |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| One CPU | 9.80 | 14.15 | 19.43 | 118.96 | 150.11 | 46.17 |
| Four CPUs, AMD-Vi | 10.36 | 14.96 | 20.31 | 153.73 | 204.52 | 48.91 |
| Four CPUs, VT-d | 10.49 | 15.09 | 21.66 | 125.74 | 179.71 | 50.42 |

These include handle-based timestamp endpoints, validation and request release;
they are not pure IPC latency. Lock peaks cover process startup/teardown and
owner waits as well as measured calls, so they do not isolate an RPC bottleneck.

First bounded tuning: gate input frames up to 256 bytes use per-call kernel-stack
scratch instead of temporary heap allocation. Larger messages retain the heap
path; retained requests/replies keep independent owned copies. All user-range,
authorization, lifecycle and lock checks remain. This removes one allocation/free
pair per small nonempty gate call without introducing a shared cache or ABI limit.
Host boundary/OOM tests cover empty, 256-byte and 257-byte frames. Native speedup
is not yet measured. Compare the next native collection against the accepted
session using `--reference-session bin/tests/kvm-handles/20261018T062204.486040Z`.

The scratch change passes handle/manifest/report host tests and the isolated
four-CPU AMD-Vi TCG process/handler acceptance, run
`5134829661d04033b248b454739cead3`, including keyboard hotplug, provider fault/
restart, copied IPC and benchmark collection. The candidate image and developer
disks were not used for these tests. TCG confirms correctness only.


## Small gate input native comparison (2026-10-18)

Session `20261018T063747.870742Z`, revision `cad0297`, passes all nine KVM
correctness/timing runs against `20261018T062204.486040Z`. Host compatibility
checks pass. All phase snapshots retain qualified TSC; participating-CPU lock
samples contain no HPET or unavailable timestamps. Both IOMMU cases still use
the same AMD host, not separate native Intel CPU acceptance.

Raw median of boot medians, microseconds (percentage change from baseline):

| Case | Acquire memory | Reflection | Empty echo | 4096-byte echo | Allocate/free page |
| --- | ---: | ---: | ---: | ---: | ---: |
| One CPU | 12.62 (-10.9%) | 17.22 (-11.4%) | 114.02 (-4.2%) | 156.80 (+4.5%) | 43.37 (-6.1%) |
| Four CPUs, AMD-Vi | 12.05 (-19.5%) | 16.86 (-17.0%) | 138.38 (-10.0%) | 198.91 (-2.7%) | 41.70 (-14.7%) |
| Four CPUs, VT-d | 13.56 (-10.2%) | 18.88 (-12.8%) | 145.08 (+15.4%) | 235.83 (+31.2%) | 67.74 (+34.3%) |

The small-request medians improve across all configurations, but this is not an
overall performance acceptance: VT-d IPC/allocation regressions remain unresolved.
Three separate boots per case do not establish statistical significance or
attribute variation to a particular subsystem. For example, current VT-d
acquisition boot medians span 11.62–22.09 us and clock brackets 8.12–15.17 us.
The single-CPU clock bracket also spans 8.41–19.58 us. Timings include endpoint
and lifecycle overhead; subtracting independent group medians cannot isolate
pure IPC or remove host scheduling variation.

Retain both sessions and the bounded scratch implementation. Before further
performance changes, add test-only phase attribution for user-copy/validation,
broker work, provider execution and block/wakeup handoff, with scoped lock
measurement and explicit instrumentation overhead. Compare those phases in
isolated repeated native runs; do not infer a mapping/lock bottleneck from
whole-run peaks that also include process launch and teardown. No new production
optimization or clock acceptance relaxation was made from this mixed result.


## Handle attribution, bounded tuning and concurrency closure (2026-10-18)

The requested build check passes before implementation (`make -C src -j4 build`).

1. Implemented isolated phase attribution: a separately compiled test broker,
   selected only in disposable process-test images, reports checked capture/
   copyout, broker work, blocking, kernel-resource execution and provider handoff.
   Broker-local lock statistics exclude process mapping locks; clock-pair samples
   document a lower bound on probe cost. Raw reports mark profiled/unprofiled
   sessions and reject cross-mode performance references. Native phase collection
   remains user-host work because this environment has no /dev/kvm.
2. Applied the measured allocation reduction: immutable input now trails its
   request record in one allocation. Before/after instrumented one-CPU runs show
   5,782 -> 4,494 allocations, exactly one saved for each of 1,288 invocations.
   This is 22.3% less allocation work in this workload, not a native latency claim.
   Host failure/ownership tests pass. No checks, lock ordering, retained outcomes
   or payload isolation were removed. Broader tuning awaits native phase evidence.
3. Implemented two-cycle simultaneous-client acceptance: three independent clients
   exchange distinct 4 KiB payloads; a slow provider exercises queued versus
   dispatched deadlines, preserved input, BUSY release and late committed result
   retrieval. Unrelated memory work progresses while the provider is outstanding.
   Provider discovery disappears after teardown, broker blocks return exactly to
   baseline in the traced variant, and page/stack counts return after warmup.
   Existing active USB/provider-fault/restart tests still run. Ordinary and traced
   one/four-CPU TCG process boots pass. Traced run IDs:
   `fb8ca01f3edb4cfda4fcc5d4ff352c02` (one CPU) and
   `3cc92842893e466e8f685130207fb2dc` (four CPUs/AMD-Vi).

The [profiling guide](tests/handle-profiling.md) defines measurement boundaries,
remaining acceptance and native commands. Production changes are confined to
request storage; tracing/stress fixtures stay out of the candidate image.

Final verification: the candidate rebuild and broker/manifest/report host tests
pass; candidate module exports contain no `handles.profile`, and its module/bundle
staging contains no test fixtures. The updated collector passes a complete
one-CPU profiled TCG session `20261018T152345.941848Z`, preserving raw logs,
identities and archive. This is harness/correctness acceptance, not native timing.


Native phase collection closes in session `20261018T152958.059166Z`: all three
four-CPU AMD-Vi KVM runs pass stable-clock, concurrency and resource acceptance.
Allocation counts reproduce the deterministic reduction (4,494 each run), with
six ending broker blocks and no allocation failures or invalid spans. Mean IPC
queue/delivery time is 60.65–61.78 us, delivery/reply 16.68–21.02 us and
reply/client-resume 51.32–56.59 us across the instrumented runs. These elapsed
handoff spans include notification, scheduling, broker and lock work; no single
cause is established. See [the native phase analysis](tests/handle-profiling.md#native-phase-acceptance-2026-10-18).

Next remains the ordinary uninstrumented native comparison against
`20261018T063747.870742Z`. Profiling-only results cannot establish a production
latency win or justify changing scheduling/locking on their own. No production
code changed at this acceptance step.


## Uninstrumented native handle acceptance (2026-10-18)

Session `20261018T153448.340455Z` at revision `ab6a747` passes all nine
uninstrumented KVM boots against `20261018T063747.870742Z`: three each on one
CPU/no IOMMU, four CPUs/AMD-Vi and four CPUs/VT-d, on the same AMD host. All
origin/begin/end snapshots retain qualified TSC, without runtime/qualification
errors or unavailable/HPET lock samples. Every run also passes simultaneous
clients, queued/dispatched deadlines, late results and page/stack cleanup.
Exact broker heap restoration is separately accepted by the preceding profiled
native session; the uninstrumented variant contains no heap-count probe.

Raw median of boot medians, microseconds (change from reference):

| Case | Acquire | Reflection | Empty echo | 4096-byte echo | Page allocate/free |
| --- | ---: | ---: | ---: | ---: | ---: |
| One CPU | 11.04 (-12.5%) | 15.19 (-11.8%) | 102.33 (-10.2%) | 136.85 (-12.7%) | 38.46 (-11.3%) |
| Four CPUs, AMD-Vi | 11.20 (-7.0%) | 15.41 (-8.6%) | 125.98 (-9.0%) | 176.43 (-11.3%) | 39.30 (-5.7%) |
| Four CPUs, VT-d | 11.18 (-17.5%) | 15.37 (-18.6%) | 111.99 (-22.8%) | 148.49 (-37.0%) | 39.22 (-42.1%) |

All eight measured case medians improve in all configurations. Kernel-path
boot medians are now tightly grouped; IPC remains more variable (AMD-Vi 4096-byte
echo boot medians span 151.13–185.13 us). Three boots do not isolate host effects
or establish statistical significance; the noisier preceding session especially
limits attribution of large VT-d percentage changes to the code change alone.
Accept this as the new observed native baseline and close the current performance/
concurrency follow-up, without a hard latency guarantee or native Intel claim.

The profiled and uninstrumented sessions have identical common kernel/probe/app
binary hashes; the disposable broker variant and generated ISO differ. On the
four-CPU AMD-Vi case, profiled/unprofiled medians are 3.24x for the clock bracket,
3.37x for acquisition and 1.52x for empty echo. These cross-session observations
show substantial probe perturbation; they are not universal correction factors.
Do not optimize production scheduling from uncorrected instrumented totals alone.
No further production code changed at this acceptance step.


## Installed application lifecycle milestone (2026-10-18)

1. Implemented `/@applications` in privileged userland under `handlers/applications`.
   Public C launch/info/wait/stop/release operations use copied frames. The service
   uses authenticated broker PID/installation context, retains deferred waits,
   authorizes launcher installations and enforces child ownership. Startup context
   carries argv/cwd without kernel pointers. Kernel additions remain mechanisms:
   installed-image loading/admission through the I/O bridge, owned completion/
   owner-exit channels, join wakeups and safe reap. No kernel application manager
   or bootstrap runtime supervisor was introduced.
2. Implemented a build-generated immutable `installed.bin` registry outside app
   bundles, binding image paths, embedded manifests, identity, role and execution
   settings. Build-time privilege/path/duplicate checks and kernel framing/admission
   checks fail closed. The manager's process authority is separately bound by the
   kernel installed-image loader; a manifest cannot grant that binding. Current
   trust root is the read-only image, without installer/signature or user policy.
3. Packaged ordinary `launch` and `true` apps and the shared C startup helper.
   Disposable registered fixtures validate concurrent children, whitespace/case
   preservation, cwd, denied arbitrary/provider launches, manifest isolation,
   foreign-child denial, deferred timeout/late wait result, cooperative stop,
   child fault containment and abandonment cleanup. Ordinary boot starts only
   keyboard/application providers; test apps and initial test launches stay out.

A valid provider BAD_REQUEST now completes rather than being confused with error
normalization failure. Native kernel-resource copied replies are reserved before
mutation, preventing a post-launch allocation failure from hiding the child ID.
Focused host tests cover both cases, plus registry parsing, binding and OOM
rollback; C/C++ contracts and keyboard regressions pass. Existing process/USB/
concurrency acceptance remains enabled. Runtime details and limits are documented
in [the applications provider](handlers/applications/README.md).

Initial isolated TCG acceptance: one CPU run
`1497bd2b991a4f4fabdca50d1705a999`, four CPUs/AMD-Vi run
`07a0c41cca5543029186a25fef92dd62`. These are correctness tests, not a new native
performance baseline. Processes remain pinned/single-threaded, stop is cooperative,
cwd is metadata rather than filesystem permission, and automatic provider recovery
is not part of this milestone. Native acceptance follows below; physical hardware
and native Intel CPU acceptance remain open.


Final runtime verification: the traced four-CPU AMD-Vi TCG run
`9c46c95a82c148f298f53f328fd3b48e` passes all runtime scenarios and exact broker
live-block restoration after lifecycle-provider teardown. The ordinary four-CPU
candidate boot `d9b455b12ffc434b8dda53de79961f4d` publishes both providers using
only the read-only hybrid boot image: no test modules or developer data disks.
Packaging checks confirm four OS installation records (keyboard, applications,
launch, true), an empty production initial-launch file and no profiling export.
The candidate is rebuilt.

Native lifecycle correctness acceptance (2026-10-18): isolated four-CPU AMD-Vi
KVM run `535b22562f0d45b4b0752e72a5ade1e6` passes installed launch, argv/cwd,
manifest isolation, concurrent children, deferred waits, cooperative stop, fault
notification, owner abandonment and cleanup. Existing handle concurrency,
page/stack baseline restoration and keyboard hotplug/decommission checks pass.
TSC remains qualified through measurement on all four CPUs (480 qualification
samples); participating lock samples use TSC without HPET/unavailable samples.
Reports are in `bin/tests/isolated/tests/process-4cpu-amd/`. This uninstrumented
correctness run does not establish exact broker live-block restoration (covered
by the traced TCG run above) or replace the accepted performance baseline.


## Interactive console milestones closed — 2026-10-19

1. Privileged display, graphics and text providers implement the exchangeable
   text → graphics → display chain. Display initially uses the boot framebuffer;
   native GPU drivers remain future replacements behind the same contract.
2. Graphics supplies owned surfaces, clipping/transforms, composition, lines,
   rectangles, ellipses/arcs, cubic curves, paths, bitmap rendering and blits.
   Generation counters and dirty bounds track actual changes; presentation clears
   screen damage only after every display transfer succeeds. Owner exit reclaims
   offscreen resources. Text retains cells, derives dimensions and scrolls by blit.
3. An ordinary terminal launches catalog-bound apps with explicit stdin/stdout/
   stderr capabilities. `=>` recreates, `->` appends and `|>` runs concurrent
   bounded-pipe stages. Destinations are authorized before launch. CPU placement
   belongs to the kernel; processes remain pinned. Echo/cat/ls/pwd are apps.

Host UBSan contracts/raster/stream checks and ext4 journal recovery pass. Isolated
TCG console runs 61c61c01976043a7ae127b3cf1713a4f (four CPUs) and
dc72042008844c0da19fbcc98b8e4fc7 (one CPU) passed graphics ownership/damage,
binary pipelines larger than ring capacity, redirects, stderr, stdin/EOF and
keyboard typeahead. Fixtures never enter the candidate catalog. A manual KVM
candidate smoke run was reported healthy; automated native console acceptance
remains separate. The user deferred sluggish text rendering for later work.

Remaining console limits: one shared boot canvas, a bitmap ASCII fallback font,
no history/full cursor editing, background jobs, sessions, installer or per-user
application policy. Foreground multiplexing uses bounded nonblocking drains and
keyboard deadlines rather than a general wait-set. Delayed journal flush stays
on the roadmap. No automated native Intel/physical hardware claim follows from
these console tests.

## Filesystem namespace applied — 2026-10-19

Image construction now prepares the reviewed filesystem tree, flat system apps
and providers, shared keyboard configuration, generated `_index` caches and
root-only app-state directories. Limine modules are split into boot drivers and
helpers without changing module identities. Catalog-bound private root app-state
writes and underscore-aware `ls -a` accompany the layout. No boot migration,
formatting, placeholder account, installer or user-session system was added.
See [filesystem namespace](maps/filesystem.md) for current and reserved paths.


Filesystem validation: production build, installation/config/interface host tests,
namespace/ownership/Limine-path checks and independent GPT/ext4/e2fsck checks pass.
Isolated one-CPU TCG console run `706333e2464449d680f110b4b1e26cad` passed the
new layout, terminal private-data write/read, underscore filtering and `ls -a`,
as well as existing graphics/pipeline/stdin tests. Four-CPU TCG runs
`45bf8459cad14982a400e38f1edbdeb2` and `eef932b3229848da8992bd5075eddccc` reached
the terminal and executed commands but timed out on subsequent synthetic keyboard
input; the latter showed missing/repeated characters during concurrent host build
work. This is not passing four-CPU interactive acceptance or an established root
cause. No text-performance changes were made; native KVM validation is pending.


## Keyboard stall investigation — 2026-10-19

The four-CPU TCG timeouts were real runtime failures, not accepted as mere input
injection timing. Debugger captures found two concrete failure paths:

- Kernel lock watchdog panic after an invalid HPET reference read. First-failure
  evidence (`b156fc8532424cd8a8e101c6aba927a1`) recorded start=120105905,
  end=0 and elapsed_ns=0: regression handling, not five seconds of lock ownership.
  Clock/watchdog/sample readers now use bounded high/low/high 32-bit validation,
  rejecting rollover and transient all-zero samples. Exhausted retries still
  fail explicitly; elapsed-time and iteration watchdogs remain enabled. The raw
  sample validation is containment. Follow-up host MMIO tracing establishes
  wrong-device dispatch in QEMU 6.2 MTTCG: HPET loads reached IOAPIC while AHCI
  memory decoding changed. Guest page-table walks remain correct. See
  [clock evidence and upstream fix](kmodules/shared/clock.md#zero-read-cause-established-2026-10-19).
- Keyboard capture worker exited after xHCI cancellation (`48e337393e5e4f13bccf82f20896696b`:
  Stopped transfer completion 26 and Context State Error 19). Cancellation now
  distinguishes Running/Halted/Stopped endpoint states and retains intermediate
  Stopped events until dequeue commitment. Unexpected worker exit notifies the
  provider, which withdraws its publication; terminal reports input failure.

Keyboard report acknowledgement precedes decoding/read replies, and one owned
pending-read node is reused to avoid page-map/unmap churn. Normal stop remains
separate from failure; unproven DMA quiescence still retains ownership. Test
injection uses explicit input edges, and optional GDB snapshots remain isolated
from candidate startup.

Host rollover/zero-read tests, clock modes, xHCI state/Stopped-event tests,
keyboard replay/node reuse and failed-worker notification pass. Production
four-CPU TCG integration passes in `880fe579554e4dfa99176ddc94c0798c` and
`63febf4d2e7f40cb880bcaa7a1423781`; the latter explicitly idles then resumes
input after multiple interrupt-IN cancellation cycles. Both include graphics,
binary pipelines, stdin/EOF, stderr and root app-data/hidden-entry checks.
Earlier manual KVM acceptance predates these fixes; native requalification is
not inferred from TCG. Text-rendering performance remains deferred as requested.

Follow-up qualification is not fully closed: four-CPU run
`2f9c66eb4cfd44049aed0a321b8286ad` starts all providers, then reports keyboard
input failure; the capture worker exits with xHCI command completion 19 and a
retained pending transfer with Stopped completion 26. The earlier state-aware
cancellation fix therefore does not cover every runtime case. Provider startup
failures now log bundle, launch/readiness phase, error and available process
exit/fault details, instead of only a generic failure. Native KVM acceptance and
a QEMU build containing the MMIO-dispatch race fix remain outstanding.

KVM startup report `c10274f35d5f43138f6f1826bd1d17dc` identifies a separate
startup-deadline defect: keyboard is alive and `/@keyboard` is present in the
capture, but bootstrap has already abandoned readiness. Its single 30-second
budget covered volume discovery and all ELF loads/providers. Each provider now
gets its own 30-second publication budget after load/start; storage keeps its
separate budget. Focused host tests exercise readiness after 40 seconds of prior
work, successive provider windows, genuine timeout and clock overflow.

Quiet USB interrupt-IN transfers no longer inherit the control/bulk one-second
timeout. They stay armed and use notification-only `xhci.wait(UINT64_MAX)` until
completion, disconnect or explicit cancellation. Finite caller deadlines still
apply. This removes the unnecessary recurring idle cancellation path; explicit
shutdown/cancellation and fail-closed DMA ownership remain. Host USB tests verify
default versus explicit deadlines; xHCI tests verify blocking without a timer.
An isolated four-CPU TCG run with disposable NVMe/SATA disks,
`f9012e4a594c4a93800ffe9dc3ba5790`, passes the startup-budget fix before the idle
receive change. Native KVM requalification remains pending.

After both changes, `a6ff2e9b55ec437bbb865e343a6d1f10` passes the complete
four-CPU TCG console fixture with disposable NVMe/SATA: provider startup,
foreground input, graphics, binary pipeline/file verification, stdin/EOF,
redirection, private data, hidden entries and idle/resume. Bootstrap, USB, xHCI
and keyboard host checks and the candidate build pass. This does not substitute
for rerunning the reported native KVM case.

## Console performance investigation — 2026-10-19

Three avoidable costs are addressed without bypassing resource handles:

- Text previously made a synchronous graphics request and presentation for every
  glyph. The text provider now sends bounded batches of positioned glyphs, with
  graphics validating the complete batch and presenting once. Font ownership,
  clipping, colors, UTF-8 state, wrapping and scroll ordering remain in their
  existing providers. Legacy single-glyph calls retain their contract.
- A scroll previously allocated/freed a 4,014,080-byte snapshot at 1280×800,
  creating page-mapping/unmapping and global TLB-shootdown work. Identity SOURCE
  self-blits now copy in overlap-safe order without allocation; transformed or
  alpha-composited blits retain the snapshot path.
- Idle terminal prompts no longer issue clock/keyboard timeout calls every
  10 ms. They block on input; foreground multiplexing and orphan reaping retain
  bounded waits until a general wait-set exists.

Same four-CPU TCG text probe, before `6790ee2085ba4702bda787a27895a9a0` and
after glyph batching `a1cd7f8e3e964575bad8f27b9daab491`: 32 characters
157,966,050 → 26,221,180 ns (6.0×); 128 characters 730,145,340 → 45,728,950 ns
(16.0×). The cold one-character sample did not improve (32.0 → 46.3 ms).
These are individual diagnostic samples, not a native KVM latency guarantee.
Comparison data is saved in `bin/tests/console-performance-comparison.json`.

The broader probe `ce3f244f44be4f54ae1e72f177a39ca4` measured stat at 17.9 ms
and create/write-32-bytes/remove at approximately 2.4 s each. It overlapped a TCG
integration run, so these are diagnostic observations, not a clean baseline.
Filesystem mutations still synchronously commit the ext4 journal. Deferred
journal flush remains deferred; display optimization does not remove disk latency.

Host raster/stream checks and candidate build pass. Full four-CPU console
integration `95a96bf224634041bbde9f13a22a6616` passes batching and idle waiting,
including graphics, pipelines/files, keyboard and 60-line scrolling. After the
allocation-free self-blit change, focused run `a033e0dcbe4b4e109fd1d3767055e79a`
passes 60-line scrolling and prompt return in 4.05 host seconds including command
injection. Native KVM performance requalification is still pending.

## Startup-to-interactivity timing — 2026-10-19

Sequential production-image TCG measurements, including firmware/Limine and
fresh disposable NVMe/SATA devices, excluding image preparation:

| CPUs | First observed prompt | Verified key echo | Report |
| --- | --- | --- | --- |
| 4 | 17.379 s | 17.604 s | `c51e3ec97e2548d697071972624e1117` |
| 1 | 17.402 s | 17.622 s | `994b8a8bb83844a98975e6a4ae05c2c4` |

The four-CPU candidate's host-observed milestones are kernel entry 3.805 s,
event loop 4.524 s, volume ready 7.073 s, provider startup completed about
16.84 s, then prompt 17.379 s. The provider-ready log is printed as a group and
cannot individually time providers without the diagnostic build.

Separate four-CPU phase trace `f48de7e16884403a8d98f223400bc00f` reaches its prompt
in 17.752 s (logging overhead included):

| Provider | Load/admit/start wall time | Remaining readiness | Total |
| --- | --- | --- | --- |
| keyboard | 1.904 s | 0.031 s | 1.935 s |
| display | 0.972 s | 0.003 s | 0.975 s |
| graphics | 1.077 s | 0.348 s | 1.425 s |
| text | 0.968 s | 2.122 s | 3.089 s |
| filesystem | 0.943 s | 0.003 s | 0.946 s |
| applications | 1.469 s | 0.007 s | 1.477 s |

Text readiness includes its initial full-screen clear/presentation. Clock init
is 18 ms and ACPI init 443 ms in this TCG trace. These timings concentrate this
observed delay in provider loading and initial presentation; one versus four
CPUs does not materially change the measured TCG total. They do not establish
native KVM timings or isolate exclusive CPU time inside each phase.

At the time of the baseline measurement, `make run` depended on a forced kernel relink, forcing boot-image
recreation on every invocation. An isolated image-builder-only measurement is
5.921 s (`bin/tests/startup-packaging/48e2782028954c8e9eb90dad2f29e57e/timing.json`),
so there is additional host-side wait before QEMU starts, even with unchanged
sources. Compilation and staging are additional to that builder-only sample.
Next targets are removing unconditional repackaging and profiling provider
load/allocation/I/O and text's initial presentation before changing runtime policy.

Reports are combined in `bin/tests/startup-timing-comparison.json`. Candidate
kernel/ISO hashes are unchanged by the measurements; traced replacements stay
in disposable test images. The agent cannot access KVM here; the focused native
command is documented in `tests/README.md`. That measurement-only baseline preceded the repairs recorded below.

## Startup repairs — 2026-10-19

Removed unconditional candidate relinking/repackaging. Content-preserving input
stamps detect selected object variants and image settings, with source/tool and
firmware prerequisites still driving rebuilds. An unchanged `make build` takes
0.081 s and preserves the image timestamp (`bin/recovery-incremental.json`).

A diagnostic trace split bootstrap file reads from process load/admission and
provider readiness. Small provider definitions/manifests took roughly 0.25 s each.
A host reproduction identified cache thrashing: the upstream eight-block ext4
cache issued 200 physical reads for twenty repeated deep-path metadata lookups.
The engine now retains up to 128 lazily allocated cache blocks (512 KiB of block
data for a 4 KiB filesystem); the identical warmed lookup loop issues zero reads.
Synchronous journal commits and barriers are unchanged. The existing journal
suite passes 29 crash cuts, six injected I/O failures and independent fsck.

Graphics presents a uniformly colored damaged region using display.fill rather
than sending every pixel through chunked display.blit calls. It inspects final
RGB pixels, preserving backing data, state and pending damage; mixed regions
still blit and failed presentation retains damage. Host bounds/alpha/uniformity
checks and raster/stream/bootstrap checks pass.

Production image, four CPUs, TCG, disposable USB boot + NVMe/SATA media:

| Measurement | Earlier baseline | Repaired |
| --- | --- | --- |
| First observed prompt | 17.379 s | 11.304 s |
| Verified first key echo | 17.604 s | 11.526 s |

Repaired report: `7ec6b4d1bbd94f8084f0a265046ab9da`. Comparison:
`bin/tests/startup-repair.json`. These are individual host-observed samples,
including firmware and framebuffer polling, excluding image preparation. They
do not establish native KVM timings. Metadata cache retention and solid fills
address measured costs; synchronous mutations remain a separate latency limit.

Full four-CPU TCG console acceptance passes on the repaired image:
`79416c61ce7a447caac48e56c30c9f17`, including graphics ownership, byte-verified
large file pipelines, stderr, stdin EOF, installation-private state, hidden
listings, idle/resume keyboard input and sixty-line scrolling. All disk paths
were disposable; developer persistent disks were not attached. Native KVM
requalification remains pending because this environment has no `/dev/kvm`.

## Native graphics startup timeout / profiling cost — 2026-10-19

Native KVM report `03f0f0e282c34c6facf5be16153b1922` fails graphics publication
readiness after 30 s. PID 3 is still running inside its initial memory request,
with about 7,975 thread switches; it has not faulted. The BSP snapshot is inside
a profiling HPET read. Shared time and statistics use HPET because cross-CPU TSC
qualification was not accepted. This is a real failed startup, not a completed
performance measurement. The provider deadline is unchanged.

The diagnostic fallback previously performed repeated three-load HPET reads
around every outer lock. It now samples one entry in 64 independently for each
CPU/global-lock/clock-lock/standalone-IRQ domain, retaining matching references
through each selected entry. Raw ordered tick counters and regression detection
still cover all entries. Qualified TSC statistics remain unsampled. A dedicated
`sampled_out_ns` counter distinguishes deliberate omissions from invalid samples;
CPU_LATENCY exposes `hpet_profile_stride`. HPET-mode nanosecond peaks and notices
are sample maxima, not exhaustive worst-case observations. System timestamps,
TSC qualification and independent lock/IPI/TLB watchdog polling are unchanged.

Build, profiling/clock host checks and corrected thread-state fixture pass. The
isolated failed-owner watchdog (`bin/tests/kvm-latency-repair/tests/lock-watchdog`)
still triggers its independent HPET timeout, register dump and halt while the
fixture forces a TSC statistics rate. Four-CPU TCG profile run
`701f6ea236b741d88ea99781419331ef` passes with prompt observed at 6.778 s. Its
CPU0 snapshot records 19,508 HPET duration samples, 1,227,978 deliberately omitted
durations and zero unavailable duration samples. The seven text/disk probes
complete; `performance-state.json` preserves clock and profiler state. These
observations support reduced instrumentation cost but do not qualify KVM; native
replay against the rebuilt candidate is still required. Successful diagnostic
profiling runs now capture state after measurement, without debugger calls into
the guest.

Production-catalog TCG startup replay `a47c969faf4f4bdca5f9f1dcf5832488` reaches
the prompt at 6.610 s and verifies keyboard echo at 6.844 s (four CPUs, disposable
NVMe/SATA), versus the previous 11.304/11.526 s. These are individual observations,
not a native speedup claim. Collected evidence: `bin/tests/kvm-startup-repair.json`.

## Focused native KVM acceptance — 2026-10-19

User-run four-CPU KVM profile `7166875b0eec446c93158021ffe2a5c0` passes with
disposable USB boot, NVMe and SATA media. First observed prompt: 6.994 s, including
3.960 s before kernel entry. Event loop is ready at 5.917 s, volume at 6.420 s,
and providers at 6.569 s. The graphics publication timeout does not recur in this
run. These are host-observed, single-run timings with framebuffer polling.

Shared TSC qualification succeeds (`source=1`, `smp_tsc=1`, rejection reason 0)
at 2,096,180,151 Hz. Statistics also use that rate. All CPUs report zero
unavailable duration samples; startup HPET samples and deliberate omissions are
retained separately. Runtime qualified-TSC measurements remain unsampled.

| Probe | Guest elapsed time |
| --- | --- |
| Text, 1 character | 0.920 ms |
| Text, 32 characters | 1.032 ms |
| Text, 128 characters | 4.115 ms |
| Filesystem stat | 0.351 ms |
| Create | 65.797 ms |
| Write 32 bytes | 57.205 ms |
| Remove | 79.450 ms |

This closes focused native startup/performance replay, not long-duration or full
console acceptance. Synchronous journaled mutations remain the clearest measured
interactive cost. Lifetime-wide counters also contain a 105.438 ms maximum lock
hold and up to 8.637 ms managed IRQ-off interval; these include bootstrap and host
descheduling, so attribution and a post-startup reset are needed before treating
them as steady-state latency. No additional runtime change follows this sample.

## Clear and scroll rendering — 2026-10-19

Identity rectangle fills now use clipped rows instead of polygon edge testing.
Overlap-safe SOURCE self-copies also use clipped rows; damage is merged per row
and generation saturates by the exact changed-pixel count. Known covered opaque
fills use display.fill directly without a uniform-color scan. Transformed and
alpha-composited operations retain the general renderer.

Display.copy and IO display_copy are appended compatible operations. The initial
backend validates exact sizes, both rectangle bounds, installation owner, native
pixel layout and pitch before copying in overlap-safe order. Fill channel packing
is calculated once per request. No pointers or framebuffer layout escape.

Graphics queues a screen self-copy only when the displayed source is synchronized
with backing pixels. It preserves deferred BLIT behavior; presentation performs
the copy and then only subsequent dirty pixels (normally the exposed text row).
A covering clear supersedes it. Unsupported copy uses ordinary pixel transfers;
a failed/partial copy retains destination pixel damage and is never retried as
a non-idempotent overlapping copy. Surface reflection includes queued damage.

Host UBSan checks pass for raster/reference equivalence, exact generation and
damage, pitch/format/owner/bounds, deferred presentation, clipped/multiple copies,
unsupported backends, failed fills and repair after a simulated partial copy.
Build and C/C++ interface checks pass. The candidate catalog contains no probes.

The dedicated guest probe measures five nonempty clears and five full-width text
scrolls over alternating colored rows, excluding preparation/output. Four-CPU
TCG, 1280×800, disposable boot/NVMe/SATA; single boot per implementation:

| Operation | Before median | After median | Ratio |
| --- | --- | --- | --- |
| Clear | 659.427 ms | 21.380 ms | 30.8× |
| Dense scroll | 1188.392 ms | 26.415 ms | 45.0× |

Before: `544c87cf30de40549f9bd60841824a6e`; after:
`180d6a0c944e45d3bf0de79c23f9c4de`. Raw samples/medians are saved in each
render.json and `bin/tests/render-comparison.json`. These are TCG observations.
Native KVM/physical framebuffer-read costs still require their own measurement.
The display contract can later use a native driver's copy engine or a pixel
fallback; text stays independent of the backend.

## Native region-copy follow-up — 2026-10-19

Four-CPU KVM render replay `c6a5770963b64e3688873034b5168393` passes, but its
five-sample medians are clear 5.337 ms and dense scroll 311.815 ms. Shared clock
is HPET in this boot (SMP qualification reason 14); it is not a stable-TSC timing
baseline. The original display-copy loop also read each source pixel directly
from framebuffer memory. This is an expensive virtual/physical device-memory
path that the earlier TCG speedup did not qualify.

The resident framebuffer I/O backend now keeps a lazily allocated native-pixel
RAM shadow through the existing mem.pages_alloc hub surface, owner 0. It costs
3.90625 MiB at 1280×800, retained for this foundational backend's lifetime. Fills
and RGB blits update the shadow. Overlapping region copies read ordinary RAM
and send changed row spans to the framebuffer. A complete initial paint seeds
it without framebuffer reads; a copy before that paint performs one readback.
Allocation failure leaves fills/blits functional and returns copy UNAVAILABLE
so graphics uses its pixel fallback. Neither handle ABI nor graphics policy
changes; shadow pixels and physical addresses stay private.

Host checks cover zero framebuffer reads after full painting, partial blit
coherence, overlap, pitch/format guards, authority and allocation-unavailable
fallback. Candidate rebuilt. The prior run is recorded as the native pre-shadow
result in `bin/tests/render-comparison.json`; native shadow acceptance follows.

Shadow-path TCG checks pass: render probe
`b2823460f5b043dda7e3ff299f3ed678` (five-sample medians: clear 15.969 ms, scroll
29.423 ms) and visible sixty-line scroll
`16c9ac101cdc41dcbf82d258a43cde95` (remaining lines 12–59 visible and oldest
line absent). The timing run overlapped final host/build work and is not a clean
comparative baseline. Native verification is recorded below.


Native shadow acceptance: four-CPU KVM replay
`9ffd0a0a32aa496ca2ef66031d09d082` passed using the candidate kernel and
disposable test disks. Five-sample median dense scroll fell from 311.815 ms to
8.322 ms (37.5× faster, 97.3% reduction). Clear measured 6.521 ms versus the
previous 5.337 ms; this change does not establish a clear speedup. Both native
boots used HPET after shared-TSC uncertainty rejection (reason 14, CPU 1 in the
new run). These are focused single-boot rendering results, not stable-TSC or
physical-hardware acceptance. Raw samples, clock context and ratios are saved
in `bin/tests/render-comparison.json`. This closes native shadow-path validation.


## Terminal wait-any — 2026-10-19

Native full-console/long-duration acceptance and filesystem mutation latency
attribution are deferred at user request. This item addresses only replacing the
terminal's 10 ms foreground/orphan polling with wake-driven multiplexing.

The existing keyboard read remains provider-owned. Appended system-gate
submission/collection and wait-any operations let the terminal retain that read
alongside child completion waits and directional pipe readiness. Existing copied
contracts, manifest checks, ownership and synchronous INVOKE remain intact.
Registrations use stack storage under the broker lock, scheduler wake credits,
and fresh ID lookup after waking. Replies/failures and stream transitions notify
matching waiters; EOF/broken peers remain ready so ordinary calls report them.
Timeouts bound the wait, never cancel dispatched provider work. No vows, threads,
background jobs, disk durability changes or clock optimizations are introduced.
The terminal prefetches its next keyboard batch before echo, but slow synchronous
UI work can still delay collection/rendering. Complete native acceptance remains
deferred. Build, UBSan wait-any/handle/console host checks and C/C++ contracts
pass. Focused four-CPU TCG guest `fe5b611412514103aad9a50207be9447` passes
delayed stdout/stderr without keypress, silent child completion, stdin/Ctrl-D,
pipeline completion and idle keyboard resume using a disposable fixture image.
No native acceptance or filesystem performance work was performed.


## Collection status and abandonment — 2026-10-19

1. Collection now separates transport status from provider status. The raw gate
   returns OK when a validated reply is copied; the SDK reports its operation
   status with an explicit collected flag. Completed WOULD_BLOCK/BUFFER_TOO_SMALL
   replies release their request normally. Buffer/allocation failures retain it.
   Production broker/SDK tests cover both collisions and successful cleanup.
2. An appended ABANDON gate atomically removes queued work, reports dispatch
   phase and known/unknown outcome, and relinquishes dispatched work without
   promising rollback. Late replies discard the retained record without allocation;
   provider death also releases it. Active synchronous waiters are protected.
   Tests cover foreign IDs, insufficient output space, OOM without side effects,
   queued removal, late completion under OOM, completed outcomes and provider death.
   The terminal abandons its prefetched keyboard call before a normal exit.

Both items pass the focused host suite. Native full-console/long-duration and
filesystem latency work remain deferred.


## Terminal editing — 2026-10-19

3. The terminal now keeps session-only history in a growing private-memory list,
   recalls it with Up/Down and restores the unfinished draft. UTF-8-aware
   insertion/overwrite, arrows, Home/End, Backspace/Delete and a reversed cursor
   cell are implemented. Long lines retain their full transport-bounded contents
   in a horizontal input viewport. Tab uses the existing filesystem handle to
   complete cwd-relative/absolute names, preserve quoting through literal escaping,
   add directory slashes, extend common prefixes and list ambiguous matches in
   four columns. Hidden entries require an explicit underscore prefix. Child
   output preserves the unfinished input. Text receives only the appended bounded
   cursor-positioning primitive; editing/completion/history remain application code.

Host editor/contract/handle checks pass. Focused four-CPU TCG guest
`fb6560966c12462594c72eebcca804de` passes editing and completion, draft recall,
and delayed-output/stdin/pipeline/idle wait-any regressions using disposable media.
Candidate built. History is not persisted, the cursor does not require a blink
polling timer, and synchronous rendering/directory calls can still delay UI work.
Native long-duration acceptance and disk latency optimization stay deferred.


## Overwrite cursor indication — 2026-10-19

Insert now toggles between the default steady reversed cursor (insertion) and a
blinking reversed cursor (overwrite), as requested. Only active overwrite input
arms a 500 ms deadline on the existing wait-any call; keyboard reads remain
pending and provider-owned. A lazily acquired clock capability supplies deadlines.
Blink transitions repaint one positioned text cell, restoring the covered
character on the off phase. Editing resets the visible phase. Returning to
insertion mode removes the blink deadline; no periodic keyboard polling is added.

Build and the focused disposable four-CPU TCG cursor check pass:
`548a6f985ce94f5dab305648a5fe0ddd`. Captured framebuffer samples show steady
insertion, visible/hidden overwrite phases without character damage, steady
insertion after toggling back, and correct overwrite/insert command output.

## Descriptor-driven USB mouse — 2026-10-19

The installed privileged mouse handler now publishes `/@mouse` with manifest-bound
READ/STATE operations. It discovers HID interfaces, parses their report descriptors
in userland and normalizes axes/buttons/wheels/report IDs. Multiple interface
attachments keep independent identity/state. Reads block with replay cursors and
work with wait-any; removal releases held buttons and tears down capture safely.
Standard Resolution Multiplier feature reports use read/modify/write with unrelated
bits preserved. State pagination and owned lists avoid a device-count ceiling.

The HID kmodule (extracted from IO in the following milestone) supplies generic, catalog-bound HID transport: descriptor and
feature control, claimed endpoints, preallocated acknowledged report mailboxes,
USB change notifications and owned BSP workers. Capture length comes from the
largest declared input report rather than assuming one USB packet. USB now has
independent per-endpoint transfer state/bounce buffers alongside serialized EP0;
metadata and sibling interfaces remain usable during idle interrupt-IN. Teardown
waits for all active endpoints. An explicit cancellable-request extension preserves
the original usb_io prefix and closes the queued/submission cancellation race.

The isolated boot test caught a flat-PIC pointer initialization issue when the
kernel I/O operation list grew: the compiler generated absolute table pointers.
Publication now builds relocated pointers from a contiguous character table at
runtime. Boot and device testing pass after the correction. No developer disk
or host boot configuration was modified.

Validation: build; HID parser and transport/ownership/OOM checks; overlapping
composite endpoint/control and cancellation checks; existing USB/keyboard tests;
installation privilege checks; C/C++ contracts. Final disposable four-CPU TCG
run `ae8a70f9b3954c4e86c6b25ef66606b2` passes motion/buttons/wheel, second-device
attachment, held-button removal, reconnect with a fresh identity, wait-any and
continued terminal keyboard use. Earlier full mouse replay
`0785baaf302245f7aa46de7cf4fc0e4c` also passed before the final lifecycle changes.
Test clients remain outside the candidate catalog.

Limits remain explicit in handlers/mouse/README.md: existing 4 KiB transport
frames, 32-bit interpreted fields, 64 button bits, unsupported long/alternative
HID items, vendor-specific features and Bluetooth. An interface exclusively owned
by the boot-keyboard provider is respected; shared keyboard/mouse collections on
that same interface need shared HID demultiplexing. Separate interfaces can run
concurrently. This milestone provides input; cursor drawing is separate. Physical
mouse passthrough and physical controller qualification remain pending. Native
long-duration acceptance and filesystem latency optimization remain deferred.

### Independent IO backends and filesystem format boundary

Split the former IO implementation into Limine-loaded `hid`, `display`,
`filesystem` and `applications` kmodules. IO routes copied requests through hub
surfaces, forwards cleanup and retains narrow clock/owner-watch gateways. Provider
binding and installation loading use hub calls; worker/state ownership moves to
the relevant backend. Existing public IO/resource contracts remain unchanged.

The filesystem manager discovers registered format-query surfaces, probes each
selected partition, rejects ambiguous matches and retains a backend per mount.
Dynamic mounts route by longest component prefix. Open streams retain mount IDs;
file workers call the common filesystem contract rather than ext4 directly.
Backends remain pinned until reboot; mount teardown refuses references/active
calls/children and preserves failed unmounts. Ext4 supplies the first real adapter.
Two-format host fixtures are test-only. Bootstrap still explicitly mounts its
known ext4 volume and adopts it as `/`; automatic mounting policy, additional
formats and shared keyboard/mouse HID demultiplexing remain future work.

Validation: candidate build passes; focused installation, HID transport, renderer,
stream and two-format mount-manager checks pass; legacy process probe compiles
against the extracted applications contract. Disposable four-CPU TCG terminal
run `2f1c2c66895c4db89c16a576ef86cb82` passes recreate/append redirection, pipelines,
file reads, protected writes, graphics/app execution and private data access.
Final extracted-module mouse/hotplug run
`4c4217af08aa4002a9845d7db22e0092` passes. Developer disks were not attached.

### Shared HID interface demultiplexing

HID now owns one physical claim, descriptor, report buffer and capture worker per
interface. Installed keyboard and mouse providers attach logical class consumers
with their own copied mailboxes and ACKs. Collection/report-ID routing can fan one
mixed report out to both consumers. All recipients are posted before waiting;
a slow consumer applies bounded physical-endpoint backpressure. Closing one class
preserves the other; final close cancels/drains and retains uncertain DMA ownership.
Lifecycle waits use wake credits and stack wait records; callback references keep
links alive during posting. Worker failure is latched so a pending notification
cannot hide it from the next ACK.

Keyboard now uses report protocol and descriptor-defined arrays/bitmaps, including
NKRO and state retained across report IDs, behind its unchanged /@keyboard handle.
Kernel routing and both userland codecs use common/hid-report.c; class-specific
interpretation remains in the handlers. Old private boot-keyboard IO operation
numbers are reserved, not reused. Keyboard still selects one interface at a time;
physical combined-receiver qualification is not claimed by host/QEMU tests.

Added the production startup graph to maps/startup.md, showing boot handoff,
kmodule initialization, asynchronous storage selection, provider binding and the
shared input path. This supersedes the earlier pending-demultiplexer notes.

Validation: candidate build; common parser/codecs, shared transport/lifecycle,
keyboard stream/failure, mouse descriptors, installations and C/C++ interfaces
pass. Fault-injection fixtures compile with explicit pre-start test-only PID
binding, without a production fixture-name bypass. Four-CPU disposable TCG runs
`c30e6d4a58b14268bd2830c0f44baf18` (mouse hotplug plus keyboard) and
`000efe50f5fc438fbc95da805c54e18a` (editing/completion/history and wake-driven
streams) pass. Combined-interface IDs and mixed reports use host transport
fixtures; native physical combined receivers remain unvalidated.

### Built-in process monitor

`monitor` is an ordinary installed system app. Manifest-authorized read-only
`/@applications.processes` returns bounded copied PID sweeps, including privileged
handlers. Kernel `process.sample` and manager-only IO `process_sample` bridge to
thread runtime accounting and committed user-page counters. Existing process
control permissions and operation numbers remain intact. Kernel worker threads
are outside this process-only view.

The monitor reports state, assigned CPU, per-core CPU%, committed USER KiB and
cumulative CPU time, with dynamic row storage, CPU sorting, display-derived
pagination and changed-row rendering through text -> graphics -> display.
Default refresh is 1 second; Escape quits and Up/Down change pages immediately. `-b`, `-n COUNT`
and `-d SECONDS` support stdout snapshots and terminal redirection. An invalid or
first CPU sample shows --. There is no kill control or invented whole-system
CPU/load figure. Live mode claims /@keyboard focus and reads device events
directly. Standard streams remain independent byte transports.

Validation: candidate build; C/C++ interface checks; production accounting and
CPU-percentage helpers (blocked time, regression, overflow); process record
pagination/memory fields/lifecycle changes; installation authority denial; broker
wait/reply capacity regression. Four-CPU disposable TCG guest
`5fffaa68c96045dd9a2d7d75082161fe` passes two batch snapshots, all provider rows,
live measured CPU refresh and subsequent pwd (initial version, before the
monitor rename and event-input controls). Developer persistent
disks were not attached. Native KVM monitoring acceptance remains unrun here.


### Direct keyboard ownership and named independent standard streams

Monitor acquires /@keyboard and claims focus directly. Keyboard focus uses
admission-authenticated identity, exclusive claim/release and owner-lifetime
watching. Other readers park; optional 1.1 read/batch focus generations notify
the terminal to stop editing/blinking and establish a barrier before resuming.
The existing 1.0 frame prefixes remain supported. Escape and arrow keys never
travel through stdin. Terminal resumes its cwd prompt after screen clearing.

Removed the temporary stream input_mode operation and its terminal forwarding.
Stdin/stdout/stderr are byte stream endpoints supplied by any launcher. Reserved
caller-relative /@stdin, /@stdout and /@stderr names acquire independently owned
copies of those bindings; no global stream is shared and apps cannot publish
those names. cb_standard wraps named acquisition. The numeric gate remains ABI
compatible. Headless timer-only waits accept a deadline without a stream.

Validation: candidate build, production keyboard stream/focus tests, C/C++ ABI,
stream ownership/alias tests and wait-any tests pass. Four-CPU disposable TCG
monitor acceptance `3d0b6c852a344a1cb17f0b8c0d612494` passes direct input, live
CPU data, arrows, immediate Escape, cleared prompt, pwd and subsequent cat input.
Headless acceptance `801e272a9c414b729c3305ee0a500418` passes binary stdin/stdout
through cat with no terminal, monitor without stdin, and automatic focus release
after a faulted owner followed by successful reclaim/release. Test apps remain
outside candidate media and developer persistent disks were not attached.

### Persistent binary identity storage

Added a continuously running privileged /@identity provider, started after
filesystem and before applications. Build-time provisioning creates protected
users.bin, groups.bin and credentials.bin under /system/config/identity, plus a
private transaction.bin replay journal. Root UID/GID 0 and the admin/users groups
(GIDs 1/2) are seeded; the root boot secret is random and only a salted derived
hash is persisted. No account provisioning or directory migration runs at boot.

Explicit little-endian headers/records have versions, lengths, CRCs, a shared
transaction generation and persistent next-ID high-water marks. Account/group
renames preserve IDs; deleted IDs cannot be reintroduced. Public users/groups
listing is chunked. Credentials are never listed: a management transaction may
replace/delete one derived hash while retaining all other records internally.
Immutable manifest grants authorize each operation; owner-exit notifications
release abandoned staging. Read-only media allows listing but rejects commits.

Commits persist a complete three-file journal before applying snapshots; recovery
replays committed snapshots and rejects incoherent/corrupt sets. Uncertain I/O
failures stop further access until recovery instead of retrying mutations.
Public filesystem access to the subtree is denied except to the catalog-bound
identity installation; the handler gains no write access elsewhere in /system.

Validation: candidate build; C/C++ identity ABI; provisioning/private modes;
production format/root/high-water validation; fault injection at all five write
steps and read-only behavior; installation identity/path authorization and
filesystem boundary checks. Four-CPU isolated TCG acceptance
792595344ee743d7bb7fefccce15e714 passes create/delete and credential mutations,
incomplete/reused-ID rejection, generation checks and denied direct reads.
Post-stop ext4 inspection confirms coherent generation 3, root-only remaining
users/credentials, UID high-water 2, three initial groups and an empty journal.
Fixtures and account mutations remain in a copied test image.

The subsequent identity execution milestone below closes password verification,
trusted process credentials, root/admin management and filesystem mode checks.
Explicit homes and system app-data provisioning are closed by the session/storage milestone below.

### Identity execution and administration

Kernel-owned process credentials contain UID, primary GID, a local generation and
dynamically allocated supplementary GIDs. Children inherit independent snapshots
before start. Only the catalog-bound identity provider may replace a verified
caller through IO; no ordinary setter or payload admin flag exists. Boot remains
root. Account/group edits govern later authentication, not existing snapshots.

Identity management checks root/admin credentials on each dispatch in addition
to immutable manifest grants; root credential updates require UID 0. PASSWORD
and AUTHENTICATE keep hashes private, accept 1..64 Unicode scalars and derive
PBKDF2-HMAC-SHA256 with checked hardware salt entropy. Failed verification has a
one-second global admission delay. No secure-attention UI or crypto audit is
claimed. SHA-256 provenance/license and KDF host vectors are retained.

The filesystem manager enforces traversal, owner/group/other modes and sticky
unlink; caller-owned creates use 0600/0700 and explicit ownership. File stream
operations recheck the actual caller. Per-mount sleeping serialization keeps the
check and operation together without holding CPU locks over I/O. Installed
executables also require traversal/execute access. Format-neutral backend/file
contracts are version 2; the old ext4 stat prefix remains capacity-compatible.

The ordinary installed accounts app supplies listings, user/group edits,
non-echoed password changes and authenticated child execution. Root terminal
identity stays unchanged. The session/storage milestone below adds nested interactive login and explicit
homes. User installation policy, ACL/setuid and live group revocation remain open.

Validation: focused credential/IO-authority/permission tests, password derivation
against hashlib, storage/recovery and C/C++/installation/backend checks. Four-CPU
TCG identity acceptance 7da655ca0a734896bead951d7f2a6221 passes authentication,
child inheritance, non-admin/wrong-current-password denial and old-stream access loss; post-stop ext4
inspection verifies generation 4 and UID/mode metadata. Four-CPU accounts acceptance
f539047405654914bd8657e7f7bda9f5 passes actual user/group commands, secret entry,
authenticated child identity and supplementary-admin management. These are isolated
copied images, not physical or native KVM acceptance.

### Separate boot filesystem and system kmodules

Retired ext4 /boot as a completion tree. Generated ext4 contains an empty /boot
mountpoint and /system/kmodules/{drivers,helpers}; driver selection, installation
fixtures and tests use the new system directory without aliases or boot migrations.
Foundational modules remain Limine-loaded. ISO staging now places kernel/config,
kmodules, limine and EFI at the filesystem root, matching mounted /boot paths.
The packaged keyboard default is ABNT2; IBM remains bundled/selectable.

Added the boot-loaded iso9660-fs helper behind the same ABI2 filesystem backend
contract as ext4. It reads primary/Joliet directories and files, validates metadata
and extent bounds, retains parent descriptors, and rejects mutations. The builder
bounds ISO volume space before appended ext4; boot reads cannot expose identity
or application data. Mount-path traversal also checks ancestors across mounts.

Bootstrap mounts the ISO9660 filesystem at /boot only with a verified matching
Limine parent-disk GUID. Unknown/different origin leaves an empty mountpoint and
a diagnostic. Independent boot/system disks, a FAT backend, Rock Ridge symlink
semantics, multi-extent/interleaved files and writes remain unsupported. The FAT
EFI partition still starts Limine; it is not the filesystem mounted by this backend.

Validation: real-image UBSan backend tests cover 512/4096-byte sectors, nested and
UTF-8 Joliet lookup, listing/EOF, malformed metadata/extents, OOM, I/O failure,
read-only behavior and retained parent release. Namespace, installation, keyboard,
credential/mount-ancestor checks and host GPT/ext4 image verification pass.
Four-CPU TCG guest 085b6015a700463699f988fed305d6c9 passes public /boot reads,
foundational module listing, new system module directory, default ABNT2, protected
boot writes and subsequent terminal input. Reports remain; disposable VM media
was removed automatically. Native KVM acceptance is not claimed for this change.

### Ownership, interactive sessions and persistent development media

1. Closed ownership/home administration: public metadata/chmod/chown preserve old
   STAT ABI and use backend metadata operations. Owner/admin policy intersects
   manifests, ancestor permissions and protected namespaces. `fsctl` is an ordinary
   catalog app. `accounts home USER` explicitly creates 0700 UID/GID-owned homes,
   standard folders, system app data and a catalog-derived index. Root remains
   /system/root. Homes are separate from account transactions; failures may leave
   a private staging tree, and unrelated directories are never taken over.
2. Closed nested interactive sessions: privileged /@sessions owns dynamic records;
   trusted process mechanics inherit membership/ancestry and reject closed-session
   launches. `accounts login USER [APP [ARGS]]` authenticates before session creation,
   defaults to terminal at the existing home, and logs out on child completion.
   Keyboard-owned focus nests by verified ancestry and restores on release/death.
   Mouse is independent inside the foreground session and blocks cross-session
   reads. Standard streams remain independent directional bytes. Cooperative stop
   preserves running contexts; hostile/unresponsive-app forced kill remains open.
3. Closed persistent development launch: init-dev-disk atomically clones the built
   candidate once and refuses an existing destination. run-dev launches that image
   as-is, without build/repair/copy work. Candidate and developer NVMe/SATA images
   are preserved. All provisioning remains explicit or image-build-time.

Validation: candidate build and focused C/C++ contracts, installation/namespace,
identity storage/recovery, credential/session/IO authority, filesystem permission,
format backend, keyboard nested focus and ISO9660 tests. Isolated four-CPU TCG
reports d7b3ca3d211449e486e6da5911e6b277 and c414aab2930840eaac75af396f1a87b9
verify accounts/passwords/home/files/mode bits across two boots, unauthorized
ownership/root-data access, nested terminal → monitor → terminal focus, logout
and restored root identity. Successful test media are automatically removed;
reports/logs remain under bin/tests. Initializer refusal/preservation is host-tested.
Native KVM/physical acceptance is not claimed by these checks.

Final candidate acceptance: four-CPU TCG accounts report
31143300412d40a3ae62880c8f9f6679 passes supplementary-admin account management and
authenticated execution. Rebuilt-candidate interactive report
2176b5f3fc6c491fbea3c27a51cd5fe7 passes nested terminal, ownership denial, monitor
handoff and root restoration after the closed-session input fix. Focus tests also
cover revoking a pending read from a closed owner and restoring its live parent.
