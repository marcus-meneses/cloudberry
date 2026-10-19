# Filesystem provider

`filesystem.elf` publishes `/@filesystem` and uses a catalog-bound `/@io` disk
bridge to the selected ext4 mount. Public operations open directional stream
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
the build-owned installation tree. This is boot-domain policy,
not the future home/private-folder or administrator permission model.

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
