#!/usr/bin/env python3
"""Focused actual NVMe/AHCI boot plus GPT/ext4, using only disposable media."""
import argparse,json,pathlib,shutil,subprocess,sys,time,uuid,datetime
p=argparse.ArgumentParser();p.add_argument('build',type=pathlib.Path);p.add_argument('--backend',choices=('nvme','sata'),required=True);p.add_argument('--cpus',type=int,default=1);p.add_argument('--iommu',choices=('none','intel','amd'),default='none');p.add_argument('--accel',choices=('tcg','kvm'),default='tcg');p.add_argument('--cpu',default='max');args=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[2];build=args.build.resolve()
if not build.is_relative_to(root/'bin/tests'):raise SystemExit('isolated build required')
if args.accel=='kvm':subprocess.run([sys.executable,str(root/'src/tests/native-check.py')],check=True)
test=build/'tests'/f'block-{args.backend}-{args.cpus}cpu-{args.iommu}';test.mkdir(parents=True,exist_ok=True)
report=test/'report.json';report.unlink(missing_ok=True)
result={'run_id':uuid.uuid4().hex,'status':'running','backend':args.backend,'cpus':args.cpus,'iommu':args.iommu,'accelerator':args.accel,'started_at':datetime.datetime.now(datetime.timezone.utc).isoformat()}
def save(): (test/'result.json').write_text(json.dumps(result,indent=2)+'\n')
save()
try:
    stage=test/'stage';shutil.rmtree(stage,ignore_errors=True);shutil.copytree(build/'src-iso-root',stage)
    shutil.copy2(build/'tests/src-block-kernel.elf',stage/'boot/kernel.elf');shutil.copy2(build/'kmodules/probe.kmod',stage/'boot/kmodules/probe.kmod')
    with (stage/'boot/limine/limine.conf').open('a') as f:f.write('    module_path: boot():/boot/kmodules/probe.kmod\n    module_cmdline: probe\n')
    fixture=test/'drivers';fixture.mkdir(exist_ok=True);(fixture/'probe.bin').write_bytes(bytes((i*37+11)%256 for i in range(16384)))
    image=test/'boot.img';subprocess.run([sys.executable,str(root/'src/tools/build-boot-image.py'),str(stage),str(image),'--drivers-source',str(fixture)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    cmd=['qemu-system-x86_64','-no-user-config','-nodefaults','-machine','q35','-m','512M','-accel',args.accel,'-cpu',args.cpu,'-smp',str(args.cpus),'-bios','/usr/share/ovmf/OVMF.fd','-device','VGA','-device','qemu-xhci,id=xhci','-device','usb-kbd,bus=xhci.0','-device','usb-mouse,bus=xhci.0','-drive',f'if=none,id=bootmedia,format=raw,file={image}','-device',('nvme,drive=bootmedia,serial=CLOUDBERRYTEST,bootindex=1' if args.backend=='nvme' else 'ide-hd,bus=ide.0,drive=bootmedia,bootindex=1'),'-display','none','-debugcon','stdio','-global','isa-debugcon.iobase=0xe9','-monitor','none','-no-reboot','-no-shutdown']
    if args.iommu!='none':cmd+=['-device',f'{args.iommu}-iommu,intremap=off']
    log=test/'boot.log'
    with log.open('w') as output:
        process=subprocess.Popen(['bash',str(root/'src/boot/run-qemu.sh')]+cmd,cwd=root/'src',stdout=output,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+90
            while time.monotonic()<deadline:
                text=log.read_text()
                if 'FAIL' in text or 'FAULT' in text or 'module startup failed' in text:raise RuntimeError(text[-4000:])
                if 'PASS block backend test' in text and 'managed BSP event stack active' in text:break
                if process.poll() is not None:raise RuntimeError(f'QEMU exited {process.returncode}: {text[-2000:]}')
                time.sleep(.1)
            else:raise RuntimeError('boot timeout: '+log.read_text()[-3000:])
        finally:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    subprocess.run([sys.executable,str(root/'src/tests/boot-image.py'),str(image)],check=True)
    result['status']='passed';save();report.write_text(json.dumps(result,indent=2)+'\n');print('PASS isolated',args.backend,'boot/GPT/ext4 block backend; report:',report)
except BaseException as error:
    result.update(status='failed',reason=str(error)[-4096:]);save();raise
