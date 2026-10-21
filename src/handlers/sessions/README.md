# Interactive sessions

The continuously running privileged `/@sessions` handler owns dynamic session
records. `self` and `begin` take no payload; empty `end` closes the owner session; `status` optionally takes a SID. `self` returns the authenticated
caller's `cb_session`. `begin` uses current kernel credentials, creates a session
owned by the caller PID, and records its parent session. It does not authenticate
or elevate. `accounts login` authenticates through `/@identity` first.

The process kmodule stores inherited parent PID and session membership separately
from the unchanged credential ABI. Only the catalog-bound session provider can
begin/close another process's session through IO. Ordinary callers cannot choose a
UID, owner PID or SID. Session IDs use monotonic process IDs and are not recycled.

Empty `end` is owner-only; an explicit nonzero SID requires root/admin. It marks the session subtree closed, rejects subsequent
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
Membership/group/password changes retain existing credential snapshots unless an administrator explicitly revokes them through `revoke_uid`.

## Explicit revocation and close failures

`revoke_uid` takes a nonzero numeric UID. The handler queries the requesting
process's kernel credentials: root or primary/supplementary admin GID 1 is required.
It closes sessions owned by that UID and their descendants, then revokes every
live process carrying that UID, including authenticated processes still in boot
session 0. Other session-0 processes remain open. UID 0 cannot be selected as the revocation target;
its boot session remains open. Descendant sessions are still contained by their
parent session, even if a descendant later authenticates as a different UID. Revoked processes cannot replace credentials or launch children.

The kernel marks matching processes before dispatching cooperative stop requests.
Already dispatched work may finish; running code/mappings remain until actual exit.
The result is `cb_session_status` with `session.uid` set to the target. Its UID
counts do not include differently authenticated descendant sessions; those are
still closed by the session-subtree pass. This is a point-in-time revocation, not
an account ban. Disable the account first to prevent subsequent authentication.

Close failures are retained in `close_error` (the former reserved u32, preserving
64-byte ABI size). END preserves its original close error even if collecting the
drain report also fails. When a report is available it accompanies the error
reply; STATUS also exposes retained errors. Missing records fail NOT_FOUND rather
than being dereferenced. Root/admin can repeat `revoke_uid` to retry stops without
re-enabling any capability. Successful retry clears the retained close error;
finished processes do not accumulate false stop failures. An owner whose END has
already revoked its capabilities cannot issue a new request; an administrator
performs the retry. No automatic busy retry loop runs in the handler.

## Foreground console boundary

The keyboard handler publishes focus to kernel IO. Text, graphics and display
frontends authorize the broker-authenticated delivery PID through their own
installation-bound `console_access` operation before touching screen state.
Only the foreground session and its focused launch lineage may draw. Ancestors
in that same session remain able to drain stdout and render password prompts;
unrelated branches and other sessions are denied. A queued request is checked
when the frontend serves it; an already authorized operation may finish across
a focus change. Closed processes are rejected. Before the first focus only boot
session 0 may draw; after focus has existed, a missing owner fails closed.

Only the bound text provider may use the graphics upstream exemption, and only
the bound graphics provider may use the display upstream exemption. Ordinary
apps cannot manufacture these identities or query authority for arbitrary PIDs.
Offscreen surfaces retain owner-local access without foreground ownership, but
copying screen pixels into one requires foreground authority. This protects a
single shared console; it is not a secure-attention mechanism or a compositor.

Administrators can also send END an explicit nonzero SID (`accounts end-session SID`)
to close or retry a particular session, including a non-boot root session. SID 0
cannot be closed through this path. Empty END retains owner-only behavior.
