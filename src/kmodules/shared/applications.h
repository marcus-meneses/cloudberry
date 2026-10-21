#ifndef CLOUDBERRY_KMODULE_APPLICATIONS_H
#define CLOUDBERRY_KMODULE_APPLICATIONS_H
#include "io.h"
struct applications_info {uint64_t manager,children,launched,reaped,faulted,last_exit;};
struct kernel_surface_result applications_surface_info(MODULE_ARGS);
int applications_init(const struct kernel_surfaces *,uintptr_t);
int applications_invoke(struct io_invocation *);
cb_status_t applications_exit(uint64_t);
struct kernel_surface_result applications_surface_installations(MODULE_ARGS);
struct kernel_surface_result applications_surface_installed_load(MODULE_ARGS);
struct kernel_surface_result applications_surface_worker(MODULE_ARGS);
#endif
