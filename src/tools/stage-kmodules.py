#!/usr/bin/env python3
"""Classify compiled boot modules; module identities are unchanged."""
import pathlib,sys
DRIVERS = {'timer', 'iommu-intel', 'usb', 'xhci', 'sata', 'usb-storage', 'acpi', 'platform', 'pci', 'iommu', 'nvme', 'irq', 'apic', 'usb-hub', 'iommu-amd'}
root=pathlib.Path(sys.argv[1])
for group in ('drivers','helpers'):(root/group).mkdir(exist_ok=True)
for module in root.glob('*.kmod'):
    module.replace(root/('drivers' if module.stem in DRIVERS else 'helpers')/module.name)
