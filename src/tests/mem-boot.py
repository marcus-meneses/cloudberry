#!/usr/bin/env python3
"""One modern-bus TCG boot using disposable media and a test-only kernel."""
from tsc_profile import summarize as summarize_tsc, parse_exchanges
import re
import json
import sys
import pathlib
import shlex
import shutil
import subprocess
import time
import atexit
import datetime
import uuid
import select

root = pathlib.Path(__file__).resolve().parents[2]
build = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'bin/tests/isolated'
if not build.is_relative_to((root/'bin/tests').resolve()):
    raise SystemExit('Boot tests require an isolated build below bin/tests')
missing_clock = '--missing-clock' in sys.argv[2:]
config_error=next((arg.split('=',1)[1] for arg in sys.argv[2:] if arg.startswith('--config-error=')),None)
if config_error not in (None,'missing','duplicate','invalid'): raise SystemExit('Unsupported config failure case')
watchdog = '--watchdog' in sys.argv[2:]
xhci_hotplug = '--xhci-hotplug' in sys.argv[2:]
acpi_fixture = '--acpi-fixture' in sys.argv[2:]
usb_hotplug = '--usb-hotplug' in sys.argv[2:]
usb_hubs = '--usb-hubs' in sys.argv[2:]
storage = '--storage' in sys.argv[2:]
storage_write = '--storage-write' in sys.argv[2:]
storage_nested = '--storage-nested' in sys.argv[2:]
storage_hotplug = '--storage-hotplug' in sys.argv[2:]
hotplug = xhci_hotplug or usb_hotplug or usb_hubs or storage_hotplug
cpus = next((int(arg.split('=')[1]) for arg in sys.argv[2:] if arg.startswith('--cpus=')), 1)
iommu = next((arg.split('=')[1] for arg in sys.argv[2:] if arg.startswith('--iommu=')), 'none')
cpu_model = next((arg.split('=',1)[1] for arg in sys.argv[2:] if arg.startswith('--cpu=')), 'host,migratable=off,+invtsc' if '--accel=kvm' in sys.argv[2:] else 'max')
accel = next((arg.split('=')[1] for arg in sys.argv[2:] if arg.startswith('--accel=')), 'tcg')
if iommu not in ('none','intel','amd') or accel not in ('tcg','kvm'):
    raise SystemExit('Unsupported test topology')
if accel == 'kvm':
    subprocess.run([sys.executable, str(root/'src/tests/native-check.py')],check=True)
suffix = ('-intelcpu' if 'GenuineIntel' in cpu_model else '-amdcpu' if 'AuthenticAMD' in cpu_model else '-fxsave' if '-xsave' in cpu_model else '-sse-xsave' if '-avx' in cpu_model else '') + ('' if iommu == 'none' else '-'+iommu) + ('' if accel == 'tcg' else '-native')
if watchdog and cpus<2:
    raise SystemExit('Lock watchdog requires at least two CPUs')
if xhci_hotplug: suffix += '-xhci-hotplug'
if acpi_fixture: suffix += '-acpi'
if usb_hotplug: suffix += '-usb-hotplug'
if usb_hubs: suffix += '-usb-hubs'
if storage: suffix += '-storage'
if storage_write: suffix += '-write'
if storage_hotplug: suffix += '-hotplug'
if storage_nested: suffix += '-nested'
test = build / ('tests/config-'+config_error if config_error else 'tests/startup-failure' if missing_clock else 'tests/lock-watchdog' if watchdog else f'tests/mem-boot-{cpus}cpu{suffix}')
test.mkdir(parents=True, exist_ok=True)
# A failed rerun must never leave an older successful report looking current.
report_path=test/'report.json'
if report_path.exists():
    history=test/'history';history.mkdir(exist_ok=True)
    saved=history/f'success-{report_path.stat().st_mtime_ns}-{uuid.uuid4().hex[:8]}.json'
    report_path.replace(saved)
run_id=uuid.uuid4().hex
run_result={'run_id':run_id,'status':'running','started_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'accelerator':accel,'cpus':cpus,'iommu':iommu,'storage':storage,'storage_write':storage_write,'storage_hotplug':storage_hotplug}
def finish(status,reason=None):
    run_result.update(status=status,finished_at=datetime.datetime.now(datetime.timezone.utc).isoformat())
    if reason is not None:run_result['reason']=str(reason)[-4096:]
    if status=='failed':report_path.unlink(missing_ok=True)
    (test/'result.json').write_text(json.dumps(run_result,indent=2)+'\n')
(test/'result.json').write_text(json.dumps(run_result,indent=2)+'\n')
atexit.register(lambda:finish('failed','Harness exited before completion') if run_result['status']=='running' else None)
stage = test / 'iso-root'
if stage.exists():
    shutil.rmtree(stage)
shutil.copytree(build / 'src-iso-root', stage)
shutil.copyfile(build / ('tests/src-lock-watchdog.elf' if watchdog else 'tests/src-kernel.elf'), stage / 'boot/kernel.elf')
shutil.copyfile(build / 'kmodules/probe.kmod', stage / 'boot/kmodules/probe.kmod')
with (stage / 'boot/limine/limine.conf').open('a') as config:
    config.write('    module_path: boot():/boot/kmodules/probe.kmod\n    module_cmdline: probe\n')
if missing_clock:
    config=stage/'boot/limine/limine.conf'
    config.write_text(config.read_text().replace('    module_path: boot():/boot/kmodules/clock.kmod\n    module_cmdline: clock\n',''))
if config_error:
    config=stage/'boot/limine/limine.conf'
    entry='    module_path: boot():/boot/cloudberry.cnf\n    module_cmdline: cloudberry-config\n'
    if config_error=='missing': config.write_text(config.read_text().replace(entry,''))
    elif config_error=='duplicate': config.write_text(config.read_text()+entry)
    else:
        data=stage/'boot/cloudberry.cnf'
        data.write_text(data.read_text().replace('version=1','version=2'))
iso = test / 'test.iso' 
image_options=[]
if storage:
    fixtures=test/'drivers-source';fixtures.mkdir(exist_ok=True)
    (fixtures/'probe.bin').write_bytes(bytes((i*37+11)%256 for i in range(16384)))
    image_options=['--drivers-source',str(fixtures)]
subprocess.run([sys.executable, str(root/'src/tools/build-boot-image.py'), str(stage), str(iso)]+image_options,
               check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
subprocess.run([sys.executable, str(root/'src/tests/boot-image.py'), str(iso)], check=True)
for name in ('nvme.img', 'sata.img'):
    with (test / name).open('wb') as output:
        output.truncate(1024 ** 3)
with (test / 'nvme.img').open('r+b') as disk:
    disk.write(bytes((i*37+11)%256 for i in range(16384)))
    disk.seek(1024**3-512)
    disk.write(b'CLOUDBERRY-NVME-END'+bytes(512-len(b'CLOUDBERRY-NVME-END')))
plan = subprocess.check_output(['make', '-s', '-n', '-C', str(root / 'src'), 'run',
    f'BUILD={build}', f'QEMU_CPU={cpu_model}', f'QEMU_CPUS={cpus}', 'QEMU_DISPLAY=none', f'QEMU_ACCEL={accel}', f'QEMU_IOMMU={iommu}', f'ISO={iso}',
    f'NVME_DISK={test}/nvme.img', f'SATA_DISK={test}/sata.img'], text=True)
command = next(line for line in plan.splitlines() if line.startswith('bash boot/run-qemu.sh'))
if storage_write:
    assert storage
    command=command.replace('id=bootmedia,format=raw,readonly=on,','id=bootmedia,format=raw,')
command = command.replace('id=nvmedisk,format=raw,', 'id=nvmedisk,format=raw,readonly=on,')
if '--xhci-trace' in sys.argv[2:]:
    for event in ('usb_xhci_oper_write','usb_xhci_runtime_write','usb_xhci_doorbell_write','usb_xhci_fetch_trb','usb_xhci_queue_event','usb_xhci_irq_msix','usb_xhci_run','usb_xhci_stop'):
        command += f' -trace enable={event}'
if acpi_fixture:
    fixture_path=test/'fixture.aml'
    subprocess.run([sys.executable,str(root/'src/tests/acpi-fixture.py'),str(fixture_path)],check=True)
    command += ' -acpitable file=' + shlex.quote(str(fixture_path))
if usb_hubs:
    command += ' -device usb-hub,id=testhub,bus=xhci.0,port=4 -device usb-kbd,id=hub-child,bus=xhci.0,port=4.1 -device usb-hub,id=nestedhub,bus=xhci.0,port=4.2 -device usb-mouse,id=hub-grandchild,bus=xhci.0,port=4.2.1'
if storage_hotplug:
    command=command.replace('-device usb-storage,bus=xhci.0', '-device usb-storage,id=storage-test,bus=xhci.0')
if storage_nested:
    # OVMF's boot-time USB stack does not reliably boot through two virtual hubs.
    # A disposable AHCI copy boots the test; the writable target stays downstream.
    shutil.copyfile(iso,test/'sata.img')
    command=command.replace(',drive=bootmedia,bootindex=1',',drive=bootmedia')
    command=command.replace('-device ide-hd,bus=ide.0,drive=satadisk','-device ide-hd,bus=ide.0,drive=satadisk,bootindex=1')
    command=command.replace('-device usb-storage,id=storage-test,bus=xhci.0','-device usb-hub,id=storage-hub,bus=xhci.0,port=4 -device usb-hub,id=storage-inner,bus=xhci.0,port=4.2 -device usb-storage,id=storage-test,bus=xhci.0,port=4.2.1')
log = test / 'boot.log'
if hotplug:
    command = command.replace('-device usb-kbd,bus=xhci.0', '-device usb-kbd,id=xhci-test-keyboard,bus=xhci.0,port=1')
    command = command.replace('-debugcon stdio', f'-debugcon file:{log}')
    command += ' -qmp stdio'
with log.open('w') as output, (test/'qemu.log').open('w') as qemu_errors:
    process = subprocess.Popen(shlex.split(command), cwd=root / 'src', bufsize=0, stdin=subprocess.PIPE if hotplug else None, stdout=subprocess.PIPE if hotplug else output, stderr=qemu_errors if hotplug else output)
    try:
        deadline = time.monotonic() + (90 if storage_write else 45 if hotplug else 30)
        removed = added = False
        storage_removed = storage_added = False
        usb_removed = usb_added = False
        hub_child_removed = hub_child_added = hub_removed = hub_added = False
        def qmp_call(name, arguments=None):
            request = {'execute':name}
            if arguments is not None: request['arguments'] = arguments
            process.stdin.write((json.dumps(request)+'\n').encode()); process.stdin.flush()
            while True:
                if not select.select([process.stdout],[],[],3)[0]: raise TimeoutError('QMP response timeout')
                response = json.loads(process.stdout.readline())
                if 'error' in response: raise RuntimeError(response)
                if 'return' in response: return response['return']
        if hotplug:
            if not select.select([process.stdout],[],[],3)[0]: raise TimeoutError('QMP greeting timeout')
            json.loads(process.stdout.readline()); qmp_call('qmp_capabilities')
        while time.monotonic() < deadline:
            content = log.read_text()
            if xhci_hotplug and 'WAIT XHCI REMOVE' in content and not removed:
                qmp_call('device_del', {'id':'xhci-test-keyboard'}); removed = True
            if xhci_hotplug and 'WAIT XHCI ADD' in content and not added:
                qmp_call('device_add', {'driver':'usb-kbd','id':'xhci-test-keyboard','bus':'xhci.0','port':'1'}); added = True
            if storage_hotplug and 'WAIT STORAGE REMOVE' in content and not storage_removed:
                qmp_call('device_del', {'id':'storage-hub' if storage_nested else 'storage-test'});storage_removed=True
            if storage_hotplug and 'WAIT STORAGE ADD' in content and not storage_added:
                # QEMU deletes the legacy -drive backend with its USB device.
                qmp_call('blockdev-add', {'driver':'raw','node-name':'storage-replug','read-only':not storage_write,'file':{'driver':'file','filename':str(iso)}})
                if storage_nested:
                    # Removing the outer hub detaches the whole downstream tree.
                    qmp_call('device_add', {'driver':'usb-hub','id':'storage-hub','bus':'xhci.0','port':'4'})
                    qmp_call('device_add', {'driver':'usb-hub','id':'storage-inner','bus':'xhci.0','port':'4.2'})
                qmp_call('device_add', {'driver':'usb-storage','id':'storage-test','bus':'xhci.0','drive':'storage-replug',**({'port':'4.2.1'} if storage_nested else {})});storage_added=True
            if usb_hotplug and 'WAIT USB REMOVE' in content and not usb_removed:
                qmp_call('device_del', {'id':'xhci-test-keyboard'}); usb_removed = True
            if usb_hotplug and 'WAIT USB ADD' in content and not usb_added:
                qmp_call('device_add', {'driver':'usb-kbd','id':'xhci-test-keyboard','bus':'xhci.0','port':'1'}); usb_added = True
            if usb_hubs and 'WAIT HUB CHILD REMOVE' in content and not hub_child_removed:
                qmp_call('device_del', {'id':'hub-child'}); hub_child_removed=True
            if usb_hubs and 'WAIT HUB CHILD ADD' in content and not hub_child_added:
                qmp_call('device_add', {'driver':'usb-kbd','id':'hub-child','bus':'xhci.0','port':'4.1'}); hub_child_added=True
            if usb_hubs and 'WAIT HUB REMOVE' in content and not hub_removed:
                qmp_call('device_del', {'id':'testhub'}); hub_removed=True
            if usb_hubs and 'WAIT HUB ADD' in content and not hub_added:
                qmp_call('device_add', {'driver':'usb-hub','id':'testhub','bus':'xhci.0','port':'4'})
                qmp_call('device_add', {'driver':'usb-kbd','id':'hub-child','bus':'xhci.0','port':'4.1'})
                qmp_call('device_add', {'driver':'usb-hub','id':'nestedhub','bus':'xhci.0','port':'4.2'})
                qmp_call('device_add', {'driver':'usb-mouse','id':'hub-grandchild','bus':'xhci.0','port':'4.2.1'}); hub_added=True
            if config_error:
                expected='Cloudberry startup: config.init failed' if config_error=='invalid' else 'Cloudberry startup: cloudberry.cnf missing or duplicate data module'
                if expected in content:
                    assert 'BOOT OK;' not in content and 'PASS mem surface test' not in content
                    if config_error=='invalid': assert 'Cloudberry config error=0x00000006 line=0x0000000000000002' in content
                    print('PASS: configuration startup rejection:',config_error)
                    break
            if watchdog and 'inthandle: CPU halted' in content:
                assert 'fatal lock acquisition timeout' in content and 'vector=0x0000000000000006' in content and 'rsp=0x' in content
                print('PASS: failed lock owner triggers bounded watchdog, register dump and halt')
                break
            if missing_clock and 'Cloudberry startup: clock.init missing' in content:
                assert 'BOOT OK;' not in content
                print('PASS: isolated startup failure identifies clock.init as missing')
                break
            if not watchdog and not missing_clock and 'PASS mem surface test' in content and 'managed BSP event stack active' in content:
                methods = ('init', 'malloc', 'free', 'calloc', 'realloc', 'aligned_alloc', 'usable_size')
                for method in methods:
                    assert re.search(rf'^mem\.{method} -> 0x[0-9a-f]{{16}}$', content, re.MULTILINE)
                for method in ('hub.publish_table', 'hub.dump', 'splash.show', 'hardware.read', 'hwenum.enumerate', 'hwenum.find', 'hwenum.dump', 'hwenum.hardware', 'scheduler.init', 'scheduler.submit', 'scheduler.run', 'scheduler.cancel', 'scheduler.result', 'scheduler.stats', 'scheduler.remove_process', 'hardware.map_mmio', 'mmio.init', 'mmio.map', 'mmio.read', 'mmio.write', 'mmio.regions', 'clock.init', 'clock.calibrate', 'clock.now_ns', 'clock.now_us', 'clock.now_ms', 'clock.info', 'clock.rtc', 'inthandle.claim', 'inthandle.release', 'inthandle.cmos_read', 'inthandle.nmi_mask', 'scheduler.next_deadline', 'timer.init', 'timer.arm', 'timer.stop', 'timer.take', 'timer.info', 'timer.interrupt', 'apic.init', 'apic.info', 'apic.spurious', 'scheduler.info', 'event.info', 'memory.release', 'memory.map_owned', 'memory.unmap_owned', 'mem.pages_init', 'mem.pages_alloc', 'mem.pages_free', 'mem.stack_alloc', 'mem.stack_free', 'mem.pages_info', 'event.init', 'event.step', 'event.run'):
                    assert re.search(rf'^{re.escape(method)} -> 0x[0-9a-f]{{16}}$', content, re.MULTILINE)
                assert content.index('Cloudberry v2: inthandle ready') < content.index('Cloudberry v2: pre-flight Cloudberry logo displayed') < content.index('Cloudberry backend: cpu-')
                if iommu != 'none':
                    assert 'PASS IOMMU:' in content
                assert ('PASS ACPI AML fixture:' if acpi_fixture else 'PASS ACPI:') in content
                assert ('PASS USB storage/ext4:' if storage else 'PASS USB hubs:' if usb_hubs else 'PASS USB core:') in content
                if usb_hubs:
                    assert hub_child_removed and hub_child_added and hub_removed and hub_added
                    assert 'PASS USB hub class restart:' in content
                if storage_write: assert 'PASS writable USB/ext4:' in content
                if storage_hotplug: assert storage_removed and storage_added
                if usb_hotplug: assert usb_removed and usb_added
                assert 'PASS config: Limine text handoff, typed owned descriptors and atomic publication' in content
                assert 'PASS xHCI:' in content
                if xhci_hotplug: assert removed and added
                assert 'PASS NVMe:' in content
                assert 'PASS device IRQ:' in content
                assert 'MEASURE max managed IRQ-off ticks:' in content
                assert 'MEASURE thread max runnable wait ns:' in content
                assert 'MEASURE TSC validation status:' in content
                assert 'PASS xstate:' in content
                assert 'PASS threads/SMP:' in content
                assert f'CPUs online: {cpus}' in content
                assert 'PASS managed pages:' in content
                assert 'PASS event loop:' in content
                assert 'PASS clock boot:' in content
                count = re.search(r'^hwenum\.hardware \((\d+) descriptors\)$', content, re.MULTILINE)
                assert count and int(count.group(1)) == len(re.findall(r'^hw ', content, re.MULTILINE))
                for category in ('memory', 'acpi', 'cpu', 'pci', 'local-apic', 'io-apic', 'hpet'):
                    assert re.search(rf'^hw {category}\b', content, re.MULTILINE)
                for pci_class in ('01:08:02', '01:06:01', '0c:03:30'):
                    assert f'class={pci_class}' in content
                measurements = {}
                for key, value in re.findall(r'^MEASURE ([^:]+): (-?\d+)$', content, re.MULTILINE):
                    measurements.setdefault(key, []).append(int(value))
                expected_vendor='intel' if 'GenuineIntel' in cpu_model else 'amd' if 'AuthenticAMD' in cpu_model else None
                selected_vendor=re.search(r'Cloudberry backend: cpu-(\w+)',content).group(1)
                if expected_vendor: assert selected_vendor==expected_vendor
                assert len(re.findall(r'Cloudberry backend: cpu-',content))==1
                if iommu!='none':
                    assert f'Cloudberry backend: iommu-{iommu}' in content
                    assert len(re.findall(r'Cloudberry backend: iommu-',content))==1
                else: assert 'Cloudberry backend: iommu-' not in content
                host_snapshot=None
                if accel=='kvm':
                    proc=pathlib.Path('/proc/cpuinfo')
                    first=proc.read_text().split('\n\n',1)[0] if proc.exists() else ''
                    host_snapshot={key:value.strip() for line in first.splitlines() if ':' in line for key,value in [line.split(':',1)] if key.strip() in ('vendor_id','cpu family','model','model name','stepping','flags')}
                    host_snapshot={key.strip():value for key,value in host_snapshot.items()}
                report = {'run_id':run_id,'status':'passed','started_at':run_result['started_at'],'host_cpu':host_snapshot,'cpu_model': cpu_model, 'cpu_backend': re.search(r'Cloudberry backend: cpu-(\w+)',content).group(1), 'accelerator': accel, 'cpus': cpus, 'iommu': iommu, 'qemu_command': shlex.split(command), 'measurements': measurements, 'notes': re.findall(r'^NOTE .+$', content, re.MULTILINE), 'physical_hardware_validated': False, 'physical_dma_takeover_validated': False, 'x2apic_requested': next((arg.split('=',1)[1]=='1' for arg in sys.argv[2:] if arg.startswith('--x2apic-requested=')),False), 'x2apic_validated': measurements.get('APIC mode')==[2]}
                reasons=('none','not initialized','TSC missing','invariant TSC missing','HPET missing','uncalibrated','calibration jitter','frequency mismatch','counter regression','CPU capability mismatch','reference failure','conversion failure','CPU offset','CPU drift','measurement uncertainty','timeout','activation failure','reference source','setup failure','missing method')
                reason=measurements['TSC rejection reason'][0]
                report['tsc_rejection_reason']=reasons[reason] if reason<len(reasons) else f'unknown {reason}'
                report['tsc_profile']=summarize_tsc(measurements, parse_exchanges(content))
                report_path.write_text(json.dumps(report,indent=2)+'\n')
                print('Acceptance report:',test/'report.json')
                print(f'PASS: {cpus} CPU boot, timer-preempted threads, block/wake, SMP allocation and remote TLB reuse; separate mem/hwenum modules, hub methods/table publication, memory/CPU/NVMe/AHCI/xHCI enumeration, scheduler method/interrupt jobs, real HPET MMIO mapping/read/write, clock calibration/monotonic timestamps, RTC/NMI preservation and APIC deadline event loop')
                break
            if re.search(r'^Cloudberry v2:.*failed|^.*\bFAIL\b', content, re.MULTILINE) or process.poll() is not None:
                raise SystemExit(content)
            time.sleep(0.1)
        else:
            raise SystemExit('Boot timed out:\n' + log.read_text())
    except BaseException as error:
        finish('failed',f'{type(error).__name__}: {error}')
        raise
    else:
        finish('passed')
    finally:
        try:
            if process.poll() is None:process.terminate()
            process.wait(timeout=5)
        except BaseException as error:
            finish('failed',f'VM cleanup failed: {error}')
            raise
if storage_write:
    try:subprocess.run([sys.executable,str(root/'src/tests/boot-image.py'),str(iso)],check=True)
    except BaseException as error:
        finish('failed',f'Post-write filesystem validation failed: {error}');raise
print('Boot log:', log)
