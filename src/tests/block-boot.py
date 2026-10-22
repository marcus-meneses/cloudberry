#!/usr/bin/env python3
"""Focused actual NVMe/AHCI boot plus GPT/ext4, using only disposable media."""
import argparse,json,pathlib,shutil,subprocess,sys,time,uuid,datetime,fcntl,socket,struct,os
p=argparse.ArgumentParser();p.add_argument('build',type=pathlib.Path);p.add_argument('--backend',choices=('nvme','sata','usb'),required=True);p.add_argument('--cpus',type=int,default=1);p.add_argument('--iommu',choices=('none','intel','amd'),default='none');p.add_argument('--accel',choices=('tcg','kvm'),default='tcg');p.add_argument('--cpu',default='max');p.add_argument('--disk-success',action='store_true');p.add_argument('--disk-modules',action='store_true');p.add_argument('--bootstrap',action='store_true');p.add_argument('--duplicate',action='store_true');p.add_argument('--multi',action='store_true');p.add_argument('--spaces-only',action='store_true');p.add_argument('--process-only',action='store_true');p.add_argument('--profile-handles',action='store_true');p.add_argument('--hub-only',action='store_true');args=p.parse_args()
if args.profile_handles and not 1<=args.cpus<=4:p.error('test-only handle profiling supports one to four CPUs')
root=pathlib.Path(__file__).resolve().parents[2];build=args.build.resolve()
if not build.is_relative_to(root/'bin/tests'):raise SystemExit('isolated build required')
if args.accel=='kvm':subprocess.run([sys.executable,str(root/'src/tests/native-check.py')],check=True)
test=build/'tests'/f'{"process" if args.process_only else "spaces" if args.spaces_only else "hub" if args.hub_only else ("bootstrap-" if args.bootstrap else "block-")+args.backend+("-multi" if args.multi else "")+("-duplicate" if args.duplicate else "")+("-modules-success" if args.disk_success else "-modules" if args.disk_modules else "")}-{args.cpus}cpu-{args.iommu}';test.mkdir(parents=True,exist_ok=True)
lock=(test/'run.lock').open('a')
try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
except BlockingIOError:raise SystemExit('this isolated test is already running; refusing to overwrite its media')
report=test/'report.json';report.unlink(missing_ok=True)
result={'run_id':uuid.uuid4().hex,'status':'running','backend':args.backend,'hub_only':args.hub_only,'spaces_only':args.spaces_only,'process_only':args.process_only,'cpus':args.cpus,'iommu':args.iommu,'accelerator':args.accel,'cpu_spec':args.cpu,'profile_handles':args.profile_handles,'started_at':datetime.datetime.now(datetime.timezone.utc).isoformat()}
def save(): (test/'result.json').write_text(json.dumps(result,indent=2)+'\n')
save()
try:
    stage=test/'stage';shutil.rmtree(stage,ignore_errors=True);shutil.copytree(build/'src-iso-root',stage)
    shutil.copy2(build/('tests/src-process-kernel.elf' if args.process_only else 'tests/src-spaces-kernel.elf' if args.spaces_only else 'tests/src-bootstrap-kernel.elf' if args.bootstrap else 'tests/src-hub-kernel.elf' if args.hub_only else 'tests/src-block-kernel.elf'),stage/'kernel.elf');shutil.copy2(build/'kmodules/probe.kmod',stage/'kmodules/probe.kmod')
    config=stage/"cloudberry.cnf";config.write_text(config.read_text().replace("filesystem.writable=true","filesystem.writable=false"))
    if args.profile_handles:
        if not args.process_only:raise RuntimeError('handle profiling requires process test')
        shutil.copy2(build/'tests/handles-profile.kmod',stage/'kmodules/helpers/handles.kmod')
    with (stage/'limine/limine.conf').open('a') as f:f.write('    module_path: boot():/kmodules/probe.kmod\n    module_cmdline: probe\n')
    fixture=test/'drivers';fixture.mkdir(exist_ok=True);shutil.copytree(build/'src-handler-bundles',fixture,dirs_exist_ok=True);(fixture/'probe.bin').write_bytes(bytes((i*37+11)%256 for i in range(16384)))
    if args.process_only:
        import importlib.util
        spec=importlib.util.spec_from_file_location('runtime_fixture',root/'src/tests/runtime-fixture.py');runtime_fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(runtime_fixture);runtime_fixture.install(fixture,build)
        shutil.copy2(build/'tests/apps/handle-stress.elf',fixture/'handle-stress.elf')
        for name in ('stress-client.json','stress-server.json'):shutil.copy2(root/'src/tests/apps'/name,fixture/name)
        shutil.copy2(build/'tests/apps/handle-bench.elf',fixture/'handle-bench.elf')
        for name in ('bench-client.json','bench-server.json'):shutil.copy2(root/'src/tests/apps'/name,fixture/name)
        shutil.copy2(build/'tests/apps/execution-probe.elf',fixture/'execution-probe.elf');shutil.copy2(build/'tests/apps/process-probe.elf',fixture/'process-probe.elf');shutil.copy2(build/'tests/apps/handles-probe.elf',fixture/'handles-probe.elf');shutil.copy2(root/'src/tests/apps/handles-server.json',fixture/'handles-server.json');shutil.copy2(root/'src/tests/apps/handles-client.json',fixture/'handles-client.json');(fixture/'invalid.elf').write_bytes(b'not an ELF')
    if args.process_only:
        shutil.copytree(build/'src-handler-bundles/keyboard',fixture/'keyboard',dirs_exist_ok=True);shutil.copy2(build/'tests/apps/keyboard-client.elf',fixture/'keyboard-client.elf')
        plain=fixture/'startup-check';plain.mkdir(exist_ok=True)
        shutil.copy2(build/'tests/apps/startup.elf',plain/'entry.elf')
        (plain/'manifest.json').write_text('{"version":1,"resources":[]}')
        (plain/'arbitrary.data').write_text('generic-startup')
        (plain/'provider.cnf').write_text('version=1\nexecutable=entry.elf\nmanifest=manifest.json\ninstallation_id=01020304050607080910111213141516\nfiles=arbitrary.data\n')
        shutil.copytree(build/'src-handler-bundles/keyboard',fixture/'keyboard-fault',dirs_exist_ok=True)
        shutil.copy2(build/'tests/apps/keyboard-fault.elf',fixture/'keyboard-fault/keyboard.elf')
        definition=fixture/'keyboard-fault/provider.cnf';definition.write_text(definition.read_text().replace('636c6f756462657272792e6b62643100','01020304050607080910111213141517'))
        # Prepack the fault provider's startup packet. The probe explicitly
        # binds its PID before start; no test alias is authorized by production HID.
        startup_files=[]
        for startup_name in ('config.cnf','layouts/ibm.json','layouts/abnt2.json'):
            data=(fixture/'keyboard-fault'/startup_name).read_bytes();encoded=startup_name.encode()
            startup_files.append(struct.pack('<QQ',len(encoded),len(data))+encoded+data)
        startup_body=b''.join(startup_files)
        (fixture/'keyboard-fault/startup.bin').write_bytes(struct.pack('<IHHQQ',24,1,0,24+len(startup_body),len(startup_files))+startup_body)
        for policy in ('read','state','denied'):shutil.copy2(root/f'src/tests/apps/keyboard-{policy}.json',fixture/f'keyboard-{policy}.json')
    if args.disk_modules:
        config=stage/'cloudberry.cnf';text=config.read_text();text=text.replace('drivers.modules=','drivers.modules='+('future' if args.disk_success else 'future,future-fail'));config.write_text(text)
        for name in ('future','future-fail'):shutil.copy2(build/f'kmodules/{name}.kmod',fixture/f'{name}.kmod')
        (fixture/'future.driver').write_text('version=1\nmodule=future\ninit=future.init\nstart=future.start\nrequires=clock.now_ns@1.0\npci.class=030000\n')
        (fixture/'future-fail.driver').write_text('version=1\nmodule=future-fail\ninit=future_fail.init\nrequires=future.value@1.2\nmatch=always\n')
    image=test/'boot.img';subprocess.run([sys.executable,str(root/'src/tools/build-boot-image.py'),str(stage),str(image),'--drivers-source',str(fixture)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    cmd=['qemu-system-x86_64','-no-user-config','-nodefaults','-machine','q35','-rtc','base=utc,clock=host','-m','512M','-accel',args.accel,'-cpu',args.cpu,'-smp',str(args.cpus),'-bios','/usr/share/ovmf/OVMF.fd','-device','VGA','-device','qemu-xhci,id=xhci','-device','usb-kbd,id=test-keyboard,bus=xhci.0,port=1','-device','usb-mouse,bus=xhci.0','-drive',f'if=none,id=bootmedia,format=raw,file={image}','-device',('nvme,drive=bootmedia,serial=CLOUDBERRYTEST,bootindex=1' if args.backend=='nvme' else 'usb-storage,bus=xhci.0,drive=bootmedia,bootindex=1' if args.backend=='usb' else 'ide-hd,bus=ide.0,drive=bootmedia,bootindex=1'),'-display','none','-debugcon','stdio','-global','isa-debugcon.iobase=0xe9','-monitor','none','-no-reboot','-no-shutdown']
    if args.duplicate:
        if not args.bootstrap:raise RuntimeError('duplicate fixture is a bootstrap test')
        duplicate=test/'duplicate.img';shutil.copyfile(image,duplicate)
        cmd+=['-drive',f'if=none,id=duplicate,format=raw,file={duplicate}','-device','nvme,drive=duplicate,serial=DUPLICATE']
    if args.multi:
        # Only disposable blank media; keep the boot filesystem unchanged.
        for i in range(3):
            disk=test/f'data{i}.img'
            with disk.open('wb') as f:f.truncate(32*1024*1024)
            cmd+=['-drive',f'if=none,id=data{i},format=raw,file={disk}']
        # Detach bootmedia's original backend; attach it as a boot-only USB disk.
        backend_device=next(i for i,x in enumerate(cmd) if x.startswith('nvme,drive=bootmedia') or x.startswith('ide-hd,bus=ide.0,drive=bootmedia'))
        cmd[backend_device]='usb-storage,bus=xhci.0,drive=bootmedia,bootindex=1'
        if args.backend=='nvme':cmd+=['-device','nvme,id=controller0,serial=MULTI0','-device','nvme-ns,drive=data0,bus=controller0,nsid=1','-device','nvme-ns,drive=data1,bus=controller0,nsid=2','-device','nvme,drive=data2,serial=MULTI1']
        else:cmd+=['-device','ich9-ahci,id=controller1','-device','ide-hd,bus=controller1.0,drive=data0','-device','ide-hd,bus=controller1.1,drive=data1','-device','ide-hd,bus=ide.0,drive=data2']
    if args.iommu!='none':cmd+=['-device',f'{args.iommu}-iommu,intremap=off']
    qmp_path=test/'qmp.sock';qmp_path.unlink(missing_ok=True)
    if args.process_only:
        for name in ('gdb.sock','diagnostics.log','diagnostics-error.txt','execution-state.json'):(test/name).unlink(missing_ok=True)
    if args.process_only:cmd+=['-qmp',f'unix:{qmp_path},server=on,wait=off','-gdb',f'unix:{test / "gdb.sock"},server=on,wait=off']
    log=test/'boot.log'
    with log.open('w') as output:
        process=subprocess.Popen(['bash',str(root/'src/boot/run-qemu.sh')]+cmd,cwd=root/'src',stdout=output,stderr=subprocess.STDOUT)
        try:
            qmp=None;qmp_file=None;qmp_id=0;typed=False;removed=False;added=False;right=False;removed_at=0
            def qmp_call(command,arguments=None):
                global qmp,qmp_file,qmp_id
                if qmp is None:
                    qmp=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);qmp.settimeout(3);qmp.connect(str(qmp_path));qmp_file=qmp.makefile('rwb',buffering=0);json.loads(qmp_file.readline());qmp_call('qmp_capabilities')
                qmp_id+=1;identifier=qmp_id;qmp_file.write((json.dumps({'execute':command,'arguments':arguments or {},'id':identifier})+'\n').encode())
                while True:
                    reply=json.loads(qmp_file.readline())
                    if reply.get('id')==identifier:
                        if 'error' in reply:raise RuntimeError('QMP '+command+': '+str(reply['error']))
                        return reply.get('return')
            deadline=time.monotonic()+90
            while time.monotonic()<deadline:
                text=log.read_text()
                if 'FAIL' in text or 'FAULT' in text or 'inthandle: fatal' in text or 'module startup failed' in text:raise RuntimeError(text[-4000:])
                if args.process_only:
                    if 'READY keyboard input' in text and not typed:qmp_call('send-key',{'keys':[{'type':'qcode','data':'a'}],'hold-time':100});typed=True
                    if 'READY keyboard unplug' in text and not removed:qmp_call('device_del',{'id':'test-keyboard'});removed=True;removed_at=time.monotonic()
                    if removed and not added and time.monotonic()-removed_at>.3:qmp_call('device_add',{'driver':'usb-kbd','id':'test-keyboard-replug','bus':'xhci.0','port':'1'});added=True
                    if 'READY keyboard replug input' in text and not right:qmp_call('send-key',{'keys':[{'type':'qcode','data':'right'}],'hold-time':100});right=True
                if ('PASS process test' if args.process_only else 'PASS address spaces test' if args.spaces_only else 'PASS bootstrap test' if args.bootstrap else 'PASS dynamic hub test' if args.hub_only else 'PASS block backend test') in text and 'managed BSP event stack active' in text:break
                if process.poll() is not None:raise RuntimeError(f'QEMU exited {process.returncode}: {text[-2000:]}')
                time.sleep(.1)
            else:raise RuntimeError('boot timeout: '+log.read_text()[-3000:])
        except BaseException:
            # Snapshot before stopping QEMU. Diagnostics never execute guest
            # methods and never turn a failed functional check into success.
            if args.process_only and process.poll() is None:
                try:
                    commands=test/'diagnostics.gdb'
                    commands.write_text('file '+str(build/'tests/src-process-kernel.elf')+'\nset pagination off\nset print elements 12\nset architecture i386:x86-64\ntarget remote '+str(test/'gdb.sock')+'\nsource '+str(root/'src/tests/console-diagnostics.py')+'\ndetach\nquit\n')
                    with (test/'diagnostics.log').open('w') as diagnostic:
                        subprocess.run(['gdb-multiarch','-nx','-batch','-x',str(commands)],env=dict(os.environ,CB_DIAG_BUILD=str(build),CB_DIAG_REPORT=str(test/'execution-state.json')),stdout=diagnostic,stderr=subprocess.STDOUT,timeout=30,check=True)
                except Exception as error:(test/'diagnostics-error.txt').write_text(str(error))
            raise
        finally:
            if args.process_only:
                if qmp_file:qmp_file.close()
                if qmp:qmp.close()
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    if args.process_only and ('PASS handle concurrency:' not in log.read_text() or 'PASS handle resources:' not in log.read_text() or 'PASS application runtime:' not in log.read_text()):raise RuntimeError('handle concurrency/resource acceptance incomplete')
    if args.process_only and not (typed and removed and added and right and 'PASS keyboard handler' in log.read_text()):raise RuntimeError('keyboard input/hotplug acceptance incomplete')
    if args.disk_modules and 'PASS disk modules' not in log.read_text():raise RuntimeError('disk module acceptance did not run')
    if args.bootstrap and args.duplicate and 'PASS bootstrap duplicate GUID rejected' not in log.read_text():raise RuntimeError('duplicate was not rejected')
    if args.bootstrap and not args.duplicate and 'PASS bootstrap unique GUID' not in log.read_text():raise RuntimeError('unique volume was not selected')
    if args.process_only:
        from handle_report import parse_handles
        result['handles']=parse_handles(log.read_text(),args.cpus)
        if args.profile_handles and ('phases' not in result['handles'] or result['handles']['phases']['heap']['clock_errors'] or result['handles']['phases']['heap']['failures']):raise RuntimeError('missing or invalid handle phase profiling')
    if args.spaces_only:
        from space_report import parse_spaces
        if 'PASS address-space profiling' not in log.read_text():raise RuntimeError('profiling acceptance did not run')
        result['address_spaces']=parse_spaces(log.read_text(),args.cpus)
    subprocess.run([sys.executable,str(root/'src/tests/boot-image.py'),str(image)],check=True)
    result['status']='passed';result['finished_at']=datetime.datetime.now(datetime.timezone.utc).isoformat();save();report.write_text(json.dumps(result,indent=2)+'\n');print('PASS isolated',('protected processes' if args.process_only else 'address spaces' if args.spaces_only else 'dynamic hub' if args.hub_only else args.backend+' boot/GPT/ext4 block backend'),'; report:',report)
except BaseException as error:
    result.update(status='failed',reason=str(error)[-4096:]);save();report.write_text(json.dumps(result,indent=2)+'\n');raise
