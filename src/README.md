# Cloudberry source

Cloudberry explores replaceable operating-system services, named resource
handles and explicit application permissions. The [project presentation](../README.md)
introduces the motivation, architecture, applications and screenshots.
This directory contains the active implementation.

## Build and run

```sh
make -C src build
make -C src run QEMU_ACCEL=kvm QEMU_CPUS=4 QEMU_GPU=virgl
```

Run those commands from the repository root. `make -C src run` uses software
emulation and the VGA backend by default. Qualified virtio-vga/VirGL selects the
accelerated provider pair; the application interfaces stay the same. The requested
startup mode is 1024×768 and providers derive their dimensions from the display.

The local GCC/Binutils/Limine installation lives in `../.toolchain`; see the
[Ubuntu toolchain helper](../scripts/install-toolchain-ubuntu.sh). QEMU, OVMF,
xorriso, mtools, e2fsprogs, gdisk and Python/Pillow support the image/test tooling.
SDK builds download pinned upstream sources into `../bin/sdk`.

Artifacts belong directly in `../bin`. `cloudberry.iso` is a hybrid boot image
with an ext4 system volume, prepared at compile time. Normal runs attach a
read-only boot image. `QEMU_BOOT_WRITABLE=1` creates a temporary writable VM
snapshot. Explicit `init-dev-disk` creates persistent development media;
`run-dev` launches that installed image without rebuilding it. Existing development
disks are not silently reformatted or migrated. The host bootloader is untouched.

Useful options include `QEMU_CPUS`, `QEMU_ACCEL=tcg|kvm`,
`QEMU_GPU=vga|virtio|virgl`, `QEMU_IOMMU=none|intel|amd` and `OVMF=/path/to/OVMF.fd`.
`make -C src debug` waits for GDB on port 1234.

## Source layout

```text
src/
├── AGENTS.md
├── Makefile
├── ROADMAP.md
├── apps/
│   ├── installed.json              Built-in installation catalog
│   ├── terminal/                   Shell and interactive line editing
│   ├── editor/                     Native graphical editor
│   ├── js/                         QuickJS application
│   ├── monitor/                    Process/handler monitor
│   ├── bench3d/                    Projected cubes and rendering statistics
│   └── …                           File, account and installation utilities
├── assets/                         Logo and actual guest screenshots
├── boot/                           Limine entry/configuration and memory bootstrap
├── common/                         Shared hardware-independent helpers
├── drivers/                        Disk-loaded driver descriptors
├── fonts/                          Default fonts and notices
├── handlers/
│   ├── graphics-common/            Shared public graphics decoder
│   ├── graphics-vga/               Software raster/composition implementation
│   ├── graphics-opengl/            GPU surface caching and composition
│   ├── display-vga/                Firmware framebuffer provider
│   ├── display-opengl/             Paired virtio/VirGL rendering provider
│   └── …                           Text, input, filesystem, identity and lifecycle
├── interfaces/                     Versioned resource/handle C contracts
├── kernel/                         Core bootstrap, hub, loader and mediation
├── kmodules/
│   ├── shared/                     Architecture-independent/common x86 mechanisms
│   ├── intel/                      Intel CPU/IOMMU backends
│   └── amd/                        AMD CPU/IOMMU backends
├── maps/                           Memory ownership, startup and filesystem design
├── script/samples/                 JavaScript applications
├── sdk/                            C system library, libc and runtime adapters
├── tests/                          Host checks, disposable guests and profilers
└── tools/                          Image, SDK, font and module builders
```

## Contracts and design

Kmodules are coupled kernel components called through the internal versioned hub.
Its method index uses numeric hash keys; publication grows incrementally and
callable addresses remain stable. Resource handlers are separate protected
processes serving manifest-authorized handles through the broker. The two layers
have separate lifetime and authorization contracts.

- [Kernel hub and provider lifetime](kernel/hub.md)
- [Resource interfaces and ABI](interfaces/README.md)
- [Startup graph and memory ownership](maps/startup.md)
- [Filesystem tree and mount/backend contracts](maps/filesystem.md)
- [Kernel components](kmodules/README.md)
- [Native SDK](sdk/README.md)
- [Applications](apps/README.md)

## Development and validation

Choose checks matching the component being changed. The [test guide](tests/README.md)
contains commands, evidence and the distinction between modeled, emulated and
native validation. Guest tests use disposable media; test fixtures and profiler
variants stay outside the ordinary image.

```sh
make -C src test-bench3d-model
make -C src test-bench3d QEMU_CPUS=4 QEMU_ACCEL=tcg
```

Long-term priorities and chronological implementation/acceptance notes belong in
[ROADMAP.md](ROADMAP.md), the test guide and component documentation.
