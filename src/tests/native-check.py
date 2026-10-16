#!/usr/bin/env python3
"""Read-only KVM availability check; never changes host device permissions."""
import os
try:
    descriptor = os.open('/dev/kvm', os.O_RDWR | os.O_CLOEXEC)
    os.close(descriptor)
except OSError as error:
    raise SystemExit(f'Native validation unavailable: cannot open /dev/kvm: {error.strerror}')
print('KVM device is accessible; the isolated acceptance VM will use -accel kvm; default CPU is host,migratable=off,+invtsc.')
