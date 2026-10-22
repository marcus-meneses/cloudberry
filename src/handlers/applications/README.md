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
- `terminate`: manifest-authorized forced termination of an owned child at a clean user-return boundary; exit status 137.
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
copies the immutable built-in catalog once; a versioned supplemental registry
provides installed apps. Its installed-image loader uses the selected record's
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
an explicitly writable development snapshot. Signed local application installation is now provided by the dedicated
installer handler and versioned supplemental registry. Built-in admission remains
rooted in the build-owned image, without Secure Boot or a public issuer ecosystem. Cwd is immutable startup metadata; there is no filesystem grant implied
by it. Argv currently travels within one bounded handle message, retaining empty
arguments, spaces and case; larger streamed launch packets are future transport
work. Launch ABI 1.1 supplies explicit parent-owned standard-stream endpoints. The
kernel validates and delegates new child-local handles before start, intersecting
the child manifest. The system library retrieves these bindings; raw parent
tokens are cleared from child startup data. Processes still have one user thread;
the kernel selects its initial CPU by runnable pressure, and eligible user contexts
may migrate at clean user-return boundaries. This handler and the device
I/O execution path currently run on CPU0. The catalog and handler child/wait
records are dynamically sized. Built-ins are immutable and boot-pinned;
supplemental registrations use refcounted immutable generations.


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

## Per-user private data

`data` (operation 8) takes no input and returns the authenticated caller's canonical
app-data path as UTF-8 bytes without NUL. The kernel resolves installed identity
and an owned home snapshot; caller-selected UID, app name or path is never accepted.
The operation returns a location, not extra filesystem authority or automatic
provisioning. `fsctl data` demonstrates it. Manifests still authorize acquisition
and operations; filesystem checks apply on every call, including delegated files.
Same-user applications cannot access each other's private containers. Root keeps
its administrative exceptions, with credential storage restricted to identity.


Runtime discovery and launch policy use the authoritative applications registry,
not startup catalog copies or `_index` caches. `resources` returns the caller's
registered resource directory, alongside the existing private `data` resolver.
The manager delegates list/launch decisions through IO; identity home provisioning
queries that registry too. Installed app scope, metadata and lifetime rules are
specified by the [installer](../../handlers/installer/README.md).


`terminate` is operation 10, appended without renumbering existing operations.
It requests authority revocation and forced user exit, never an asynchronous unwind
of kernel work. Dispatched side effects may still commit. Completion/wait remains
available and execution must reach thread EXITED before reap. STOP remains graceful
and no automatic escalation policy is implied. See [process mechanics](../../kmodules/shared/process.md)
and [handler recovery](../recovery.md). Core binding is a scheduler mechanism;
this provider remains pinned to CPU0.
