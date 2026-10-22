# Handler failure and recovery contract

Kmodules are coupled kernel mechanisms. These rules describe isolated userland
handlers and their publications, not live replacement of arbitrary kernel code.

## Failure, requests and identity

A failed provider disappears from named discovery. Existing capabilities retain
the old publication and epoch; they do not silently resolve a replacement. Calls
to a failed publication fail PROVIDER_FAILED; deliberate retirement can report
REVOKED. A fresh acquisition after explicit restart names a new publication/epoch
and repeats manifest authorization/reflection. Matching a name alone does not
prove implementation compatibility or state continuity.

Queued operations fail with NOT_DISPATCHED. Dispatched operations whose completion
is lost report OUTCOME_UNKNOWN, retaining the original copied payload and trace.
A completed reply keeps its reported outcome. Caller death removes its records
only after the syscall continuation returns; a provider's later reply to a removed
request ID fails BAD_REQUEST. No automatic retry is performed for unknown outcomes.
Request IDs correlate calls; they are not persistent cross-restart deduplication
receipts and must not be treated as proof that a side effect did not happen.

Explicit restart follows: withdraw/decommission, drain kernel/device work, reach
thread exit, reap the old process and release bootstrap ownership, load a fresh
compatible installation, rebind its permitted kernel interfaces and publish a new
epoch. Only then may clients reacquire. No executing mapping/stack is reused early.
Failure to drain keeps resources owned and is an error, not permission to free them.

## Recovery eligibility

| Provider | State location and required recovery decision |
| --- | --- |
| Keyboard / mouse | Kernel HID transport and physical device can be re-enumerated; decoder/focus/event sequence state must be re-established. Clients must tolerate explicit reset/resynchronization and fresh grants. Keyboard fault/restart is guest-tested; mouse restart is not thereby qualified. |
| Filesystem frontend | Mount/file execution lives behind kernel filesystem/storage mechanisms. Restart still needs rebinding and admission; interrupted mutations retain their recorded outcome. No transparent recovery claim from frontend statelessness alone. |
| Display / graphics / text | Device/surface/backing/cursor state is split across providers. Loss of a provider is not proof that all visible state can be reconstructed. A coordinated reset/repaint or retained-state protocol is required before automatic recovery. |
| Identity / installer | Versioned binary databases and transaction journals support startup recovery. Reloading durable data does not prove an interrupted request was not committed; inspect durable state/outcomes before retry. Installer containment can exclude dynamic apps while built-in recovery remains available. |
| Sessions / applications manager | Live ownership, waiters, nested sessions and child results need reconstruction/draining. Manager exit currently requests cooperative child stop and can wait on an uncooperative child. Explicit child termination exists; automatic manager replacement is not implemented. |

This milestone keeps restart explicit and fail-closed. It does not enable a generic
automatic supervisor or promise recovery for stateful/essential providers. The
long-term interchangeable-handler goal remains; automatic reconnection will need
validated identity/version/state continuity and defined handling of in-flight work.

## Validation boundary

The isolated process suite covers keyboard failure during active HID/read work,
original payload and UNKNOWN outcome, quiescent release and two fresh restart
cycles. Broker/concurrency checks cover queued versus dispatched deadlines,
retained/late replies and unrelated client progress. Execution tests add forced
caller death during a dispatched request and reject its late provider reply.
A real-broker host test republishes the same name under a new provider/epoch,
rechecks filtered grants and verifies the old capability fails against its old
publication. These tests qualify these paths, not every provider or arbitrary
disk power loss.
