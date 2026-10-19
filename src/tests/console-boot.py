#!/usr/bin/env python3
"""Exercise production providers/terminal on a disposable writable image via USB HID.
Fixtures are built separately; no developer NVMe/SATA media or candidate image is written.
"""
import argparse,importlib.util,json,pathlib,re,shutil,socket,subprocess,time,uuid,struct,os,threading
p=argparse.ArgumentParser();p.add_argument('build',type=pathlib.Path);p.add_argument('--fixtures',action='store_true');p.add_argument('--startup-only',action='store_true');p.add_argument('--startup-trace',action='store_true');p.add_argument('--profile-only',action='store_true');p.add_argument('--render-only',action='store_true');p.add_argument('--scroll-only',action='store_true');p.add_argument('--diagnostics',action='store_true');p.add_argument('--kernel',type=pathlib.Path,help='test-only replacement kernel, in a copied stage');p.add_argument('--mmio-trace',action='store_true');p.add_argument('--storage-devices',action='store_true',help='attach disposable 1 GiB NVMe and SATA disks');p.add_argument('--cpus',type=int,default=4);p.add_argument('--accel',default='tcg',choices=['tcg','kvm']);a=p.parse_args()
if sum((a.startup_only,a.profile_only,a.scroll_only,a.render_only))>1:p.error('choose only one focused run mode')
if a.startup_only and a.fixtures:p.error('startup timing uses production apps; omit --fixtures')
if (a.kernel or a.profile_only or a.scroll_only or a.render_only) and not a.fixtures:p.error('--kernel/--profile-only/--scroll-only requires --fixtures')
if a.startup_trace and (not a.startup_only or a.kernel):p.error('--startup-trace requires --startup-only and cannot use --kernel')
root=pathlib.Path(__file__).resolve().parents[2];build=a.build.resolve();work=root/'bin/tests/console'/uuid.uuid4().hex;work.mkdir(parents=True)
image=work/'boot.img'
if a.startup_trace:
    with (work/'trace-build.log').open('w') as trace_log:subprocess.run(['make','-C',str(root/'src'),'BUILD='+str(build),str(build/'tests/startup-kernel.elf'),str(build/'tests/startup-bootstrap.kmod')],check=True,stdout=trace_log,stderr=subprocess.STDOUT)
    a.kernel=build/'tests/startup-kernel.elf'
if a.fixtures or a.startup_trace:
    stage=work/'stage';shutil.copytree(build/'src-iso-root',stage)
    if a.kernel:shutil.copy2(a.kernel,stage/'boot/kernel.elf')
    if a.startup_trace:
        paths=list(stage.rglob('bootstrap.kmod'));assert len(paths)==1;shutil.copy2(build/'tests/startup-bootstrap.kmod',paths[0])
    bundles=work/'bundles';shutil.copytree(build/'src-handler-bundles',bundles)
    if a.fixtures:
        directory=bundles/'graphics-check';directory.mkdir();shutil.copy2(build/'tests/apps/graphics-check.elf',directory/'graphics-check.elf')
        operations=['info','fill','glyph','surface_create','surface_release','surface_info','state','line','rectangle','ellipse','arc','bezier','path','bitmap','blit','present']
        (directory/'manifest.json').write_text(json.dumps(dict(version=1,resources=[dict(name='/@graphics',operations=operations),dict(name='/@applications',operations=['launch','wait','release']),dict(name='/@streams',operations=['read','write']),dict(name='/@text',operations=['write','clear']),dict(name='/@io',scope='self',operations=['now_ns']),dict(name='/@filesystem',operations=['open','stat','remove'])])))
        records=json.loads((root/'src/apps/installed.json').read_text());records.append(dict(name='graphics-check',executable='/system/apps/graphics-check',manifest='graphics-check/manifest.json',installation_id='636c6f756462657272792e6766786368',flags=3,cpu=0,stack_bytes=262144));spec=work/'installed.json';spec.write_text(json.dumps(records));subprocess.run(['python3',str(root/'src/tools/build-installations.py'),str(spec),str(bundles),str(bundles/'installed.bin')],check=True);shutil.copy2(bundles/'installed.bin',bundles/'applications/catalog.bin')
        name=b'graphics-check';arguments=name+b'\0!supervise\0';(bundles/'applications/initial.bin').write_bytes(struct.pack('<IHHQQQQ',40,1,0,len(name),2,len(arguments),1)+name+arguments+b'/')
        (bundles/'pipe-source.bin').write_bytes(bytes((i*37+11)%256 for i in range(65536+317)))
    subprocess.run(['python3',str(root/'src/tools/build-boot-image.py'),str(stage),str(image),'--drivers-source',str(bundles)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
else:subprocess.run(['cp','--reflink=auto','--sparse=always',str(build/'cloudberry.iso'),str(image)],check=True)
qmp=work/'qmp.sock';log=work/'boot.log';screenshot=work/'screen.ppm'
command=['qemu-system-x86_64','-no-user-config','-nodefaults','-machine','q35','-m','512M','-accel',a.accel,'-cpu','host,migratable=off,+invtsc' if a.accel=='kvm' else 'max','-smp',str(a.cpus),'-bios','/usr/share/ovmf/OVMF.fd','-device','VGA','-device','qemu-xhci,id=xhci','-device','usb-kbd,bus=xhci.0','-device','usb-mouse,bus=xhci.0','-drive',f'if=none,id=boot,format=raw,file={image}','-device','usb-bot,id=bot,bus=xhci.0','-device','scsi-hd,bus=bot.0,drive=boot,bootindex=1','-display','none','-debugcon','stdio','-global','isa-debugcon.iobase=0xe9','-monitor','none','-qmp',f'unix:{qmp},server=on,wait=off','-no-reboot','-no-shutdown']
if a.diagnostics:command+=['-gdb','unix:'+str(work/'gdb.sock')+',server=on,wait=off']
if a.mmio_trace:command+=['-trace','enable=memory_region_ops_read','-trace','enable=memory_region_ops_write','-trace','file='+str(work/'mmio.log')]
if a.storage_devices:
    for name in ('nvme','sata'):
        with (work/(name+'.img')).open('wb') as disk:disk.truncate(1024**3)
        command+=['-drive',f'if=none,id={name},format=raw,file={work/(name+".img")}']
    command+=['-device','nvme,drive=nvme,serial=CONSOLETESTNVME','-device','ide-hd,bus=ide.0,drive=sata']
font_source=(root/'src/handlers/graphics/font.h').read_text();font=[]
for row in re.findall(r'\{([0-9,]+)\}',font_source):font.append(tuple(map(int,row.split(','))))
fontmap={pattern:chr(i+32) for i,pattern in enumerate(font)}
class ProfileComplete(Exception):pass
status='failed';failure='';proc=None;reader=None;milestones=[];boot_timing={}
def capture_diagnostics():
    commands=work/'diagnostics.gdb'
    commands.write_text('file '+str(a.kernel.resolve() if a.kernel else build/'kernel.elf')+'\nset pagination off\nset print elements 12\nset architecture i386:x86-64\ntarget remote '+str(work/'gdb.sock')+'\nsource '+str(root/'src/tests/console-diagnostics.py')+'\ndetach\nquit\n')
    with (work/'diagnostics.log').open('w') as output:
        subprocess.run(['gdb-multiarch','-nx','-batch','-x',str(commands)],env=dict(os.environ,CB_DIAG_BUILD=str(build),CB_DIAG_REPORT=str(work/'performance-state.json'),**({'CB_DIAG_BOOTSTRAP':str(build/'tests/startup-bootstrap.elf')} if a.startup_trace else {})),stdout=output,stderr=subprocess.STDOUT,timeout=30)
try:
    out=log.open('w');launched=time.monotonic();proc=subprocess.Popen(['bash',str(root/'src/boot/run-qemu.sh')]+command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    def capture_log():
        for raw in iter(proc.stdout.readline,b''):
            observed=time.monotonic()-launched;line=raw.decode(errors='replace');out.write(line);out.flush()
            if line.startswith(('Cloudberry','bootstrap:','BOOT-TIME')):milestones.append(dict(seconds=observed,message=line.rstrip()))
    reader=threading.Thread(target=capture_log,daemon=True);reader.start()
    until=time.monotonic()+60
    while not qmp.exists():
        if proc.poll() is not None or time.monotonic()>until:raise RuntimeError('QMP startup failed')
        time.sleep(.05)
    connection=socket.socket(socket.AF_UNIX);connection.connect(str(qmp));connection.settimeout(10);stream=connection.makefile('rwb',buffering=0);json.loads(stream.readline());counter=0
    def call(execute,arguments=None):
        global counter
        counter+=1;stream.write(json.dumps(dict(execute=execute,arguments=arguments or {},id=counter)).encode()+b'\n')
        while True:
            result=json.loads(stream.readline())
            if result.get('id')==counter:
                if 'error' in result:raise RuntimeError(str(result['error']))
                return result.get('return')
    call('qmp_capabilities')
    def screen():
        call('screendump',{'filename':str(screenshot)})
        data=screenshot.read_bytes();magic,dimensions,maximum,pixels=data.split(b'\n',3);assert magic==b'P6' and maximum==b'255';w,h=map(int,dimensions.split());lines=[]
        for y in range(h//16):
            line=''
            for x in range(w//8):
                pattern=[]
                for row in range(7):
                    bits=0
                    for col in range(5):
                        off=((y*16+1+row*2)*w+x*8+1+col)*3
                        bits=bits*2+int(max(pixels[off:off+3])>100)
                    pattern.append(bits)
                line+=fontmap.get(tuple(pattern),'?')
            lines.append(line.rstrip())
        text='\n'.join(lines);(work/'screen.txt').write_text(text);return text
    def wait_for(check,seconds=40):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            if proc.poll() is not None:raise RuntimeError('VM exited')
            text=log.read_text()
            if 'FATAL' in text or 'startup failed' in text or 'Cloudberry: fatal' in text or 'inthandle: fatal' in text:raise RuntimeError('guest startup failed')
            rendered=screen()
            if 'keyboard input failed' in rendered:raise RuntimeError('keyboard capture/provider failed; see diagnostics.log')
            if 'terminal exited:' in rendered:raise RuntimeError('terminal exited: '+rendered.split('terminal exited:')[1].split('\n')[0])
            if check(rendered):return
            time.sleep(.3)
        raise RuntimeError('screen check timeout; see screen.txt and boot.log')
    wait_for(lambda text:'/ >' in text,120 if a.startup_only else 90)
    boot_timing['first_prompt_seconds']=time.monotonic()-launched
    def send_keys(keys,hold=.035):
        # QMP send-key serializes delayed edges using guest virtual time.
        # Explicit edges make injection independent of guest timer progress.
        def events(down):return [{'type':'key','data':{'down':down,'key':key}} for key in keys]
        call('input-send-event',{'events':events(True)});time.sleep(hold)
        call('input-send-event',{'events':events(False)});time.sleep(.03)
    def type_line(text):
        base={' ':'spc','/':'slash','-':'minus','.':'dot','=':'equal',"'":'apostrophe'};shift={'>':'dot','|':'backslash','"':'apostrophe','_':'minus'}
        for c in text:
            code=shift.get(c,base.get(c,c.lower()));keys=([{'type':'qcode','data':'shift'}] if c in shift or c.isupper() else [])+[{'type':'qcode','data':code}]
            send_keys(keys)
        send_keys([{'type':'qcode','data':'ret'}]);time.sleep(.1)
    if a.startup_only:
        pressed=time.monotonic();send_keys([{'type':'qcode','data':'x'}]);wait_for(lambda text:'/ > x' in text,15)
        boot_timing.update(interactive_seconds=time.monotonic()-launched,key_to_echo_seconds=time.monotonic()-pressed,cpus=a.cpus,accelerator=a.accel,storage_devices=a.storage_devices,phase_tracing=a.startup_trace,includes='QEMU launch, firmware, Limine, kernel, providers and first prompt; image preparation excluded',observation='host monotonic clock; prompt/echo detected by framebuffer polling; detection includes polling/decoding delay')
        raise ProfileComplete()
    if a.scroll_only:
        started=time.monotonic();type_line('graphics-check scroll');wait_for(lambda text:'SCROLL 59\n/ >' in text and all(f'SCROLL {i:02}' in text for i in range(12,60)) and 'SCROLL 00' not in text,90)
        (work/'scroll.json').write_text(json.dumps(dict(wall_seconds=time.monotonic()-started,cpus=a.cpus,accelerator=a.accel),indent=2)+'\n');raise ProfileComplete()
    if a.render_only:
        started=time.monotonic();type_line('graphics-check render');wait_for(lambda text:'RENDER DONE' in text,120)
        values={k:int(v) for k,v in re.findall(r'PROFILE ((?:clear|scroll)[0-4]_ns)=([0-9]+)',screen())};assert len(values)==10
        import statistics
        summary={kind:statistics.median(values[f'{kind}{i}_ns'] for i in range(5)) for kind in ('clear','scroll')}
        (work/'render.json').write_text(json.dumps(dict(samples=values,median_ns=summary,wall_seconds=time.monotonic()-started,accelerator=a.accel,cpus=a.cpus),indent=2)+'\n');raise ProfileComplete()
    if a.profile_only:
        started=time.monotonic();type_line('graphics-check profile');wait_for(lambda text:'PROFILE DONE' in text and text.count('/ >')>=2,90)
        values={k:int(v) for k,v in re.findall(r'PROFILE ([a-z0-9_]+_ns)=([0-9]+)',screen())};assert len(values)==7
        (work/'profile.json').write_text(json.dumps(dict(samples=values,wall_seconds=time.monotonic()-started,accelerator=a.accel,cpus=a.cpus),indent=2)+'\n');raise ProfileComplete()
    type_line('echo hello |> cat => /terminal-check.txt')
    wait_for(lambda text:text.count('/ >')>=2)
    type_line('echo again -> /terminal-check.txt')
    wait_for(lambda text:text.count('/ >')>=3)
    type_line('cat /terminal-check.txt')
    wait_for(lambda text:'hello\nagain' in text and text.count('/ >')>=4)
    type_line('pwd |> cat')
    wait_for(lambda text:text.count('/ >')>=5)
    type_line('echo blocked => /boot/forbidden.txt')
    wait_for(lambda text:'permission denied' in text and text.count('/ >')>=6)
    if a.fixtures:
        type_line('graphics-check')
        wait_for(lambda text:'graphics ok' in text and 'stderr ok' in text and text.count('/ >')>=7)
        type_line('cat /boot/kmodules/drivers/pipe-source.bin |> cat => /pipe-copy.bin')
        wait_for(lambda text:text.count('/ >')>=8,90)
    type_line('cat /missing-file')
    wait_for(lambda text:'not found' in text and text.count('/ >')>=(9 if a.fixtures else 7))
    type_line('cat')
    type_line('stdin-check')
    wait_for(lambda text:text.count('stdin-check')>=2)
    send_keys([{'type':'qcode','data':'ctrl'},{'type':'qcode','data':'d'}],.06)
    wait_for(lambda text:text.count('/ >')>=(10 if a.fixtures else 8))
    prompts=10 if a.fixtures else 8
    type_line('echo saved-history => /system/root/_apps/terminal.data/history')
    wait_for(lambda text:text.count('/ >')>=prompts+1)
    type_line('cat /system/root/_apps/terminal.data/history')
    wait_for(lambda text:'\nsaved-history\n' in text and text.count('/ >')>=prompts+2)
    type_line('ls /system/root/_apps')
    wait_for(lambda text:text.count('/ >')>=prompts+3)
    assert '_index' not in screen(), 'default listing exposed hidden index'
    type_line('ls -a /system/root/_apps')
    wait_for(lambda text:'_index' in text and text.count('/ >')>=prompts+4)
    # Idle HID interrupt-IN receives stay armed. Resume input after more than
    # the former idle timeout; a live publication alone is not proof.
    time.sleep(2.2)
    type_line('pwd')
    wait_for(lambda text:text.count('/ >')>=prompts+5)
    if a.fixtures:
        type_line('graphics-check scroll')
        wait_for(lambda text:'SCROLL 59\n/ >' in text,90)
    status='passed'
except ProfileComplete:
    status="passed"
    if a.diagnostics and (a.profile_only or a.render_only):
        try:capture_diagnostics()
        except Exception as error:(work/'diagnostics-error.txt').write_text(str(error))
except Exception as e:
    failure=str(e)
    if a.diagnostics and proc and proc.poll() is None:
        try:
            capture_diagnostics()
        except Exception as error:(work/'diagnostics-error.txt').write_text(str(error))
finally:
    if proc:
        proc.terminate()
        try:proc.wait(timeout=5)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
    if reader:reader.join(timeout=5);out.close()
    boot_timing['milestones']=milestones
    boot_timing['qemu_command']=command
    (work/'startup.json').write_text(json.dumps(boot_timing,indent=2)+'\n')
    if status=='passed' and a.fixtures and not a.profile_only and not a.scroll_only and not a.render_only and not a.startup_only:
        try:
            spec=importlib.util.spec_from_file_location('boot_image',root/'src/tools/build-boot-image.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);partition=next(p for p in module.partitions(image) if p['index']==4);raw=work/'ext4.img'
            with image.open('rb') as source,raw.open('wb') as target:
                source.seek(partition['start']*512);remaining=partition['sectors']*512
                while remaining:
                    data=source.read(min(1024*1024,remaining));target.write(data);remaining-=len(data)
            copied=subprocess.run(['debugfs','-R','cat /pipe-copy.bin',str(raw)],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE).stdout
            if copied!=bytes((i*37+11)%256 for i in range(65536+317)):raise RuntimeError('large-pipe file differs from source')
            raw.unlink()
        except Exception as e:status='failed';failure=str(e)
    (work/'result.json').write_text(json.dumps(dict(status=status,error=failure,cpus=a.cpus,accelerator=a.accel,storage_devices=a.storage_devices,diagnostic_kernel=str(a.kernel) if a.kernel else None,mmio_trace=a.mmio_trace),indent=2)+'\n')
if a.startup_only and status=='passed':print(f'STARTUP: prompt {boot_timing["first_prompt_seconds"]:.3f}s; verified key echo {boot_timing["interactive_seconds"]:.3f}s; timings {work}/startup.json',flush=True)
print(f'{status.upper()}: console, report {work}/result.json',flush=True)
if status!='passed':raise SystemExit(failure)
