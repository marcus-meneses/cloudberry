# Interactive sessions

The continuously running privileged `/@sessions` handler owns dynamic session
records. `self`, `begin` and `end` take no payload; `status` optionally takes a SID. `self` returns the authenticated
caller's `cb_session`. `begin` uses current kernel credentials, creates a session
owned by the caller PID, and records its parent session. It does not authenticate
or elevate. `accounts login` authenticates through `/@identity` first.

The process kmodule stores inherited parent PID and session membership separately
from the unchanged credential ABI. Only the catalog-bound session provider can
begin/close another process's session through IO. Ordinary callers cannot choose a
UID, owner PID or SID. Session IDs use monotonic process IDs and are not recycled.

`end` is owner-only. It marks the session subtree closed, rejects subsequent
launch/inheritance and requests cooperative stop of remaining members. Owner exit
also closes its subtree through preallocated owner-watch notifications. Closing revokes new handle acquisition/publication/invocation, queued requests and
blocked pipe/filesystem operations. Dispatched provider/backend work may finish;
collection/reply/release remain available for draining. Closing
retains running contexts and mappings until actual exit; this is not forced
termination of hostile or unresponsive applications. Closing records remain after owner exit while members drain. END returns a copied
status (members, running children, failed stop requests). STATUS permits the owner
UID or root to inspect a retained SID. Stop failures remain distinct from pending
drain; records are collected on subsequent service activity after drain, with
closed descendants reparented for cleanup.

`accounts login USER` starts an ordinary nested terminal at the verified existing
home; `accounts login USER APP [ARGS]` retains authenticated one-app execution.
The invoking terminal retains its own identity. Terminal exit logs out the nested
session and returns to the parent. Root boot remains session 0 with cwd `/`.

Keyboard focus belongs to the keyboard handler. A descendant may push the current
owner; release/death restores the nearest live saved owner, with generation/cursor
barriers. Closed-session pending reads are revoked when served, and closed saved
owners are skipped on restoration. Unrelated claims fail BUSY. Each terminal holds focus while feeding its
ordinary foreground byte stdin. Direct keyboard consumers temporarily take it.
Mouse remains independent inside the foreground session, with cross-session reads
rejected and replay history reset on focus transitions. Input is not redirected
through a terminal event broker. stdin/stdout/stderr remain directional byte
streams bound at launch, including pipelines and file redirection.

One shared text/display console remains: this is not a secure compositor, login
display manager, multi-seat system, persistent session manager or thread migration.
Membership/group/password changes do not revoke existing credential snapshots.
