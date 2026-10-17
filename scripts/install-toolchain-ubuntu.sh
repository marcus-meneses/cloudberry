#!/usr/bin/env bash
# Development tools only: never installs a bootloader onto a disk or host EFI.
set -Eeuo pipefail
trap 'printf "Error at line %s. Fix the problem and rerun the script.\n" "$LINENO" >&2' ERR

usage() {
    cat <<'EOF'
Usage: ./scripts/install-toolchain-ubuntu.sh [--skip-apt] [--help]

Builds x86_64-elf GCC + Binutils locally, downloads Limine development
files, and installs Ubuntu build dependencies, Make, QEMU, and GDB.
Run as your normal user; sudo is used only for apt-get.
APT uses temporary official Ubuntu sources, excluding third-party repositories.

Environment overrides:
  CLOUDBERRY_TOOLCHAIN_DIR  Installation directory (default: project/.toolchain)
  JOBS                  Parallel build jobs (default: up to 4)
  LIMINE_REF            Limine binary branch/tag (default: v10.x-binary)

--skip-apt assumes all Ubuntu dependencies are already installed.
No disk writes, host bootloader installation, EFI changes, or shell profile edits.
EOF
}

skip_apt=false
for arg in "$@"; do
    case "$arg" in
        --help|-h) usage; exit 0 ;;
        --skip-apt) skip_apt=true ;;
        *) printf 'Unknown option: %s\n' "$arg" >&2; usage >&2; exit 2 ;;
    esac
done

if (( EUID == 0 )); then
    echo 'Run this script as your normal user, without sudo.' >&2
    exit 1
fi
if [[ ! -r /etc/os-release ]]; then
    echo 'This script requires Ubuntu.' >&2; exit 1
fi
source /etc/os-release
if [[ ${ID:-} != ubuntu ]]; then
    echo 'This script requires Ubuntu.' >&2; exit 1
fi

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
toolchain_dir=${CLOUDBERRY_TOOLCHAIN_DIR:-"$project_dir/.toolchain"}
jobs=${JOBS:-$(nproc)}
if [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
    echo 'JOBS must be a positive integer.' >&2; exit 2
fi
if [[ -z ${JOBS:-} ]] && (( jobs > 4 )); then jobs=4; fi
limine_ref=${LIMINE_REF:-v10.x-binary}
target=x86_64-elf
gcc_version=15.2.0
binutils_version=2.45.1

install_ubuntu_packages() (
    # Use official archives for this Ubuntu release without altering /etc/apt.
    # Scope both commands, and retain cached indexes for unrelated repositories.
    local codename=${UBUNTU_CODENAME:-${VERSION_CODENAME:-}}
    local architecture archive security apt_sources
    [[ $codename =~ ^[a-z]+$ ]] || {
        echo 'Cannot determine Ubuntu release codename.' >&2; exit 1
    }
    [[ -r /usr/share/keyrings/ubuntu-archive-keyring.gpg ]] || {
        echo 'Ubuntu archive keyring is missing.' >&2; exit 1
    }
    architecture=$(dpkg --print-architecture)
    case "$architecture" in
        amd64|i386)
            archive=https://archive.ubuntu.com/ubuntu
            security=https://security.ubuntu.com/ubuntu ;;
        arm64|armhf|ppc64el|riscv64|s390x)
            archive=https://ports.ubuntu.com/ubuntu-ports
            security=$archive ;;
        *) printf 'Unsupported Ubuntu architecture: %s\n' "$architecture" >&2; exit 1 ;;
    esac
    apt_sources=$(mktemp /tmp/cloudberry-ubuntu-sources.XXXXXX.list)
    trap 'rm -f -- "$apt_sources"' EXIT
    local options="arch=$architecture signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg"
    printf 'deb [%s] %s %s main restricted universe multiverse\n' \
        "$options" "$archive" "$codename" \
        "$options" "$archive" "$codename-updates" \
        "$options" "$security" "$codename-security" > "$apt_sources"
    chmod 644 "$apt_sources"
    local apt_options=(
        -o "Dir::Etc::sourcelist=$apt_sources"
        -o 'Dir::Etc::sourceparts=-'
        -o 'APT::Get::List-Cleanup=0'
    )
    echo 'Installing development packages using temporary official Ubuntu sources.'
    sudo apt-get "${apt_options[@]}" -o 'APT::Update::Error-Mode=any' update
    sudo apt-get "${apt_options[@]}" install --no-install-recommends -y \
        build-essential make bison flex texinfo libgmp-dev libmpfr-dev \
        libmpc-dev zlib1g-dev libisl-dev ca-certificates curl git xz-utils \
        qemu-system-x86 qemu-system-gui libcanberra-gtk3-module qemu-utils \
        gdb-multiarch ovmf xorriso mtools e2fsprogs gdisk
)
if ! "$skip_apt"; then
    install_ubuntu_packages
fi
for command in gcc g++ make curl git tar xz sha512sum bison flex makeinfo \
    qemu-system-x86_64 gdb-multiarch xorriso mcopy mke2fs sgdisk e2fsck debugfs; do
    command -v "$command" >/dev/null || {
        printf 'Missing dependency: %s (rerun without --skip-apt).\n' "$command" >&2
        exit 1
    }
done

mkdir -p -- "$toolchain_dir"
toolchain_dir=$(cd -- "$toolchain_dir" && pwd -P)
prefix="$toolchain_dir/cross"
work_dir="$toolchain_dir/build"
source_dir="$toolchain_dir/sources"
mkdir -p -- "$prefix" "$work_dir" "$source_dir"
export PATH="$prefix/bin:$PATH"

# Pinned upstream SHA-512 digests; cached downloads are verified on every run.
download() {
    local url=$1 digest=$2 file="$source_dir/${1##*/}"
    if [[ ! -f $file ]]; then
        curl --fail --location --retry 3 --proto '=https' --proto-redir '=https' \
            "$url" -o "$file.part"
        mv -- "$file.part" "$file"
    fi
    printf '%s  %s\n' "$digest" "$file" | sha512sum --check --status || {
        printf 'Checksum mismatch: %s. Remove this file and rerun.\n' "$file" >&2
        exit 1
    }
}

download "https://sourceware.org/pub/binutils/releases/binutils-$binutils_version.tar.xz" \
    ea030419eba387579ab717be7e3223fc99e93b586860b06003c12489f93441640d4082736f76aa5e98233db4f46e232f536a45e471486de1f5b64e1b827c167e
download "https://gcc.gnu.org/pub/gcc/releases/gcc-$gcc_version/gcc-$gcc_version.tar.xz" \
    89047a2e07bd9da265b507b516ed3635adb17491c7f4f67cf090f0bd5b3fc7f2ee6e4cc4008beef7ca884b6b71dffe2bb652b21f01a702e17b468cca2d10b2de
for package in "binutils-$binutils_version" "gcc-$gcc_version"; do
    if [[ ! -f $source_dir/$package/configure ]]; then
        tar -xJf "$source_dir/$package.tar.xz" -C "$source_dir"
    fi
done

mkdir -p "$work_dir/binutils-$binutils_version" "$work_dir/gcc-$gcc_version"
(
    cd "$work_dir/binutils-$binutils_version"
    if [[ ! -f Makefile ]]; then
        "$source_dir/binutils-$binutils_version/configure" \
            --target="$target" --prefix="$prefix" --with-sysroot \
            --disable-nls --disable-werror
    fi
    make -j "$jobs"
    make install
)
(
    cd "$work_dir/gcc-$gcc_version"
    if [[ ! -f Makefile ]]; then
        "$source_dir/gcc-$gcc_version/configure" \
            --target="$target" --prefix="$prefix" --disable-nls \
            --enable-languages=c,c++ --without-headers --disable-multilib
    fi
    make -j "$jobs" all-gcc all-target-libgcc
    make install-gcc install-target-libgcc
)

# Binary branches contain boot image assets plus source for the host utility.
# Deliberately no `make install`, `bios-install`, or disk/EFI operations.
limine_dir="$toolchain_dir/limine"
if [[ ! -d $limine_dir ]]; then
    git clone --depth=1 --branch "$limine_ref" \
        https://github.com/limine-bootloader/limine.git "$limine_dir"
    printf '%s\n' "$limine_ref" > "$toolchain_dir/limine-ref.txt"
else
    git -C "$limine_dir" rev-parse --git-dir >/dev/null
    if [[ $(cat "$toolchain_dir/limine-ref.txt" 2>/dev/null || true) != "$limine_ref" ]]; then
        echo 'Existing Limine checkout uses another ref. Choose a fresh CLOUDBERRY_TOOLCHAIN_DIR.' >&2
        exit 1
    fi
fi
printf '%s\n' "$limine_ref" > "$toolchain_dir/limine-ref.txt"
make -C "$limine_dir" -j "$jobs"
git -C "$limine_dir" rev-parse HEAD > "$toolchain_dir/limine-commit.txt"

{
    printf 'export PATH=%q:"$PATH"\n' "$prefix/bin"
    printf 'export CLOUDBERRY_CROSS_PREFIX=%q\n' "$prefix/bin/$target-"
    printf 'export LIMINE_DIR=%q\n' "$limine_dir"
} > "$toolchain_dir/env.sh"

# Verify actual freestanding compilation and ELF linking, without booting.
smoke_dir=$(mktemp -d "$work_dir/smoke.XXXXXX")
trap 'rm -rf -- "$smoke_dir"' EXIT
printf 'void _start(void) { for (;;) {} }\n' > "$smoke_dir/kernel.c"
"$target-gcc" -ffreestanding -mno-red-zone -fno-stack-protector \
    -fno-pie -nostdlib -static -Wl,-e,_start -Wl,-Ttext=0x100000 \
    "$smoke_dir/kernel.c" -o "$smoke_dir/kernel.elf"
"$target-readelf" -h "$smoke_dir/kernel.elf"
"$target-gcc" --version
"$target-ld" --version
qemu-system-x86_64 --version
gdb-multiarch --version
printf '\nToolchain ready. Load it in your current shell:\n  source %q\n' "$toolchain_dir/env.sh"
