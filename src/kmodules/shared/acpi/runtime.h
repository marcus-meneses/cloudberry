#ifndef CLOUDBERRY_ACPI_RUNTIME_H
#define CLOUDBERRY_ACPI_RUNTIME_H
#include "../acpi.h"
#include <uacpi/uacpi.h>
extern const struct kernel_surfaces *acpi_hub __attribute__((visibility("hidden")));
extern uintptr_t acpi_heap __attribute__((visibility("hidden")));
extern uint64_t acpi_rsdp __attribute__((visibility("hidden")));
extern unsigned acpi_log_errors;
int acpi_runtime_setup(const struct kernel_surfaces *,uintptr_t,uint64_t);
int acpi_runtime_enter(void);
void acpi_runtime_leave(void);
void acpi_drain_work(void);
char *acpi_copy_string(const char *);
#endif
