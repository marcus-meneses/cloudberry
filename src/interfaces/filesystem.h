#ifndef CLOUDBERRY_INTERFACE_FILESYSTEM_H
#define CLOUDBERRY_INTERFACE_FILESYSTEM_H
#include "handle.h"
#define CB_FILESYSTEM_NAME "/@filesystem"
#define CB_FILESYSTEM_INTERFACE_ID {{0x43,0x42,0x46,0x53,0x59,0x31,0x46,0x3b,0x91,0xe1,0x40,0x0a,0x1c,0x87,0xc0,0x0b}}
enum {CB_FILESYSTEM_OPEN=1,CB_FILESYSTEM_STAT,CB_FILESYSTEM_LIST,CB_FILESYSTEM_REMOVE,CB_FILESYSTEM_MKDIR,CB_FILESYSTEM_METADATA,CB_FILESYSTEM_CHMOD,CB_FILESYSTEM_CHOWN,CB_FILESYSTEM_RMDIR};
enum {CB_FILE_READ=1,CB_FILE_RECREATE,CB_FILE_APPEND};
struct cb_filesystem_request {uint32_t mode,path_bytes;uint64_t cursor;};
/* Inline canonical absolute UTF-8 path, without NUL. LIST returns one entry,
 * cursor for the next call, and zero result bytes at EOF. OPEN returns a stream
 * capability owned by the authenticated caller; not a provider-local token. */
struct cb_filesystem_stat {uint64_t bytes;uint32_t directory,reserved;};
struct cb_filesystem_entry {uint64_t cursor;uint32_t directory,name_bytes;char name[256];};
/* METADATA preserves STAT ABI. CHMOD uses request.mode (0777); CHOWN appends
 * cb_filesystem_owner after the path. Both require cursor=0. */
struct cb_filesystem_metadata {uint64_t bytes,uid,gid;uint32_t mode,directory;};
struct cb_filesystem_owner {uint64_t uid,gid;};
#endif
