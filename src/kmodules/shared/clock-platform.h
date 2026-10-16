#ifndef CLOUDBERRY_CLOCK_PLATFORM_H
#define CLOUDBERRY_CLOCK_PLATFORM_H
#include <stdint.h>
#include "../../kernel/surfaces.h"
int clock_platform_configure(const struct kernel_surfaces *);
struct clock_cpu { uint64_t hz; uint32_t flags; };
struct clock_cpu clock_cpu_detect(void);
uint64_t clock_cpu_ticks(void);
int clock_hpet_read(uintptr_t address,uint64_t *out);
uint8_t clock_cmos_read(uint8_t index);
#endif
