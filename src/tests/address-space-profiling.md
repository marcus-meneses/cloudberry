# Address-space profiling

The isolated address-space test measures the existing implementation. All probes,
fault fixups, measurement loops and report processing are test-only. Production
root switching, scheduling, TLB policy and allocation are unchanged.

## KVM collection and automatic comparison

From the project root, run:

```sh
./scripts/bench-spaces-kvm.sh
# Optional independent repetitions; the default already samples extensively:
./scripts/bench-spaces-kvm.sh --runs 3
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
seven host checks exercise those failure paths and preservation of noisy data.
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
