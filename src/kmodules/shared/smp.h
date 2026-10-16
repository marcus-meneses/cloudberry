#ifndef CLOUDBERRY_SMP_H
#define CLOUDBERRY_SMP_H
#include "module.h"
#include "../../kernel/cpu.h"
struct smp_info {uint64_t count,online;uint32_t bsp,shared_clock;};
/* prepare(hub,heap,Limine MP response), start(), info(out), interrupt(frame,ctx).
 * No hotplug or migration. APs execute pinned threads and idle on IPIs. */
struct kernel_surface_result smp_surface_prepare(MODULE_ARGS);
struct kernel_surface_result smp_surface_start(MODULE_ARGS);
struct kernel_surface_result smp_surface_info(MODULE_ARGS);
struct kernel_surface_result smp_surface_interrupt(MODULE_ARGS);
struct kernel_surface_result smp_surface_clock_status(MODULE_ARGS);
struct kernel_surface_result smp_surface_clock_sample(MODULE_ARGS);
struct kernel_surface_result smp_surface_clock_group(MODULE_ARGS);
struct smp_clock_cost {uint64_t empty_ns,bracket_ns,call_ns,relay_ns,hz,epoch;};
struct kernel_surface_result smp_surface_clock_cost(MODULE_ARGS);
struct kernel_surface_result smp_surface_clock_exchange(MODULE_ARGS);
struct kernel_surface_result smp_surface_clock_offsets(MODULE_ARGS);
#endif
