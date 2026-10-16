#!/usr/bin/env python3
"""Three deliberate faults in isolated kernels; never linked into normal boot."""
import sys
import pathlib
import shlex
import shutil
import subprocess
import time
root=pathlib.Path(__file__).resolve().parents[2]
build=pathlib.Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'bin/tests/isolated'
if not build.is_relative_to((root/'bin/tests').resolve()): raise SystemExit('Fault tests require an isolated build below bin/tests')
test=build/'tests/inthandle-faults'
test.mkdir(parents=True,exist_ok=True)
stage=test/'iso-root'
if stage.exists(): shutil.rmtree(stage)
shutil.copytree(build/'src-iso-root',stage)
for name in ('nvme.img','sata.img'):
    with (test/name).open('wb') as output: output.truncate(1024**3)
for scenario,vector in ((1,6),(2,14),(3,8)):
    shutil.copyfile(build/f'tests/src-fault-{scenario}.elf',stage/'boot/kernel.elf')
    iso=test/f'fault-{scenario}.iso'
    subprocess.run(['xorriso','-as','mkisofs','-R','-r','-J','--efi-boot',
        'boot/limine/limine-uefi-cd.bin','-efi-boot-part','--efi-boot-image',
        '--protective-msdos-label',str(stage),'-o',str(iso)],check=True,
        stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    plan=subprocess.check_output(['make','-s','-n','-C',str(root/'src'),'run',
        f'BUILD={build}','QEMU_DISPLAY=none','QEMU_ACCEL=tcg',f'ISO={iso}',
        f'NVME_DISK={test}/nvme.img',f'SATA_DISK={test}/sata.img'],text=True)
    command=next(line for line in plan.splitlines() if line.startswith('bash boot/run-qemu.sh'))
    log=test/f'fault-{scenario}.log'
    with log.open('w') as output:
        process=subprocess.Popen(shlex.split(command),cwd=root/'src',stdout=output,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+20
            while time.monotonic()<deadline:
                content=log.read_text()
                if 'inthandle: CPU halted' in content:
                    assert f'vector=0x{vector:016x}' in content,content
                    assert 'rip=0xffffffff8000' in content and 'rsp=0x' in content,content
                    assert all(name+'=0x' in content for name in ('rax','rbx','r15','cr2','cr3'))
                    if scenario==2:
                        assert 'error=0x0000000000000002' in content and 'cr2=0xfffffe0000000000' in content,content
                    if scenario==3: assert 'rsp=0x0000000000000000' in content,content
                    print(f'PASS: inthandle fault vector {vector}; context dump and halt')
                    break
                if process.poll() is not None: raise SystemExit(content)
                time.sleep(.1)
            else: raise SystemExit('Fault test timed out:\n'+log.read_text())
        finally:
            process.terminate();process.wait(timeout=5)
