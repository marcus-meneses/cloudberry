#ifndef CLOUDBERRY_HANDLES_H
#define CLOUDBERRY_HANDLES_H
#include "module.h"
#include "../../interfaces/handle.h"
struct handle_policy {uint64_t process;const void *manifest;uint64_t bytes;struct cb_provider_identity identity;};
/* Trusted boot admission only, while process is READY. The manifest is copied.
 * gate(authenticated_process, copied local gate) never accepts a user PID.
 * exit(process) removes publications, wakes callers and releases owned records.
 */
struct kernel_surface_result handles_surface_init(MODULE_ARGS);
struct kernel_surface_result handles_surface_admit(MODULE_ARGS);
struct kernel_surface_result handles_surface_gate(MODULE_ARGS);
struct kernel_surface_result handles_surface_exit(MODULE_ARGS);
#endif
