#ifndef CLOUDBERRY_PCI_PLATFORM_H
#define CLOUDBERRY_PCI_PLATFORM_H
#include <stdint.h>
uint16_t pci_read16(uintptr_t);
uint32_t pci_read32(uintptr_t);
void pci_write16(uintptr_t,uint16_t);
void pci_write32(uintptr_t,uint32_t);
#endif
