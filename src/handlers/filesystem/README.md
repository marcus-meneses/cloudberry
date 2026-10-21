# Filesystem provider

`filesystem.elf` publishes `/@filesystem` and uses a catalog-bound `/@io` disk
bridge to the format-neutral mount manager. Ext4 supplies / and read-only
ISO9660 supplies /boot when the boot disk identity is verified. Public operations open directional stream
handles, stat, list one directory entry, remove a file and create a directory.
Relative-path policy is a caller concern; the provider accepts bounded canonical
absolute paths, rejecting traversal, duplicate separators and embedded NULs.

Open modes are read, recreate, and append. Recreate creates/truncates once before
launch, then advances a sequential write offset; append creates when absent and
uses ext4 append semantics on every write. File stream handles belong to the
original authenticated application, not the filesystem provider. Its own stream
manifest must authorize the endpoint direction. No raw mount cookies or paths
in kernel memory are returned. Writes/removal under `/boot` and `/system` are
refused except for the caller’s catalog-bound root app-data descendants, protecting
the build-owned installation tree. This installation policy is combined with kernel-owned UID/GID checks;
UID 0 does not bypass the installation boundary. Automatic home/private-folder
provisioning remains separate work.

Kernel file workers execute bounded synchronous ext4 calls on CPU0; apps can run
on any selected CPU and block through thread joins. Calls on one ext4 mount wait
by stable cookie, with wake credits, rather than failing merely because another
reader/writer is active. Disconnect/unmount wakes queued callers without retaining
pointers to freed volumes. File writes retain existing synchronous journal/barrier
semantics; scheduling delayed journal flushes remains deferred. I/O failures may
have partial/unknown effects; applications must not blindly retry mutations.

`filesystem.writable=true` permits a writable mount only when the selected medium
supports it. QEMU's normal boot remains read-only. `QEMU_BOOT_WRITABLE=1` gives the
boot image a temporary writable QEMU snapshot: redirection works without changing
the built ISO, and those writes are discarded when QEMU exits. No boot-time
formatting, migration or developer disk modification is introduced.


The [filesystem map](../../maps/filesystem.md) defines the packaged namespace.
The protected-system write policy has one narrow exception: descendants of the
original caller's own `/system/root/_apps/<appname>.data/`, resolved using its
authenticated installation identity and the trusted catalog. Parent directories,
indexes, executables and other apps' state remain protected against mutation.
Hidden underscore names remain present in filesystem enumeration.

The IO gateway now forwards to the independent `filesystem` kmodule. It routes
mounts through registered format backends; the handler remains format-agnostic.
Ext4 is the initial real backend, and bootstrap adopts its known driver volume at
`/`. Mount lifecycle operations remain kernel-only; no user-facing mount policy
or additional on-disk format is introduced by this split.

The /system/config/identity subtree is an additional protected boundary for all
public filesystem operations, including reads. Only the catalog-authenticated
identity provider may access it; it may update its own account/journal files
without gaining write authority over the rest of /system. This is installation
policy, in addition to runtime UID/GID/mode enforcement.

File streams recheck the current caller identity and on-disk permissions on each
operation. See [filesystem authorization](../../kmodules/shared/filesystem.md).

`rmdir` removes an empty directory, distinct from file `remove`. It requires an
explicit manifest grant and parent write/search authority; nonempty directories
fail. Traversal/recursive cleanup belongs to userland. Backend ABI2 minor 1 adds
this operation; minor 0 backends report UNAVAILABLE for it.
