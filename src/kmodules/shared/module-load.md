# Selected future modules from disk

All existing modules, including bootstrap and every storage dependency, remain
Limine-loaded from the boot partition. Disk loading is opt-in for future modules.
No directory scan executes arbitrary files.

Set `drivers.modules` in `boot/cloudberry.cnf` to comma-separated module basenames
in dependency order; empty (the default) loads none. The complete list is
validated for names and duplicates before execution. Prepare the ext4 contents at
build time with `BOOT_DRIVERS_SOURCE=/absolute/directory`. Each selected basename
has `<name>.kmod` and `<name>.driver` below `drivers.directory`. The build copies
that directory into the ext4 partition; boot never formats, installs or migrates.

A `.driver` file is newline-separated text with a strict versioned schema:

```ini
version=1
module=example
init=example.init
start=example.start
requires=clock.now_ns@1.0
requires=mem.calloc@1.0
pci.class=030000
```

`start` is optional. `requires` may repeat with distinct signatures; major/minor
numbers are decimal. `pci.class` is the six hexadecimal digits class/subclass/
programming-interface; every matching PCI inventory index is passed to init/start.
Use `match=always` instead for a module with no PCI selector. A missing device
match skips execution and records SKIPPED. The selectors are not PCI ownership:
a driver still claims resources through PCI/DMA/IRQ surfaces. USB/platform/vendor
ID matching policies can be added when a selected future driver needs them.
Unknown/duplicate scalar keys, invalid versions/names/NULs and repeated dependency
signatures fail. Requirements use dynamic storage, with no 16-resource ceiling.
Basenames follow the existing 31-character hub/module naming contract.

Bootstrap owns the live `bootstrap.drivers` table header and per-selection records
until reboot. Read under the CPU lock or call `bootstrap.driver_info(out)` for the
header snapshot. Records contain selected/skipped/live/failed status, selection
errors, device counts and the kernel loader's owner/error/detail/state result.
Loading stops at the first failure; already successful modules remain live and
later entries remain selected. There is no implicit retry or rollback of earlier
module/device activity. The filesystem can remain usable even if module loading
fails. The selection worker logs the result separately from volume readiness.

## Kernel boundary and lifetime

`kernel.module_load(request,out result)` is the mediated trusted-kernel surface.
`module-load.h` defines its C structures and the version-1 initializer context.
It accepts calls only from a CPU0 thread with interrupts enabled, outside critical
sections and deferred interrupt callbacks. It serializes loading while remaining
preemptible. The caller owns stable image/manifest/device buffers until return;
initializers copy any context data needed afterward. No borrowed input persists
in the kernel loader.

The kernel checks exact dependency major/minimum minor versions and pins their
providers, then reuses format3 image/name/export/range/permission/guard validation.
Only complete images get owner/method records. The new owner is INITIALIZING;
ordinary discovery and mediated calls reject it while private init runs. Init
receives the public hub, kernel heap, owner ID and selected inventory indices.
It must prepare private state and clean its own allocations on failure. It must
not publish tables or queue asynchronous callbacks before becoming live.

After successful init, the owner becomes LIVE. Optional start may then resolve
its own surfaces, claim hardware and create IRQ/thread/scheduler work. Exported
methods must reject premature operations until the module marks itself ready.
This is not an atomic hardware activation transaction. If start fails, FAILED
immediately hides/rejects new calls; already admitted calls finish. Queued leases,
published tables and uncertain device state retain safe lifetimes. The loader
does not forcibly unwind callbacks, reset arbitrary hardware or free unknown
private allocations. A trusted caller may retire a failed owner only after its
active calls, leases and table pins have drained. Failed init/start details remain
available, and names are not reused during this boot.

Dependency leases last through the provider owner's retirement. Retirement
releases them after draining; image mappings and mediation stubs remain reserved
as before. Table publication still prevents retirement. Executable reclamation,
general unload/hot replacement and transactional hardware rollback remain separate
work. Configuration and the selected disk must be trusted: this kernel loader is
not a user installation/signature-validation or sandbox boundary.

## Focused validation

```sh
make -C src test-driver-manifest
make -C src test-module-disk-boot QEMU_CPUS=4 QEMU_IOMMU=amd
make -C src test-module-disk-boot DISK_MODULES_SUCCESS=1 QEMU_CPUS=4 QEMU_IOMMU=intel
```

Disk-only test fixtures never enter Limine's list or candidate prerequisites. The
guest loads a fixture solely from ext4, matches the VGA inventory, verifies hidden
init and activation, rejects dependency versions and malformed images, exercises
init/start failures, releases dependency leases, retires providers and rejects
captured stale stubs. The success variant leaves an unselected image unexecuted.
Host UBSan checks cover manifest syntax/types/duplicates, 40 dependencies and OOM
cleanup. Existing hub concurrency and typed-config checks remain separate.
