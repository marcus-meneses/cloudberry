# GPT partition kmodule

`partition` is loaded through Limine alongside USB storage and ext4. It consumes
[the common block contract](block.h), not USB or SCSI structures.
`partition.find(parent, guid, out_block, capacity)` selects exactly one GPT unique partition
GUID in the explicitly supplied device. GUID bytes match `config_guid`/GPT layout.
It does not select a disk by enumeration order or assume that partition 4 is special.

Discovery validates primary and backup GPT headers and entry-array CRC32s, matching
geometry/identity, header locations, usable bounds and selected partition overlap.
Absent/duplicate GUIDs, bad CRCs and out-of-range extents fail. Entries are scanned
with a bounded 4 KiB cache; no full-disk/table-sized allocation is made. GPT logical
sector size comes from the backend. There is no MBR fallback or host formatting.

The returned descriptor translates read/write sector offsets and enforces partition
bounds, forwards flush barriers, and preserves root transport identity/range for
filesystem overlap checks. Capacity zero returns the original read-only ABI1.0
prefix; pass sizeof(struct block_device) for ABI1.1 write/flush and identity fields.
It retains the parent until `partition.close(cookie)`. `partition.control` supplies
lifetime retain/release for filesystem consumers; close refuses retained/busy
instances. Monotonic cookies reject stale descriptors. Per-slice owned state has
4 KiB scratch and a 4 KiB entry cache. Reads and lifecycle calls require BSP threads
with IRQs enabled; metadata locks never cover backend I/O.

Discovery validates a single selected device. The future driver manager must search
available devices and reject ambiguous matches across disks, including cloned images
with identical GUIDs. This module neither performs that policy nor claims to prove
Limine boot-volume identity. It never changes GPT or writes outside the selected partition.

`disconnect(root_read,root_cookie)` permanently invalidates every matching
slice. New reads/writes/flushes/retains fail; existing calls drain before freeing.
Unretained idle views close immediately. Externally retained dead views keep
only their reference/lifetime chain; their final release closes them and releases
the transport. Replacement media requires a new GUID selection and new cookie.
