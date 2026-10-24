#!/usr/bin/env python3
"""Virtio-gpu transport/scanout acceptance on copied media; never host disks."""
import argparse,hashlib,json,os,pathlib,re,shutil,socket,subprocess,time,uuid
from gpu_frame import inspect_ppm,validate_gpu_result
p=argparse.ArgumentParser();p.add_argument('build',type=pathlib.Path);p.add_argument('--cpus',type=int,default=4);p.add_argument('--accel',choices=['tcg','kvm'],default='tcg');p.add_argument('--gpu',choices=['virtio','virgl'],default='virtio');p.add_argument('--iommu',choices=['none','intel','amd'],default='none');p.add_argument('--display',help='QEMU frontend; virgl defaults to egl-headless, requires host EGL/render-node access');p.add_argument('--keep-artifacts',action='store_true');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[2];build=a.build.resolve();work=root/'bin/tests/gpu-virtio-boot'/uuid.uuid4().hex;work.mkdir(parents=True);image=work/'boot.img';log=work/'boot.log';frame=work/'frame.ppm';proc=None;connection=None
result=dict(status='running',cpus=a.cpus,accelerator=a.accel,gpu=a.gpu,iommu=a.iommu,error=None)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def save():(work/'result.json').write_text(json.dumps(result,indent=2)+'\n')
def qmp(command,arguments=None):
    wire.write(json.dumps(dict(execute=command,**({'arguments':arguments} if arguments else {}))).encode()+b'\n');wire.flush()
    while True:
        reply=json.loads(wire.readline())
        if 'error' in reply:raise RuntimeError(str(reply['error']))
        if 'return' in reply:return reply['return']
def frame_check():
    try:qmp('screendump',dict(filename=str(frame)))
    except RuntimeError as error:
        if a.gpu=='virgl' and 'no surface' in str(error):return dict(state='unavailable',reason='QMP has no CPU-readable GL scanout surface')
        raise
    return inspect_ppm(frame.read_bytes())

save()
try:
    assert build.is_relative_to(root/'bin'),'build must be inside bin'
    fixture=build/'tests/gpu-virtio-probe.kmod';assert fixture.is_file()
    if a.accel=='kvm':subprocess.run(['python3',str(root/'src/tests/native-check.py')],check=True)
    stage=work/'stage';bundles=work/'bundles';shutil.copytree(build/'src-iso-root',stage);shutil.copytree(build/'src-handler-bundles',bundles)
    shutil.copy2(fixture,bundles/'gpu-virtio.kmod');cnf=stage/'cloudberry.cnf';cnf.write_text(re.sub(r'^drivers\.modules=.*$','drivers.modules=gpu-virtio',cnf.read_text(),flags=re.M))
    result.update(driver_sha256=digest(build/'kmodules/gpu-virtio.kmod'),fixture_sha256=digest(fixture),pci_sha256=digest(build/'kmodules/pci.kmod'),sources={str(f.relative_to(root)):digest(f) for f in [root/'src/kmodules/shared/gpu-virtio.c',root/'src/kmodules/shared/gpu-virtio-io.inc',root/'src/interfaces/gpu.h',root/'src/interfaces/io.h',root/'src/kmodules/shared/io.c',root/'src/kmodules/shared/handles.c',root/'src/kmodules/shared/virtio/pci.c',root/'src/kmodules/shared/virtio/gpu-wire.h',root/'src/tests/gpu-virtio-probe.c',root/'src/handlers/display-opengl/render.c',root/'src/handlers/display-opengl/render-surfaces.inc',root/'src/handlers/display-opengl/render.h',root/'src/handlers/graphics-opengl/virgl.c',root/'src/handlers/graphics-opengl/virgl-alpha.inc',root/'src/handlers/graphics-opengl/virgl.h',root/'src/interfaces/render-backend.h',root/'src/tests/gpu_frame.py']})
    with (work/'image.log').open('w') as out:subprocess.run(['python3',str(root/'src/tools/build-boot-image.py'),str(stage),str(image),'--drivers-source',str(bundles)],check=True,stdout=out,stderr=subprocess.STDOUT)
    with image.open('r+b') as out:os.fsync(out.fileno())
    endpoint=work/'qmp.sock';device='virtio-vga-gl' if a.gpu=='virgl' else 'virtio-vga';device+=',iommu_platform=on' if a.iommu!='none' else ''
    display=a.display or ('egl-headless' if a.gpu=='virgl' else 'none')
    if a.gpu=='virgl' and display.startswith(('gtk','sdl')) and 'gl=' not in display:display+=',gl=on'
    command=['bash',str(root/'src/boot/run-qemu.sh'),'qemu-system-x86_64','-no-user-config','-nodefaults','-machine','q35','-m','512M','-rtc','base=utc,clock=host','-accel',a.accel,'-cpu','host,migratable=off,+invtsc' if a.accel=='kvm' else 'max','-smp',str(a.cpus),'-bios','/usr/share/ovmf/OVMF.fd','-device',device,'-device','qemu-xhci,id=xhci','-device','usb-kbd,bus=xhci.0','-device','usb-mouse,bus=xhci.0','-drive',f'if=none,id=boot,format=raw,file={image}','-device','usb-bot,id=bot,bus=xhci.0','-device','scsi-hd,bus=bot.0,drive=boot,bootindex=1','-display',display,'-debugcon','stdio','-global','isa-debugcon.iobase=0xe9','-monitor','none','-qmp',f'unix:{endpoint},server=on,wait=off','-no-reboot','-no-shutdown']
    if a.iommu!='none':command+=['-device',('intel-iommu' if a.iommu=='intel' else 'amd-iommu')+',intremap=off']
    (work/'command.json').write_text(json.dumps(command,indent=2)+'\n')
    with log.open('w') as out:proc=subprocess.Popen(command,stdout=out,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL)
    deadline=time.monotonic()+120;capture=dict(state='not-observed');capture_at=0
    while time.monotonic()<deadline:
        text=log.read_text()
        if proc.poll() is not None:raise RuntimeError('QEMU exited; see boot.log (VirGL requires working host EGL/render-node access)')
        if 'GPU_TEST FAIL' in text:raise RuntimeError(text.split('GPU_TEST FAIL',1)[1].split('\n')[0])
        if 'provider startup failed' in text or 'FATAL' in text:raise RuntimeError('guest startup failed; see boot.log')
        if connection is None and endpoint.exists():
            connection=socket.socket(socket.AF_UNIX);connection.settimeout(5);connection.connect(str(endpoint));wire=connection.makefile('rwb');json.loads(wire.readline());qmp('qmp_capabilities')
        if capture['state']!='matched' and 'GPU_TEST FRAME' in text and 'GPU_TEST STAGE 2d cleanup' not in text and connection and time.monotonic()-capture_at>=.05:
            capture=frame_check();capture_at=time.monotonic();result['capture']=capture
        match=re.search(r'GPU_TEST PASS commands=(\d+) interrupts=(\d+) virgl=(\d+)',text)
        if match:
            assert 'GPU_TEST BRIDGE PASS' in text,'missing GPU bridge IO/ownership/cleanup acceptance'
            if a.gpu=='virgl':assert 'GPU_TEST DRAW PASS pixels=3072' in text,'missing real GPU drawing/readback acceptance'
            if a.gpu=='virgl':assert 'GPU_TEST COMPOSE PASS pixels=3072' in text,'missing GPU composition/alpha/readback acceptance'
            result['composition_verified']='GPU_TEST COMPOSE PASS pixels=3072' in text
            result['composition_readback_pixels']=3072 if result['composition_verified'] else 0
            result['drawing_verified']='GPU_TEST DRAW PASS pixels=3072' in text
            result['drawing_readback_pixels']=3072 if result['drawing_verified'] else 0
            result.update(validate_gpu_result(text,a.gpu,capture));result['bridge_verified']=True;result['status']='passed'
            if frame.exists():result['frame_sha256']=digest(frame)
            break
        time.sleep(.02)
    else:raise RuntimeError('GPU acceptance timed out; see boot.log')
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
print(result['status'].upper()+': virtio-gpu, report '+str(work/'result.json'),flush=True)
if result['error']:print(result['error'],flush=True)
raise SystemExit(0 if result['status']=='passed' else 1)
