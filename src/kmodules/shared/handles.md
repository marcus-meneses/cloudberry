# Application-facing handle boundary, revision 1

The headers define the public C ABI. Initial protected processes, copied handle
mediation, boot-policy JSON authorization and the C system library are implemented;
see [handles](../kmodules/shared/handles.md). These interfaces never expose the
kmodule hub. Installed application identity and lifecycle now use a build-owned
catalog and privileged userland provider. Sections below describe the complete
target contract; vows, interactive approval, automatic safe rebind and C++ wrappers
are not implemented by the initial boot-provider milestone.

## Identity, discovery and reflection

The system library acquires handles by name and interface ID/version. The kernel
derives process, session, installation and privilege from the executing context;
the application cannot supply another caller identity. A handle is an opaque,
process-local capability with a generation, not a provider pointer or a global
numeric ID that another process may reuse. Zero is invalid. A stale capability
must never alias a new grant. Transport adapters translate capability references;
they never send local token integers as remote authority.

The first loaded driver/handler is the privileged **keyboard handler**. Its
single named resource is **`/@keyboard`**, interface `keyboard` 1.0. Foundational
kmodules still load before it. `/@` is reserved for OS resources; custom `@` names
are per-user and userland-owned. One logical handle has one name; a provider can
publish several distinct handles. Duplicate names fail atomically.

Every handle supports the base interface: describe, release and request-status.
Development tools may inspect a provider's complete published interface schema.
Runtime describe reports only operations/data visible under that capability's
effective grant. It exposes contract identity/version, compatible provider epoch,
available granted operations and permitted resource facts, never implementation
addresses, other clients, ungranted operations or hidden data. Providers supply
their interface; the broker filters the view and independently checks every call.
Importing these interface headers establishes types, not resource acquisition.

Interface IDs and operation numbers are stable. Major versions change incompatible
contracts; minor versions add compatible operations/fields. Requests require an
exact major and minimum minor. Explicit structure byte lengths allow later known
prefixes; unknown mandatory operations/flags fail. Rights are sets of operation
IDs and data scopes, not a fixed-size bitmask or a 16-resource manifest ceiling.

## Authorization and publication

Applications declare resource names, contract versions, demanded operations and
data scopes in JSON manifests. Acquisition resolves availability/version, then
evaluates those demands against installation policy and user grants. Authorization
denial goes to the trusted OS approval path; refusal returns DENIED, with approval
lifetime chosen by the user. Missing/unavailable resources return UNAVAILABLE
without a permission prompt. Manifest declarations do not require every permission
to be granted at app startup: a capability can expose the granted subset, and
denied operations fail when requested rather than terminating an app merely for
declaring them. No denied operation reaches the provider. A later permission review changes future admissions;
dispatched calls finish and queued calls fail on revocation.

A provider's privileged status does not grant arbitrary publication or hardware
access. Only kernel-approved OS providers can publish `/@` resources. Identity
combines the signing-key fingerprint and a system-assigned installed-provider ID
(distinct for different installed providers, stable across their updates); key changes
follow the existing explicit old/new-pair and user-approved installation policy.
Boot providers receive explicit policy grants before startup, preventing the
keyboard provider from needing a keyboard-driven approval prompt to initialize.

## Invocation, buffers and errors

The kernel entry is a copied-frame gate into kernel-space handle mediation.
Resource operations remain handle invocations; no independent filesystem,
keyboard or other resource-specific syscall family bypasses those grants.

The public foundation is the C ABI; C++20 RAII/types/exceptions wrap it without
putting vtables, C++ layouts or compiler symbols into messages. C callers receive
status/outcome and retain full error details. Native applications are the target.

`handle.h` defines fixed-width request/reply/error prefixes. Payloads follow the
declared header size, with explicit byte counts. No pointers, callbacks, physical
addresses or mapped-memory references appear on the wire. The system library's
local C pointer arguments are marshalled into these records. The kernel first
copies/validates caller buffers, checks overflow and all pages/permissions, then
owns a stable request copy before dispatch. No raw caller pointer survives into
provider execution. A future shared-memory handle is separately authorized and
method-based; ordinary invocation does not imply direct shared-memory access.

This framing specifies crossings, not mandatory IPC for every implementation.
A provider in the same address space may use a private C dispatch path while
preserving the interface, grants, immutable input, errors and lifetime rules.
Kernel-native handlers can execute through kernel handle mediation without an
extra provider process. Privilege/process crossings still require safe copies;
remote transports marshal explicitly. Admission metadata can be pre-encoded and
cached rather than rebuilt for each call. Implementations remain interchangeable
without pretending that locality, latency and failure outcomes are identical.

Current target encoding is little endian, with the tested C layouts. Remote or
different-ABI adapters encode/decode these fields explicitly; they do not send
native struct padding. Message chunks may be bounded; resource/file sizes and
permission counts are not inferred from one frame's buffer size. Undersized reply
buffers report the required size without repeating provider side effects: the
broker retains the completed result until delivery/release under explicit resource
failure policy. `cb_frame_fits` only checks framing, not authorization or user-copy.

Methods are synchronous by default; blocking waits suspend the caller rather than
poll. Zero deadline means no timeout. Optional async methods return an unnamed
vow with await: completed results can be awaited repeatedly, a timeout does not
cancel the operation, and overlapping awaits on the same vow fail BUSY.

The kernel assigns a request namespace/serial and authenticates it per process.
An initial call supplies a zero request ID and attempt zero; the broker assigns
the ID returned in the reply. Only IDs already owned by that caller are accepted
for retries/status queries. The broker rewrites the received handle token to the
provider's local publication token and binds reply authorization to the admitted
request/provider epoch; neither a forwarded client token nor a guessed request
ID authenticates a provider response. Provider receive waits on its delivery queue.
Its delivery-only prefix contains the provider epoch and a kernel-owned admission
context before the original payload. That context supplies the effective operation/
data grants and opaque input-routing/stream identity; clients cannot set it.
Client invoke rejects the delivery flag and provider-only context. A provider uses
this authenticated context, never client-supplied process/session/scope fields.
Opaque IDs in its JSON are hexadecimal strings, preserving full-width identity
for languages whose ordinary numeric values cannot represent every uint64_t.
Retries preserve the logical request ID and use a new attempt number; they cannot
change its original payload. Reports distinguish NOT_DISPATCHED, NOT_COMMITTED,
COMMITTED and UNKNOWN. These are provider-validated outcomes, not an exactly-once
promise. Expired/lost outcome records are UNKNOWN; clients choose retries.
Failure preserves the original complete payload plus request ID/outcome and an
appended trace chain. The broker supplies that original payload from its captured
request, never from a provider's replacement. Frames carry provider/operation/attempt identity; addresses,
private arguments and protected details are filtered across privilege boundaries.
The C++ SDK maps these errors to exceptions; C gets the same structured details.

## Lifetime and failure

Release drops a capability reference. Ordinary providers live until final handle
release unless explicitly decommissioned; boot providers also have an OS-owned
reference. Owner revocation/decommission stops admission, finishes dispatched
calls and fails queued calls before releasing state. Process exit drops its grants
and references, and cannot free memory still owned by dispatched work.

A provider failure removes its name immediately. Existing logical capabilities
become suspended; undispatched calls may wait for safe recovery with deadlines.
Dispatched calls fail with their original payload/outcome; the broker never
silently redispatches them. Safe recovery rebinds existing logical handles only
after verifying provider identity, compatible version, equivalent resource/data
scope and the existing operation grants. Replacement code cannot expand rights.
Provider epochs change; stale transport/device identities are rejected. Essential
providers that cannot recover safely produce a stack dump. Userland restart/death
remains user policy. Table pointers and executable lifetimes inside the trusted
kmodule hub retain their existing separate rules.

## First executable milestone

Load the keyboard handler first from the configured driver filesystem after its
foundational transport/storage kmodules are ready. It is a privileged isolated
process using an OS I/O handle and its own memory handle, not a ring-0 kmodule
given the public hub. The existing format3 `.kmod`/`.driver` loader loads trusted
kmodules; it cannot silently substitute for a handler executable loader.

Initial ring-3 entry/return, fault containment, owned process lifetime, static
ext4 ELF loading and the copying/admission gate are implemented. Scoped USB HID facilities
are bridged through /@io for the keyboard provider. The disk-loaded provider publishes `/@keyboard`; independent isolated clients
acquire only manifest-authorized read/state operations. No general app installer, graphical interface, arbitrary
device manager, networking or module image unloading is required for this step.
See [the keyboard provider](../handlers/keyboard/README.md).

`startup.h` defines the readonly native process startup-data envelope: versioned
header plus named inline file entries. It carries bundle data explicitly selected
by trusted startup policy, never handles, kernel pointers or additional runtime
authority. The keyboard provider selects/interprets its own config/layout within
that envelope. This is separate from handle invocation and from format3 kmodule
initialization. Conventional argv/environment and runtime bundle filesystem access
remain future work.


## Installed application lifecycle

`applications.h` defines launch/info/wait/stop/release frames for `/@applications`.
`installations.h` defines the immutable OS catalog; `sdk/application.h` validates
readonly argv/cwd startup data. `admission.h` decodes the broker-generated caller
PID, installation ID and grant time. No payload field can replace those identities.
Launch authority additionally requires the installation's launcher role; child
operations require the caller owner. This initial installed policy is bound to the
trusted build-owned image/catalog, without a general installer, sessions or signature checks.
The [applications provider](../handlers/applications/README.md) owns runtime policy;
`/@io` only bridges the catalog-bound process execution and completion mechanisms.


## Console, rendering and standard streams

`display.h` defines dimensions, fill and canonical RGB region blits. `graphics.h`
defines dynamically owned ARGB surfaces, software shapes/paths, transforms,
clipping/composition, bitmaps/blits and generation/dirty-region tracking. `text.h`
defines the actual cell grid, UTF-8 output and positioned cells. Their providers
are independent privileged processes in text → graphics → display order.

`stream.h` defines directional pipe/file read/write capabilities. Launch ABI 1.1
adds explicit stdin/stdout/stderr sources. Only the kernel translates these to
new child-local capabilities after child-manifest validation; the SDK retrieves
them with `cb_standard`. The new system gate returns zero for an absent binding.
`filesystem.h` exposes canonical-path open/stat/list/remove/mkdir and returns
caller-owned streams, preserving the same read/write contract across backing types.
The C layouts contain no kernel or provider pointers. See [streams](../sdk/streams.md)
and [graphics](../handlers/graphics/README.md) for bounds and lifetime rules.


## Standard stream mechanisms

The broker publishes the `/@streams` I/O factory. Directional endpoints carry
owned pipe/file state and reflect only their granted direction. Blocking releases
metadata locks and uses stack wait records plus scheduler wake credits. Pipe EOF,
backpressure and broken-peer errors are independent of terminal policy.
`handles.stream_bind` is trusted launch-time delegation into a READY admitted
child, constrained by its manifest. `CB_SYSTEM_STANDARD` retrieves those bindings;
releasing a bound endpoint clears that standard slot. File workers retain bounded
buffers until full join completion and never execute ext4 on an AP.

Catalog-bound display/filesystem/graphics providers acquire narrower `/@io`
operations. `handles.owner_watch` preallocates owner-exit mailboxes used for
abandoned graphics-surface reclamation; notifications never contain kernel pointers.
File mutation resource failures are reported with unknown outcome conservatively.
The normal broker variant contains no test profiling exports.


The appended system-gate operations SUBMIT, COLLECT and WAIT implement copied
provider-call submission, nonblocking retained-result retrieval and wait-any.
Existing operation numbers and synchronous INVOKE behavior are unchanged.
Wait registrations are stack-owned under CPU_ENTER. Reply/failure, endpoint
removal and pipe transitions wake matching registrations; wake credits close the
unlock/block race. Every wake re-resolves process-local IDs, so no reclaimable
request/capability/stream pointer is retained across sleep. Waits validate all
items and return the first ready index without consuming or reserving anything.
A wait deadline does not cancel submitted work. See [stream SDK](../../sdk/streams.md).
