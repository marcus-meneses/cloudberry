# Native runtime foundation

The optional static userland SDK consists of Newlib libc/libm plus Cloudberry's
allocator, startup entry and handle adapters. Existing providers/apps retain
their freestanding builds. The ordinary [js app](../apps/js/README.md) uses this
SDK, as does the [native editor](../apps/editor/README.md); test executables/scripts and SDK archives stay outside candidate media. The kernel and ISRs retain their existing compiler
flags; SDK applications use SSE2-capable userland compilation.

```sh
make -C src sdk
make -C src test-sdk-heap test-runtime-time test-sdk-link
make -C src test-runtime-foundations QEMU_CPUS=4 QEMU_ACCEL=tcg
# Host acceptance of these new userland paths:
make -C src test-runtime-foundations QEMU_CPUS=4 QEMU_ACCEL=kvm
```

Build output lives under `bin/sdk`. `tools/build-sdk.py` fetches/verifies Newlib
4.5.0.20241231 from the official source archive, builds static x86_64-elf libc/libm
without supplied syscalls, libgloss, TLS reentrancy or multiple threads, and installs
only into `bin/sdk/sysroot`. It never modifies host headers, the bootloader or the
existing cross-toolchain. Subsequent builds use the verified source/cache. Compiler,
flags and source digest are recorded in `bin/sdk/newlib.json`; configuration changes
force a Newlib rebuild. The upstream license collection is copied alongside the
SDK as `COPYING.NEWLIB` and must accompany SDK redistribution.

Source: https://sourceware.org/pub/newlib/newlib-4.5.0.20241231.tar.gz
SHA-256: `33f12605e0054965996c25c1382b3e463b0af91799001f5bb8c0630f2ec8c852`.
The SDK depends on Newlib's established math/formatting implementation, not host
glibc or handwritten replacements for transcendental functions. Tests establish
the exercised cases, not exhaustive libc conformance or every libm ULP bound.

## Allocator

`heap.h/.c` implements a single-user-thread arena allocator with injected backing
callbacks. `runtime.c` backs those callbacks with acquired `/@memory` allocate/free
operations. Small allocations share 64 KiB arenas; large requests use page-rounded
arenas. Sixteen-byte alignment, adjacent-block coalescing, overflow-safe calloc,
in-place/moving realloc and usable-size lookup are implemented. Moving growth
preserves the previously reported usable capacity. Failed growth keeps the old
allocation. Empty arenas return to the OS; release failure retains owned backing
for reuse/retry. `cb_heap_destroy` refuses live allocations.

Newlib public and reentrant malloc/free/calloc/realloc calls all reach this same
heap. `_sbrk_r` returns ENOSYS; Newlib cannot silently introduce a second allocator.
The C `malloc(0)` result is NULL; free(NULL) succeeds, realloc(p,0) frees p. Process
exit also reclaims owned pages. errno/libc state/FD lists are process-local, suitable
for the current single-user-thread process model. This is not a future multi-thread
libc implementation.

## Handles and C adapters

`runtime.h/.c` provides a caller-owned runtime context, lazy named discovery,
binary stream transfers, full writes, canonical file loading with an explicit
trailing NUL outside its byte count, launch/wait/control/release of owned children,
entropy, clocks, one-shot timers and async provider submit/collect.

Timer IDs are not reused in a context; cancellation removes the owned callback.
`cb_runtime_poll` combines existing wait-any readiness with the nearest timer or
caller deadline and dispatches at most the supplied callback budget. Callbacks may
schedule/cancel other timers. Ready I/O and due timers are processed in the same
step; no periodic polling is introduced. Budget bounds callback count, not callback
execution time. Interpreter-specific microtask and module integration remains future
shell work. The existing request and stream semantics are unchanged.

Callers own returned file/standard handles, heap buffers, child lifecycle and any
external async `cb_client`. Collect/release or explicitly abandon pending requests;
abandon is not a side-effect cancellation. Destroy cancels local timers, refuses
live heap allocations/internal pending requests and releases cached resource handles.
It cannot infer ownership of independently allocated async clients or child streams.
`cb_runtime_launch(..., streams=NULL, ...)` explicitly supplies no byte stream
bindings; pass the parent's acquired standard handles to inherit those channels.

Failures retain their exact copied reply frame, original payload, IDs, outcome and
filtered trace until the next failure. Cleanup successes do not erase that frame.
Transport failures without a reply retain their status with zero frame bytes.
There is no automatic UNKNOWN retry, provider rebind or integer-to-float identity
conversion. Runtime errors stay C status codes; language exceptions are future bindings.

`libc.c` adapts Newlib's reentrant read/write/open/close/fstat/gettimeofday functions
to handles. Descriptor records grow dynamically. Closed standard descriptors do
not silently reacquire their stream. C stdio supports sequential read, recreate and
append; seek reports ESPIPE. Unsupported access modes, exclusive/must-exist output,
stat/link/unlink, process signals and sbrk return explicit errors. These are scoped
C-runtime adapters, not POSIX compatibility or a claim that all Newlib APIs link.
Scripts/programs use the runtime child API rather than a hidden system()/fork bridge.

`crt0.o` decodes the installed app's readonly arguments/cwd and calls C main/exit.
Every SDK app needs manifest memory allocate/free grants. Standard streams retain
their inherited direction and permissions. Filesystem/app/clock/random access needs
its own grant; libc does not grant filesystem authority or create private app data.

## Time and authority

Public IO appends `utc` (operation 38), preserving all prior numbers. Its copied
16-byte result contains signed POSIX seconds, nanoseconds and quality flags. The
kernel reads RTC through the existing clock module; it does not assume the firmware
calendar is UTC. Availability requires `clock.rtc_utc=true` in boot configuration;
missing/false policy, missing RTC or an invalid calendar returns UNAVAILABLE.
Development configuration explicitly declares UTC for the QEMU RTC. Physical-machine
configuration must reflect its firmware policy. No uptime-as-epoch fallback exists.

RTC snapshots have one-second resolution, nanoseconds zero and UNSYNCHRONIZED set;
an assumed century is flagged. Gregorian validity and leap rules are checked.
`gettimeofday` maps this wall snapshot; `cb_runtime_now`, delay, poll and timer
deadlines use monotonic nanoseconds. Newlib calendar conversion defaults to UTC
without a TZ environment setting; there is no installed system timezone database.

Ordinary manifests may request only self-scoped now_ns/utc/random through IO.
Catalog, bundle, installer and kernel-registry admission retain the device/process
authority ceiling. Entropy is existing hardware randomness or UNAVAILABLE; a failed
multi-chunk request clears the entire output buffer. No timestamp fallback is used.

## Evidence and remaining gate

`test-sdk-link` resolves all 55 pinned audit external names against target archives
and libgcc, with no unresolved imports, dynamic interpreter or TLS segment. It does
not compile or embed the JavaScript engine. The host assertion name maps to Newlib's
assertion routine; target assert.h uses the native Newlib entry directly.

Host tests cover randomized fragmentation, partial allocation failure, usable-size
preservation, overflow/zeroing, retained backing and UTC policy/calendar failures.
The isolated installed runtime probe exercises real handles, C formatting/math,
rounding, binary files, denied acquisition, partial streams/EOF/readiness, timers,
async child completion and owner denial. Four-CPU AMD KVM acceptance of these SDK
paths passes in report `81d6f8ba5c2141c9919b0af94846ca7f`, with the same probe
digest and Newlib identity as the final TCG runs. Native Intel/physical-device
qualification and broader libc conformance remain separate.

The [js runner and initial REPL](../apps/js/README.md) now use these foundations.
General user threads/TLS, networking,
dynamic linking, Node compatibility and richer public file APIs remain later work.

Final TCG acceptance: one CPU `8d5b65c533754970b7fc60898d27292a` and four CPUs
`1326e79865984e99b9b7f108e875b9ed` both pass with probe SHA-256
`108c447ce1b33683f12142800e48b2efebc5aa7bab6173f0a897c19aadb31dbc`.
Results are under bin/tests/console; they record the probe digest and Newlib
identity. These reports also exercise restricted-app UTC/entropy denial,
abandoned wait without child cancellation, allocator context destruction,
aligned allocation and counted binary append. Submission error host checks
prove BUSY/transport failure does not present an old reply as a new error frame.
