#!/usr/bin/env python3
"""Isolated user-mode/handle correctness and repeatable native measurements."""
import argparse
import datetime
import fcntl
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import os
import platform
import statistics

ROOT = Path(__file__).resolve().parents[2]

def write(path, value):
    path.write_text(json.dumps(value, indent=2)+'\n')

def capture(command):
    return subprocess.check_output(command, cwd=ROOT, text=True).strip()

def fingerprint():
    paths = sorted(set(capture(['git', 'ls-files', '-co', '--exclude-standard', 'src']).splitlines()))
    h = hashlib.sha256()
    for name in paths:
        p = ROOT/name
        if p.is_file():
            h.update(name.encode()+b'\0'+p.read_bytes())
    return h.hexdigest()

def cpu_identity(text):
    keep={'vendor_id','model name','flags','microcode'}
    return {key.strip():value.strip() for line in text.splitlines() if ':' in line for key,value in [line.split(':',1)] if key.strip() in keep}

def comparable_host(host):
    out=dict(host)
    if isinstance(out.get('cpu'),str):out['cpu']=cpu_identity(out['cpu'])
    return out

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda:source.read(1048576),b''):h.update(chunk)
    return h.hexdigest()

def logged(command, path):
    print('  log:', path.relative_to(ROOT), flush=True)
    with path.open('w') as out:
        result = subprocess.run(command, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f'command exited {result.returncode}; see {path}')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--case', action='append', choices=('1cpu-none','4cpu-amd','4cpu-intel'))
    parser.add_argument('--accel', choices=('kvm','tcg'), default='kvm', help='TCG validates the harness, not native performance')
    parser.add_argument('--reference-session', type=Path)
    parser.add_argument('--build',type=Path,default=ROOT/'bin/tests/kvm-handles-build')
    args = parser.parse_args()
    if args.runs<1 or args.jobs<1:parser.error('runs/jobs must be positive')
    if args.accel=='kvm':subprocess.run([sys.executable,str(ROOT/'src/tests/native-check.py')],check=True)
    base=ROOT/'bin/tests/kvm-handles';base.mkdir(parents=True,exist_ok=True)
    with (base/'run.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        session=base/datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ');session.mkdir()
        build=args.build.resolve()
        if not build.is_relative_to(ROOT/'bin/tests'):raise RuntimeError('isolated build below bin/tests required')
        build.mkdir(parents=True,exist_ok=True)
        host=dict(kernel=platform.release(),architecture=platform.machine(),cpu=cpu_identity(Path('/proc/cpuinfo').read_text().split('\n\n')[0]),qemu=capture(['qemu-system-x86_64','--version']),compiler=capture([str(ROOT/'.toolchain/cross/bin/x86_64-elf-gcc'),'--version']),affinity=sorted(os.sched_getaffinity(0)))
        source=fingerprint();write(session/'host.json',host)
        summary=dict(status='running',accelerator=args.accel,runs=args.runs,source_sha256=source,git_revision=capture(['git','rev-parse','HEAD']),cases={})
        archive=Path(str(session)+'.tar.gz');failure=None
        try:
            names=('cloudberry.iso','tests/src-process-kernel.elf','kmodules/probe.kmod','tests/apps/process-probe.elf','tests/apps/handles-probe.elf','tests/apps/keyboard-client.elf','tests/apps/keyboard-fault.elf','tests/apps/handle-bench.elf','tests/apps/startup.elf')
            command=['make','-C','src',f'-j{args.jobs}',f'BUILD={build}',f'ISO={build}/cloudberry.iso',f'KERNEL={build}/kernel.elf',f'STAGE={build}/src-iso-root']+[str(build/n) for n in names]
            logged(command,session/'build.log')
            identities={n:digest(build/n) for n in names};write(session/'binaries.json',identities)
            for case in args.case or ('1cpu-none','4cpu-amd','4cpu-intel'):
                cpus=int(case[0]);iommu=case.split('-')[1];reports=[]
                for repeat in range(1,args.runs+1):
                    target=session/case/f'run-{repeat:02d}';target.mkdir(parents=True)
                    live=build/'tests'/f'process-{cpus}cpu-{iommu}'
                    for name in ('report.json','result.json','boot.log'):(live/name).unlink(missing_ok=True)
                    command=[sys.executable,str(ROOT/'src/tests/block-boot.py'),str(build),'--backend=usb','--process-only',f'--cpus={cpus}',f'--iommu={iommu}',f'--accel={args.accel}','--cpu='+('host,migratable=off,+invtsc' if args.accel=='kvm' else 'max')]
                    print(f'{args.accel}: {case}, run {repeat}/{args.runs}',flush=True);write(target/'command.json',command)
                    try:logged(command,target/'runner.log')
                    finally:
                        for name in ('report.json','result.json','boot.log'):
                            if (live/name).exists():shutil.copy2(live/name,target/name)
                    r=json.loads((target/'report.json').read_text())
                    if r.get('status')!='passed' or r.get('accelerator')!=args.accel or r.get('cpus')!=cpus or r.get('iommu')!=iommu:raise RuntimeError('guest acceptance identity mismatch')
                    if 'handles' not in r:raise RuntimeError('missing handle measurements')
                    h=r['handles']
                    if args.accel=='kvm' and (h['clock']['source']!=1 or any(m['unavailable_samples'] for m in h['locks'])):raise RuntimeError('native timing requires stable qualified TSC and available profiling samples; inspect the saved guest log')
                    reports.append(r)
                summary['cases'][case]={name:dict(median_of_boot_medians=statistics.median(r['handles']['measurements'][name]['median'] for r in reports),boot_medians=[r['handles']['measurements'][name]['median'] for r in reports]) for name in reports[0]['handles']['measurements']}
            if source!=fingerprint() or any(digest(build/n)!=value for n,value in identities.items()):raise RuntimeError('sources/binaries changed during measurements')
            if args.reference_session:
                ref=args.reference_session.resolve();old=json.loads((ref/'session.json').read_text());old_host=json.loads((ref/'host.json').read_text())
                if old.get('status')!='passed' or old.get('accelerator')!=args.accel or comparable_host(old_host)!=comparable_host(host):raise RuntimeError('reference host/accelerator differs or reference failed')
                comparison={}
                for case,values in summary['cases'].items():
                    if case not in old['cases']:raise RuntimeError('reference lacks case '+case)
                    comparison[case]={name:dict(previous_ns=old['cases'][case][name]['median_of_boot_medians'],current_ns=v['median_of_boot_medians'],ratio=v['median_of_boot_medians']/old['cases'][case][name]['median_of_boot_medians'] if old['cases'][case][name]['median_of_boot_medians'] else None) for name,v in values.items()}
                write(session/'comparison.json',comparison)
            summary['status']='passed'
        except BaseException as error:
            failure=error;summary.update(status='failed',reason=str(error))
        finally:
            write(session/'session.json',summary);write(base/'latest.json',dict(status=summary['status'],session=str(session.relative_to(ROOT))))
            with tarfile.open(archive,'w:gz') as out:out.add(session,arcname=session.name)
            print('Session:',session.relative_to(ROOT));print('Archive:',archive.relative_to(ROOT))
        if failure:raise failure
        for case,values in summary['cases'].items():print(case,', '.join(f"{name}: {v['median_of_boot_medians']:.0f} ns" for name,v in values.items()))

if __name__=='__main__':
    try:main()
    except Exception as error:print('FAILED:',error,file=sys.stderr);sys.exit(1)
