#!/usr/bin/env python3
"""Build once, run isolated native address-space measurements, archive evidence."""
import argparse
import datetime
import fcntl
import hashlib
import json
import os
import platform
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
from space_compare import CASES, compare, guest_fingerprint, markdown, validate_native, load_native_reference

ROOT = Path(__file__).resolve().parents[2]


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2)+'\n')


def capture(command):
    return subprocess.check_output(command, cwd=ROOT, text=True).strip()


def host_info():
    cpu = {}
    path = Path('/proc/cpuinfo')
    if path.exists():
        for line in path.read_text().split('\n\n')[0].splitlines():
            key, _, value = line.partition(':')
            if key.strip() in ('vendor_id', 'model name', 'flags', 'microcode'):
                cpu[key.strip()] = value.strip()
    compiler = ROOT/'.toolchain/cross/bin/x86_64-elf-gcc'
    return dict(kernel=platform.release(), architecture=platform.machine(), cpu=cpu,
                logical_cpus=os.cpu_count(), affinity_cpus=sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None,
                compiler=capture([str(compiler), '--version']) if compiler.exists() else 'See build.log for compiler path',
                qemu=capture(['qemu-system-x86_64', '--version']),
                git_revision=capture(['git', 'rev-parse', 'HEAD']),
                working_tree_dirty=bool(capture(['git', 'status', '--porcelain'])))


def logged(command, path):
    print('  log:', path.relative_to(ROOT), flush=True)
    with path.open('w') as output:
        status = subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT).returncode
    if status:
        print('\n'.join(path.read_text().splitlines()[-20:]), file=sys.stderr)
        raise RuntimeError(f'Command exited {status}; see {path}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runs', type=int, default=1, help='independent boots per configuration (default: 1)')
    parser.add_argument('--jobs', type=int, default=min(4, os.cpu_count() or 1))
    parser.add_argument('--reference-session', type=Path, help='accepted previous KVM session (default: latest passed session, if available)')
    args = parser.parse_args()
    if args.runs < 1 or args.jobs < 1:
        parser.error('--runs and --jobs must be positive')
    # Fail before compiling or making images; never change permissions or sudo.
    subprocess.run([sys.executable, str(ROOT/'src/tests/native-check.py')], check=True)
    output_root = ROOT/'bin/tests/kvm-spaces'
    output_root.mkdir(parents=True, exist_ok=True)
    with (output_root/'run.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError('Another KVM benchmark is already using this build directory')
        reference_path = args.reference_session
        if reference_path is None and (output_root/'latest.json').exists():
            previous = json.loads((output_root/'latest.json').read_text())
            if previous.get('status') == 'passed':
                reference_path = ROOT/previous['session']
        native_reference = load_native_reference(reference_path) if reference_path else None
        session = output_root/datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
        session.mkdir()
        build = ROOT/'bin/tests/kvm-spaces-build'
        build.mkdir(parents=True, exist_ok=True)
        baseline = json.loads((ROOT/'src/tests/baselines/address-spaces-tcg.json').read_text())
        fingerprint = guest_fingerprint(ROOT)
        summary = dict(status='running', runs=args.runs, session=str(session.relative_to(ROOT)),
                       guest_source_sha256=fingerprint)
        archive = Path(str(session)+'.tar.gz')
        summary['archive'] = str(archive.relative_to(ROOT))
        reports = {}
        failed = None
        try:
            host = host_info()
            write_json(session/'host.json', host)
            if native_reference:
                write_json(session/'native-reference.json', native_reference)
                summary['reference_session'] = str(reference_path)
            write_json(session/'tcg-baseline.json', baseline)
            write_json(session/'session.json', summary)
            print('Building isolated benchmark once; existing artifacts are reused.', flush=True)
            command = ['make', '-C', 'src', f'-j{args.jobs}', f'BUILD={build}',
                       'X2APIC=0', f'ISO={build}/cloudberry.iso', f'KERNEL={build}/kernel.elf', f'STAGE={build}/src-iso-root',
                       f'{build}/cloudberry.iso', f'{build}/tests/src-spaces-kernel.elf', f'{build}/kmodules/probe.kmod']
            logged(command, session/'build.log')
            binaries = [build/'tests/src-spaces-kernel.elf', build/'kmodules/probe.kmod']
            identity = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in binaries}
            write_json(session/'guest-binaries.json', identity)
            for cpus, iommu in CASES:
                key = f'{cpus}cpu-{iommu}'
                reports[key] = []
                for repetition in range(1, args.runs+1):
                    dest = session/'cases'/key/f'run-{repetition:02d}'
                    dest.mkdir(parents=True)
                    live = build/'tests'/f'spaces-{cpus}cpu-{iommu}'
                    # These are script-owned disposable results, never TCG baselines.
                    for name in ('report.json', 'result.json', 'boot.log'):
                        (live/name).unlink(missing_ok=True)
                    command = [sys.executable, str(ROOT/'src/tests/block-boot.py'), str(build),
                               '--backend=usb', '--spaces-only', f'--cpus={cpus}', f'--iommu={iommu}',
                               '--accel=kvm', '--cpu=host,migratable=off,+invtsc']
                    write_json(dest/'command.json', command)
                    print(f'KVM: {key}, boot {repetition}/{args.runs}', flush=True)
                    try:
                        logged(command, dest/'runner.log')
                    finally:
                        for name in ('report.json', 'result.json', 'boot.log'):
                            if (live/name).exists():
                                shutil.copy2(live/name, dest/name)
                    report = json.loads((dest/'report.json').read_text())
                    validate_native(report, cpus, iommu)
                    reports[key].append(report)
            if guest_fingerprint(ROOT) != fingerprint or any(hashlib.sha256(p.read_bytes()).hexdigest() != identity[p.name] for p in binaries):
                raise RuntimeError('Guest sources or binaries changed during the benchmark; rerun')
            result = compare(baseline, reports, fingerprint)
            write_json(session/'comparison.json', result)
            (session/'comparison.md').write_text(markdown(result))
            if native_reference:
                native = compare(native_reference, reports, fingerprint, reference_kind='previous_kvm')
                native['reference_session'] = str(reference_path)
                # A new binary is expected; machine/execution changes need separate scrutiny.
                native['host_warnings'] = [f'Host {field} differs from reference.' for field in ('cpu','kernel','qemu','affinity_cpus','compiler') if host.get(field) != native_reference['host'].get(field)]
                write_json(session/'native-comparison.json', native)
                (session/'native-comparison.md').write_text(markdown(native)+'\n'+'\n'.join(native['host_warnings'])+'\n')
            summary['status'] = 'passed'
        except BaseException as error:
            failed = error
            summary.update(status='failed', error=str(error))
        finally:
            write_json(session/'session.json', summary)
            # Deliberately omit the reusable build directory and all VM disk images.
            with tarfile.open(archive, 'w:gz') as bundle:
                bundle.add(session, arcname=session.name)
            summary['archive'] = str(archive.relative_to(ROOT))
            temporary = output_root/'latest.tmp'
            write_json(temporary, summary)
            temporary.replace(output_root/'latest.json')
        print('Results:', session.relative_to(ROOT))
        print('Archive:', archive.relative_to(ROOT))
        if failed:
            print('FAILED:', failed, file=sys.stderr)
            return 1
        print('Comparison:', (session/'comparison.md').relative_to(ROOT))
        if native_reference:
            print('Before/after KVM:', (session/'native-comparison.md').relative_to(ROOT))
        print('Done. Reply "done" so the saved reports can be inspected.')
        return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
