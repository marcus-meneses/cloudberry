#ifndef CLOUDBERRY_APIC_PLATFORM_H
#define CLOUDBERRY_APIC_PLATFORM_H
#include <stdint.h>
struct apic_platform {uint64_t physical;uint32_t mode;};
int apic_platform_probe(struct apic_platform *);
void apic_platform_activate(uintptr_t address,uint32_t mode);
uint32_t apic_register_read(uint32_t offset);
void apic_register_write(uint32_t offset,uint32_t value);
uint64_t apic_irq_save(void);
void apic_irq_restore(uint64_t flags);
void apic_mask_pic(void);
#endif
