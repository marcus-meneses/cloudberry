#ifndef CLOUDBERRY_IO_PROCESS_H
#define CLOUDBERRY_IO_PROCESS_H
#include "io.h"
struct io_process_info {uint64_t manager,children,launched,reaped,faulted,last_exit;};
struct kernel_surface_result io_surface_process_info(MODULE_ARGS);
int io_process_init(const struct kernel_surfaces *,uintptr_t);
int io_process_invoke(struct io_invocation *);
cb_status_t io_process_exit(uint64_t);
struct kernel_surface_result io_surface_installations(MODULE_ARGS);
struct kernel_surface_result io_surface_installed_load(MODULE_ARGS);
struct kernel_surface_result io_surface_process_worker(MODULE_ARGS);
#endif
