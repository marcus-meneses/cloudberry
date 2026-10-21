#ifndef CLOUDBERRY_INTERFACE_SESSIONS_H
#define CLOUDBERRY_INTERFACE_SESSIONS_H
#include "handle.h"
#define CB_SESSIONS_NAME "/@sessions"
#define CB_SESSIONS_INTERFACE_ID {{0x43,0x42,0x53,0x45,0x53,0x53,0x49,0x31,0x91,0xe1,0x40,0x0a,0x1c,0x87,0xc0,0x16}}
enum {CB_SESSIONS_SELF=1,CB_SESSIONS_BEGIN,CB_SESSIONS_END,CB_SESSIONS_STATUS,CB_SESSIONS_REVOKE_UID};
/* SELF/BEGIN requests are empty. Empty END closes the owner session. BEGIN creates a session for the authenticated caller,
 * preserving its current credentials. Descendants inherit the session. END is
 * owner-only and closes descendant sessions too. Stop is cooperative; executable
 * mappings remain alive until processes actually exit. Session 0 is boot. */
struct cb_session {uint64_t id,parent,owner,uid;};
struct cb_session_status {struct cb_session session;uint64_t members,running,stop_failures;uint32_t closing,close_error;};
/* END optionally takes an eight-byte SID for root/admin targeted close/retry;
 * SID 0 is protected. END returns status after revocation. STATUS takes empty (self) or an eight
 * byte SID; other SIDs require matching owner UID or root. Running counts other
 * members (owner excluded). Stop failures are distinct from a pending drain. */
/* REVOKE_UID takes a nonzero uint64 UID, requires root/admin and closes all
 * matching live processes plus their session subtrees. Repeat to retry failed
 * cooperative stops. Returns cb_session_status with session.uid=target; kernel
 * membership counts cover that UID. END/REVOKE_UID may return an error with a
 * status payload; close_error preserves the close error independently of drain.
 * No implicit credential rewriting or forced termination. */
#endif
