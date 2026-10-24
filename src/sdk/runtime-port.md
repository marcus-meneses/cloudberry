# Runtime foundation contract and JavaScript port

Status: runtime foundations implemented 2026-10-22; see [the native SDK](README.md).
The optional static SDK supplies handle-backed allocation, target Newlib libc/libm,
startup and reusable C adapters. The ordinary [js application](../apps/js/README.md)
now uses this SDK; engine acceptance precedes its packaged integration tests. The existing
execution foundation and these new SDK paths passed AMD KVM acceptance, alongside
focused host/target-link/TCG evidence. The SDK report is
`81d6f8ba5c2141c9919b0af94846ca7f`, with a matching probe digest and Newlib identity.
Physical hardware and native Intel CPU acceptance remain separate.

## Reference profile

Audit target: upstream **QuickJS 2026-06-04**, a statically linked, single-user-thread
interpreter with stack checking retained, no JIT, no upstream `qjs`/REPL,
`quickjs-libc`, native dynamic modules, Workers, OS Atomics or
Node compatibility. This reference now cross-compiles for Cloudberry; the runner
uses its own stdin/stdout/handle adapters, not upstream OS shims.

Sources: [official release](https://bellard.org/quickjs/) and
[embedding API](https://bellard.org/quickjs/quickjs.html).
Archive: `https://bellard.org/quickjs/quickjs-2026-06-04.tar.xz`.
SHA-256: `b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a`.
Core units: quickjs, dtoa, libregexp, libunicode, cutils. The audit removes only
the upstream CONFIG_ATOMICS definition from its disposable source copy. Defining
`__EMSCRIPTEN__` to suppress Atomics would also disable stack checks and is not an
acceptable substitute. Existing kernel stack guards remain necessary. Upstream
SharedArrayBuffer objects remain interpreter-local; they do not expose Cloudberry
shared memory or cross-process transport.

Reproduce the dependency inventory (explicit download; nothing enters boot):

```sh
python3 src/tests/runtime-audit.py --download
# Offline repeat against the same verified archive:
python3 src/tests/runtime-audit.py --archive bin/tests/runtime-audit/quickjs-2026-06-04.tar.xz
```

The report records source hash, compiler/flags and unresolved symbols from a
relocatable whole-core link using host headers. It separately probes target C
headers. Host objects are dependency evidence only; they are never linked into
the guest. Some whole-core dependencies can disappear with custom hooks/dead-code
elimination. A successful audit does not satisfy the target-link/guest gate.

## Foundation status

| Requirement | Implementation and validation boundary |
| --- | --- |
| Native execution | Existing static ELF, guarded stacks, isolated address spaces, preemption and safe user-boundary migration. AMD KVM execution acceptance passed before this SDK; new SDK acceptance is a separate focused command. |
| Allocation | `heap.h/.c`: shared arenas, aligned allocation, calloc, realloc, usable size, empty-backing return and explicit ownership on backend failure. Public/reentrant libc allocation uses `/@memory`; resize operation 3 stays reserved. Host tests inject OOM and exercise randomized fragmentation, alignment and preservation of usable capacity. |
| C headers/library | Pinned Newlib 4.5.0.20241231 target libc, custom reentrant handle adapters, startup and assertion termination. SDK sysroot is under bin/sdk, never installed into host/compiler defaults. All 55 audit names resolve in a target static link. This is support for this audit profile, not full POSIX or every Newlib API. |
| Floating point | Target Newlib libm plus target libgcc; SSE2-capable user compilation. Guest checks exercise transcendentals, NaN/infinity/signed zero, explicit rounding and formatting. Kernel/ISR flags remain separate. Exhaustive ULP/libc conformance is not inferred. |
| Stack safety | Existing guarded process stacks and explicit installation stack sizes; the isolated C probe uses 512 KiB. The js app configures a 128 KiB engine limit within a 512 KiB process stack; the isolated engine probe verifies catchable recursion overflow. |
| Monotonic timers | `cb_runtime_now/delay/timer/cancel_timer/poll`, using existing IO now_ns and system wait-any/deadlines. Timer IDs are owned/nonreused, dispatch is budgeted, and due callbacks coexist with ready I/O. No recurring idle polling or new kernel event loop. |
| Wall time | Appended public UTC snapshot, explicit boot RTC-as-UTC policy, Gregorian conversion, availability/resolution/quality flags and gettimeofday bridge. Missing/false policy or missing RTC returns UNAVAILABLE. Wall time is never fabricated from uptime. No system timezone database; libc defaults to UTC without app TZ configuration. |
| Randomness | Existing IO hardware randomness is now within the ordinary manifest-authorized self-scope ceiling. Multi-chunk runtime adapter wipes output on failure; no timestamp fallback. A restricted guest app proves denied access still fails. |
| Streams/files | Checked partial read/write/full-write helpers, canonical counted binary load, trailing NUL outside byte count, and sequential C stdio read/recreate/append. Native probes exercise binary data, EOF, nonblocking readiness, append and ESPIPE. Richer public file APIs remain separate. |
| Program execution | Runtime launch/child-control/release adapters use the installed application provider; explicit child stream bindings, cwd and owned-child status. Guest probe validates asynchronous completion, ownership denial and an abandoned wait without canceling child execution. |
| Async completion | Owned SDK SUBMIT/COLLECT/WAIT/ABANDON remain the transport. The runtime exposes submit/collect plus timer/readiness polling; kernel streams retain blocking/nonblocking readiness semantics. The js application adds budgeted Promise job draining, readiness/deadline completion and canonical source-module caching. |
| Errors/authority | Runtime retains the copied failure frame, request ID/outcome, original payload and trace until the next failure. No automatic UNKNOWN retry, provider rebind, lossy ID conversion or privilege bypass. The js binding preserves lossless BigInt identifiers, frame/payload/trace bytes and ordinary JavaScript stacks in HandleError. |
| Resource lifetime | Context destruction cancels local timers, refuses live heap/internal requests and releases cached handles. Callers own returned streams/heap buffers/children and external async clients. Process exit uses existing kernel draining/reclamation. Guest probe exercises context destruction and ownership checks. |

## Gate and engine acceptance

1. Existing execution acceptance: passed on AMD KVM, session
   `20261022T163746.733190Z`.
2. User allocator, target C/math headers/libraries and static audit link: implemented;
   focused host and target-link checks pass. Newlib licenses/provenance accompany the SDK.
3. Time and reusable C handle adapters: implemented and exercised by isolated native
   C executables under one/four-CPU TCG. Four-CPU AMD KVM acceptance passes in
   `81d6f8ba5c2141c9919b0af94846ca7f`; this closes the native SDK foundation gate
   for the tested configuration.
4. Pinned QuickJS core cross-compile and isolated one-CPU TCG engine acceptance
   passed in `fcc543a0dd024119a830afd433f95d47`: custom allocation, recursion limits,
   exceptions, Unicode, math, Promise jobs and injected OOM teardown. The later
   UTC-failure adapter/binary constructor repeat passed four-CPU TCG in
   `a7d051a7f39948ceb8d9ae89efcc66cf`; the initial packaged runner passed one-CPU
   TCG in `118392dab0bf48acbc21a1fcb4d60f2d`; final four-CPU runner acceptance
   (including REPL/child shared stdin) passes in
   `1cb638e04afa4b84ae45c18c54e43945` with the candidate binary digest.
   User-host four-CPU KVM packaged acceptance also passes in
   `aef558a8fb5945ee8a89b42b58bf31f2`: saved invocation, candidate binary, script
   digests and QuickJS/Newlib identities were verified. The isolated engine's
   injected-OOM/recursion tests retain their separate host-UBSan/TCG evidence;
   this KVM run qualifies the packaged runner paths on the tested host.

General user threads/TLS/synchronization, dynamic linking, JIT page protection,
networking and Node/libuv compatibility remain subsequent work. They are not
requirements of this explicitly single-threaded interpreter profile. A later
V8/Node audit must examine its own version/configuration.

## Recorded audit evidence

The pinned whole-core host inventory produced 55 external symbols, including
malloc/realloc/free/usable-size, memory/string routines, snprintf/vsnprintf/fprintf,
assert/abort, gettimeofday/localtime_r, libm and compiler 128-bit division helpers.
The original compiler-default header probe failed at `stdlib.h`; the optional SDK
now supplies those headers through explicit build flags. Target libgcc resolves
the compiler helpers in test-sdk-link. Formatting
is used by actual engine error paths, so dropping only debug dumps cannot remove
the whole formatting requirement. This inventory is conservative and is not a
substitute for the target adapter's own dependency inventory.

Evidence: `bin/tests/runtime-audit/report.json`, `target-header-probe.log` and
`bin/tests/sdk-link/report.json`. The audit records whether it used compiler-default
or optional-SDK headers. Target link evidence is separate from host source inventory;
the isolated engine acceptance above is separate execution evidence. See the
[js contract](../apps/js/README.md) for packaged bindings and current limitations.
