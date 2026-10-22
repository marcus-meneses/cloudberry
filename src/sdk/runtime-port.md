# Runtime foundation contract and JavaScript gate

Status: audited 2026-10-22. No JavaScript interpreter, shell, engine library or
new provider is installed by this milestone. Priority 3 stays deferred until the
mandatory foundations below have working implementations and validation evidence.
Native execution acceptance passed on the user AMD KVM host with matching source
fingerprint (session `20261022T163746.733190Z`); other mandatory foundations remain
open. Physical hardware and native Intel CPU acceptance are separate.

## Reference profile

Audit target: upstream **QuickJS 2026-06-04**, a statically linked, single-user-thread
interpreter with stack checking retained, no JIT, no upstream `qjs`/REPL,
`quickjs-libc`, native dynamic modules, Workers, SharedArrayBuffer/OS Atomics or
Node compatibility. This is the foundation-audit reference, not an installed shell
or a claim that QuickJS already cross-compiles for Cloudberry.

Sources: [official release](https://bellard.org/quickjs/) and
[embedding API](https://bellard.org/quickjs/quickjs.html).
Archive: `https://bellard.org/quickjs/quickjs-2026-06-04.tar.xz`.
SHA-256: `b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a`.
Core units: quickjs, dtoa, libregexp, libunicode, cutils. The audit removes only
the upstream CONFIG_ATOMICS definition from its disposable source copy. Defining
`__EMSCRIPTEN__` to suppress Atomics would also disable stack checks and is not an
acceptable substitute. Existing kernel stack guards remain necessary.

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

## Existing facilities and required work

| Requirement | Current source evidence | Required foundation / acceptance |
| --- | --- | --- |
| Native execution | `sdk/application.ld`, `application.h`; `process.load` static ELF; preemptible isolated address space | Existing. Keep each app in ring 3; no hub access or interrupt-mask hatch. New ABI needs a separate review. |
| Allocation | `interfaces/memory.h`, `handles.c:kernel_call`, `process.memory_alloc/free` | Kernel allocate/free exist and commit zeroed pages. Missing user C allocator: aligned suballocation, requested-size metadata, overflow-safe calloc, realloc with old-data retention on failure, usable-size callback and full cleanup. No callable resize exists; operation 3 remains reserved. Do not map one page for every tiny engine object. |
| C headers and library | SDK currently supplies framing, byte copy, paths and stream helpers; application links use `-nostdlib` | Missing target headers/library for audited externals: strings/memory, formatting, diagnostic FILE/stdio paths and fatal assertions. The pinned core supplies its own sorting and float conversion units. Inventory exact symbols in report; provide tested implementations or documented removal/adaptation of unused core paths. No host glibc objects. |
| Floating point | `xstate` preserves x87/SSE/qualified AVX; execution tests guard SIMD across preemption/migration | Missing target libm coverage and IEEE edge/rounding tests. Build engine/user math with SSE2-capable flags; existing `-mgeneral-regs-only` app recipe cannot compile general floating-point expressions. Kernel/ISR compilation stays separate. Compiler-runtime helpers must be resolved by the target toolchain. |
| Stack safety | Guarded process stack reservation, explicit configured app stack size, engine stack-check API | Initialize engine stack limit below the app's configured usable stack with headroom for SDK frames; validate recursion overflow/recovery. Migration retains the virtual stack. No unbounded host-default engine stack assumption. |
| Monotonic timers | `CB_IO_NOW_NS`, `CB_SYSTEM_WAIT` deadline-only/wait-any, `clock.now_ns` | Mechanisms exist. Missing reusable runtime adapter: nanosecond-to-JS conversion policy, timer ownership, spurious-ready handling, cleanup and deadlines that do not poll. TSC availability is not required. |
| Wall time / Date | Kernel clock exposes RTC; public IO exposes monotonic now, not a general UTC/calendar service | Missing public UTC snapshot/availability contract and calendar bridge. Monotonic uptime must never masquerade as Unix epoch time. Specify UTC-only behavior initially; any disabled Date functionality must be explicit. RTC absence must have a defined result. |
| Randomness | `CB_IO_RANDOM` through manifest-authorized IO | Existing entropy mechanism; missing engine-host binding/propagation of unavailable/denied results. Do not substitute a monotonic timestamp for entropy. |
| stdin/stdout/stderr | `cb_standard`, `stream.h`, `sdk/stream.h`, `streams.md` | Existing directional bindings and EOF/backpressure. Missing runtime adapter: partial transfers, error mapping, binary/text conversion, handle release and readiness integration. Direct keyboard ownership is unnecessary for script byte input. |
| Files / source modules | `filesystem.h` open/stat/list/mutations, caller-owned streams; `cb_path` | Sequential script/module loading mechanisms exist. Missing adapter for cwd canonicalization, counted UTF-8, complete reads, source lifetime, module cache/cycle/error behavior and grant failures. No private app data without installed identity. Richer seek/truncate/rename/fsync/locking APIs are later extensions unless a specific chosen shell operation requires one. |
| Program execution | `applications.h` launch/info/wait/stop/terminate/release; launch-time stream and cwd bindings | Existing. Missing typed runtime binding, owned-child results/reaping and handle cleanup. A child launch does not imply the parent can stop another launcher's child. Command parsing/quoting is a shell concern after this gate. |
| Promises / completion | QuickJS pending-job API; SDK SUBMIT/COLLECT/WAIT/ABANDON | Mechanisms exist; missing user runtime job pump/adapters. Drain ready JS jobs with a budget, collect owned completions, then wait on readiness/deadlines. SUBMIT accepts userland-provider requests; kernel streams use blocking/nonblocking readiness. File readiness is not pending disk-completion notification. Abandoning dispatched work does not cancel its side effect. |
| Error / authority | `handle.h`, `service.h`, filtered describe, immutable admission, epochs/outcomes | Existing wire contracts. Missing runtime value/error conversion preserving request ID, outcome, payload/trace and 64-bit identity (strings/BigInt, not lossy JS Number). Stale caps require explicit reacquisition; never silently retry UNKNOWN writes. |
| Resource lifetime | Process termination/draining, app release, provider epochs; `handlers/recovery.md` | Mechanisms exist. Adapter must close streams/requests/timers and release C allocations on normal exit, exception and forced termination. No automatic provider recovery is claimed. |

## Gate before priority 3

Required completion order:

1. Accept the current execution paths on one/four-CPU KVM (passed on the AMD host,
   session `20261022T163746.733190Z`). On failure retain the
   first failure and scheduler/broker/HID snapshot; do not hide it with retries.
   A single passed run does not certify every hardware/input path.
2. Supply and test the user allocator and exact target C/math/header dependency
   set. Prove an isolated static target link with no unresolved host/POSIX imports.
   Test OOM/overflow/realloc preservation, floating-point edges and stack limits.
3. Supply the time/UTC contract and reusable Cloudberry host adapters listed
   above. Validate them using small native probes first: partial I/O and EOF,
   deadlines, denial, owned launch/wait/release, interruption and cleanup. Native
   probes remain isolated test fixtures, not default environment apps.
4. Update this gate with exact artifacts/test reports. Only then add the selected
   engine and minimal JavaScript shell. Do not let stubs that return success,
   host-header audit objects or unavailable APIs count as completed foundations.

General user threads/TLS/synchronization, dynamic linking, JIT page protection,
networking and Node/libuv compatibility remain subsequent work. They are not
requirements of this explicitly single-threaded interpreter profile. A later
V8/Node audit must examine its own version/configuration instead of treating this
smaller engine's gate as proof of readiness.

## Recorded audit evidence

The pinned whole-core host inventory produced 55 external symbols, including
malloc/realloc/free/usable-size, memory/string routines, snprintf/vsnprintf/fprintf,
assert/abort, gettimeofday/localtime_r, libm and compiler 128-bit division helpers.
The target header probe failed at `stdlib.h`. Compiler helpers may be satisfied by
target libgcc; that must be demonstrated by the eventual target link. Formatting
is used by actual engine error paths, so dropping only debug dumps cannot remove
the whole formatting requirement. This inventory is conservative and is not a
substitute for the future adapter's own dependency inventory.

Local evidence: `bin/tests/runtime-audit/report.json` and
`target-header-probe.log`. Neither successful host compilation nor a missing-header
probe claims a completed foundation. The shell gate remains closed.
