#!/usr/bin/env python3
"""Boot the production image in an isolated VM, without probe modules or apps."""
import datetime
import json
import pathlib
import subprocess
import sys
import time
import uuid

root = pathlib.Path(__file__).resolve().parents[2]
build = pathlib.Path(sys.argv[1]).resolve()
if not build.is_relative_to(root / 'bin/tests'):
    raise SystemExit('isolated build below bin/tests required')
test = build / 'tests/keyboard-startup'
test.mkdir(parents=True, exist_ok=True)
result = {'run_id': uuid.uuid4().hex, 'status': 'running', 'cpus': 4,
          'accelerator': 'tcg', 'iommu': 'amd',
          'started_at': datetime.datetime.now(datetime.timezone.utc).isoformat()}
report = test / 'report.json'
report.write_text(json.dumps(result, indent=2) + '\n')
log = test / 'boot.log'
command = ['qemu-system-x86_64', '-no-user-config', '-nodefaults', '-machine', 'q35',
           '-m', '512M', '-accel', 'tcg', '-cpu', 'max', '-smp', '4',
           '-bios', '/usr/share/ovmf/OVMF.fd', '-device', 'VGA',
           '-device', 'amd-iommu,intremap=off', '-device', 'qemu-xhci,id=xhci',
           '-device', 'usb-kbd,bus=xhci.0',
           '-drive', f'if=none,id=bootmedia,format=raw,readonly=on,file={build / "cloudberry.iso"}',
           '-device', 'usb-bot,id=bootusb,bus=xhci.0',
           '-device', 'scsi-hd,bus=bootusb.0,drive=bootmedia,bootindex=1',
           '-display', 'none', '-debugcon', 'stdio', '-global', 'isa-debugcon.iobase=0xe9',
           '-monitor', 'none', '-no-reboot', '-no-shutdown']
try:
    with log.open('w') as output:
        process = subprocess.Popen(['bash', str(root / 'src/boot/run-qemu.sh')] + command,
                                   stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline:
                text = log.read_text()
                if any(marker in text for marker in ('FAULT', 'FAIL', 'inthandle: fatal', 'provider unavailable')):
                    raise RuntimeError(text[-4000:])
                if 'bootstrap: /@keyboard provider ready from ext4' in text and 'managed BSP event stack active' in text:
                    break
                if process.poll() is not None:
                    raise RuntimeError(f'QEMU exited {process.returncode}: {text[-2000:]}')
                time.sleep(.1)
            else:
                raise RuntimeError('boot timeout: ' + log.read_text()[-3000:])
        finally:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    result['status'] = 'passed'
    print('PASS isolated production keyboard startup; report:', report)
except BaseException as error:
    result.update(status='failed', reason=str(error)[-4096:])
    raise
finally:
    result['finished_at'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    report.write_text(json.dumps(result, indent=2) + '\n')
