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
