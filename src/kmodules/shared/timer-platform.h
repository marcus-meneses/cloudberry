#ifndef CLOUDBERRY_TIMER_PLATFORM_H
#define CLOUDBERRY_TIMER_PLATFORM_H
#include <stdint.h>
void timer_platform_configure(uintptr_t address,uint32_t mode);
uint32_t timer_register_read(uint32_t offset);
void timer_register_write(uint32_t offset,uint32_t value);
uint64_t timer_irq_save(void);
void timer_irq_restore(uint64_t flags);
#endif
