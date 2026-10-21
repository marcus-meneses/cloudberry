#ifndef CLOUDBERRY_EXT4_FS_H
#define CLOUDBERRY_EXT4_FS_H
#include "block.h"
enum ext4_error {EXT4_OK,EXT4_ARGUMENT,EXT4_MEMORY,EXT4_IO,EXT4_CORRUPT,EXT4_UNSUPPORTED,EXT4_NOT_FOUND,EXT4_TYPE,EXT4_BUSY,EXT4_READ_ONLY,EXT4_NO_SPACE,EXT4_EXISTS,EXT4_NOT_EMPTY};
enum ext4_mount_flags {EXT4_MOUNT_WRITE=1};
enum ext4_write_flags {EXT4_WRITE_CREATE=1,EXT4_WRITE_EXCLUSIVE=2,EXT4_WRITE_APPEND=4};
enum ext4_mutation {EXT4_MUTATE_CREATE=1,EXT4_MUTATE_TRUNCATE,EXT4_MUTATE_REMOVE,EXT4_MUTATE_MKDIR,EXT4_MUTATE_RMDIR,EXT4_MUTATE_RENAME,EXT4_MUTATE_OWNER,EXT4_MUTATE_MODE};
struct ext4_stat {uint64_t inode,bytes;uint32_t mode,flags,uid,gid;};
struct ext4_dirent {uint64_t inode;uint32_t type,name_bytes;char name[256];};
struct ext4_info {uint64_t cookie,blocks,inodes;uint32_t block_bytes,inode_bytes,error,readonly;uint8_t uuid[16];};
/* init(hub,heap); mount(partition block,out info,flags) -> cookie; unmount(cookie).
 * stat(cookie,absolute_path,out,capacity): capacity=0 preserves the original
 * 24-byte result; capacity>=sizeof(ext4_stat) additionally copies UID/GID. read(cookie,path,offset,buffer,capacity,0)
 * returns bytes (possibly a prefix on I/O failure), clamps at EOF.
 * list(cookie,path,inout byte cursor,out entry): value 1=entry, 0=EOF.
 * info(cookie,out). Mount retains parent until unmount; no borrowed buffers.
 * BSP-thread synchronous I/O. Symlink traversal is unsupported.
 * Read-only mounts require clean extent-based 1/2/4 KiB filesystems; explicit
 * writable mounts replay an internal journal. Unsupported features fail closed.
 */
struct kernel_surface_result ext4_fs_surface_init(MODULE_ARGS);
struct kernel_surface_result ext4_fs_surface_mount(MODULE_ARGS);
struct kernel_surface_result ext4_fs_surface_unmount(MODULE_ARGS);
struct kernel_surface_result ext4_fs_surface_stat(MODULE_ARGS);
struct kernel_surface_result ext4_fs_surface_read(MODULE_ARGS);
struct kernel_surface_result ext4_fs_surface_list(MODULE_ARGS);
struct kernel_surface_result ext4_fs_surface_info(MODULE_ARGS);
/* mount(block,out,flags): default 0 is read-only. WRITE requires block ABI1.1
 * write+flush and an internal journal. write(cookie,path,offset,source,bytes,flags)
 * returns a durable completed prefix on success. Mutate(cookie,operation,path,
 * size,destination,0); sync(cookie). Failed write/barrier can have unknown on-disk
 * outcome; never infer rollback. See ext4-fs.md for recovery/lifetime semantics. */
struct kernel_surface_result ext4_fs_surface_write(MODULE_ARGS);
struct kernel_surface_result ext4_fs_surface_mutate(MODULE_ARGS);
struct kernel_surface_result ext4_fs_surface_sync(MODULE_ARGS);
/* disconnect(root read surface,root cookie): invalidate then drain/abort without writes.
 * Retry busy cleanup; stale identities never reconnect. */
struct kernel_surface_result ext4_fs_surface_disconnect(MODULE_ARGS);
#endif
