#ifndef CLOUDBERRY_TEST_SPACE_FAULT_H
#define CLOUDBERRY_TEST_SPACE_FAULT_H
#include "../kmodules/shared/module.h"
struct space_fault_slot {uint64_t root,address,rip,resume,error,seen,armed;};
enum space_fault_access {SPACE_READ,SPACE_WRITE,SPACE_EXEC};
/* Entirely test-only. Installs a CPU-local #PF gate for a single guarded
 * instruction with IRQs off, then restores the original gate and flags. */
int space_fault_access(unsigned access,uintptr_t address,uint64_t value,int expect_fault,uint64_t error);
struct kernel_surface_result probe_space_fault_config(MODULE_ARGS);
#endif
