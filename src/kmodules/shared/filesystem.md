# Format-neutral filesystem manager

`filesystem` is a trusted kernel module, separate from the privileged userland
filesystem handler and from the actual format implementation `ext4-fs`. Its
contract is `filesystem.h`. The handler continues publishing `/@filesystem`;
applications and their manifests do not select an on-disk format.

At initialization the manager discovers published `<provider>.fs_backend` queries.
Each copies an ABI2 descriptor containing its mediated invocation entry. Both
published code owners are pinned before the descriptor is retained. Additional
backends can register through `filesystem.register(query)`. Registry storage is
dynamic; registrations remain resident until reboot. Runtime backend unloading
and replacing a mounted backend are not supported by this contract.

`filesystem.mount(partition_block,absolute_mount_path,flags)` asks every registered
backend to PROBE the supplied partition view. A different format returns
CB_NOT_FOUND; read failure propagates. No match fails explicitly; multiple matches
are rejected rather than selecting by load order. Exactly one match receives
MOUNT, with explicit write permission. The returned nonzero backend cookie stays
private to its mount. Ext4 and read-only ISO9660 are the production backends.
Adding another format requires its own implementation of the same contract.

Partition discovery stays in the storage/partition modules and boot policy. The
filesystem manager does not scan arbitrary disks or automatically mount every
partition. A caller supplies each selected partition block view; its transport
may be USB, NVMe or SATA. Different formats can coexist in the same mount table.

Mounts and paths are dynamically allocated. Longest component-prefix matching
routes namespace paths, stripping the mount prefix before invoking the backend.
Mount points must exist in the parent filesystem for normal directory-listing
visibility; the manager does not create directories or fabricate LIST entries.
There is no userland mount-management operation in this milestone.

`filesystem.adopt(query,cookie,mount_path)` imports a boot-owned, already mounted
volume without remounting or acquiring ownership of its unmount. Bootstrap still
selects and mounts its known ext4 driver partition, then explicitly adopts it at
`/` before installation admission. Driver/image loading during boot still uses
ext4; this is distinct from runtime filesystem backend interchangeability.
The installed filesystem provider receives the manager's mount ID, not an ext4
cookie. Its source, and the handle stream broker, contain no ext4-specific calls.

`filesystem.file(mount_id,request)` dispatches STAT/LIST/READ/WRITE/PREPARE/
REMOVE/MKDIR/SYNC. Backend requests and result buffers belong to the synchronous
caller and must not be retained. PREPARE implements read/recreate/append opening;
WRITE preserves append semantics. Statuses use the common CB_* contract, not
backend errno values. Mutation failure may have an unknown on-disk outcome;
there are no automatic mutation retries or rollback claims.

Open streams retain their mount through `filesystem.retain/release`; delegated
endpoints share that stream lifetime. An active call also protects its mount.
UNMOUNT fails BUSY for references, active requests, adopted boot media or nested
mounts. Registry/mount lifecycle operations are serialized across yielding I/O;
failed unmount keeps the mount available for retry. Bookkeeping is reserved
before MOUNT so allocation failure cannot leak a newly mounted backend volume.

Namespace validation and protected `/boot`/`/system` writes remain in the manager.
The authenticated installation-data check is delegated to `applications.data`.
Normal operations retain a temporary route reference before sleeping; open file
streams retain their own reference until the final endpoint is released.

`ext4-backend.inc`, compiled into `ext4-fs`, adapts this contract to native ext4
operations, including its existing journal/durability implementation. The adapter
recognizes the ext-family superblock magic; the actual mount validates supported
features and refuses unsupported filesystems. It does not add ext2/ext3 support.

## Credential enforcement

User requests carry an authenticated process ID; process=0 is reserved for
trusted kernel calls. ABI2 metadata supplies UID/GID, mode and an explicit valid
flag. The manager checks owner/group/other bits and directory search permissions,
read/list/write access, parent write+search for create/unlink, and sticky-directory
unlink ownership. UID 0 bypasses discretionary mode checks (executables still require an execute
bit), while the
separate installation/OS-path policies still apply. No ACL, setuid/setgid execution
or capability bypass is implemented. Symlink traversal remains unsupported.

Each operation uses one coherent credential/group snapshot. Open stream calls use the actual caller PID and recheck credentials and metadata
on every read/write, including delegated endpoints. A credential drop therefore
cannot reuse a root-only file stream. Permission checks and backend execution
serialize per mount using sleeping waiters without retaining CPU locks over I/O;
already dispatched work completes, while queued requests use current policy.
New files/directories receive caller UID/primary GID and modes 0600/0700. IDs that
cannot be represented in ext4's 32-bit ownership fields fail rather than truncate.
Public METADATA returns a separate copied UID/GID/mode reply; the old STAT ABI
remains unchanged. CHMOD accepts 0777 bits and requires owner/root/admin; CHOWN
requires root/admin and representable 32-bit IDs. Both check ancestors, metadata
and namespace policy, and invoke backend journaled mutations. Protected system
paths still fail without installation authority. Identity owns explicit home
provisioning through the same handle; no automatic boot provisioning is added.

The verified boot disk's ISO9660 filesystem mounts at /boot after ext4 adoption.
Its metadata is synthesized read-only, and ordinary mutations are additionally
blocked by the protected /boot policy. Parent-directory search permissions are
checked across mount boundaries, so placing a mount under a private directory
cannot bypass that directory's access rules. See [ISO9660](iso9660-fs.md).


The manager also calls `applications.access` with the canonical mounted-namespace
path and authenticated PID. This enforces protected namespaces and same-user
private-container policy on metadata, mutations and every file-stream read/write.
Mounted aliases cannot bypass it. `process.active` rejects closed sessions before
backend execution; `filesystem.revoke(PID)` wakes queued mount waiters. Backend
operations that have already begun finish synchronously; executable/context
reclamation still waits for actual process exit.
