#ifndef CLOUDBERRY_INTERFACE_IDENTITY_H
#define CLOUDBERRY_INTERFACE_IDENTITY_H
#include "handle.h"
#include "credentials.h"
#define CB_IDENTITY_NAME "/@identity"
#define CB_IDENTITY_INTERFACE_ID {{0x43,0x42,0x49,0x44,0x45,0x4e,0x54,0x31,0x91,0xe1,0x40,0x0a,0x1c,0x87,0xc0,0x15}}
enum {CB_IDENTITY_INFO=1,CB_IDENTITY_USERS,CB_IDENTITY_GROUPS,CB_IDENTITY_BEGIN,CB_IDENTITY_STAGE,CB_IDENTITY_COMMIT,CB_IDENTITY_ABORT,CB_IDENTITY_CREDENTIAL,CB_IDENTITY_SELF,CB_IDENTITY_AUTHENTICATE,CB_IDENTITY_PASSWORD};
enum {CB_IDENTITY_USERS_FILE=0,CB_IDENTITY_GROUPS_FILE=1,CB_IDENTITY_CREDENTIALS_FILE=2};
/* File payloads are explicit little-endian encoded bytes, never these ABI structs.
 * INFO and listing omit credentials. BEGIN/STAGE/COMMIT/ABORT require distinct
 * manifest grants. IDs are monotonically allocated, never recycled. */
struct cb_identity_info {uint64_t generation,next_uid,next_gid,users,groups;uint32_t healthy,reserved;};
struct cb_identity_list {uint64_t cursor;};
/* LIST result: next byte cursor uint64 followed by up to 4096 encoded file
 * bytes (including its header). Start at cursor 0; EOF returns zero bytes. */
struct cb_identity_begin {uint64_t generation,bytes[3];};
/* bytes[k]=0 preserves that file and advances its generation internally.
 * bytes[2] must be zero: credentials are edited by CREDENTIAL, never exported
 * or replaced wholesale by a client. Algorithm 0 removes a non-root credential;
 * algorithm 1 supplies PBKDF2-HMAC-SHA256, rounds>=600000, 32-byte salt/hash. */
struct cb_identity_credential {uint64_t transaction,uid;uint32_t algorithm,rounds;unsigned char salt[32],hash[32];};
struct cb_identity_transaction {uint64_t transaction;};
struct cb_identity_stage {uint64_t transaction,offset;uint32_t file,reserved;};
/* STAGE appends inline bytes at the exact expected offset. COMMIT is a
 * generation-checked three-file transaction; I/O errors may have committed.
 * Never blindly retry after a storage failure: restart/reload to recover. */
/* AUTHENTICATE: password frame followed by UTF-8 secret (1..64 Unicode
 * scalars, <=256 bytes); replaces only authenticated caller credentials.
 * PASSWORD: same frame; root/admin may set accounts, root only sets root.
 * Non-admin may change its own password only with old secret supplied first.
 * No passwords in argv. SELF returns cb_credentials and supplementary GIDs. */
struct cb_identity_password {uint64_t uid;uint32_t old_bytes,password_bytes;};
#endif
