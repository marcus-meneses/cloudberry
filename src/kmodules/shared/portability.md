# Intel and AMD platform contracts

All current code targets x86-64; Intel and AMD are vendors within that architecture.
Kmodules remain trusted, tightly coupled C methods in one kernel address space.
They are not the future application-facing handlers.

Sources are segregated into `shared/`, `intel/` and `amd/`. Shared hardware
standards stay shared: PCIe/NVMe, ACPI, APIC/x2APIC register interfaces, HPET,
XSAVE/FXSAVE and generic memory/scheduling mechanisms. `apic.c` owns controller
policy and public surfaces; `apic-x86.c` implements its common x86 register adapter.
The filenames describe two layers, not two independently loaded modules.

The ISO supplies both CPU and IOMMU alternatives. Limine delivers these as inert
images; the kernel maps/publishes only the selected implementation. Unselected
images remain bootloader-reserved data, not active kmodules. Selection is fixed
for this boot; runtime replacement/unload is not implemented.

| Image | Selection | Shared frontend surfaces |
| --- | --- | --- |
| `cpu-intel.kmod` / `cpu-amd.kmod` | Exact CPUID vendor string, after early fault handling | `cpu.init`, `cpu.id`, `cpu.ticks`, `cpu.ipi_fence`, `cpu.features`, `cpu.tsc` |
| `iommu-intel.kmod` / `iommu-amd.kmod` | DMAR / IVRS in hwenum ACPI inventory | `iommu.backend.init`, `discover`, `route`, `start`, `context`, `invalidate`, `collect` |
| `iommu.kmod` | Always shared; no translation backend without firmware | Existing `iommu.*` ownership/mapping contract, plus `iommu.platform` |
| `xstate.kmod` | Always shared; CPU capabilities select instruction format | `xstate.cpu_init`, `info`, `init`, `save`, `restore` |

Alternative images retain identical image contract names (`cpu-backend` and
`iommu-backend`) and versioned frontend exports. A missing selected image,
duplicate selected alternative tag, unknown CPU vendor, or conflicting DMAR/IVRS selection
fails startup. CPU and IOMMU choices are independent: Intel CPU + AMD-Vi is a
valid emulator configuration. Hardware firmware parsers reject unsupported layouts.

CPU identification prefers valid CPUID 1F/B topology leaves, then AMD's gated
8000001E extended APIC ID, then the eight-bit legacy ID. AMD timestamp reads use
capability-selected ordering: LFENCE/RDTSC/LFENCE when CPUID 0x80000021 EAX[2]
guarantees dispatch serialization; otherwise MFENCE/RDTSCP/CPUID when RDTSCP is
advertised, with CPUID/RDTSC/CPUID as the conservative fallback. The selected
mode is immutable and validated at AP attachment. No optional DE_CFG MSR is
modified. Intel uses its documented LFENCE ordering.
All x2APIC wake/shootdown notifications fence prior stores before WRMSR; the common
fence uses MFENCE plus CPUID, valid for both vendors. These methods allocate
nothing and take no kernel locks. CPU attach validates IDs on each processor.

IOMMU backend initialization receives a versioned, trusted C helper descriptor
for frontend-owned state, allocation, bounded waits, publication and fault reporting.
Vendor firmware parsing, requester routing, PTE policy and fault decoding reside
in the selected backend; the frontend owns lifecycle and pinning. Backend calls are
made only by the serialized frontend. There are no cross-image static relocations
or duplicated copies of the frontend's ownership state. Intel selects supported
39/48-bit legacy translation geometry; AMD uses four-level 48-bit v1 translation.
AMD preserves firmware control fields and enables coherent device-table fetches;
Intel explicitly flushes tables when hardware does not snoop them.

First activation disables endpoint bus mastering, verifies readback and waits
boundedly for PCIe Transactions Pending to clear. This is not a generic engine
reset or proof of drained posted writes. NVMe must reset and observe RDY=0 before
DMA attachment; uncertain reset/invalidation retains storage. Whole-platform
firmware DMA takeover remains unverified on physical machines and is explicitly
reported as such. Fault-log overflow faults translation health, rather than
silently losing evidence. Intel bridge paths/hierarchy scopes are resolved using
enumerated bridge configuration; RMRR-owned endpoints remain refused. AMD aliases,
IVMD reserved-memory policy, interrupt remapping and firmware-active engines
remain unsupported; there is no permissive fallback when firmware is present.

`iommu.platform` reports firmware support, active minimum IOVA width and takeover
policy separately, preserving the existing `iommu.info` structure/ABI. Production
startup only caches dependencies: it never opens the persistent storage images.

Validation: `make -C src test-portability` uses isolated disposable media for
four-CPU Intel/VT-d and AMD/AMD-Vi, mixed CPU/IOMMU selection, XSAVE without AVX,
and FXSAVE fallback. Tests check actual DMA revocation, fresh extended state,
x87/SSE/AVX/control-word preservation under yield/timer preemption on every CPU,
full-ID topology fixtures and AMD coherence control. Native `test-native` saves
host CPU metadata plus negotiated guest capabilities; it requires accessible KVM
and never silently falls back. KVM emulated IOMMUs are not physical DMA validation.
AMD KVM/x2APIC/guest AVX acceptance passed on Ryzen 5 5500U. Native Intel CPU,
physical Intel/AMD devices and HPET-less SMP remain outstanding. Both native
IOMMU device models now pass on the AMD KVM host.

References: [AMD architecture manual](https://docs.amd.com/api/khub/documents/sD1_QL~h4Afq2_tvzxqqSQ/content),
[Intel SDM](https://cdrdv2-public.intel.com/835781/325462-sdm-vol-1-2abcd-3abcd-4.pdf),
[AMD IOMMU](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/specifications/48882_IOMMU.pdf),
[Intel VT-d](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/vt-directed-io-spec.pdf).

AVX validation limit: this environment's QEMU 6.2 lacks AVX TCG emulation (added
in [QEMU 7.2](https://qemu-project.gitlab.io/qemu-web/2022/12/14/qemu-7-2-0/)).
The production save/restore path passes a native host x87/SSE/AVX round-trip test,
and an earlier report records guest AVX timer-preemption on four user-host KVM CPUs.
Reports explicitly note unavailable guest AVX; capability branches are not counted
as executed AVX tests. `test-native X2APIC=1` asks Limine to prefer x2APIC when
supported; saved APIC mode determines whether that path was actually exercised.
The default X2APIC=0 retains firmware/bootloader xAPIC mode.

Current status (2026-10-15): both four-CPU KVM runs pass on Ryzen 5 5500U,
with matching report/result run IDs, error-free x87/SSE/AVX preemption, NVMe and
emulated Intel VT-d/AMD-Vi DMA revocation. Both report x2APIC mode 2 and active,
qualified shared TSC. Earlier xAPIC/HPET-only reports describe prior revisions.
Native Intel CPU and physical Intel/AMD devices remain unqualified.

## TSC discovery contract

Both CPU backend images now export `cpu.tsc(out cpu_tsc_info)` alongside ordered
`cpu.ticks`. Capability decoding is shared because CPUID 0x80000007 EDX[8] has the
same invariant-TSC meaning on Intel and AMD. Optional CPUID 0x15 frequency requires
all ratio/crystal fields; unsupported/incomplete enumeration falls back to HPET
calibration. No Intel model-frequency lookup, AMD core-frequency assumption or
optional firmware/MSR write is needed. Vendor-specific serialization remains in
cpu-intel.c/cpu-amd.c; clock source selection and validation stay shared.

KVM defaults now use `host,migratable=off,+invtsc`, with explicit CPU override
available. Missing guest invariant-TSC support is diagnosed separately from
calibration jitter, frequency disagreement, cross-CPU offset/drift/uncertainty and
runtime regression. Latest four-CPU AMD KVM reports (2026-10-15) qualify and
activate shared TSC at about 2.0963 GHz with both emulated IOMMU models.
TSC-only exchanges bound cross-CPU offsets; uncertainty-aware frequency filtering
and bounded replacement batches retain the existing safety limits. This validates
the AMD CPU backend under KVM, not native Intel CPU or physical hardware.
