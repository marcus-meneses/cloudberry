# Bootstrap storage selection

`bootstrap.kmod` remains Limine-loaded. `init(hub,heap)` binds the existing
inventory/transport/partition/filesystem surfaces and publishes `bootstrap.volume`.
`start()` creates a CPU0 worker; normal startup invokes it after USB, hub and
storage lifecycle workers start. `select()` is its synchronous one-shot operation.
The kernel supplies no transport policy and parses no filesystem metadata.

Selection waits for the initial root-port and hub scans, then scans every active
NVMe namespace on supported controllers, connected AHCI port and USB transparent
BOT interface at LUN0. UAS, USB multi-LUN multiplexing, ATAPI and unsupported
controller types remain outside the supported discovery set. Hardware startup
readiness has a deadline; normal idle operation has no periodic discovery loop.
Controller/namespace/port identities remain explicit in the resulting snapshot.

Blank/non-GPT disks are skipped. GPT devices are checked against
`config.drivers.partition_guid`; primary/backup checksums, bounds and uniqueness
remain partition-module responsibilities. A corrupt GPT or failed supported
transport stops selection. No match and duplicate matches are distinct errors.
Changing USB topology during selection fails instead of choosing a partial scan.
A unique partition is mounted read-only. Bootstrap owns the selected transport,
slice and mount; consumers use its filesystem cookie and must not close these
objects. Other devices close without touching their contents. Uncertain cleanup
records remain reachable in the module rather than freeing live DMA or ownership.
There is no boot formatting, migration, journal replay or automatic writable mount.
A dirty journal requires an explicitly writable maintenance mount outside this
selection procedure.

`info(out bootstrap_info)` copies state, errors, inspected/matched counts,
transport, PCI hardware index (UINT64_MAX for USB), namespace/port or USB host/ref,
GPT disk/partition GUIDs and the owned block/filesystem descriptors. The immutable
`boot.origin` table contains copied Limine executable-file scalar metadata, never
firmware pointers. A nonzero GPT disk GUID allows `match` or `different` origin
reporting; absent metadata leaves `unknown`. The configured driver-volume GUID
selects the volume; it does not prove that the kernel itself came from that disk.
The [Limine file/request contract](https://github.com/limine-bootloader/limine-protocol/blob/trunk/include/limine.h)
is the metadata source. Optical/network/hybrid-image provenance may remain unknown.

Read the live table under the CPU lock and use fields only once state is READY.
The snapshot call also reaps an exited selection worker when called outside a
critical section with interrupts enabled. USB removal invalidates the underlying
mount; `info` then reports DISCONNECTED. No replacement disk inherits the cookies,
and bootstrap does not silently remount it. Code and registry headers remain
boot-lifetime. Explicitly selected future modules use this mount through the
[disk-module loader](module-load.md). Volume readiness and module-load completion
are separate states; consumers needing selected modules also check driver status.

Focused acceptance uses disposable images below `bin/tests`:

```sh
make -C src test-bootstrap-boot BOOTSTRAP_BACKEND=usb QEMU_CPUS=4 QEMU_IOMMU=amd
make -C src test-bootstrap-boot BOOTSTRAP_BACKEND=nvme QEMU_CPUS=4 QEMU_IOMMU=amd
make -C src test-bootstrap-boot BOOTSTRAP_BACKEND=sata QEMU_CPUS=4 QEMU_IOMMU=intel
make -C src test-bootstrap-boot BOOTSTRAP_DUPLICATE=1 QEMU_CPUS=4 QEMU_IOMMU=amd
make -C src test-bootstrap-boot BOOTSTRAP_BACKEND=nvme BLOCK_MULTI=1 QEMU_CPUS=4 QEMU_IOMMU=amd
```

These check real transport selection, readonly ext4 fixture reads, duplicate
rejection before mounting and complete enumeration across multiple namespaces.
No candidate VM or developer persistent disk is run or attached by these tests.

## Isolated provider startup

The core calls `bootstrap.apps` after volume selection starts. The startup worker
waits for the selected readonly ext4 mount, then processes `boot.providers` in
creation/readiness order. An empty list starts none; the default is `keyboard`.
Each item is a bundle basename below `drivers.directory`. Bootstrap understands
only the generic `provider.cnf` contract, never keyboard layouts or handler types.
All foundational kmodules remain Limine-loaded and use their separate format3
image loader; native handlers remain protected ring-3 processes.

A descriptor is newline-delimited ASCII `key=value`, with blank/comment lines and
CRLF accepted. Unknown/duplicate fields, invalid versions/identities and unsafe
paths fail before admission. Required fields:

- `version=1`, `executable=<relative file>`, `manifest=<relative file>`.
- `installation_id=<32 hex digits>`: a nonzero 16-byte development identity.

Optional fields: `ready=<resource name>` (otherwise running process readiness),
`cpu=0`, `stack_bytes=65536`, and `files=<comma-separated relative files>`.
There is no fixed file/provider count ceiling. Absolute paths, empty path segments,
`.`/`..` traversal and duplicate startup-file names are rejected. Bundle names
are single path components. Values have no quoting or interpolation.

`bootstrap.launch(filesystem,bundle)` reads that definition, the named executable
and manifest, and copies only the listed files into a generic startup packet.
The process mechanism owns ELF validation/loading and private pages; handle
mediation admits the copied manifest before execution. The C ABI startup packet
in `interfaces/startup.h` contains named inline file data, with explicit lengths
and no kernel pointers or implicit runtime resource grants. The handler interprets
its own files. Keyboard consumes `config.cnf` and selects its own layout JSON.
No keyboard header, filename convention, identity or layout parser exists in the
bootstrap implementation. Signing/installed-app verification remains future work;
these are trusted development image contents, not app-selected authority.

The owned `bootstrap.applications` header contains worker, dynamic record count
and aggregate state/error. Private records retain process ID, bundle, readiness
name and state/error until explicit release. No reclaimable record/string pointer
is published. `bootstrap.application_info(pid,out)` and
`bootstrap.application_next(after_pid,out)` copy per-process status, with increasing
never-reused IDs for enumeration. `bootstrap.apps_info`
refreshes process/publication ownership and copies current aggregate status:
0 idle, 1 awaiting volume, 2 starting, 3 ready, 4 failed/stopping. Reaped/faulted/
stopping processes or withdrawn publications cannot remain READY. Snapshot APIs
serialize with launch/release using a nonblocking BSP token, never a kernel-wide
lock across I/O or waits. Callers retry a busy snapshot. The stable aggregate table is a trusted diagnostic reference; use the snapshot
APIs for coherent state. Copied snapshots require no reader leases and remain
valid values after metadata release.

Launch failures report stage 1 directory/config, 2 definition, 3 data/allocations,
4 ELF load, 5 manifest admission or 6 start in aggregate error. Live-record errors
2/3 indicate process failure/stop or withdrawn readiness publication. Failed
rollback retains its cleanup PID and record. Failed startup retains already
started providers; it does not silently kill them or retry side effects.

Lifecycle ownership is explicit: a trusted owner calls `process.stop`, waits for
actual exit, calls `process.reap` until I/O is quiescent, then
`bootstrap.release(process)` to free startup metadata. Release refuses a live or
unreaped process; a retained bundle cannot be launched twice. Restart means a
fresh load after these steps, with a fresh process/capability epoch. Bootstrap
coordinates startup and supplies on-demand status; it is not an ongoing
supervision daemon. Automatic restart/rebinding and forced cancellation remain
separate policy/mechanism work.

Acceptance uses host descriptor/startup/OOM checks and isolated real-process
boots. An unrelated test bundle consumes arbitrary startup data through the same
loader, proving the path does not depend on keyboard semantics. Test-only clients
and fault providers never enter candidate images. `tests/keyboard-boot.py` boots
the ordinary production image on disposable readonly USB BOT/SCSI disk media and
waits for `bootstrap: provider ready: keyboard`. Its explicit disk attachment
preserves the hybrid image's 512-byte GPT geometry. Developer disks are untouched.
