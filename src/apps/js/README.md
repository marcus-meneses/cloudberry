# JavaScript application

`js` is an ordinary, single-threaded native application, built from pinned
QuickJS **2026-06-04** and the Cloudberry C SDK. It imports no kernel functions,
keyboard or display backend. All output follows inherited stdout/stderr; input
uses inherited stdin. Terminal pipelines and file redirection therefore work
without a terminal-specific JavaScript command.

```
js /home/marcus/example.js argument
js -e 'console.log(await system.exec("true"))'
js -i
```

Files and `-e` sources are ES modules, including top-level `await`. Files may
import explicit absolute paths or `./` / `../` paths, resolved relative to the
importing file. Canonical paths are module-cache keys within one invocation;
imports use the same scoped filesystem authority as the application. There is
no package search, dynamic native module loading or bytecode loading.
`import { system } from 'cloudberry'` accesses the same adapter as the global.
`import.meta.url` is the canonical guest path.

Interactive mode evaluates one line of JavaScript at a time, prints expression
results and drains asynchronous work before the next prompt. `.exit` or stdin
EOF exits. It uses terminal line editing through stdin, with no keyboard focus
claim. Its C stdin is unbuffered so a child or JS stream consumer can receive
subsequent input without REPL prefetch stealing it. This is an initial JavaScript REPL and scripting runner; it does not
implement shell command parsing, job control, Node or browser APIs.

## Handle adapters

- `system.args`: script filename followed by script arguments; `system.cwd`:
  inherited absolute working directory. `-e` and interactive mode have no script
  arguments. Neither object grants filesystem permissions.
- `system.open(path, mode='r')`: owned stream; `r` reads, `w` recreates, `a`
  appends. Paths resolve against startup cwd. `stream.read(n=4096)` returns a
  Promise of a `Uint8Array`, possibly a prefix; empty means EOF. `stream.write`
  accepts a UTF-8 string or byte typed array and returns a Promise of a BigInt
  byte count after writing all bytes. Partial transfers and backpressure are
  handled without replaying accepted bytes.
- `stream.close()`: releases its capability. Closing or starting another operation
  while the stream has pending work fails with `CB_BUSY`. Closed streams fail
  locally. GC releases unclosed streams; pending work retains its stream until
  settlement. Prefer explicit close, especially before awaiting pipe EOF.
- `system.stdin`, `.stdout`, `.stderr`: independently acquired inherited stream
  objects, or `null` when absent. `console.log/error` use C stdio over their own
  inherited bindings; closing a JavaScript stream does not close the C descriptor.
- `system.pipe()`: `{reader,writer}`; directional, bounded kernel pipe storage.
- `system.readFile(path)`: synchronous whole-file byte load.
  `system.writeFile(path,data,append=false)`: synchronous recreate/append and
  close. Large files use repeated bounded transport transfers. These convenience
  calls can block; stream methods provide Promise/readiness-based flow.
- `system.exec(name,args=[],options={})`: launch an installed application;
  returns a Promise whose `.process` is the admitted BigInt PID. `options.cwd`
  overrides inherited cwd; `stdin/stdout/stderr` override endpoints (`null`
  supplies none). Omitted endpoints inherit this runner's bindings. Completion
  is `{process,exitCode,state,faultVector}` with lossless BigInt numeric IDs/codes;
  the owned child record is released after completion. A nonzero child exit is
  returned to script policy. Admission/completion failures reject; they are never
  silently retried. A post-admission error carries `.process` where available;
  abandoning a wait does not cancel a dispatched child.
- `system.wait(pid)`: observe completion of an already owned child (for example,
  after an explicit post-admission failure); the Promise also releases its record
  after completion. `system.release(pid)` releases an exited owned record. IDs
  must be BigInt and are range-checked without Number coercion. Ownership is
  enforced by the application handler.
- `system.nowNs()`: monotonic BigInt nanoseconds. `system.sleep(milliseconds)`:
  Promise completed at or after an absolute monotonic deadline; finite,
  nonnegative durations required. No busy-spin delay.
- `system.randomBytes(length)`: handle-backed entropy; unavailable entropy throws,
  with no pseudo-random security fallback. `Math.random` is the engine's ordinary
  noncryptographic PRNG.

Promise work owns its endpoint/data/request and uses existing nonblocking stream
operations, SUBMIT/COLLECT and kernel wait-any/deadlines. Microtasks run in batches
of 64 before servicing host completions. Timers use a dynamic list; simultaneous
stream/child waits are admitted up to the public wait-any transport capacity,
with `CB_BUSY` before accepting more. No helper threads, polling timer or hidden
process migration policy is introduced.

`HandleError` includes `status` and the exact copied reply `frame` as bytes. When
provided by the broker, it also carries BigInt request namespace/ID and provider
epoch, numeric outcome, original `payload` and provider `trace` bytes. Its normal
JavaScript `stack` records script call sites. Local failures have no fabricated
provider frame. No UNKNOWN side effect is automatically retried or old capability
rebound. Unhandled Promise rejection or uncaught exception exits nonzero.

## Engine profile and validation

The build verifies the official archive SHA-256 and preserves its MIT license in
`js.resources`; Newlib's license collection is included too. Source/build/cache
remain under `bin/sdk/quickjs`. Only the C core is used, with custom checked
allocation hooks. OS Atomics/Workers, upstream quickjs-libc and host filesystem
adapters are excluded; stack checks remain enabled. Upstream SharedArrayBuffer
objects remain local to this interpreter; no OS shared-memory binding or
cross-process transfer is exposed. The runner currently uses a
128 MiB engine allocation budget and 128 KiB engine stack guard within a 512 KiB
process stack; these are interpreter safeguards, not new kernel resource limits.

Initial timezone support is UTC only. `Date.now()` uses the authorized RTC UTC
adapter (currently one-second precision); unavailable UTC produces NaN/Invalid
Date rather than a fabricated epoch or uninitialized data. Timing uses `nowNs`.
Wall-clock seeding is initialized even when UTC is denied.

`test-js-engine-host` exercises engine logic and failure cleanup under UBSan;
`test-js-engine` runs the same target code in an isolated guest. The first target
engine acceptance passed one-CPU TCG, report
`bin/tests/console/fcc543a0dd024119a830afd433f95d47/result.json`.
The updated engine (including initialized wall-clock failure handling and binary
construction) passed four-CPU TCG in
`bin/tests/console/a7d051a7f39948ceb8d9ae89efcc66cf/result.json`, and host UBSan
(`bin/quickjs-host-final.log`). Initial packaged one-CPU acceptance passed in
`bin/tests/console/118392dab0bf48acbc21a1fcb4d60f2d/result.json`.
Four-CPU packaged acceptance passed in
`bin/tests/console/09b2dbab9f2a4bcab3c15ab2bc3a3a0d/result.json`.
`test-javascript` tests the packaged runner, modules, manifest denial, file and
pipe I/O/backpressure, timers, children, redirection, pipelines and interactive
return using disposable media. Test scripts and restricted fixture apps are
absent from candidate images. KVM validation is recorded separately from TCG.

Final packaged acceptance, including the unbuffered REPL/child shared-stdin case,
passes four-CPU TCG in
`bin/tests/console/1cb638e04afa4b84ae45c18c54e43945/result.json`.
The report's binary digest matches the built candidate; script digests match the
retained source fixtures. Candidate/static-link checks are in
`bin/tests/javascript-candidate.json`; native SDK link and C/C++ interface and
installation-contract checks pass.

User-host four-CPU KVM runner acceptance passes in
`bin/tests/console/aef558a8fb5945ee8a89b42b58bf31f2/result.json`. The saved QEMU
command explicitly uses `-accel kvm -smp 4`; the runner digest, all script digests,
QuickJS build identity and Newlib identity match the candidate/cache from that acceptance checkpoint.
The final screen confirms child stdin forwarding, completion and terminal return.
This closes packaged runner acceptance on the tested host; the isolated engine
OOM/recursion probe retains its separate host-UBSan/TCG evidence. Native Intel
and physical-device qualification remain separate. To repeat:

```
make -C src test-javascript QEMU_CPUS=4 QEMU_ACCEL=kvm
```

## Graphics and input handles

`system.graphics()` acquires `/@graphics`, returning an owned object with current
pixel `width`/`height`, measured `cellWidth`/`cellHeight`, `fontFamily`, `fill(x,y,width,height,rgb)`,
`text(x,y,string,fg=0xffffff,bg=0)`, `beginFrame()`, `present()` and `close()`. Fill uses the existing
rectangle primitive; text uses the default Ubuntu Mono TrueType face.
`beginFrame()` defers bounded native glyph batches until `present()`, which flushes
the frame once; outside a frame text presents immediately. Fill remains deferred. Coordinates/colors are checked before
marshalling. Rendering still follows graphics → display, without exposing a
framebuffer or hardware pointer to JavaScript.

`system.keyboard()` acquires `/@keyboard` and claims focus. `system.mouse()`
acquires `/@mouse` without changing existing mouse ownership policy. Both provide
`next()` (a Promise of an event array) and `close()`. Exactly one read per input
object may be pending; another read fails BUSY. Closing abandons pending requests,
rejects their Promises as REVOKED, releases keyboard focus when owned, and closes
the capability. Pending reads retain their objects; explicit close is necessary
when leaving an input loop with outstanding work. Dispatched abandonment is not a
provider operation cancellation or automatic replay.

Keyboard events expose kind, usage/page, Unicode scalar, modifiers, and BigInt
sequence/device/timestamp. Modifier bits are USB HID modifier bits. Mouse events
expose kind, relative/absolute flags, BigInt coordinates/deltas, buttons/changed
bits, vertical wheel/resolution and event-associated `cursorX`/`cursorY` from the mouse-owned arrow. Resynchronization returns a `reset` event and
updates the input cursor rather than replaying old input. Providers continue to
validate foreground/focus authority. These adapters do not read stdin, forward
terminal key events, implement a compositor, or alter kernel scheduling.

The runner now declares the graphics/input permissions above in its manifest;
scripts run with that runner's authority, without a per-script sandbox. Acquisitions
are lazy. The [editor sample](../../script/samples/README.md) is installed at
`/system/script/samples/editor.js`; it implements its own UI in JavaScript.

The later graphical/input adapter and editor extension passed four-CPU TCG in
`4e3cbfd3a156482aa75187da05899578`; its runner/sample digests are recorded in
`editor.json`. That extension is newer than the core-runner KVM checkpoint above;
its own native-host check is `make -C src test-js-editor QEMU_CPUS=4 QEMU_ACCEL=kvm`.

`system.list(path=system.cwd)` returns `{name,directory}` entries through the
filesystem LIST contract; `system.stat(path)` returns `{bytes: BigInt,directory}`;
`system.mkdir(path)` creates a directory. All use the runner's scoped filesystem
capability, including denial/not-found errors; no host path or implementation ABI
is exposed to scripts.

## Owned graphical surfaces

Graphics objects additionally expose:

- `createSurface(width,height,container=false)` → opaque BigInt ID.
- `releaseSurface(id)`; `fillSurface(id,rgb)` fills that bitmap, deferred.
- `placeSurfaces([{surface,parent:0n,x,y,visible:true}, ...])` updates a hierarchy.
- `scrollSurface(parent,dx,dy)` repositions direct children, deferred.
- `text(x,y,text,fg,bg,surface=0n)` draws into a selected bitmap.

A graphics object tracks and releases its own created surfaces on close/finalization;
the provider independently enforces process ownership and owner-exit cleanup. A
container clips descendants but has no bitmap. Releasing a parent detaches its
children; callers release them separately or let graphics.close reclaim them.
`beginFrame`/`present` keep glyph/layout updates deferred until the frame is ready.
No native pixels or implementation pointers are exposed to JavaScript.

The editor creates a clipping editing-area container with reusable row bitmaps;
menu/status/header lines are separate main-canvas children. Vertical scrolling
rotates cached row references and changes their placement, painting only recycled
rows and caret changes. Open/Save dialogs remain application UI drawn into those
row surfaces and are restored from the retained model on closing.

Mouse objects expose `hideCursor()` / `showCursor()`. The focused editor wraps
frame recomputation/presentation in hide/try/finally/show; motion-only events do
not trigger this redraw path. Explicit close/finalization restores an owned hide,
and the provider's owner-exit watch covers abrupt process death. These are mouse
handle operations; the display contract gains no cursor policy.
