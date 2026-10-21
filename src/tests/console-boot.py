#!/usr/bin/env python3
"""Exercise production providers/terminal on a disposable writable image via USB HID.
Fixtures are built separately; no developer NVMe/SATA media or candidate image is written.
"""
import argparse,importlib.util,json,pathlib,re,shutil,socket,subprocess,time,uuid,struct,os,threading
p=argparse.ArgumentParser();p.add_argument('build',type=pathlib.Path);p.add_argument('--private-check',action='store_true');p.add_argument('--persistence-phase',choices=['create','verify']);p.add_argument('--disk-source',type=pathlib.Path);p.add_argument('--fixtures',action='store_true');p.add_argument('--startup-only',action='store_true');p.add_argument('--startup-trace',action='store_true');p.add_argument('--profile-only',action='store_true');p.add_argument('--render-only',action='store_true');p.add_argument('--scroll-only',action='store_true');p.add_argument('--wait-only',action='store_true');p.add_argument('--edit-only',action='store_true');p.add_argument('--cursor-only',action='store_true');p.add_argument('--mouse-only',action='store_true');p.add_argument('--monitor-only',action='store_true');p.add_argument('--stdio-only',action='store_true');p.add_argument('--identity-only',action='store_true');p.add_argument('--accounts-only',action='store_true');p.add_argument('--bootfs-only',action='store_true');p.add_argument('--keep-artifacts',action='store_true',help='retain successful-run VM media and screenshot');p.add_argument('--diagnostics',action='store_true');p.add_argument('--kernel',type=pathlib.Path,help='test-only replacement kernel, in a copied stage');p.add_argument('--mmio-trace',action='store_true');p.add_argument('--storage-devices',action='store_true',help='attach disposable 1 GiB NVMe and SATA disks');p.add_argument('--cpus',type=int,default=4);p.add_argument('--accel',default='tcg',choices=['tcg','kvm']);a=p.parse_args()
focused_modes=(a.startup_only,a.profile_only,a.scroll_only,a.render_only,a.wait_only,a.edit_only,a.cursor_only,a.mouse_only,a.monitor_only,a.stdio_only,a.identity_only,a.accounts_only,a.bootfs_only,bool(a.persistence_phase))
if sum(focused_modes)>1:p.error('choose only one focused run mode')
if a.disk_source and (a.fixtures or a.startup_trace):p.error('--disk-source cannot rebuild fixtures')
if a.disk_source and not a.disk_source.resolve().is_relative_to(pathlib.Path(__file__).resolve().parents[2]/'bin/tests'):p.error('disk-source must be isolated inside bin/tests')
if a.startup_only and a.fixtures:p.error('startup timing uses production apps; omit --fixtures')
if (a.identity_only or a.stdio_only or a.kernel or a.profile_only or a.scroll_only or a.render_only or a.wait_only or a.edit_only or a.mouse_only) and not a.fixtures:p.error('--kernel/--profile-only/--scroll-only requires --fixtures')
if a.startup_trace and (not a.startup_only or a.kernel):p.error('--startup-trace requires --startup-only and cannot use --kernel')
root=pathlib.Path(__file__).resolve().parents[2];build=a.build.resolve();work=root/'bin/tests/console'/uuid.uuid4().hex;work.mkdir(parents=True)
image=work/'boot.img'
if a.startup_trace:
    with (work/'trace-build.log').open('w') as trace_log:subprocess.run(['make','-C',str(root/'src'),'BUILD='+str(build),str(build/'tests/startup-kernel.elf'),str(build/'tests/startup-bootstrap.kmod')],check=True,stdout=trace_log,stderr=subprocess.STDOUT)
    a.kernel=build/'tests/startup-kernel.elf'
if a.fixtures or a.startup_trace:
    stage=work/'stage';shutil.copytree(build/'src-iso-root',stage)
    if a.kernel:shutil.copy2(a.kernel,stage/'kernel.elf')
    if a.startup_trace:
        paths=list(stage.rglob('bootstrap.kmod'));assert len(paths)==1;shutil.copy2(build/'tests/startup-bootstrap.kmod',paths[0])
    bundles=work/'bundles';shutil.copytree(build/'src-handler-bundles',bundles)
    if a.fixtures:
        directory=bundles/'graphics-check';directory.mkdir();shutil.copy2(build/'tests/apps/graphics-check.elf',directory/'graphics-check.elf')
        operations=['info','fill','glyph','surface_create','surface_release','surface_info','state','line','rectangle','ellipse','arc','bezier','path','bitmap','blit','present']
        (directory/'manifest.json').write_text(json.dumps(dict(version=1,resources=[dict(name='/@graphics',operations=operations),dict(name='/@applications',operations=['launch','wait','release','processes']),dict(name='/@keyboard',scope='boot-input',operations=['read','focus_claim','focus_release']),dict(name='/@streams',operations=['pipe','read','write']),dict(name='/@text',operations=['write','clear']),dict(name='/@io',scope='self',operations=['now_ns']),dict(name='/@filesystem',operations=['open','stat','remove'])])))
        records=json.loads((root/'src/apps/installed.json').read_text());records.append(dict(name='graphics-check',executable='/system/apps/graphics-check',manifest='graphics-check/manifest.json',installation_id='636c6f756462657272792e6766786368',flags=3,cpu=0,stack_bytes=262144));
        if a.private_check:
            directory=bundles/'private-check';directory.mkdir();shutil.copy2(build/'tests/apps/private-check.elf',directory/'private-check.elf')
            (directory/'manifest.json').write_text(json.dumps(dict(version=1,resources=[dict(name='/@filesystem',operations=['open','metadata','chmod']),dict(name='/@applications',operations=['data','launch','wait','release']),dict(name='/@sessions',operations=['begin','end','status']),dict(name='/@streams',operations=['read','write'])])))
            records.append(dict(name='private-check',executable='/system/apps/private-check',manifest='private-check/manifest.json',installation_id='636c6f756462657272792e7072697600',flags=3,cpu=0,stack_bytes=262144))
        if a.mouse_only:
            directory=bundles/'mouse-check';directory.mkdir();shutil.copy2(build/'tests/apps/mouse-check.elf',directory/'mouse-check.elf')
            (directory/'manifest.json').write_text(json.dumps(dict(version=1,resources=[dict(name='/@mouse',scope='boot-input',operations=['read','state']),dict(name='/@streams',operations=['read','write'])])))
            records.append(dict(name='mouse-check',executable='/system/apps/mouse-check',manifest='mouse-check/manifest.json',installation_id='636c6f756462657272792e6d74657374',flags=1,cpu=0,stack_bytes=262144))
        if a.identity_only:
            directory=bundles/'identity-check';directory.mkdir();shutil.copy2(build/'tests/apps/identity-check.elf',directory/'identity-check.elf')
            (directory/'manifest.json').write_text(json.dumps(dict(version=1,resources=[dict(name='/@identity',operations=['info','users','groups','begin','stage','commit','abort','credential','self','authenticate','password']),dict(name='/@memory',scope='self',operations=['allocate','free']),dict(name='/@streams',operations=['pipe','read','write']),dict(name='/@sessions',operations=['begin','revoke_uid']),dict(name='/@text',operations=['clear']),dict(name='/@graphics',operations=['fill','surface_create','surface_release','blit']),dict(name='/@display',operations=['fill']),dict(name='/@filesystem',operations=['stat','open','remove']),dict(name='/@applications',operations=['launch','wait','release'])])))
            records.append(dict(name='identity-check',executable='/system/apps/identity-check',manifest='identity-check/manifest.json',installation_id='636c6f756462657272792e6964746573',flags=3,cpu=0,stack_bytes=262144))
        spec=work/'installed.json';spec.write_text(json.dumps(records));subprocess.run(['python3',str(root/'src/tools/build-installations.py'),str(spec),str(bundles),str(bundles/'installed.bin')],check=True);shutil.copy2(bundles/'installed.bin',bundles/'applications/catalog.bin');shutil.copy2(bundles/'installed.bin',bundles/'identity/catalog.bin')
        name=b'graphics-check';arguments=name+(b'\0!stdio\0' if a.stdio_only else b'\0!supervise\0');(bundles/'applications/initial.bin').write_bytes(struct.pack('<IHHQQQQ',40,1,0,len(name),2,len(arguments),1)+name+arguments+b'/')
        if a.edit_only:
            fixtures=bundles/'completion fixtures';fixtures.mkdir()
            (fixtures/'alpha one.txt').write_text('first-file\n');(fixtures/'alpha two.txt').write_text('second-file\n')
            (fixtures/'folder space').mkdir();(fixtures/'folder space'/'entry.txt').write_text('nested-file\n')
            (fixtures/'_hidden.txt').write_text('hidden-file\n')
        (bundles/'pipe-source.bin').write_bytes(bytes((i*37+11)%256 for i in range(65536+317)))
    subprocess.run(['python3',str(root/'src/tools/build-boot-image.py'),str(stage),str(image),'--drivers-source',str(bundles)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
else:subprocess.run(['cp','--reflink=auto','--sparse=always',str(a.disk_source.resolve() if a.disk_source else build/'cloudberry.iso'),str(image)],check=True)
qmp=work/'qmp.sock';log=work/'boot.log';screenshot=work/'screen.ppm'
command=['qemu-system-x86_64','-no-user-config','-nodefaults','-machine','q35','-m','512M','-accel',a.accel,'-cpu','host,migratable=off,+invtsc' if a.accel=='kvm' else 'max','-smp',str(a.cpus),'-bios','/usr/share/ovmf/OVMF.fd','-device','VGA','-device','qemu-xhci,id=xhci','-device','usb-kbd,bus=xhci.0','-device','usb-mouse,id=mouse0,bus=xhci.0','-drive',f'if=none,id=boot,format=raw,file={image}','-device','usb-bot,id=bot,bus=xhci.0','-device','scsi-hd,bus=bot.0,drive=boot,bootindex=1','-display','none','-debugcon','stdio','-global','isa-debugcon.iobase=0xe9','-monitor','none','-qmp',f'unix:{qmp},server=on,wait=off','-no-reboot','-no-shutdown']
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
                background=((y*16)*w+x*8)*3
                inverse=max(pixels[background:background+3])>100
                pattern=[]
                for row in range(7):
                    bits=0
                    for col in range(5):
                        off=((y*16+1+row*2)*w+x*8+1+col)*3
                        bits=bits*2+int((max(pixels[off:off+3])>100)!=inverse)
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
            if a.identity_only and 'IDENTITY FAIL' in rendered:raise RuntimeError('identity acceptance failed: '+rendered.split('IDENTITY FAIL',1)[1].split('\n')[0])
            if 'PRIVATE FAIL' in rendered:raise RuntimeError('private-data acceptance failed: '+rendered.split('PRIVATE FAIL',1)[1].split('\n')[0])
            if 'keyboard input failed' in rendered:raise RuntimeError('keyboard capture/provider failed; see diagnostics.log')
            if 'terminal exited:' in rendered:raise RuntimeError('terminal exited: '+rendered.split('terminal exited:')[1].split('\n')[0])
            if check(rendered):return
            time.sleep(.3)
        raise RuntimeError('screen check timeout; see screen.txt and boot.log')
    if a.stdio_only:
        wait_for(lambda text:'PASS headless stdin/stdout:' in text,90)
        raise ProfileComplete()
    wait_for(lambda text:'/ >' in text,120 if a.startup_only else 90)
    boot_timing['first_prompt_seconds']=time.monotonic()-launched
    def send_keys(keys,hold=.035):
        # QMP send-key serializes delayed edges using guest virtual time.
        # Explicit edges make injection independent of guest timer progress.
        def events(down):return [{'type':'key','data':{'down':down,'key':key}} for key in keys]
        call('input-send-event',{'events':events(True)});time.sleep(hold)
        call('input-send-event',{'events':events(False)});time.sleep(.03)
    # Generate text through the configured layout; never assume a US keyboard.
    layout_name=dict(line.split('=',1) for line in (root/'src/handlers/keyboard/config.cnf').read_text().splitlines() if line and not line.startswith('#'))['layout']
    layout=json.loads((root/'src/handlers/keyboard/layouts'/(layout_name+'.json')).read_text())
    qcodes={**{4+i:chr(97+i) for i in range(26)},**{30+i:str(i+1) for i in range(9)},39:'0',44:'spc',45:'minus',46:'equal',47:'bracket_left',48:'bracket_right',49:'backslash',51:'semicolon',52:'apostrophe',53:'grave_accent',54:'comma',55:'dot',56:'slash',100:'less'}
    typed={}
    for usage,*values in layout['keys']:
        if usage not in qcodes:continue
        for level,cp in enumerate(values):
            if cp is None:continue
            keys=([{'type':'qcode','data':'alt_r'}] if level&2 else [])+([{'type':'qcode','data':'shift'}] if level&1 else [])+[{'type':'qcode','data':qcodes[usage]}]
            if chr(cp) not in typed or len(keys)<len(typed[chr(cp)]):typed[chr(cp)]=keys
    def type_line(text,enter=True):
        for c in text:
            if c not in typed:raise RuntimeError('No QMP key mapping for '+repr(c)+' in '+layout_name)
            send_keys(typed[c])
        if enter:send_keys([{'type':'qcode','data':'ret'}]);time.sleep(.1)
    if a.persistence_phase:
        def command_done(command,expected):
            before=screen();prompt='/home/alice >' if before.rstrip().endswith('/home/alice >') else '/ >';count=before.count(prompt)
            type_line(command);wait_for(lambda text:expected in text and text.count(prompt)>count and text.rstrip().endswith(prompt),120)
        if a.persistence_phase=='create':
            command_done('accounts useradd alice','Updated.')
            command_done('accounts home alice','/home/alice')
            command_done('fsctl stat /home/alice','uid=1 gid=2 mode=700')
            command_done('fsctl stat /home/alice/_apps','uid=1 gid=2 mode=700')
            type_line('accounts passwd alice');wait_for(lambda text:'New password:' in text,40)
            secret='session-test-password';type_line(secret);wait_for(lambda text:'Confirm password:' in text,40);type_line(secret)
            wait_for(lambda text:'Password updated.' in text and text.rstrip().endswith('/ >'),120)
            assert secret not in screen()
        type_line('clear');wait_for(lambda text:text.count('/ >')==1 and text.rstrip().endswith('/ >'),45)
        type_line('accounts login alice');wait_for(lambda text:'Password:' in text,40);type_line('session-test-password')
        wait_for(lambda text:text.rstrip().endswith('/home/alice >'),120)
        command_done('accounts self','uid=1 gid=2')
        if a.private_check:command_done('private-check','PRIVATE PASS')
        if a.persistence_phase=='create':
            command_done('echo durable-home => documents/persist.txt','/home/alice >')
            command_done('fsctl chmod 640 documents/persist.txt','/home/alice >')
            command_done('fsctl stat documents/persist.txt','uid=1 gid=2 mode=640')
            # Deny user ownership takeover and access to root's private data.
            command_done('fsctl chown 0 0 documents/persist.txt','fsctl: status 1')
            command_done('cat /system/root/_apps/terminal.data/history','cat: permission denied')
        else:
            command_done('cat documents/persist.txt','durable-home')
            command_done('fsctl stat documents/persist.txt','uid=1 gid=2 mode=640')
        # A direct keyboard consumer temporarily takes focus from this terminal;
        # ESC exits monitor, then the nested terminal must accept more input.
        type_line('monitor');wait_for(lambda text:'PROCESSES' in text or 'PID' in text,45)
        send_keys([{'type':'qcode','data':'esc'}]);wait_for(lambda text:text.rstrip().endswith('/home/alice >'),45)
        command_done('pwd','/home/alice')
        type_line('exit');wait_for(lambda text:text.rstrip().endswith('/ >'),60)
        command_done('accounts self','uid=0 gid=0')
        raise ProfileComplete()
    if a.bootfs_only:
        type_line('ls /boot');wait_for(lambda text:'kernel.elf' in text and 'cloudberry.cnf' in text and text.count('/ >')>=2,45)
        type_line('cat /boot/cloudberry.cnf');wait_for(lambda text:'drivers.directory=/system/kmodules/drivers' in text and text.count('/ >')>=3,45)
        type_line('ls /boot/kmodules/helpers');wait_for(lambda text:'iso9660-fs.kmod' in text and text.count('/ >')>=4,45)
        type_line('cat /system/config/keyboard/config.cnf');wait_for(lambda text:'layout=abnt2' in text and text.count('/ >')>=5,45)
        type_line('ls /system/kmodules/drivers');wait_for(lambda text:text.count('/ >')>=6,45)
        type_line('echo boot-read-only => /boot/forbidden.txt');wait_for(lambda text:'permission denied' in text and text.count('/ >')>=7,45)
        type_line('pwd');wait_for(lambda text:'\n/\n/ >' in text and text.count('/ >')>=8,45)
        raise ProfileComplete()
    if a.startup_only:
        pressed=time.monotonic();send_keys([{'type':'qcode','data':'x'}]);wait_for(lambda text:'/ > x' in text,15)
        boot_timing.update(interactive_seconds=time.monotonic()-launched,key_to_echo_seconds=time.monotonic()-pressed,cpus=a.cpus,accelerator=a.accel,storage_devices=a.storage_devices,phase_tracing=a.startup_trace,includes='QEMU launch, firmware, Limine, kernel, providers and first prompt; image preparation excluded',observation='host monotonic clock; prompt/echo detected by framebuffer polling; detection includes polling/decoding delay')
        raise ProfileComplete()
    if a.accounts_only:
        prompts=1
        def account_command(command,expected):
            type_line(command)
            wait_for(lambda text:expected in text and text.count('/ >')>=prompts+1,90)
        account_command('accounts self','uid=0 gid=0');prompts+=1
        for command in ('accounts groupadd staff','accounts useradd alice staff','accounts join alice admin'):
            account_command(command,'Updated.');prompts+=1
        account_command('accounts home alice','/home/alice');prompts+=1
        type_line('accounts passwd alice');wait_for(lambda text:'New password:' in text,40)
        secret='cloudberry-test-secret';type_line(secret);wait_for(lambda text:'Confirm password:' in text,40);type_line(secret)
        wait_for(lambda text:'Password updated.' in text and text.count('/ >')>=prompts+1,120);prompts+=1
        assert secret not in screen(), 'password was echoed'
        type_line('accounts login alice accounts self');wait_for(lambda text:text.count('Password:')>=1,40);type_line(secret)
        wait_for(lambda text:'uid=1 gid=3 groups=1' in text and text.count('/ >')>=prompts+1,120);prompts+=1
        type_line('accounts login alice accounts groupadd project');wait_for(lambda text:text.count('Password:')>=2,40);type_line(secret)
        wait_for(lambda text:text.count('/ >')>=prompts+1 and text.count('Updated.')>=4,120);prompts+=1
        for command in ('accounts revoke alice','accounts disable alice --end-sessions','accounts enable alice'):
            account_command(command,'Sessions revoked' if 'enable' not in command else 'Updated.');prompts+=1
        for command in ('accounts userdel alice','accounts groupdel staff','accounts groupdel project'):
            account_command(command,'Updated.');prompts+=1
        account_command('accounts users','0 root gid=0 active');prompts+=1
        assert 'accounts: operation failed' not in screen()
        raise ProfileComplete()
    if a.identity_only:
        type_line('identity-check');wait_for(lambda text:'IDENTITY PASS' in text and text.count('/ >')>=2,150)
        type_line('cat /system/config/identity/credentials.bin');wait_for(lambda text:'cat: permission denied' in text and text.count('/ >')>=3,30)
        type_line('pwd');wait_for(lambda text:'\n/\n/ >' in text and text.count('/ >')>=4,30)
        raise ProfileComplete()
    if a.monitor_only:
        type_line('monitor -n 2')
        wait_for(lambda text:text.count('Cloudberry monitor:')>=2 and text.count('/ >')>=2,60)
        rendered=screen();(work/'monitor-batch.txt').write_text(rendered)
        for name in ('terminal','monitor','keyboard','mouse','applications','filesystem','graphics','text','display'):
            assert re.search(r'\b'+name+r'\b',rendered), 'missing process '+name
        assert '--' in rendered, 'first sample must not invent a CPU percentage'
        assert re.search(r'\d+\.\d\s+\d+\s+\d+\.\d{3}',rendered), 'missing measured CPU usage'
        type_line('monitor')
        wait_for(lambda text:'Cloudberry monitor |' in text and 'Page 1/' in text)
        wait_for(lambda text:bool(re.search(r'\d+\.\d\s+\d+\s+\d+\.\d{3}\s+monitor',text)),30)
        (work/'monitor-live.txt').write_text(screen())
        send_keys([{'type':'qcode','data':'up'}]);send_keys([{'type':'qcode','data':'down'}])
        wait_for(lambda text:'Page 1/1' in text)
        send_keys([{'type':'qcode','data':'esc'}])
        wait_for(lambda text:text.strip()=='/ >',30)
        (work/'monitor-exit.txt').write_text(screen())
        type_line('pwd');wait_for(lambda text:'\n/\n/ >' in text,30)
        type_line('cat');type_line('after-monitor')
        wait_for(lambda text:text.count('after-monitor')>=2,30)
        send_keys([{'type':'qcode','data':'ctrl'},{'type':'qcode','data':'d'}])
        wait_for(lambda text:bool(re.search(r'/ >\s*$',text)),30)
        raise ProfileComplete()
    if a.mouse_only:
        type_line('mouse-check');wait_for(lambda text:'MOUSE READY' in text)
        def mouse_event(events,device='mouse0'):
            mice=call('query-mice');usb=[m for m in mice if 'USB' in m['name'].upper() or 'HID' in m['name'].upper()]
            (work/'qemu-mice.json').write_text(json.dumps(mice,indent=2)+'\n')
            assert usb, 'QEMU has no USB pointing input handler: '+str(mice)
            index=(min if device=='mouse0' else max)(m['index'] for m in usb)
            call('human-monitor-command',{'command-line':'mouse_set '+str(index)})
            call('input-send-event',dict(events=events));time.sleep(.1)
        mouse_event([dict(type='rel',data=dict(axis='x',value=12)),dict(type='rel',data=dict(axis='y',value=-7))])
        mouse_event([dict(type='btn',data=dict(button='left',down=True))]);mouse_event([dict(type='btn',data=dict(button='left',down=False))])
        mouse_event([dict(type='btn',data=dict(button='wheel-up',down=True))]);mouse_event([dict(type='btn',data=dict(button='wheel-up',down=False))])
        wait_for(lambda text:'MOUSE INPUT OK' in text)
        call('device_add',dict(driver='usb-mouse',id='mouse1',bus='xhci.0'));wait_for(lambda text:'MOUSE ADDED' in text)
        mouse_event([dict(type='btn',data=dict(button='left',down=True))],device='mouse1');wait_for(lambda text:'EXTRA BUTTON DOWN' in text)
        call('device_del',dict(id='mouse1'));wait_for(lambda text:'MOUSE REMOVED' in text)
        call('device_add',dict(driver='usb-mouse',id='mouse2',bus='xhci.0'));wait_for(lambda text:'MOUSE RECONNECTED' in text and text.count('/ >')>=2)
        type_line('pwd');wait_for(lambda text:'\n/\n/ >' in text and text.count('/ >')>=3)
        raise ProfileComplete()
    if a.cursor_only:
        def key(name):send_keys([{'type':'qcode','data':name}])
        def cursor_sample():
            screen()
            raw=screenshot.read_bytes();_,dimensions,_,pixels=raw.split(b'\n',3);width,height=map(int,dimensions.split())
            return [(x,y) for y in range(height//16) for x in range(width//8) if min(pixels[((y*16)*width+x*8)*3:((y*16)*width+x*8)*3+3])>180]
        def phases(seconds):
            samples=[];until=time.monotonic()+seconds
            while time.monotonic()<until:samples.append(cursor_sample());time.sleep(.08)
            return samples
        type_line('echo ac',False);key('left')
        steady=phases(.7);assert steady and all(s==steady[0] and len(s)==1 for s in steady), 'insertion cursor did not stay steady'
        key('insert');blinking=phases(1.3)
        assert any(len(s)==1 for s in blinking) and any(not s for s in blinking), 'overwrite cursor did not blink'
        assert 'echo ac' in screen(), 'cursor blink damaged covered character'
        type_line('b',False);key('insert');restored=phases(.7)
        assert restored and all(s==restored[0] and len(s)==1 for s in restored), 'insertion cursor did not return to steady'
        type_line('X')
        wait_for(lambda text:'\nabX\n' in text and text.count('/ >')>=2)
        (work/'cursor.json').write_text(json.dumps(dict(insertion=steady,overwrite=blinking,restored=restored),indent=2)+'\n')
        raise ProfileComplete()
    if a.edit_only:
        def key(name):send_keys([{'type':'qcode','data':name}])
        def submit():key('ret')
        type_line('cd "/system/kmodules/drivers/completion fixtures"')
        wait_for(lambda text:'/system/kmodules/drivers/completion fixtures >' in text)
        type_line('cat "alpha o',False);key('tab')
        wait_for(lambda text:'cat alpha\\ one.txt' in text);submit()
        wait_for(lambda text:'\nfirst-file\n' in text)
        type_line('cat "folder s',False);key('tab');type_line('ent',False);key('tab');submit()
        wait_for(lambda text:'\nnested-file\n' in text)
        type_line('cat al',False);key('tab');key('tab')
        wait_for(lambda text:'alpha one.txt' in text and 'alpha two.txt' in text)
        type_line('one',False);key('tab');submit()
        wait_for(lambda text:text.count('first-file')>=2)
        key('up');submit();wait_for(lambda text:text.count('first-file')>=3)
        type_line('echo draft',False);key('up');key('down');submit()
        wait_for(lambda text:'\ndraft\n' in text)
        type_line('echo ac',False);key('left');type_line('b',False)
        key('home')
        for _ in range(5):key('right')
        key('delete');key('end');key('backspace');key('insert');key('home')
        for _ in range(5):key('right')
        type_line('z',False);key('insert');key('end');submit()
        wait_for(lambda text:'\nz\n' in text)
        type_line('cat _hi',False);key('tab');submit()
        wait_for(lambda text:'\nhidden-file\n' in text)
        type_line('cd /');wait_for(lambda text:text.rstrip().endswith('/ >'))
        type_line('clear');wait_for(lambda text:text.count('/ >')==1)
    if a.wait_only or a.edit_only:
        type_line('graphics-check wake')
        wait_for(lambda text:'wake stdout' in text and 'wake stderr' in text and text.count('/ >')>=2)
        type_line('cat')
        type_line('stdin-wake')
        wait_for(lambda text:text.count('stdin-wake')>=2)
        send_keys([{'type':'qcode','data':'ctrl'},{'type':'qcode','data':'d'}],.06)
        wait_for(lambda text:text.count('/ >')>=3)
        type_line('echo pipeline-wake |> cat')
        wait_for(lambda text:'\npipeline-wake\n' in text and text.count('/ >')>=4)
        # Resume after idle with no foreground process and no polling deadline.
        time.sleep(.2)
        type_line('pwd')
        wait_for(lambda text:'\n/\n/ >' in text and text.count('/ >')>=5)
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
        started=time.monotonic();type_line('graphics-check profile');wait_for(lambda text:'PROFILE DONE' in text and text.count('/ >')>=2,150)
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
        type_line('cat /system/kmodules/drivers/pipe-source.bin |> cat => /pipe-copy.bin')
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
    if status=='passed' and a.fixtures and not any(focused_modes):
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
    if status=='passed' and a.identity_only:
        try:subprocess.run(['python3',str(root/'src/tests/identity-image.py'),str(image)],check=True)
        except Exception as error:status='failed';failure='durable identity check: '+str(error)
    (work/'result.json').write_text(json.dumps(dict(status=status,error=failure,cpus=a.cpus,accelerator=a.accel,storage_devices=a.storage_devices,diagnostic_kernel=str(a.kernel) if a.kernel else None,mmio_trace=a.mmio_trace),indent=2)+'\n')
if status=='passed' and not a.keep_artifacts:
    subprocess.run(['python3',str(root/'src/tools/clean-tests.py'),'--root',str(work)],check=True)
if a.startup_only and status=='passed':print(f'STARTUP: prompt {boot_timing["first_prompt_seconds"]:.3f}s; verified key echo {boot_timing["interactive_seconds"]:.3f}s; timings {work}/startup.json',flush=True)
print(f'{status.upper()}: console, report {work}/result.json',flush=True)
if status!='passed':raise SystemExit(failure)
