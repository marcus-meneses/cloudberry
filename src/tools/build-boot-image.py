#!/usr/bin/env python3
"""Build a UEFI ISO/GPT image with a separate ext4 drivers partition.

Only regular output files are formatted; no mounts, loop devices or host disks.
"""
import argparse
import json
import pathlib
import shutil
import stat
import struct
import subprocess
import uuid
import zlib

LINUX_DATA = uuid.UUID('0fc63daf-8483-4772-8e79-3d69d8477de4')
EFI_SYSTEM = uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b')
MIB = 1024 * 1024
# Pin the on-disk feature set consumed by the initial read-only ext4 module.
# Never inherit future host mke2fs defaults (e.g. orphan_file/inline_data).
EXT4_FEATURES = ('none,has_journal,ext_attr,resize_inode,dir_index,filetype,extent,'
                 '64bit,flex_bg,sparse_super,large_file,huge_file,dir_nlink,'
                 'extra_isize,metadata_csum')


def config_values(path):
    values = {}
    for line in path.read_text(encoding='utf-8').split('\n'):
        line = line.strip(' \t\r')
        if not line or line.startswith('#'):
            continue
        key, separator, value = line.partition('=')
        key, value = key.strip(' \t'), value.strip(' \t')
        if not separator or key in values:
            raise ValueError('Invalid/duplicate image configuration entry: ' + key)
        values[key] = value
    return values


def regular_output(path):
    if path.exists() or path.is_symlink():
        if not stat.S_ISREG(path.lstat().st_mode):
            raise ValueError('Output must be a regular file: ' + str(path))


def partitions(image):
    """Validate both GPT headers/arrays before returning partition references."""
    image = pathlib.Path(image)
    size = image.stat().st_size
    if size % 512:
        raise ValueError('Unaligned image size')
    with image.open('rb') as disk:
        def header(lba):
            disk.seek(lba * 512)
            block = disk.read(512)
            if block[:8] != b'EFI PART':
                raise ValueError('Missing GPT header')
            length, expected = struct.unpack_from('<II', block, 12)
            if not 92 <= length <= 512:
                raise ValueError('Invalid GPT header length')
            checked = bytearray(block[:length])
            checked[16:20] = bytes(4)
            if zlib.crc32(checked) != expected:
                raise ValueError('Invalid GPT header CRC')
            current, alternate, first, last = struct.unpack_from('<QQQQ', block, 24)
            entries_lba, count, stride, crc = struct.unpack_from('<QIII', block, 72)
            if current != lba or alternate >= size // 512 or first > last or last >= size // 512:
                raise ValueError('Invalid GPT extent')
            if stride < 128 or stride % 8 or count * stride > size or entries_lba * 512 > size - count * stride:
                raise ValueError('Invalid GPT entry array')
            disk.seek(entries_lba * 512)
            entries = disk.read(count * stride)
            if zlib.crc32(entries) != crc:
                raise ValueError('Invalid GPT entry-array CRC')
            return block, entries, stride, first, last

        primary, entries, stride, first, last = header(1)
        backup_lba = struct.unpack_from('<Q', primary, 32)[0]
        if backup_lba != size // 512 - 1:
            raise ValueError('Backup GPT is not at image end')
        backup, backup_entries, _, _, _ = header(backup_lba)
        if struct.unpack_from('<Q', backup, 32)[0] != 1 or primary[56:72] != backup[56:72] or entries != backup_entries:
            raise ValueError('Primary and backup GPT disagree')
        result = []
        for index in range(len(entries) // stride):
            item = entries[index * stride:(index + 1) * stride]
            if item[:16] == bytes(16):
                continue
            start, end = struct.unpack_from('<QQ', item, 32)
            if start < first or start > end or end > last:
                raise ValueError('Partition outside GPT usable extent')
            result.append(dict(index=index + 1, type=str(uuid.UUID(bytes_le=item[:16])),
                               guid=str(uuid.UUID(bytes_le=item[16:32])), start=start,
                               sectors=end - start + 1,
                               name=item[56:128].decode('utf-16-le').rstrip('\0')))
        ordered = sorted(result, key=lambda p: p['start'])
        if any(a['start'] + a['sectors'] > b['start'] for a, b in zip(ordered, ordered[1:])):
            raise ValueError('Overlapping GPT partitions')
        return result


def verify(image, guid, size):
    entries = partitions(image)
    matches = [p for p in entries if p['guid'] == str(guid)]
    if len(matches) != 1 or matches[0]['type'] != str(LINUX_DATA):
        raise ValueError('Drivers partition GUID/type mismatch')
    if not any(p['type'] == str(EFI_SYSTEM) for p in entries):
        raise ValueError('EFI system partition lost')
    part = matches[0]
    if part['sectors'] * 512 != size:
        raise ValueError('Drivers partition size mismatch')
    with pathlib.Path(image).open('rb') as disk:
        disk.seek(part['start'] * 512 + 1024)
        superblock = disk.read(1024)
    if superblock[56:58] != b'\x53\xef':
        raise ValueError('Missing ext4 superblock')
    blocks = struct.unpack_from('<I', superblock, 4)[0]
    if struct.unpack_from('<I', superblock, 96)[0] & 0x80:
        blocks |= struct.unpack_from('<I', superblock, 336)[0] << 32
    block_size = 1024 << struct.unpack_from('<I', superblock, 24)[0]
    if blocks * block_size != size:
        raise ValueError('Filesystem size does not match partition')
    return dict(image_bytes=pathlib.Path(image).stat().st_size, partitions=entries,
                drivers_partition=part, filesystem='ext4', filesystem_bytes=size,
                filesystem_uuid=str(uuid.UUID(bytes=superblock[104:120])))


def build(stage, output, mib=512, drivers_source=None):
    stage, output = pathlib.Path(stage).resolve(), pathlib.Path(output).absolute()
    if mib < 512:
        raise ValueError('Drivers partition must be at least 512 MiB')
    if output.resolve().is_relative_to(stage):
        raise ValueError("Output image must be outside ISO staging")
    regular_output(output)
    values = config_values(stage / 'boot/cloudberry.cnf')
    guid = uuid.UUID(values['drivers.partition_guid'])
    if guid.int == 0:
        raise ValueError('Nil drivers partition GUID')
    directory = values['drivers.directory']
    if not directory.startswith('/') or '..' in directory.split('/') or '.' in directory.split('/'):
        raise ValueError('Drivers directory must be an absolute path without traversal')
    work = output.parent / (output.name + '.parts')
    if work.is_symlink():
        raise ValueError('Partition staging must not be a symlink')
    work.mkdir(parents=True, exist_ok=True)
    root = work / 'ext4-root'
    if root.exists() or root.is_symlink():
        if root.is_symlink():
            raise ValueError('Filesystem root must not be a symlink')
        shutil.rmtree(root)
    driver_root = root / directory.lstrip('/')
    driver_root.mkdir(parents=True)
    if drivers_source is not None:
        shutil.copytree(pathlib.Path(drivers_source).resolve(), driver_root, dirs_exist_ok=True)
    fs = work / 'drivers.ext4'
    regular_output(fs)
    with fs.open('wb') as stream:
        stream.truncate(mib * MIB)
    subprocess.run(['mke2fs', '-q', '-t', 'ext4', '-F', '-b', '4096', '-I', '256', '-m', '0',
                    '-O', EXT4_FEATURES, '-L', 'CLOUDBERRY_DRV', '-U', str(uuid.uuid5(guid, 'filesystem')),
                    '-E', 'lazy_itable_init=0,lazy_journal_init=0', '-d', str(root), str(fs)], check=True)
    subprocess.run(['xorriso', '-as', 'mkisofs', '-R', '-r', '-J',
                    '--efi-boot', 'boot/limine/limine-uefi-cd.bin', '--efi-boot-part',
                    '--efi-boot-image', '--protective-msdos-label',
                    '-append_partition', '4', str(LINUX_DATA), str(fs), '-appended_part_as_gpt',
                    str(stage), '-o', str(output)], check=True)
    matches = [p for p in partitions(output) if p['type'] == str(LINUX_DATA)]
    if len(matches) != 1:
        raise ValueError('Expected one appended Linux filesystem partition')
    index = matches[0]['index']
    subprocess.run(['sgdisk', f'--partition-guid={index}:{guid}',
                    f'--change-name={index}:Cloudberry drivers', str(output)], check=True)
    report = verify(output, guid, mib * MIB)
    (work / 'layout.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Boot image: ext4 drivers partition {index}, {mib} MiB, GUID {guid}', flush=True)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage')
    parser.add_argument('output')
    parser.add_argument('--drivers-mib', type=int, default=512)
    parser.add_argument('--drivers-source', help='Build-time driver-directory contents (isolated fixtures or packaged drivers)')
    args = parser.parse_args()
    build(args.stage, args.output, args.drivers_mib, args.drivers_source)
