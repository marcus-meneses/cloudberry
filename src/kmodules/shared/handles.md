# Application lifecycle handler

`applications.elf` is a privileged ring-3 provider of `/@applications`. It
acquires `/@memory` and the manager-scoped `/@io` capability. It cannot call hub
addresses or execute kernel methods directly. The C contract lives in
`interfaces/applications.h`; ordinary apps use the normal C system library.

## Public operations

- `launch`: name an installation and pass inline argv strings plus an absolute
  cwd. Returns a process ID. The caller must have the operation in its manifest
  **and** the launcher role in the OS catalog. Provider/manager installations
  cannot be launched through this operation.
- `info`: copied running/terminal state, exit status, fault vector and stop flag.
- `wait`: synchronous to the caller. The handler retains the delivered request
  and continues receiving other calls; completion events trigger its reply.
- `stop`: cooperative base-release notification, idempotent. No forced unwind.
- `terminate`: explicitly revoke/terminate an owned user child at a safe user-return boundary; exit status 137.
- `release`: relinquish a terminal result record. Running children return BUSY.
- `list`: enumerate installed ordinary application names with an opaque cursor.

Info/wait/stop/terminate/release require the process ID to belong to the authenticated
caller. IDs are not authority. The broker supplies PID, installation identity
and grant time in its admission context; caller payload cannot replace them.
Completion follows full thread exit, rather than the earlier process exit flag.
A child fault terminates that child and returns a fault result to its owner.

The kernel reclaims executable mappings and stacks when the handler consumes a
completion. The copied userland result survives until release or caller exit.
A separate preallocated owner-exit event remains registered after execution
reap, so cached results do not leak when their owner exits. Running orphaned
children may finish; their result is then automatically released. No automatic
side-effect retry or capability rebind is introduced.

The bootstrap coordinator starts this provider from `boot.providers` and checks
readiness. It does not service launch requests or supervise running children.
A trusted optional `initial.bin` startup file can name the initial application
using the same launch frame. Normal builds launch the ordinary terminal. Disposable tests replace it with
registered test launchers; no test autostart enters the candidate.

## Kernel mechanism boundary

`kmodules/shared/applications.c` belongs to the independent applications kmodule. Its kernel-only catalog loader
copies immutable built-ins and owns versioned supplemental registry snapshots;
its installed-image loader uses the selected record's
path, manifest, identity, CPU and stack settings. Only the catalog-selected
manager process can use process_launch/stop/reap/release through `/@io`; even a
manually admitted forged manifest does not create this binding. Runtime requests
supply no executable path, replacement manifest or installation identity.

Per-child kernel worker threads wait with `THREAD_WAIT`, then post a preallocated
completion event. They do not poll the scheduler or allocate in an IRQ callback.
Physical process reap and notification/result release are distinct operations.
If the manager exits, the kernel requests cooperative stop for all surviving
children and retains their resources until quiescence. An uncooperative child can
therefore delay manager reap. Automatic manager recovery remains separate work; explicit child termination
is available, while manager-exit stop policy remains cooperative. running stacks are never freed as an emergency shortcut.

## Installation and current limits

`tools/build-installations.py` generates `installed.bin` from `apps/installed.json`
and the packaged manifests. The authoritative record is outside each app bundle;
its binary layout is described in `interfaces/installations.h`. Duplicate names/
identities, escaping paths, missing executables and ordinary-app process authority
or OS-resource publication are rejected by the build. The kernel validates framing
and the manifest at admission. Bootstrap verifies registered provider metadata
against the authoritative record. Trusted unregistered kernel fixture loading
remains available, but cannot bind the process-manager role.

The trust root is the build-owned boot image/catalog. The filesystem handler
protects its installed boot/system tree from application mutations, including on
an explicitly writable development snapshot. Signed local application installation and session management now run in
dedicated privileged userland providers. Built-ins remain rooted in the build;
this does not establish Secure Boot or a public issuer ecosystem. Cwd is immutable startup metadata; there is no filesystem grant implied
by it. Argv currently travels within one bounded handle message, retaining empty
arguments, spaces and case; larger streamed launch packets are future transport
work. Launch ABI 1.1 supplies explicit parent-owned standard-stream endpoints. The
kernel validates and delegates new child-local handles before start, intersecting
the child manifest. The system library retrieves these bindings; raw parent
tokens are cleared from child startup data. Processes still have one user thread;
the kernel selects its initial CPU by runnable pressure and ordinary user contexts
may migrate at clean return boundaries. This handler and the device
I/O execution path currently run on CPU0. The catalog and handler child/wait
records are dynamically sized; built-ins are immutable and boot-pinned.
Supplemental registrations use immutable refcounted generations.


## Validation

Host UBSan tests cover catalog framing/duplicates, path/role binding, forged
manager denial and allocation rollback. C and C++ interface layouts, keyboard
context parsing, provider BAD_REQUEST completion and pre-mutation native reply
reservation pass regression checks. Disposable one/four-CPU TCG boots execute
registered runtime apps, including the real `launch` app. The final traced
four-CPU run also verifies exact broker heap restoration after manager teardown;
ordinary candidate boot publishes both providers with no fixtures. Native
four-CPU AMD-host KVM correctness acceptance passes in run
`535b22562f0d45b4b0752e72a5ade1e6` (2026-10-18), with qualified TSC throughout.
Application lifecycle, handle concurrency, page/stack restoration and keyboard
checks all pass. This is correctness acceptance, not a new performance baseline;
physical hardware and native Intel CPU acceptance remain open. Reproduce with:

```sh
make -C src -j4 test-process-boot QEMU_ACCEL=kvm QEMU_CPUS=4 QEMU_IOMMU=amd
```


The console milestone extends native boot packaging to isolated resource providers and the
ordinary terminal. It adds `list`, launch-bound stdio and kernel-selected ordinary
process placement. The earlier native lifecycle result predates these extensions;
new console/graphics correctness is covered by isolated TCG tests and still needs
native KVM acceptance. See [console tests](../../tests/README.md#console-and-software-graphics).

Read-only `processes` is distinct from `list`: it enumerates actual ordinary and
privileged processes, rather than installed executables. Any admitted app whose
manifest requests that operation may inspect copied records. The provider uses
installation-bound IO `process_sample`; ordinary apps cannot invoke that private
backend directly. The [monitor app](../../apps/monitor/README.md) uses this contract with
no launch or stop permissions.

The kernel application launcher now inherits the authenticated owner's UID/GID and
supplementary groups into each READY child before execution, with independent
storage. Installed executable traversal/execute bits are checked for that owner.
An authenticated accounts launcher can start a different-user execution subtree;
this never changes the invoking terminal or existing sibling processes.

## Credential authority and entropy

`credentials` queries the calling process when PID is zero. Ordinary callers
cannot query other PIDs. Only the catalog-bound identity provider may query a
caller and use `identity_set` (target PID plus copied credential snapshot).
The IO gateway forwards to process.credentials, never trusting an app admin flag.

`random` requests up to 64 bytes from capability-checked x86 RDRAND, with bounded
retries and explicit UNAVAILABLE on absent/failed hardware entropy. Identity uses
this for password salts. No timer/pointer fallback or general entropy-pool claim
is made. This mechanism targets the existing modern Intel/AMD platform contract.

Process identity and user identity are separate: the admission prefix supplies
an authenticated PID/installation; handlers obtain UID/GID from kernel credentials
through restricted IO rather than client payload. Identity management intersects
manifest grants with root/admin checks at dispatch. Filesystem workers carry the
actual stream caller PID, rechecking current credentials on every operation.


## Session resource revocation

`handles.revoke(PID)` marks admission revoked without dropping capabilities,
executing code or dispatched request buffers. New acquire/publish/standard access
and invoke/submit fail REVOKED. Undispatched calls receive a completed failure with
original payload and NOT_DISPATCHED outcome; OOM still prevents their dispatch.
Blocked pipe and mount callers wake and recheck revocation. Provider publications
retire from discovery while dispatched calls may finish. Collection, reply,
abandonment and release remain available to drain existing work.

Immediate synchronous rejections retain their correlation/result record until SDK
FORGET, including rejections without a publication. They must not return a temporary
unregistered request ID: that masks the original error during SDK cleanup. Failed
submissions return no accepted ID; OOM without an output rolls back the unobservable
record. Focused broker and guest SDK tests cover these lifetime rules.

## Installation binding

Admission stores the installation ID and original scope (zero for built-in/global,
UID for a private installation). Trusted `handles.identity` accepts 16 bytes for
ID or 24 for ID plus scope; this kernel-only extension does not change the public
handle ABI. Reauthentication cannot redirect a running private app to another
user's installation or bypass active-uninstall checks. Applications resolves
launch, list, resources and private-data authority from one pinned registry view.
Only the bound installer may stage/prepare/publish supplemental records. See
[installer](../../handlers/installer/README.md) and
[applications](../../handlers/applications/README.md) for policy and lifetime rules.


Forced user termination additionally invokes `handles.terminate`: an outstanding
copied provider call wakes with REVOKED and NOT_DISPATCHED or OUTCOME_UNKNOWN as
appropriate. Dispatched work is never advertised as cancelled/rolled back. Its
retained ownership survives ordinary syscall return, then process exit removes
caller records. Late provider replies to removed request IDs fail BAD_REQUEST.
Allocation failure while constructing an error still wakes the waiting caller
with a transport failure; termination must not depend on allocating a reply.
See [provider recovery](../../handlers/recovery.md) for restart/outcome policy.


## GPU result allocation — 2026-10-24

The native I/O broker sizes GPU reply bodies by sub-operation. INFO/OPEN/NEXT/
SELECTED reserve the fixed info record, MODES the fixed scanout record, CREATE/
TEXTURE the resource record, and context creation its 32-bit ID. READ reserves
the requested bounded count; CAPSET reserves metadata plus its bounded payload.
No-result operations reserve zero body bytes. Oversized counts remain capped at
the existing reply bound; the gateway still validates request shape, authority,
resource bounds and reply capacity before mutation. Malformed/unknown GPU
operations reserve no result body and are rejected by the gateway.

The request-retained reply and caller copy remain separately owned. Both use
the same sizing rule; caller output is reserved before dispatch. Allocation
failure therefore does not authorize a mutation, and no pool reuses live results.
A no-result GPU call avoids two 15 KiB zeroed bodies (30 KiB of allocation work),
not two allocations or the retained request record. Other resource paths retain
their previous capacity policy. Host tests cover every GPU operation, count
overflow, malformed headers, exact output allocation and failure before dispatch.

`handles-profile.inc` remains a disposable variant only. HP_PAUSE closes new
measurement admissions without discarding active probes. HP_END still requires
all admitted probes to finish; the graphics fixture reaps its actors before
reading the final report. Pending RECEIVE calls can thus finish safely without
including new work in the measured edit window.

Profiling pause correction: stopping admission does not stop dispatch/output
timestamps for already-admitted probes. Entry stamps carry a measurement
generation; gates crossing a start/restart cannot attach to the wrong report.
Profile control and admission serialize, and active probes prevent a report
from being reset or read. HP_STATUS supplies diagnostic enabled/active/generation
fields; genuine clock failures continue to invalidate results. All of this stays
in the disposable profiler variant. The normal broker binary is unchanged.
