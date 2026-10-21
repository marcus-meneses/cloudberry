#ifndef CLOUDBERRY_KMODULE_FILESYSTEM_H
#define CLOUDBERRY_KMODULE_FILESYSTEM_H
#include "module.h"
#include "block.h"
#include "../../interfaces/filesystem.h"
/* Trusted kernel backend ABI2; minor 1 adds empty-directory RMDIR. A <provider>.fs_backend surface copies a
 * descriptor to (out,capacity). invoke(cookie,request) completes synchronously
 * on a BSP thread, with cb_status in request.status; no borrowed request buffers.
 * PROBE returns CB_NOT_FOUND for a different format, an error for failed I/O.
 * MOUNT returns a nonzero cookie and retains its parent block view until
 * successful UNMOUNT; UNMOUNT must retain the cookie on failure.
 * READ/WRITE set actual (including a completed prefix on error); LIST returns
 * actual=0 at EOF or 1 with entry/cursor. PREPARE validates/creates/truncates.
 * Calls never retry writes automatically; errors may have unknown disk outcome.
 */
enum filesystem_operation {FS_PROBE=1,FS_MOUNT,FS_UNMOUNT,FS_STAT,FS_LIST,FS_READ,FS_WRITE,FS_PREPARE,FS_REMOVE,FS_MKDIR,FS_SYNC,FS_EXEC,FS_CHMOD,FS_CHOWN,FS_RMDIR};
enum {FS_MOUNT_WRITE=1,FS_REGULAR=1,FS_DIRECTORY=2,FS_ACCESS_SEARCH=0x10000};
struct filesystem_stat {uint64_t bytes;uint32_t type,reserved;uint64_t uid,gid;uint32_t permissions,valid;};
struct filesystem_dirent {uint32_t type,name_bytes;char name[256];};
struct filesystem_request {
    uint32_t operation,status;const char *path;void *buffer;size_t bytes,actual;
    uint64_t offset,cursor,cookie;uint32_t mode,flags;
    struct filesystem_stat stat;struct filesystem_dirent entry;
    const struct block_device *device;
    uint64_t process,uid,gid; /* process=0 is trusted kernel access. */
};
struct filesystem_backend {uint32_t bytes;uint16_t major,minor;kernel_surface_call invoke;};
/* register(query); mount(partition_block,mount_path,flags)->mount ID (probe all
 * registered formats); adopt(query,cookie,mount_path)->borrowed mount ID.
 * unmount(ID) fails BUSY while referenced/active and never unmounts adopted boot
 * media. file(ID,request) uses a path relative to that mount, starting '/'.
 * retain/release pin a mount for open streams. Registration pins backend code
 * until reboot; no live backend replacement/unregistration is supported.
 * Mount paths must already exist in the parent namespace for LIST visibility.
 */
struct kernel_surface_result filesystem_surface_init(MODULE_ARGS);
struct kernel_surface_result filesystem_surface_invoke(MODULE_ARGS);
struct kernel_surface_result filesystem_surface_bind(MODULE_ARGS);
struct kernel_surface_result filesystem_surface_register(MODULE_ARGS);
struct kernel_surface_result filesystem_surface_mount(MODULE_ARGS);
struct kernel_surface_result filesystem_surface_adopt(MODULE_ARGS);
struct kernel_surface_result filesystem_surface_unmount(MODULE_ARGS);
struct kernel_surface_result filesystem_surface_file(MODULE_ARGS);
struct kernel_surface_result filesystem_surface_retain(MODULE_ARGS);
struct kernel_surface_result filesystem_surface_release(MODULE_ARGS);
#endif
