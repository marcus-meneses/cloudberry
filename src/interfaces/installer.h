#ifndef CLOUDBERRY_INTERFACE_INSTALLER_H
#define CLOUDBERRY_INTERFACE_INSTALLER_H
#include "registry.h"
#define CB_INSTALLER_NAME "/@installer"
#define CB_INSTALLER_INTERFACE_ID {{0x43,0x42,0x49,0x4e,0x53,0x54,0x41,0x31,0x91,0xe1,0x40,0x0a,0x1c,0x87,0xc0,0x17}}
enum {CB_INSTALLER_INSPECT=1,CB_INSTALLER_INSTALL,CB_INSTALLER_REMOVE,CB_INSTALLER_LIST,CB_INSTALLER_STATUS};
enum {CB_INSTALL_SCOPE_AUTO=0,CB_INSTALL_SCOPE_SYSTEM,CB_INSTALL_SCOPE_USER};
enum {CB_INSTALL_REUSE_DATA=1,CB_INSTALL_DELETE_DATA=2};
struct cb_installer_request {uint64_t scope,options,name_bytes,offset;unsigned char expected_hash[64];};
/* Inline canonical bundle path (inspect/install) or registered friendly name
 * (remove). Install requires the SHA-512 fingerprint returned by INSPECT.
 * Signature/key/name/manifest/resources are verified before touching storage.
 * Caller scope is derived from authenticated PID, never a supplied UID.
 * Existing data requires explicit REUSE or DELETE; removal keeps data unless
 * DELETE is supplied. Install replacement is BUSY: remove an inactive app first. */
struct cb_bundle_info {uint64_t name_bytes,manifest_bytes,code_bytes,resources,flags,stack_bytes,next;unsigned char installation[16],fingerprint[64];};
/* INSPECT returns info + name + manifest chunk beginning at request.offset.
 * next==manifest_bytes ends inspection. LIST takes an eight-byte ordinal and
 * returns cb_registry_entry + name, dynamic registrations only, or EOF.
 * STATUS takes no payload, returns four u64: generation, recovery_required,
 * last_phase, last_status. Phase zero means no retained operation failure. */
#define CB_BUNDLE_HEADER 112
#define CB_BUNDLE_SIGNATURE 64
#endif
