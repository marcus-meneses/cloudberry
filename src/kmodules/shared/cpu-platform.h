#ifndef CLOUDBERRY_CPU_PLATFORM_H
#define CLOUDBERRY_CPU_PLATFORM_H
#include "module.h"
enum cpu_vendor {CPU_VENDOR_INTEL=1,CPU_VENDOR_AMD=2};
struct cpu_features {uint32_t vendor,physical_bits,linear_bits,reserved;};
struct cpu_leaf {uint32_t a,b,c,d;};
typedef struct cpu_leaf (*cpu_cpuid_reader)(uint32_t,uint32_t);
struct cpu_leaf cpu_cpuid(uint32_t,uint32_t) __attribute__((visibility("hidden")));
uint32_t cpu_topology_id(cpu_cpuid_reader,unsigned) __attribute__((visibility("hidden")));
uint32_t cpu_backend_id(void) __attribute__((visibility("hidden")));
uint64_t cpu_backend_ticks(void) __attribute__((visibility("hidden")));
int cpu_backend_init(void) __attribute__((visibility("hidden")));
unsigned cpu_backend_vendor(void) __attribute__((visibility("hidden")));
/* ticks() orders surrounding execution, not a replacement for ipi_fence().
 * AMD selects a supported ordering mode once; APs must confirm that mode.
 * All selected implementations export these names/version 1. No allocation,
 * locks, interrupts or scheduling; usable during attach and inside an ISR. */
struct kernel_surface_result cpu_surface_init(MODULE_ARGS);
struct kernel_surface_result cpu_surface_id(MODULE_ARGS);
struct kernel_surface_result cpu_surface_ticks(MODULE_ARGS);
struct kernel_surface_result cpu_surface_fence(MODULE_ARGS);
struct kernel_surface_result cpu_surface_features(MODULE_ARGS);
enum cpu_tsc_flags {CPU_TSC_PRESENT=1,CPU_TSC_INVARIANT=2,CPU_TSC_ENUMERATED=4,CPU_TSC_RDTSCP=128,CPU_TSC_LFENCE_SERIALIZING=256};
enum cpu_tsc_frequency_source {CPU_TSC_FREQ_NONE,CPU_TSC_FREQ_CPUID15};
struct cpu_tsc_info {uint32_t abi,vendor,flags,frequency_source;uint64_t hz;uint32_t extended_power_edx,reserved;};
struct cpu_tsc_info cpu_tsc_probe(cpu_cpuid_reader,unsigned) __attribute__((visibility("hidden")));
struct cpu_tsc_info cpu_backend_tsc(void) __attribute__((visibility("hidden")));
struct kernel_surface_result cpu_surface_tsc(MODULE_ARGS);
#endif
