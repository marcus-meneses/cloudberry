#ifndef CLOUDBERRY_INTERFACE_SESSIONS_H
#define CLOUDBERRY_INTERFACE_SESSIONS_H
#include "handle.h"
#define CB_SESSIONS_NAME "/@sessions"
#define CB_SESSIONS_INTERFACE_ID {{0x43,0x42,0x53,0x45,0x53,0x53,0x49,0x31,0x91,0xe1,0x40,0x0a,0x1c,0x87,0xc0,0x16}}
enum {CB_SESSIONS_SELF=1,CB_SESSIONS_BEGIN,CB_SESSIONS_END};
/* All requests are empty. BEGIN creates a session for the authenticated caller,
 * preserving its current credentials. Descendants inherit the session. END is
 * owner-only and closes descendant sessions too. Stop is cooperative; executable
 * mappings remain alive until processes actually exit. Session 0 is boot. */
struct cb_session {uint64_t id,parent,owner,uid;};
#endif
