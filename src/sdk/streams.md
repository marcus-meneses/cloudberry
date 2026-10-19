# Standard streams and pipes

`/@streams` is a kernel I/O buffering mechanism, not a terminal or rendering
service. Acquire its factory with a manifest granting pipe/read/write. `pipe`
returns separate directional process-local handles; their reflection exposes only
read or write. Buffers are bounded to 16 KiB, copied messages to 4 KiB per transfer.
Reads and writes may return prefixes. Empty readers block while writers exist;
full writers block while readers exist. Nonblocking calls return WOULD_BLOCK.
The last writer closing produces EOF after draining; losing every reader causes
BROKEN_PIPE. Zero-byte transfers and cleanup follow the same ownership rules.

Close endpoints with `cb_system_release`. Process reap closes remaining handles.
Queued readers/writers register stack-owned wait records under the kernel lock,
release it before blocking, and use wake credits to prevent lost wakes. No ISR
allocation, scheduler polling or unbounded buffer growth is involved.

Launch ABI 1.1 adds three parent-local endpoint handles to the launch header.
The application handler uses authenticated parent identity. Before starting a
READY child, the kernel validates source direction/ownership and the child's
immutable stream manifest, then creates new child-local capabilities. Delegation
rolls back completely on failure. Parent tokens are cleared from copied startup
metadata. The child obtains its bound endpoints with `cb_standard(0/1/2)`; absent
bindings return zero. There are no global stdin/stdout/stderr resource names.
The installed `launch` app explicitly forwards its own bindings.

`stream.h` supplies bounded read/write, write-all, filesystem-call and lexical
path helpers. Zero-initialize each `cb_client`; keep a separate client for each pending call.
`service.h` handles copied invocation/publication/reply and result
release. A timed-out pending provider request is retained for retry on the same
client/operation/payload; a different operation or payload returns BUSY without
changing the pending frame. It is not silently forgotten or resubmitted. The
terminal uses a separate keyboard client for those pending reads.

Files use the same stream interface. Their backing descriptor retains a copied
path and offset; ext4 work executes on CPU0 and callers block through owned worker
joins. There is no generic remote streaming, seek, descriptor remapping after
launch or durability relaxation in this milestone.


Wait-any uses `cb_system_wait(items, count, absolute_deadline_ns, &ready_index)`.
A zero deadline waits indefinitely. Items name caller-owned submitted requests
(`CB_WAIT_REQUEST`) or directional local stream handles (`CB_WAIT_READ/WRITE`).
Readiness is level-triggered and non-consuming: buffered data or EOF makes a read
ready; capacity or a broken reader makes a write ready. Actual operations still
report their own results/errors. The entire list is validated, even when an
earlier item is ready. Readiness does not reserve data or capacity for the caller.

`cb_submit` queues a copied provider call in one `cb_client`, preserving ordinary
manifest admission and the provider's existing method/payload. `cb_collect` gets
its result without blocking or resubmitting, returning WOULD_BLOCK until ready.
Collection does not consume the broker result; the SDK releases completed request
storage after successful retrieval. Buffer/allocation failures retain it for retry.
This split-phase transport does not add provider async methods or vow handles.
Kernel operations and reflection remain synchronous; submitted calls currently
require no call deadline. Wait deadlines bound waiting, never cancel a request
or discard its original outcome. Process exit reclaims owned requests/endpoints.

The broker registers one stack-owned waiter under its existing lock, resolves
IDs on every wake and uses scheduler wake credits across unlock/block. It needs
no polling timer, ISR allocation, or direct keyboard dependency. One list is
bounded by the transport frame size; registrations exist only during the wait.
