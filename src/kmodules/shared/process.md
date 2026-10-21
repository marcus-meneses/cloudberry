# Protected processes

`process` is a boot-pinned kernel mechanism. It loads static ELF64 ET_EXEC images
from an already selected ext4 filesystem into owned lower-half pages. Code is
RX, data/stack RW/NX; writable executable pages, overlapping LOAD pages,
interpreter/dynamic/TLS segments, malformed ranges and noncanonical addresses
are refused. Empty segments are ignored only when their file size is also zero.
Each process has one CPU-pinned preemptible thread, a guarded kernel execution
stack and a separate guarded kernel-entry stack. User stack size is supplied by
trusted bootstrap policy (64 KiB default), committed eagerly; demand growth,
TLS, dynamic linking, conventional argv/environment and multi-threaded processes are
not part of this initial milestone. IDs are never reused in one boot.

The GDT includes user selectors 0x2b/0x33. Per-CPU TSS RSP0 follows the saved
thread's kernel-entry stack before switching. User interrupt entry/return owns
SWAPGS; FSGSBASE stays disabled and user RFLAGS are sanitized. IRQ/timer paths
remain preemptible outside existing critical sections. CPL3 synchronous faults
terminate only the current process and record vector/CR2/RIP. Kernel faults,
NMI, double fault and machine check retain their fatal handling.

The DPL3 vector 128 gate accepts lifecycle exit (RAX=0, RDI=status) and copied
handle mediation (RAX=1, RDI=local gate descriptor). Resource operations pass
through capabilities rather than independent resource syscall families.
A fault/exit returns through the normal interrupt unwind into the process's
retained kernel continuation. The enclosing thread must actually reach EXITED
before reap can free either kernel stack or the private address space.

Trusted hub surfaces: `init(hub,heap)`, `load(process_request)` returns process ID,
`start(id)` returns thread ID, `info(id,out)`, `reap(id)` and `current()` (zero
outside a process's running thread). `copy(id,user,kernel,bytes,write)` copies at
most 4096 bytes after validating the complete range against owned mappings;
write requires a writable user leaf. Physical aliases avoid user-copy faults.
The caller supplies authenticated process identity; these surfaces are never
mapped or callable from ring 3. No user pointer survives a copied request.

The initial process loader is not an installer or a signature verifier. Only
trusted bootstrap/kernel policy may admit an executable; arbitrary apps cannot
load or assign permissions to another process. Resource manifests/handle grants now come from the separate handles module
and trusted boot admission. Process exit can retain its result and memory until its
kernel owner explicitly reaps it. If a failed load cannot finish rollback, it
returns failure with a cleanup-only process ID for retry through reap; that
record cannot be started. Normal load failures return failure with zero.

`test-process-elf` checks malformed ELF bounds/permissions; `test-process-boot`
uses disposable ext4 media and test-only executables. It validates same-VA private
backing, preemption, contained kernel/RO/NX/unmapped accesses and CLI, user GS
selector changes, stale process IDs and allocation/stack baseline restoration.
Fixtures never enter candidate media.

Local acceptance (TCG): one-CPU/no-IOMMU run `c4c028b695ed409a848b6700b11e4037` and
four-CPU/VT-d run `1d3609b7d8d24e84ac89c2c8f9ece5f9` pass. The final four-CPU
probe also verifies NT-flag sanitization across the user gate. Pure ELF decoder
checks pass under UBSan. Native KVM/physical user-entry acceptance remains open.

`process.check` validates at most a 16 KiB message range without copying.
`memory_alloc` and `memory_free` are trusted owner-thread mechanisms behind the
self-scoped memory handle; they grow owned user pages and reuse released virtual
holes. Process exit also initiates scoped I/O cleanup and releases broker policy;
reap waits for retained kernel I/O workers before freeing process backing.

Loader request ABI1 passes a scalar entry argument. ABI2 additionally copies a
trusted startup packet into owned read-only/NX user pages, finds a nonoverlapping
lower-half gap and passes its user address in RDI. The keyboard bundle uses this
for generic named startup files; the provider owns their interpretation and
no kernel pointer crosses into the process. Failed packet
construction follows the same cleanup-ID/rollback ownership rules as ELF loading.

The integrated process boot also loads the production keyboard provider from
ext4 and runs independent manifest-authorized clients, real emulated HID reports,
hotplug and cooperative kernel-requested decommission. The provider remains an
ordinary protected process rather than a hub callback.

Integrated keyboard acceptance passes on one CPU/no IOMMU
(`4b52f2ae41564258972e34143507c949`), four CPUs/VT-d
(`5416fd1d535b47899f1eeafd75001032`) and four CPUs/AMD-Vi
(`a6fcf05306744aa3b13f59b96e329531`). These include isolated handle clients,
USB input/hotplug and provider cleanup. The broader one/four-CPU thread/SMP
regression also passes after moving its test interrupt to unowned vector 129.

`process.stop(id)` is a trusted owner-only cooperative request. It decommissions
publications, queues the lifecycle event and records stop_requested. Repeated
requests are idempotent; running memory/stack state remains owned until normal
exit and explicit reap. It never force-unwinds a kernel or provider thread.
The kernel caller owns retry/restart policy and must reap an old process before
loading a fresh identity; no automatic capability rebind is implied.

`process.sample(query,out)` is the trusted read-only telemetry surface. It copies
one PID-ordered record through a fixed sweep ceiling, excludes records being
reaped, and reports thread accounting plus committed user page bytes. The
applications backend exposes this only to the installed application manager;
its userland provider publishes manifest-authorized `processes` for monitors.
It does not grant launch/stop/reap rights or expose kernel pointers. Sweeps are
bounded but are not atomic snapshots across process lifecycle changes.

## User credentials

`process.credentials` is a trusted kernel-only surface. Each process owns a
UID/primary GID/generation header and dynamically allocated supplementary GIDs.
Boot-loaded processes explicitly start as UID/GID 0. The application launcher
copies the authenticated owner's snapshot into each READY child before start;
child and parent allocations are independent. Failed inheritance prevents launch.
Cleanup releases supplementary group storage.

The installed identity provider alone can request replacement through IO after
password verification. No ordinary process gate accepts a credential setter.
Replacement affects the requesting process and future descendants, never its
parent or existing children. Other callers may query only their own snapshot.
The filesystem and identity provider read this kernel-owned identity for policy.
Processes retain supplementary group snapshots until authentication/replacement
or exit; database edits do not silently rewrite running identities.

## Session membership and ancestry

Trusted `process.session(PID,0,out,capacity)` copies `cb_session`;
`process.session(owner,1)` begins a session using its monotonic PID;
`process.session(owner,2,excludePID)` closes member launches and requests
cooperative stops, excluding the owner currently completing END if specified.
Closed membership prevents credential inheritance and process start. Closing by
SID still works after its original owner has been reaped. Ordinary apps have no
setter; IO restricts lifecycle mechanics to the installed sessions provider.
Provider-owned records track parent sessions and owner-exit cleanup.

`process.ancestor(ancestorPID,descendantPID)` follows immutable launch parents;
missing/reaped intermediates fail closed. Keyboard uses this predicate through
restricted IO to nest focus without allowing unrelated input takeover.
Session 0 is the root boot domain. Credential replacement remains separate from
session membership; no forced termination or existing-thread migration is added.


Atomic credential operation 4 takes cb_identity_home plus groups/home bytes. It
allocates and validates both before publishing either, and inheritance deep-copies
the home. `process.home` copies the canonical snapshot (root defaults to
/system/root); legacy credential-only replacement clears a non-root home binding.
`process.active` reports open-session resource eligibility. Session close marks
membership closed, calls handles.revoke/filesystem.revoke and records failed stop
requests. Operation 3 returns a copied cb_session_status, including pending members
and failures. Closing by SID continues to work after owner reap. No code, stack or
active request payload is reclaimed by revocation.
