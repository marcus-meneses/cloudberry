#!/usr/bin/env python3
"""Read-only image checks; extract ext4 to disposable regular-file storage."""
import importlib.util
import pathlib
import re
import subprocess
import sys
import tempfile
import uuid

root = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('boot_image', root / 'tools/build-boot-image.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def check(image):
    image = pathlib.Path(image).resolve()
    if not image.is_file():
        raise ValueError('Expected a regular boot image')
    with tempfile.TemporaryDirectory(prefix='image-check-', dir=image.parent) as temporary:
        temporary = pathlib.Path(temporary)
        config = temporary / 'cloudberry.cnf'
        subprocess.run(['xorriso', '-osirrox', 'on', '-indev', str(image), '-extract',
                        '/boot/cloudberry.cnf', str(config)], check=True, capture_output=True)
        values = builder.config_values(config)
        guid = uuid.UUID(values['drivers.partition_guid'])
        partitions = builder.partitions(image)
        matches = [p for p in partitions if p['guid'] == str(guid)]
        assert len(matches) == 1
        part = matches[0]
        assert part['sectors'] * 512 >= 512 * builder.MIB
        builder.verify(image, guid, part['sectors'] * 512)
        checked = subprocess.run(['sgdisk', '--verify', str(image)], check=True, capture_output=True, text=True)
        assert 'No problems found.' in checked.stdout, checked.stdout + checked.stderr
        filesystem = temporary / 'drivers.ext4'
        with image.open('rb') as source, filesystem.open('wb') as target:
            source.seek(part['start'] * 512)
            remaining = part['sectors'] * 512
            while remaining:
                chunk = source.read(min(remaining, builder.MIB))
                assert chunk
                if chunk.strip(b'\0'):
                    target.write(chunk)
                else:
                    target.seek(len(chunk), 1)
                remaining -= len(chunk)
            target.truncate(part['sectors'] * 512)
        subprocess.run(['e2fsck', '-fn', str(filesystem)], check=True, capture_output=True)
        directory = values['drivers.directory'].replace('\\', '\\\\').replace('"', '\\"')
        listed = subprocess.run(['debugfs', '-R', f'stat "{directory}"', str(filesystem)], check=True,
                                capture_output=True, text=True)
        assert 'Type: directory' in listed.stdout, listed.stdout + listed.stderr
        root_state = subprocess.run(['debugfs', '-R', 'stat /system/root', str(filesystem)],
                                    check=True, capture_output=True, text=True)
        assert 'Mode:  0700' in root_state.stdout, root_state.stdout
        assert re.search(r'User:\s+0\s+Group:\s+0', root_state.stdout), root_state.stdout
    print(f'PASS boot image: GPT/backup CRCs, EFI preserved, ext4 >=512 MiB, matching config GUID {guid}, clean filesystem and drivers directory')


if __name__ == '__main__':
    check(sys.argv[1])
