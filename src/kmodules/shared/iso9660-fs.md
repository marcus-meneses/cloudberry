# Read-only ISO9660 boot filesystem

`iso9660-fs` is a Limine-loaded helper kmodule implementing the same ABI2
`<provider>.fs_backend` contract as ext4. It publishes `iso9660_fs.init`,
`iso9660_fs.fs_backend` and `iso9660_fs.invoke`; applications use /@filesystem
without selecting or importing a format implementation.

The backend reads ECMA-119 primary descriptors and prefers a supported Joliet
supplementary descriptor. Names are converted to UTF-8; primary ASCII names are
lowercased and ISO version suffixes removed. It validates both-endian numeric
fields, descriptor termination, single-volume geometry, record/extent bounds,
sector padding and Unicode encoding. Interleaved and multi-extent files fail
explicitly. Rock Ridge metadata/symlink semantics, UDF and FAT are not implemented.
The normal image uses Joliet to preserve module filenames and case.

STAT/LIST/READ and read-only PREPARE are supported. Mutations and writable mounts
return READ_ONLY. Permissions are synthesized as UID/GID 0, 0555 directories and
0444 files. Directory cursors are checked byte offsets; files are read directly
through the existing block contract, including sector-size adaptation.

Each mount dynamically owns one volume record and a 4 KiB sector scratch buffer,
and retains its parent block descriptor until successful unmount. Cookies are
monotonic, stale cookies fail, and failed release preserves the mount. The manager
serializes user file calls; direct overlapping backend calls return BUSY. There
is no ISR allocation or retained caller buffer. Device removal makes subsequent
I/O fail through the existing transport; it does not silently reconnect cookies.

Bootstrap adopts ext4 at /, then mounts the ISO9660 filesystem from the retained
parent disk at /boot only when Limine's disk GUID matches the selected disk.
An unknown/different origin leaves /boot unmounted with a diagnostic. Boot on a
different disk from the selected system partition needs separate disk retention
and selection policy; it is not inferred from the system partition's GUID.

The builder places kernel.elf, cloudberry.cnf, kmodules/, limine/ and EFI/ at the
ISO root, so mounted paths have only one /boot prefix. It bounds primary/Joliet
volume space at the appended ext4 start; malformed ISO extents cannot address the
system partition. The FAT EFI System Partition still starts Limine and is not
separately mounted by this backend. No media is formatted or migrated at boot.

References: [ECMA-119](https://ecma-international.org/publications-and-standards/standards/ecma-119/)
and [Microsoft disc formats](https://learn.microsoft.com/en-us/windows/win32/imapi/disc-formats).

Validation: `make -C src test-iso9660` exercises the real generated image with
512/4096-byte devices, nested/Joliet UTF-8 names, directory EOF, extent corruption,
read-only/OOM/I/O paths and parent retention. `console-boot.py --bootfs-only`
checks public /boot reads, module listings, default ABNT2 and write denial on a
copied image. Successful guest media is automatically removed.
