# Isolated handle profiling and concurrency acceptance

The candidate broker has no profiler surface, trace state or clock probes.
`--profile-handles` replaces only the disposable image's broker with a separately
compiled variant. Its preallocated report supports eight selected actors and
one to four CPUs. These are test capacities, not OS resource limits. The trusted
owner selects existing processes, starts them, waits for their calls to quiesce
and ends collection. Each process currently has one user thread; its call-local
trace context remains on that thread's kernel stack across blocking.

Run the focused native diagnostic on the user host:

```sh
./scripts/bench-handles-kvm.sh --profile-handles --case 4cpu-amd --runs 3
```

Run the ordinary native comparison separately:

```sh
./scripts/bench-handles-kvm.sh --runs 3 \
  --reference-session bin/tests/kvm-handles/20261018T063747.870742Z
```

Both commands save raw guest logs, parsed reports, source/binary/host identities
and archives under `bin/tests/kvm-handles`. Reports never influence guest builds.
Profiled and unprofiled sessions cannot be used as performance references for
one another. Compare them deliberately to estimate total probe perturbation,
not to assert a production speedup. Native acceptance still requires qualified,
stable TSC; TCG validates collection/correctness only.

## Measurement boundaries

`handles.phases` in each parsed report contains:

- `gates`: counts, accumulated elapsed time and maximum per actor/gate operation.
  Check time covers gate range validation and owner lookup. Capture includes the
  temporary input allocation/copy. Broker time excludes measured blocking and
  kernel-resource execution. Output includes copyout and temporary cleanup.
- `handoff`: aggregate queue-to-delivery, delivery-to-provider-reply and
  reply-to-client-resume elapsed times for completed userland requests. Provider
  service includes scheduling and gate work, not just application CPU time.
- `locks`: broker-local explicit lock count, wait/hold totals and maxima per CPU.
  These exclude process mapping/teardown locks and are collected during the
  benchmark window; broker entry/exit cleanup can still contribute.
- `heap`: allocations, frees, failures, ending live broker blocks and invalid
  clock spans. Counts include broker activity in the selected window, including
  actors' exit cleanup; allocations before enable explain differing free counts.
- `overhead`: 32 empty mediated clock-pair samples. This is a lower bound on
  instrumentation overhead, not a correction to subtract from every phase.

The first provider receive may already be blocked before collection starts, so
its gate count can be one below the completed reply/handoff count. Nested timing
boundaries and store overhead mean phase sums need not equal total gate time.
Rows retain the measured sums; no negative spans or allocation failures are
accepted. Ordinary benchmark endpoint timings still include handle-based clock
reads, validation and completed-request release.

## Evidence and bounded optimization

The first one-CPU TCG instrumented run, `8480710a6440476fa31223a90f74f2a9`,
recorded 5,782 allocations for the existing benchmark, including 1,288 invocations.
Every invocation separately allocated a request record and its immutable frame.
Coallocating those two objects removes one allocation/free pair per invocation.
The after run, `fb8ca01f3edb4cfda4fcc5d4ff352c02`, records 4,494 allocations:
exactly 1,288 fewer (22.3%). Both end with six live broker blocks, no allocation
failures and no invalid clock samples. Raw local evidence is retained under
`bin/tests/handle-phase-before` and `bin/tests/handle-phase-after`.
This proves reduced allocation work, not a KVM latency improvement. No global
lock redesign, validation removal or automatic retry was inferred from it.

## Simultaneous clients and resource stability

`test-process-boot` now runs two complete cycles of a separate protected fixture:
three clients each exchange twelve distinct 4 KiB payloads through one provider.
The provider adds delays while all clients retain independent request/capability
ownership. It then holds one dispatched operation long enough to exercise:

- A dispatched timeout with UNKNOWN outcome, preserved original payload and BUSY
  request release while work is outstanding.
- A queued timeout with NOT_DISPATCHED outcome; that payload must never reach the
  provider and its record can be released immediately.
- Unrelated memory allocation/free progress before the provider completes.
- Retrieval of the late committed result using the original request ID and next
  attempt, even after provider exit; no duplicate side effect or automatic retry.

After each cycle, discovery must contain no retired provider. The instrumented
variant asserts exact broker live-block restoration after each cycle. All
variants compare live page/stack counts after a warmup cycle to exclude retained
allocator metadata capacity. The existing USB/provider-fault tests also remain;
these use active transfers and pending reads, then explicit reap and fresh
provider identities. Test apps, manifests and instrumentation never enter the
candidate image or the developer's persistent disks.


Final instrumented four-CPU AMD-Vi TCG run
`3cc92842893e466e8f685130207fb2dc` also records 4,494 allocations and six ending
live blocks, with no failures or invalid spans. The complete profiled collector
passes session `20261018T152345.941848Z`; its source/binary fingerprints and raw
archive exercise the same collection path intended for KVM. Ordinary candidate
rebuild and staging/export checks confirm that no tracing surface or fixture
application is packaged there.
