#!/usr/bin/env python3
"""Remove disposable test media; preserve reports, logs and compiled caches."""
import argparse
import os
import pathlib
import socket

TESTS = pathlib.Path(__file__).resolve().parents[2] / 'bin/tests'
MEDIA = {'.img', '.iso', '.ext4', '.qcow2', '.raw', '.ppm'}


def clean(directory=TESTS, dry_run=False):
    base = TESTS.resolve()
    directory = pathlib.Path(directory).resolve()
    if directory != base and base not in directory.parents:
        raise ValueError('cleanup is restricted to workspace bin/tests')
    if not directory.exists():
        return 0, 0
    files = []
    for parent, directories, names in os.walk(directory, followlinks=False):
        parent = pathlib.Path(parent)
        directories[:] = [name for name in directories if not (parent / name).is_symlink()]
        for name in names:
            path = parent / name
            if path.is_symlink():
                continue
            if path.suffix == '.sock':
                with socket.socket(socket.AF_UNIX) as probe:
                    probe.settimeout(.1)
                    try:
                        probe.connect(str(path))
                    except (ConnectionRefusedError, FileNotFoundError):
                        pass  # Stale socket left by a stopped VM.
                    except OSError as error:
                        raise RuntimeError(f'cannot establish whether {path} is active: {error}') from error
                    else:
                        raise RuntimeError(f'active test socket {path}; stop test VMs before cleanup')
            if path.suffix in MEDIA and path.is_file():
                files.append(path)
    allocated = 0
    for path in files:
        allocated += path.stat().st_blocks * 512
        if not dry_run:
            path.unlink()
    return len(files), allocated


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=pathlib.Path, default=TESTS)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    try:
        count, allocated = clean(args.root, args.dry_run)
    except (ValueError, RuntimeError, OSError) as error:
        parser.exit(1, f'Cleanup refused: {error}\n')
    print(f'{"Would remove" if args.dry_run else "Removed"} {count} disposable test media files '
          f'({allocated / 1024**3:.2f} GiB allocated); reports, logs and build caches retained.')
