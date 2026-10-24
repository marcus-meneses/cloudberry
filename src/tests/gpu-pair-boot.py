#!/usr/bin/env python3
"""Paired userland providers on copied media; no automatic production selection."""
import argparse,hashlib,json,os,pathlib,re,socket,subprocess,time,uuid,platform
from gpu_pair_image import prepare
from gpu_pair_frame import inspect,validate_capture,complete_log,phase_records,profile_records
p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=pathlib.Path);p.add_argument('--backend',choices=['vga','virtio','virgl'],default='vga');p.add_argument('--accel',choices=['tcg','kvm'],default='tcg');p.add_argument('--cpus',type=int,default=4);p.add_argument('--display');p.add_argument('--startup-fault',choices=['open','scanout','graphics','release']);p.add_argument('--profile',action='store_true');p.add_argument('--require-screen',action='store_true',help='require actual scanout pixel matches, including GL phases');p.add_argument('--keep-artifacts',action='store_true');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[2];build=a.build.resolve();work=root/'bin/tests/gpu-pair'/uuid.uuid4().hex;work.mkdir(parents=True);image=work/'boot.img';log=work/'boot.log';endpoint=work/'qmp.sock';proc=None;connection=None
result=dict(status='running',backend=a.backend,accelerator=a.accel,cpus=a.cpus,error=None,captures={},screen_capture_required=a.require_screen,startup_fault=a.startup_fault)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def save():(work/'result.json').write_text(json.dumps(result,indent=2)+'\n')
def qmp(command,args=None):
    wire.write(json.dumps(dict(execute=command,**({'arguments':args} if args else {}))).encode()+b'\n');wire.flush()
    while True:
        data=json.loads(wire.readline())
        if 'error' in data:raise RuntimeError(str(data['error']))
        if 'return' in data:return data['return']
save()
try:
    assert build.is_relative_to(root/'bin/tests'),'paired tests require isolated build inside bin/tests'
    assert a.cpus>=1
    if a.accel=='kvm':subprocess.run(['python3',str(root/'src/tests/native-check.py')],check=True)
    assert not a.profile or (a.backend=='virgl' and not a.startup_fault and a.cpus<=4),'profiling requires healthy VirGL and at most four CPUs'
    stage,bundles=prepare(build,work,a.backend,a.startup_fault,profile=a.profile)
    result['binaries']={name:digest(bundles/name/(name+'.elf')) for name in ('display-opengl','graphics-opengl','display-vga','graphics-vga','gpu-pair')}
    result['fixture_sha256']=digest(bundles/'gpu-virtio.kmod');result['catalog_sha256']=digest(bundles/'installed.bin')
    result['profile_requested']=a.profile
    if a.profile:
        result['profile_broker_sha256']=digest(build/'tests/handles-profile.kmod')
        result['profile_host']=dict(kernel=platform.release(),machine=platform.machine(),qemu=subprocess.check_output(['qemu-system-x86_64','--version'],text=True).splitlines()[0],cpu='\n'.join(line for line in pathlib.Path('/proc/cpuinfo').read_text().splitlines()[:32] if line.startswith(('vendor_id','model name','microcode','flags'))))
    result['production_modules']={name:digest(build/'kmodules'/(name+'.kmod')) for name in ('bootstrap','filesystem','io','handles','process','display','gpu-virtio')}
    files=[root/'src/tests/gpu-pair-module.c',root/'src/tests/gpu-pair-fault-provider.c',root/'src/tests/gpu-pair-restore.inc',root/'src/tests/gpu-pair-lifecycle.inc',root/'src/tests/apps/gpu-pair.c',root/'src/tests/gpu-pair.h',root/'src/tests/gpu_pair_image.py',root/'src/tests/gpu_pair_frame.py',root/'src/tests/gpu-pair-boot.py',root/'src/kmodules/shared/gpu-virtio.c',root/'src/kmodules/shared/gpu-virtio-io.inc',root/'src/kmodules/shared/gpu-virtio-render.inc',root/'src/kmodules/shared/display.c',root/'src/kmodules/shared/display-boot.h',root/'src/kmodules/shared/display.json',root/'src/kmodules/shared/render-selection.h',root/'src/tests/gpu-performance.inc',root/'src/tests/handles-profile.inc',root/'src/tests/handles-profile.h',root/'src/interfaces/gpu.h',root/'src/apps/installed.json',root/'src/drivers/gpu-virtio.driver',root/'src/kmodules/shared/io.c',root/'src/kmodules/shared/handles.c',root/'src/kmodules/shared/bootstrap-apps.c',root/'src/kmodules/shared/bootstrap-load.c',root/'src/kmodules/shared/bootstrap.c',root/'src/kmodules/shared/process.c',root/'src/interfaces/render-backend.h',root/'src/sdk/service.h',root/'src/sdk/system.c']
    for directory in ('graphics-common','graphics-vga','graphics-opengl','display-vga','display-opengl'):
        files += [f for f in sorted((root/'src/handlers'/directory).iterdir()) if f.suffix in ('.c','.h','.inc','.json','.cnf')]
    result['sources']={str(f.relative_to(root)):digest(f) for f in files}
    with (work/'image.log').open('w') as out:subprocess.run(['python3',str(root/'src/tools/build-boot-image.py'),str(stage),str(image),'--drivers-source',str(bundles)],check=True,stdout=out,stderr=subprocess.STDOUT)
    with image.open('r+b') as out:os.fsync(out.fileno())
    display=a.display or ('egl-headless' if a.backend=='virgl' else 'none')
    if a.backend=='virgl' and display.startswith(('gtk','sdl')) and 'gl=' not in display:display+=',gl=on'
    command=['bash',str(root/'src/boot/run-qemu.sh'),'qemu-system-x86_64','-no-user-config','-nodefaults','-machine','q35','-m','512M','-rtc','base=utc,clock=host','-accel',a.accel,'-cpu','host,migratable=off,+invtsc' if a.accel=='kvm' else 'max','-smp',str(a.cpus),'-bios','/usr/share/ovmf/OVMF.fd','-device','virtio-vga-gl' if a.backend=='virgl' else 'virtio-vga' if a.backend=='virtio' else 'VGA','-device','qemu-xhci,id=xhci','-device','usb-kbd,bus=xhci.0','-device','usb-mouse,bus=xhci.0','-drive',f'if=none,id=boot,format=raw,file={image}','-device','usb-bot,id=bot,bus=xhci.0','-device','scsi-hd,bus=bot.0,drive=boot,bootindex=1','-display',display,'-debugcon','stdio','-global','isa-debugcon.iobase=0xe9','-monitor','none','-qmp',f'unix:{endpoint},server=on,wait=off','-no-reboot','-no-shutdown']
    (work/'command.json').write_text(json.dumps(command,indent=2)+'\n')
    with log.open('w') as out:proc=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.STDOUT)
    phases={};sent_mouse=sent_key=False;deadline=time.monotonic()+240
    while time.monotonic()<deadline:
        text=complete_log(log.read_text())
        if a.profile:
            diagnostics=[json.loads(line.removeprefix('GPU_PROFILE ')) for line in text.splitlines() if line.startswith('GPU_PROFILE {')]
            if diagnostics:result['profile_diagnostics']=diagnostics
            if 'GPU_PROFILE FAIL' in text:raise RuntimeError('profiling finalization failed; inspect profile_diagnostics in result.json and boot.log')
        if proc.poll() is not None:raise RuntimeError('QEMU exited; inspect boot.log')
        if 'GPU_PAIR FAIL' in text:raise RuntimeError(text.split('GPU_PAIR FAIL',1)[1].split('\n',1)[0])
        if a.startup_fault=='release' and 'bootstrap: provider startup failed' in text:
            rejected=re.search(r'GPU_PAIR STARTUP REJECTED code=204 display=(\d+) failed=(\d+)',text)
            if a.backend=='virgl':assert 'GPU_PAIR PRODUCTION PROBE verified=1' in text and 'GPU_PAIR PRODUCTION RELEASE verified=1' in text,'production hooks were not verified'
            assert rejected and 'FATAL' not in text and 'selecting display-vga + graphics-vga' not in text and 'GPU_PAIR PROVIDERS' not in text,'uncertain release did not fail closed'
            result.update(status='passed',production_probe_verified='GPU_PAIR PRODUCTION PROBE verified=1' in text,production_release_verified='GPU_PAIR PRODUCTION RELEASE verified=1' in text,startup_failure_injected=True,cleanup_failure_rejected=True,provider_ipc_verified=False,resource_pixels_verified=False,screen_capture_verified=False,restoration_verified=False,failed_display=int(rejected.group(1)),failed_provider=int(rejected.group(2)))
            break
        if 'provider startup failed' in text or 'FATAL' in text:raise RuntimeError('guest startup failed; inspect boot.log')
        if connection is None and endpoint.exists():
            connection=socket.socket(socket.AF_UNIX);connection.settimeout(10);connection.connect(str(endpoint));wire=connection.makefile('rwb');json.loads(wire.readline());qmp('qmp_capabilities')
        if connection:
            for phase,w,h,x,y in phase_records(text):
                if phase not in (1,2,4,5,8,10) or phase in phases:continue
                state=dict(width=w,height=h,x=x,y=y);frame=work/f'phase-{phase}.ppm'
                try:
                    end=time.monotonic()+.6;attempts=0
                    while True:
                        qmp('screendump',dict(filename=str(frame)));capture=inspect(frame.read_bytes(),phase,state);attempts+=1
                        if capture['state']=='matched' or not (a.backend!='virgl' or a.require_screen or phase==10) or time.monotonic()>=end:break
                        time.sleep(.02)
                    capture.update(sha256=digest(frame),attempts=attempts)
                except Exception as error:
                    if a.backend!='virgl' or a.require_screen or phase==10:raise
                    capture=dict(state='unavailable',reason=str(error))
                phases[phase]=capture;result['captures'][str(phase)]=capture;save()
                validate_capture(capture,phase,a.backend!='virgl' or a.require_screen)
            if 'GPU_PAIR MOUSE WAIT' in text and not sent_mouse:qmp('input-send-event',dict(events=[dict(type='rel',data=dict(axis='x',value=-37)),dict(type='rel',data=dict(axis='y',value=-23))]));sent_mouse=True
            if 'GPU_PAIR KEY WAIT' in text and not sent_key:qmp('send-key',dict(keys=[dict(type='qcode',data='a')],**{'hold-time':50}));sent_key=True
        match=re.search(r'GPU_PAIR PASS accelerated=(\d+) captures=(\d+) graphics_calls=(\d+)',text)
        if match:
            if a.profile:
                records=profile_records(text)
                result['profile']=dict(version=1,records=records,notes='instrumented; fence time is included in GPU I/O and broker wait/kernel spans; do not add them; no readback/screenshots during the measured window')
                (work/'profile.json').write_text(json.dumps(result['profile'],indent=2)+'\n')
            accelerated,captures,calls=map(int,match.groups());providers=re.search(r'GPU_PAIR PROVIDERS display=(\d+) graphics=(\d+) client=(\d+) separate_spaces=1',text)
            assert providers and len(set(providers.groups()))==3,'provider processes were not isolated'
            assert accelerated==(a.backend=='virgl' and not a.startup_fault) and calls>10 and sent_mouse and sent_key,'missing paired process/input evidence'
            assert set(phases)=={1,2,4,5,8,10},'missing phase screen observations'
            restored=re.search(r'GPU_PAIR RESTORE PASS client=(\d+) display=(\d+) graphics=(\d+)',text)
            assert restored and 'GPU_PAIR RESTORE quiesced=1' in text,'missing reaped-provider restoration evidence'
            assert set(providers.groups()).isdisjoint(restored.groups()) and len(set(restored.groups()))==3,'fallback reused live provider processes'
            if a.backend=='virtio' and not a.startup_fault:assert 'GPU_PAIR RESTORE native_2d_reset=1' in text,'missing native-to-VGA transition'
            assert a.backend!='virgl' or a.startup_fault or captures>=5,'missing in-process GPU resource captures'
            assert ('bootstrap: trying display-opengl + graphics-opengl' in text)==(a.backend=='virgl' or bool(a.startup_fault)),'wrong selected backend'
            if a.startup_fault:
                code={'open':201,'scanout':202,'graphics':203}[a.startup_fault]
                recovered=re.search(r'GPU_PAIR STARTUP FALLBACK READY code='+str(code)+r' display=(\d+) failed=(\d+)',text)
                assert recovered and 'selecting display-vga + graphics-vga (fallback reason 6)' in text,'missing verified startup fallback'
                assert set(providers.groups()).isdisjoint(recovered.groups()),'failed providers were rebound or reused'
                result.update(startup_failure_injected=True,startup_fallback_verified=True,failed_display=int(recovered.group(1)),failed_provider=int(recovered.group(2)))
            elif a.backend=='virgl':assert 'selecting display-vga + graphics-vga' not in text,'unexpected fallback after GPU takeover'
            if a.backend=='virgl':assert 'GPU_PAIR PRODUCTION PROBE verified=1' in text and 'GPU_PAIR PRODUCTION RELEASE verified=1' in text,'production hooks were not verified'
            result.update(status='passed',production_probe_verified='GPU_PAIR PRODUCTION PROBE verified=1' in text,production_release_verified='GPU_PAIR PRODUCTION RELEASE verified=1' in text,provider_processes=dict(zip(('display','graphics','client'),map(int,providers.groups()))),provider_ipc_verified=True,resource_pixels_verified=a.backend=='virgl' and not a.startup_fault,resource_captures=captures,graphics_calls=calls,mouse_verified=True,keyboard_verified=True,exit_cleanup_verified=True,provider_restart_verified=True,restoration_verified=a.backend in ('virtio','virgl') and a.startup_fault!='open',restored_processes=dict(zip(('client','display','graphics'),map(int,restored.groups()))),screen_capture_verified=all(c['state']=='matched' for c in phases.values()))
            break
        time.sleep(.02)
    else:raise RuntimeError('paired provider test timed out; inspect boot.log')
except Exception as error:result.update(status='failed',error=str(error))
finally:
    if proc:
        if connection:
            try:qmp('quit')
            except Exception:pass
        try:proc.wait(timeout=5)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
    if connection:connection.close()
    save()
if result['status']=='passed' and not a.keep_artifacts:subprocess.run(['python3',str(root/'src/tools/clean-tests.py'),'--root',str(work)],check=True)
print(result['status'].upper()+': paired graphics providers, report '+str(work/'result.json'),flush=True)
if result['error']:print(result['error'],flush=True)
raise SystemExit(0 if result['status']=='passed' else 1)
