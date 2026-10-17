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
boot-lifetime. Only explicitly selected future modules will use this mount.

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
