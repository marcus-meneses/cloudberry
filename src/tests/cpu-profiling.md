# Application CPU regression comparison

Run on the development KVM host:

```sh
./scripts/bench-cpu-kvm.sh --runs 3
```

The default reference is commit `d11e0b2`, before installation maintenance.
`--reference-ref COMMIT` selects another baseline; `--cpus N` defaults to four.
Topology is one socket with N cores and one thread per core, matching `make run`.
The runner exports that revision into an isolated directory, builds it with the
same local toolchain, builds the current candidate, and alternates serial boots
of disposable copies. It never attaches development NVMe/SATA disks or changes
KVM permissions. KVM access is checked before building. `--accel tcg --runs 1`
is a separate diagnostic, not a substitute for native acceptance.

Reports under `bin/tests/cpu-comparison/<session>/result.json` include source
identity, host/QEMU identity, configuration, per-run guest monitor records and
host QEMU CPU seconds/wall seconds for:

- A four-second idle interval, without screenshot polling during measurement.
- Batch monitor samples redirected to a regular file outside protected `/system`.
  This phase deliberately includes observer and filesystem/journal overhead;
  it is not an idle measurement.
- Repeated `pwd`, `ls /system/apps` and `help` commands. This includes input
  injection and completion checks; raw elapsed/CPU seconds are retained.
- A four-second live monitor interval, without screenshot polling inside it.

Guest percentages use one logical CPU as 100%, independent of CPU count. Host
percentages likewise use one host CPU as 100%, and can exceed 100% across vCPUs.
The batch monitor's first sample is excluded from percentage summaries. Native
clock source is sampled through read-only GDB diagnostics outside measured
intervals. A source change during a run or different source modes across boots
rejects a combined comparison. Candidate production-source changes also reject it.
No GDB command calls into guest code. Reports retain clock and lock diagnostics.

A JSON result is directly comparable and contains the sampled records; successful
VM media and the reference ISO are removed, while reports and compilation caches
remain. Keep unrelated VMs/builds stopped during measurement. Three runs provide
an initial comparison, not a statistical guarantee; use raw runs to assess spread.

## Local diagnostic, 2026-10-22

One serial four-CPU TCG pair passed in
`cpu-comparison/20261022T031946.896324Z`. Host CPU percentages:

| Phase | d11e0b2 | Candidate |
| --- | ---: | ---: |
| Idle | 2.0 | 1.5 |
| Batch monitor to file | 73.6 | 69.1 |
| Terminal workload | 50.0 | 51.5 |
| Live monitor | 21.5 | 12.2 |

Installer and other sleeping providers round to 0.0% in quiet guest samples.
These data do not reproduce an across-the-board increase; one pair and TCG
variation cannot establish native performance equivalence or an improvement.
This initial pair predates automatic clock diagnostics in the runner; its clock
mode was not independently captured. Native KVM was pending at that point;
the later user-host acceptance below closes that comparison. This agent
environment does not expose `/dev/kvm`. No scheduler, accounting formula,
polling frequency or rendering optimization was changed to produce these numbers.


A final four-CPU CPU/clock diagnostic passed in
`console/5bf362b496f548f8b72040074fb4852b`: HPET source 2, flags 177 and two
calibrations were unchanged across measurement, with SMP TSC inactive throughout.
The initial TCG runs used the console runner's automatic QEMU topology; the
reusable CPU comparison now explicitly matches the production one-socket layout.


## Native KVM comparison, 2026-10-22

`cpu-comparison/20261022T035706.000427Z` passed three alternating runs per
revision (six boots), on AMD Ryzen 5 5500U, four guest cores in one socket.
The reference is immutable commit d11e0b2; the candidate is the uncommitted
working tree, identified by production-source SHA-256
`d5a25324d74f69d42143a0a5b80630e2aa6507da96cf438f9f34a39740812c59`.
Both git revision labels are d11e0b2 because the candidate changes are not yet
committed; the reference was exported from Git and the candidate was built from
the working tree. The candidate fingerprint stayed unchanged during and after the run.

| Host CPU, one core = 100% | Reference median | Candidate median |
| --- | ---: | ---: |
| Idle | 0.00% | 0.00% |
| Batch monitor to file | 58.17% | 58.84% |
| Terminal workload | 40.06% | 36.82% |
| Live monitor | 7.74% | 9.24% |

Zero idle CPU seconds means no CPU tick was observed in that four-second window,
not a claim of literally zero execution. Quiet guest installer samples round to
0.0% in all runs; other sleeping providers also round to 0.0%. Batch monitor's own
reported guest CPU median is 3.05% before and 2.55% after. Live monitor host usage
ranges overlap: reference 7.50–8.99%, candidate 6.99–9.99%. Its median increased
1.50 percentage points, which is a possible observer/rendering cost worth tracking,
not evidence of an across-the-board CPU regression or proof of equivalence.
The three runs do not establish statistical significance.

All boots used stable HPET source 2, flags 187, with SMP TSC inactive. TSC was
frequency-calibrated but was not the active shared timestamp source; this condition
was the same in both revisions. No source changed during measured runs. Successful
VM media was cleaned; reports, raw samples and clock diagnostics remain available.
This closes native reproduction for the tested fresh-image workloads. It does not
reproduce every persistent installed environment or qualify physical hardware.
No speculative scheduler/accounting/rendering optimization was made.

The later installer startup-publication ordering fix is outside this measured
source fingerprint. These results remain evidence for the recorded workload and
source, rather than performance qualification of every subsequent revision.
