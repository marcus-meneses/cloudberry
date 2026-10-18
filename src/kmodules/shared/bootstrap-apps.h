#ifndef CLOUDBERRY_BOOTSTRAP_APPS_H
#define CLOUDBERRY_BOOTSTRAP_APPS_H
#include "module.h"
struct bootstrap_application_info {uint64_t process;uint32_t state,error;};
struct bootstrap_apps_info {uint64_t worker,count;uint32_t state,error;};
int bootstrap_apps_init(const struct kernel_surfaces *,uintptr_t);
struct kernel_surface_result bootstrap_surface_apps(MODULE_ARGS);
struct kernel_surface_result bootstrap_surface_apps_worker(MODULE_ARGS);
struct kernel_surface_result bootstrap_surface_apps_info(MODULE_ARGS);
struct kernel_surface_result bootstrap_surface_launch(MODULE_ARGS);
struct kernel_surface_result bootstrap_surface_release(MODULE_ARGS);
struct kernel_surface_result bootstrap_surface_application_info(MODULE_ARGS);
struct kernel_surface_result bootstrap_surface_application_next(MODULE_ARGS);
#endif
