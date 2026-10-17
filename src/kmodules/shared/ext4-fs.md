# Ext4 filesystem kmodule

`ext4-fs` remains a Limine-loaded kernel module. Its frontend consumes a retained
[block descriptor](block.h), normally a GPT partition, and knows nothing about USB,
NVMe, xHCI or DMA. Allocation and journal execution use privately linked
[lwext4 sources](ext4-fs/vendor/README.md), freshly pinned from upstream. The public
hub contract remains Cloudberry's C interface; library objects never become ABI.

## Surfaces

- `init(hub,heap)` initializes dependencies without opening a disk.
- `mount(block,out_info,flags)` returns a cookie. Flags zero retains the existing
  read-only default; `EXT4_MOUNT_WRITE` explicitly requests journaled writes.
- `stat(cookie,absolute_path,out)` returns inode, byte size, mode and flags.
- `read(cookie,path,offset,buffer,capacity)` returns bytes and clamps at EOF.
- `list(cookie,path,inout_cursor,out_entry)` advances an opaque cursor; value zero
  means EOF. Names have at most 255 bytes per component.
- `write(cookie,path,offset,source,bytes,flags)` writes/grows an existing file.
  Flags are CREATE, EXCLUSIVE (with CREATE), and APPEND (offset ignored). Source
  buffers are never retained or changed. Creation requires an existing parent.
- `mutate(cookie,operation,path,size,destination)` implements CREATE, TRUNCATE
  (shrink or grow), REMOVE, MKDIR, RMDIR (empty only), and RENAME (no replacement).
  Rename and removal use a single metadata transaction; the namespace stays within
  the selected mount. Paths are normalized within its root; no cross-volume rename.
- `sync(cookie)` drains cache and issues a backend durability barrier.
- `unmount(cookie)` stops the journal, checkpoints, marks clean, releases the parent
  and frees owned state. `info(cookie,out)` reports geometry, readonly state/error.

Writable mounts require block ABI 1.1 write/flush, supported geometry and an
internal journal. Mount rejects overlapping views if either is writable, including
separately opened GPT slices carrying the same root device/range identity. A failed
mount can return an info.cookie for a retained poisoned instance; see failure rules.

## Journal and durability

Writable mount replays an outstanding journal before accessing affected inodes,
then starts journaling. The pinned engine is locally adapted for bounded full-data
journal transactions: file data and metadata go through the log before checkpoint.
A write is split into at most 64 KiB transactions; this is not a file-size quota.
Growing a gap or truncating upward currently allocates and writes zero data in
bounded pieces rather than creating sparse extents. Existing sparse/unwritten
extents read as zeros, and partial writes into holes initialize their full blocks.

Initially every physical backend write is followed by flush. This conservatively
orders log data, commit records, checkpoints and journal-tail updates. Successful
mutations/sync/unmount are acknowledged after barriers. It costs additional USB
commands; batching must preserve those ordering boundaries and is deferred.
Large writes/growth/truncation can span transactions; a failed operation can leave
an already committed prefix. There is no implicit retry or whole-call rollback.

I/O, barrier or dirty-transaction failure poisons the mounted instance. Further
operations are refused. Normal unmount refuses poisoned state. An explicit
`disconnect(root_read,root_cookie)` stops new calls, lets an active call drain,
unlinks library registry roots and discards the mount allocation set without
I/O, checkpoint, flush or rollback. Busy cleanup is retryable. A USB lifecycle
worker invokes this on removal, then releases dependent slices. A fresh explicit
mount replays the on-disk journal; no old cookie reconnects to replacement media.
Without disconnect/abort, poisoned resources stay retained for safe inspection.
Failure is not proof that no bytes reached disk.

## Supported layouts and boundaries

The frontend validates superblock checksums, geometry and feature masks before
library execution. Supported blocks are 1/2/4 KiB and inodes 128/256/512 bytes;
64-bit descriptors, flex_bg, extent trees, metadata checksums and HTree directories
are supported. Read-only mounts use the same engine, except explicit checksum-seed
filesystems retain the qualified read-only parser. Writable checksum-seed layouts
are refused until the private engine supports them. The image builder pins a
compatible feature set; it never disables features on an existing disk at boot.

Writable filesystem blocks must be at least the backend logical sector size.
External journals, orphan-list recovery, encrypted/inline/bigalloc/unsupported
layouts, symlink traversal and feature conversion remain unsupported. Actual
filesystem-error flags require repair outside this module. Read-only mount still
refuses dirty/recovery-needed filesystems; recovery requires explicit writable
mount. Wall-clock timestamp/ownership policy and application-facing handles remain
future layers. The existing NVMe reader has not acquired a write backend.

All calls run synchronously on BSP threads with IRQs enabled. A per-volume busy
flag and private engine serialization refuse overlapping execution; CPU metadata
locks never span disk waits. Mount/device registries and cache/journal buffers
are dynamically owned rather than fixed two-slot upstream arrays. Each volume
retains its block descriptor; normal unmount reclaims its allocations.

## Validation

Focused UBSan tests cover normal mutations, sparse reads/zero growth, metadata
integrity, ABI/bounds, lifetime pins and freestanding byte/sort helpers. Generated
images are independently checked with e2fsck after unmount and recovery. Abrupt
host-process cuts at descriptor/commit/checkpoint boundaries test creation, data
writes, append, truncate, remove and rename. Injected write/barrier failures test
poisoning, parent retention and fresh-process recovery. These model whole stable
block writes; torn sectors and physical devices are not qualified.

Isolated QEMU boots exercise the actual USB BOT WRITE(10)/(16) and SYNCHRONIZE
CACHE path through GPT to ext4, including read-only remount and post-run host fsck,
with Intel/AMD IOMMU models and one/four CPUs. The ordinary boot medium remains
read-only; only explicitly requested test targets make disposable images writable.
No formatting, migration or automatic filesystem mount was added to kernel boot.

References: [Linux ext4 format](https://www.kernel.org/doc/html/latest/filesystems/ext4/index.html),
[lwext4 upstream](https://github.com/gkostka/lwext4).

Disconnect validation injects removal during a journal write for three cycles,
checks zero abort writes/flushes, keeps a retained slice invalid until final
release, verifies all allocation/reference counts return to zero, and remounts
the journal under fresh cookies. The existing 29 crash cuts and six I/O failure
cases still pass. Guest validation removes/reinserts writable USB storage.
