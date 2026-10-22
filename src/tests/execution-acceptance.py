#!/usr/bin/env python3
"""Execution correctness acceptance; isolated media, no native timing gate."""
import argparse
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[2]

def write(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')

def fingerprint():
    paths = subprocess.check_output(['git', 'ls-files', '-co', '--exclude-standard', 'src', 'scripts'], cwd=ROOT, text=True).splitlines()
    digest = hashlib.sha256()
    for name in sorted(set(paths)):
        path = ROOT / name
        if path.is_file():
            digest.update(name.encode() + b'\0' + path.read_bytes())
    return digest.hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--accel', choices=('kvm', 'tcg'), default='kvm')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('jobs must be positive')
    # Fail before building if native execution is unavailable. Never fall back.
    if args.accel == 'kvm':
        subprocess.run([sys.executable, str(ROOT/'src/tests/native-check.py')], check=True)
    base = ROOT/'bin/tests/execution-acceptance'
    base.mkdir(parents=True, exist_ok=True)
    with (base/'run.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        session = base/datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
        session.mkdir()
        # Keep QMP/GDB Unix socket paths below sockaddr_un's 108-byte limit.
        # Each session still owns a distinct build; no candidate paths are used.
        build = ROOT/'bin/tests'/('exec-'+uuid.uuid4().hex[:8])
        source = fingerprint()
        summary = dict(status='running', accelerator=args.accel, source_sha256=source, build=str(build.relative_to(ROOT)),
                       git_revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                       kernel=platform.release(), affinity=sorted(os.sched_getaffinity(0)),
                       cpu=Path('/proc/cpuinfo').read_text().split('\n\n')[0], cases=[])
        write(session/'session.json', summary)
        def run(command, name):
            write(session/(name+'.command.json'), command)
            print(name + ': ' + str(session/(name+'.log')), flush=True)
            with (session/(name+'.log')).open('w') as log:
                result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                raise RuntimeError(f'{name} exited {result.returncode}; see {session/(name+".log")}')
        make = ['make', '-C', 'src', f'-j{args.jobs}', f'BUILD={build}', f'TEST_BUILD={build}',
                f'ISO={build}/cloudberry.iso', f'KERNEL={build}/kernel.elf', f'STAGE={build}/src-iso-root']
        try:
            for cpus in (1, 4):
                run(make+['test-process-boot', f'QEMU_CPUS={cpus}', f'QEMU_ACCEL={args.accel}', 'QEMU_IOMMU=none'], f'process-{cpus}cpu')
                report = build/f'tests/process-{cpus}cpu-none/report.json'
                data = json.loads(report.read_text())
                shutil.copy2(report, session/f'process-{cpus}cpu.json')
                text = (report.parent/'boot.log').read_text()
                migrations = re.search(r'MEASURE user migrations: (\d+)', text)
                if (data.get('status') != 'passed' or data.get('accelerator') != args.accel or
                    data.get('cpus') != cpus or not migrations or
                    (cpus == 1 and int(migrations[1]) != 0) or
                    (cpus == 4 and int(migrations[1]) == 0) or 'PASS execution:' not in text):
                    raise RuntimeError('execution acceptance identity/coverage mismatch: '+str(report))
                summary['cases'].append(dict(name=f'process-{cpus}cpu', report=str(report.relative_to(ROOT)),
                                             migrations=int(migrations[1]), clock=data['handles']['clock']))
                subprocess.run([sys.executable, str(ROOT/'src/tools/clean-tests.py'), '--root', str(report.parent)], check=True)
            run(make+[str(build/'tests/apps/graphics-check.elf')], 'console-fixtures')
            # The console harness captures guest scheduler/broker/HID state on
            # failure; a failed run is retained, never silently retried.
            command = [sys.executable, str(ROOT/'src/tests/console-boot.py'), str(build),
                       '--cpus=4', '--accel='+args.accel, '--diagnostics']
            for name, flags in (('console', []), ('input-wake', ['--fixtures', '--wait-only'])):
                run(command+flags, name)
                match = re.search(r'report\s+([^\s]+/result\.json)', (session/(name+'.log')).read_text())
                if not match:
                    raise RuntimeError('missing console report: '+name)
                report = Path(match[1])
                if not report.is_absolute():report = ROOT/report
                data = json.loads(report.read_text())
                if data.get('status') != 'passed' or data.get('accelerator') != args.accel or data.get('cpus') != 4:
                    raise RuntimeError('console acceptance identity mismatch: '+str(report))
                summary['cases'].append(dict(name=name, report=str(report.relative_to(ROOT))))
            if fingerprint() != source:
                raise RuntimeError('sources changed during acceptance; rerun the fixed source')
            summary['status'] = 'passed'
        except BaseException as error:
            summary.update(status='failed', reason=str(error))
            raise
        finally:
            write(session/'session.json', summary)
            write(base/'latest.json', dict(status=summary['status'], session=str(session.relative_to(ROOT))))
            print('Session: '+str(session.relative_to(ROOT)), flush=True)
        # Keep reports, snapshots, compiled objects and all failed-run media.
        # Successful process media was cleaned after each accepted run.
        print('PASS execution correctness; no performance qualification is inferred.')

if __name__ == '__main__':
    try:main()
    except Exception as error:
        print('FAILED: '+str(error), file=sys.stderr)
        sys.exit(1)
