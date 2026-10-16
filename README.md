# Cloudberry

Cloudberry is an operating-system project targeting modern Intel/AMD x86-64
machines with UEFI, PCIe, NVMe, AHCI SATA and xHCI USB.

The active implementation lives in [src/](src/README.md). Build outputs go to
`bin/`; the locally installed cross-toolchain lives in `.toolchain/`.

```sh
make -C src build
make -C src run QEMU_ACCEL=kvm QEMU_CPUS=4
```

See the [source overview](src/README.md), [roadmap](src/ROADMAP.md),
[kmodule contracts](src/kmodules/README.md), [isolated test instructions](src/tests/README.md)
and [Ubuntu toolchain setup](scripts/README.md).

The previous implementation is preserved in `.src_old/` as a local archive.
It is excluded from Git and is not part of the active build. `src2/` has been
promoted to `src/`; use the commands above for development.
