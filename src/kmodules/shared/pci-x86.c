#include "pci-platform.h"
uint16_t pci_read16(uintptr_t p) {return *(volatile uint16_t *)p;}
uint32_t pci_read32(uintptr_t p) {return *(volatile uint32_t *)p;}
void pci_write16(uintptr_t p,uint16_t value) {*(volatile uint16_t *)p=value;__asm__ volatile("mfence":::"memory");}
void pci_write32(uintptr_t p,uint32_t value) {*(volatile uint32_t *)p=value;__asm__ volatile("mfence":::"memory");}
