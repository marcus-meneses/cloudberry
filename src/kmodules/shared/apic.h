#ifndef CLOUDBERRY_KMODULE_APIC_H
#define CLOUDBERRY_KMODULE_APIC_H
#include "module.h"
#include "inthandle.h"
#define APIC_SPURIOUS_VECTOR 255u
enum apic_mode {APIC_XAPIC=1,APIC_X2APIC=2};
struct apic_info {uintptr_t address;uint64_t physical,spurious;uint32_t mode,spurious_vector;};
/* BSP establishes controller ownership. cpu_init() enables/masks this CPU
 * using the established descriptor, without repeating global IOAPIC/PIC setup. init(hub) masks external inputs, enables the
 * local controller and claims spurious delivery. info(out) exports the stable
 * local-register access descriptor to cooperating kernel clients such as timer.
 * No external IRQ routing, AP startup or timer programming here. */
struct kernel_surface_result apic_surface_cpu_init(MODULE_ARGS);
struct kernel_surface_result apic_surface_init(MODULE_ARGS);
struct kernel_surface_result apic_surface_info(MODULE_ARGS);
struct kernel_surface_result apic_surface_spurious(MODULE_ARGS);
#endif
