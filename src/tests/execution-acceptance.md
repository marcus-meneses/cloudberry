# Execution acceptance

Run the current migration, termination and console/input checks on an accessible
KVM host:

```sh
./scripts/check-execution-kvm.sh
```

This is functional acceptance, separate from handle/clock performance profiling.
HPET fallback or insufficient timestamp samples do not invalidate functional
success. KVM is required by default; there is no silent TCG fallback.
The same harness can be checked without KVM with `--accel tcg`; that result is
labelled TCG and does not close native acceptance.

One isolated build under `bin/tests/exec-<id>` runs; its location is recorded in
`bin/tests/execution-acceptance/<timestamp>/session.json`. Short build paths keep
Unix debug socket names within the host's limit. The checks are:

1. The one-CPU process suite (zero migration).
2. The four-CPU process suite (actual migration with memory/SIMD guards), including
   owner-authorized termination, blocked I/O and dispatched-caller cleanup.
3. The normal four-CPU console, including pipelines/redirection and input.
4. Focused delayed-output/stdin/EOF/pipeline/idle-wake checks on four CPUs.

It records source fingerprint, revision, host CPU/kernel/affinity, exact commands,
reports and logs. The source must remain fixed during the run. No IOMMU matrix or
benchmark repetition is required for this focused acceptance. Test media is
disposable; developer disks are never attached. Successful process media is
cleaned after each run; console harness cleans its successful media. Build output
and failure media remain available, along with reports and snapshots.

The process runner now takes a GDB scheduler/broker/HID snapshot before terminating
a failed live VM. Both console invocations enable existing failure diagnostics.
Install `gdb-multiarch` on the host to obtain snapshots; a diagnostic error is
recorded separately and cannot make the original failure pass. The Unix GDB/QMP
sockets are local to each test directory, with no TCP debug port.

The first input timeout from the previous milestone remains unexplained. Passing
later runs does not retrospectively identify its cause. If it recurs, use the
preserved first-failure state before changing keyboard or scheduler behavior.

This environment has no `/dev/kvm`. TCG validation here does not claim native or
physical-hardware acceptance. Review `session.json` and its referenced results
after the host run; do not substitute a previous source fingerprint's acceptance.

User-host acceptance: `20261022T163746.733190Z/session.json` passes on AMD Ryzen
5 5500U with explicit KVM and matching workspace source fingerprint. Both process
suites pass (zero/three migrations for one/four CPUs), as do console and input-wake.
TSC is active around 2.0964 GHz. This is functional acceptance of that configuration;
it does not establish performance bounds or physical-device/Intel-host coverage.
