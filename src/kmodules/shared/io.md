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
- `release`: relinquish a terminal result record. Running children return BUSY.
- `list`: enumerate installed ordinary application names with an opaque cursor.

Info/wait/stop/release require the process ID to belong to the authenticated
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
copies the OS registry once and its installed-image loader uses the record's
path, manifest, identity, CPU and stack settings. Only the catalog-selected
manager process can use process_launch/stop/reap/release through `/@io`; even a
manually admitted forged manifest does not create this binding. Runtime requests
supply no executable path, replacement manifest or installation identity.

Per-child kernel worker threads wait with `THREAD_WAIT`, then post a preallocated
completion event. They do not poll the scheduler or allocate in an IRQ callback.
Physical process reap and notification/result release are distinct operations.
If the manager exits, the kernel requests cooperative stop for all surviving
children and retains their resources until quiescence. An uncooperative child can
therefore delay manager reap. Automatic manager recovery and forced termination
remain separate work; running stacks are never freed as an emergency shortcut.

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
an explicitly writable development snapshot. This is not a signature
verification chain, installer, complete login-session manager or writable app
registry. Cwd is immutable startup metadata; there is no filesystem grant implied
by it. Argv currently travels within one bounded handle message, retaining empty
arguments, spaces and case; larger streamed launch packets are future transport
work. Launch ABI 1.1 supplies explicit parent-owned standard-stream endpoints. The
kernel validates and delegates new child-local handles before start, intersecting
the child manifest. The system library retrieves these bindings; raw parent
tokens are cleared from child startup data. Processes still have one user thread;
the kernel selects its initial CPU by live process occupancy and then pins it. This handler and the device
I/O execution path currently run on CPU0. The catalog and handler child/wait
records are dynamically sized; the catalog is immutable and boot-pinned.


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
cannot query other PIDs. The catalog-bound identity and sessions providers may
query a caller; only identity may use `identity_set` (target PID plus copied credential snapshot).
The IO gateway forwards to process.credentials, never trusting an app admin flag.

`random` requests up to 64 bytes from capability-checked x86 RDRAND, with bounded
retries and explicit UNAVAILABLE on absent/failed hardware entropy. Identity uses
this for password salts. No timer/pointer fallback or general entropy-pool claim
is made. This mechanism targets the existing modern Intel/AMD platform contract.


`session` operations 4/5 are sessions-provider-only UID revoke/status mechanics.
The privileged sessions handler owns administrative authorization and subtree
policy; the kernel selects actual credential snapshots, marks them closed, blocks
credential replacement/inheritance and invokes existing resource-revocation paths.
Root UID 0 is never selected by the revoke mechanism.

`console_access` is restricted by rendering layer to the bound text, graphics or
display installation. Its copied `{process,layer,reserved}` request identifies the
broker-authenticated incoming actor, not an app-selected privilege. Kernel focus,
open-session and launch-ancestry checks authorize screen work. Trusted upstream
renderer exemptions preserve text -> graphics -> display composition. Focus is
rechecked after collecting ancestry/session facts. Decisions cover admission of
one operation; already admitted drawing may complete across focus transitions.
