Run from the project root on Ubuntu, as your normal user:

```bash
./scripts/install-toolchain-ubuntu.sh
source .toolchain/env.sh
```

The script uses sudo only to install Ubuntu development packages. Both apt
commands use a temporary list of official Ubuntu repositories for your release,
with Ubuntu archive signature verification. Third-party repositories (such as
NodeSource or Google Earth) are excluded from these commands. Existing apt source
files and third-party cached indexes are preserved. The temporary file is removed
afterward. This does not repair unrelated repositories for future system updates.

It builds
GCC 15.2.0 (C/C++) and Binutils 2.45.1 for `x86_64-elf`, including `libgcc`,
under `.toolchain/cross`. This is a bare-metal compiler with no host libc or
target C++ standard library. Use freestanding flags and your own linker script
for the kernel.

Limine's `v10.x-binary` branch supplies BIOS/UEFI boot assets and its host utility
under `.toolchain/limine`. The resolved commit is recorded in
`.toolchain/limine-commit.txt`; reruns retain that checkout. Use matching Limine
protocol headers when writing the kernel. The script builds the utility but
does not execute it. No host bootloader, partition, EFI entry, or shell startup
file is changed.

Make, QEMU, `gdb-multiarch`, OVMF, `xorriso`, `mtools`, `e2fsprogs` and `gdisk`
come from Ubuntu. Boot-image preparation uses `mke2fs` and `sgdisk` on generated
regular image files; verification uses `e2fsck` and `debugfs`.
Use `gdb-multiarch` to debug the kernel through QEMU's GDB server. OVMF is
firmware for the virtual machine.

Compilation can take considerable time and several GB of disk space. Builds
default to at most four jobs; lower `JOBS` if memory is limited:

```bash
JOBS=2 ./scripts/install-toolchain-ubuntu.sh
```

Set `CLOUDBERRY_TOOLCHAIN_DIR` for a different local destination. Run `--skip-apt`
when dependencies are already installed. GCC and Binutils archives are checked
against pinned upstream SHA-512 digests. Interrupted builds can be rerun; use a
fresh destination when changing the install prefix or Limine ref.

Sources: [GCC release](https://gcc.gnu.org/ftp/gcc/releases/gcc-15.2.0/),
[Binutils checksums](https://sourceware.org/pub/binutils/releases/sha512.sum),
[Limine](https://github.com/limine-bootloader/limine).


## KVM address-space measurements

```sh
./scripts/bench-spaces-kvm.sh
```

This compiles the isolated benchmark once and runs one/four-CPU KVM measurements
without attaching persistent disks. It writes raw reports, a TCG comparison and
a small report archive under `bin/tests/kvm-spaces/`. `latest.json` identifies the
last session. Use `--runs 3` for independent repetitions or `--jobs 2` to reduce
build parallelism. KVM must already be accessible to your user; the script never
uses sudo or falls back to TCG. See the [method and output details](../src/tests/address-space-profiling.md).
