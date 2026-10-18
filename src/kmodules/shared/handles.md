# Application handle mediation

`handles` is a boot-pinned kernel mechanism, separate from the trusted kmodule
hub. Applications enter through a copied `cb_system_gate` on vector 128; the
kernel authenticates the process from its current thread. Local pointer fields
exist only in this gate descriptor. Owned request/delivery/reply frames contain
fixed-width fields and inline data, never caller pointers or hub method addresses.

Trusted bootstrap admits an immutable copied JSON manifest while a process is
READY, before its first thread starts. Version 1 has `resources`, each with a
`name`, `operations`, optional `scope`, and optional `publish`. Resource/operation
lists allocate on demand with no fixed count ceiling. ASCII identifiers and
ASCII Unicode escapes are supported; unknown/duplicate keys, malformed framing,
duplicate resources/operations and unsupported scopes fail closed. Only kernel
boot policy can admit manifests or grant an OS publication name. Installation,
signature verification, interactive approval and multiple user sessions remain
separate work; this is not an app-supplied self-authorization mechanism.

Named acquisition matches interface UUID and major/minimum-minor version, then
mints a process-local capability with a never-reused boot token. Another process's
token does not confer authority. Operation grants are frozen at acquisition;
reflection returns only those operations and the authenticated data scope.
Unscoped provider facts are withheld. `/@memory` grants only self-owned mappings;
`/@io` currently requires keyboard scope; `/@keyboard` clients require boot-input
scope. Keyboard delivery context carries the authenticated boot stream and
capability grant timestamp. The single boot input domain is explicit, not a multi-session input router.

Calls capture/validate input and the complete output range before dispatch.
Preallocated kernel I/O channels and FIFO request queues wake blocked provider/
client threads using existing wake credits. No broker lock spans provider code,
USB I/O, waits or bulk user-memory allocation. Both same-CPU and cross-CPU process
calls use this boundary. No ordinary resource syscall bypasses handle admission.
The C implementation of the system library lives under `sdk/`; interface headers
remain compile-time contracts, not direct resource implementations.

The transport limits each captured input to 15872 bytes and each reply to 16384
bytes. These bound messages, not files, allocation sizes or manifest counts. The
caller must offer at least a reply header before an operation may dispatch. A
larger result than its buffer returns the required size and the reply header with
the kernel-assigned request ID. Retrying that ID with the next attempt and identical
payload retrieves the retained result; it never silently dispatches again. A
changed payload, guessed request ID or a reply from another provider is rejected.
Request records live until explicit `cb_system_request_release(id)`, capability
release or caller exit. Forgetting a completed record relinquishes result replay;
busy records cannot be forgotten. Providers release internal completed clock/I/O
records so ongoing input does not accumulate a permanent request cache. There is no automatic
exactly-once guarantee or automatic side-effect retry. Explicit status/vow APIs
and transport implementations for remote providers are not implemented yet.

A deadline can stop a queued call before dispatch. A timeout after dispatch has
UNKNOWN outcome and preserves the captured request; a later provider result can
replace that provisional timeout. Errors include original payload and a redacted
broker trace frame, without kernel addresses. Authorized provider result data
(such as an INPUT_RESYNC anchor) remains alongside that error information. Provider-specific private trace text
is currently withheld. C callers receive statuses and structured frames; C++
exception/RAII wrappers remain future SDK work.

`handles.decommission(pid)` is a kernel-owned cooperative lifecycle operation:
it stops publication admission and queues a preallocated base-release event to
the provider, which can finish admitted work and shut down. It is not a forced
kill. `handles.inspect` copies publication identity and request counters for trusted
diagnostics; it does not expose other clients through application reflection.

Explicit publication release stops admission and fails queued calls, while already
dispatched work may finish. Provider exit removes discovery immediately and wakes
clients with NOT_DISPATCHED or UNKNOWN outcomes. Capability/request references keep
publication metadata alive until readers release it. Caller exit releases its
capabilities, captured requests and pending kernel events; process reap also runs
cleanup for admitted but never-started images. Safe automatic provider rebind and
restart policy are not implemented by this first transport milestone.

`test-manifest` and `test-handles` run focused UBSan grammar, reflection, token,
OOM rollback and lifetime checks, including preallocated event mailbox delivery.
`test-process-boot` adds real protected server/client ELF fixtures on disposable
ext4 media: scoped I/O, private memory allocation/release, denied reflection and
operations, forged tokens/IDs/delivery flags/replies, invalid buffers, retained
results and failure after provider exit. Four-CPU tests place the client on an AP.
Fixtures never enter candidate images.

Local acceptance: UBSan manifest/broker tests pass, including mailbox backpressure
and OOM cleanup. Protected process/handle boots pass on one CPU
(`367e08baf1eb47cbba4cc33f2f4bfb25`) and four CPUs/VT-d
(`01b61ef8068547feac50ef81bbe04d9e`) under TCG. The latter uses a
provider on the BSP and client on an AP. The subsequent keyboard acceptance exercises real emulated USB reports,
independent read/state grants, grant-time-filtered replay, unplug/replug and
cooperative shutdown through the production isolated provider. See
[the keyboard handler](../../handlers/keyboard/README.md).

Provider reply allocation failure does not mark an earlier provisional timeout as
a final provider completion. The reply can be retried; captured input and retained
reply state remain owned. Host OOM checks cover preserved prior result storage.
