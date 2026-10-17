#ifndef CLOUDBERRY_BOOTSTRAP_LOAD_H
#define CLOUDBERRY_BOOTSTRAP_LOAD_H
#include "bootstrap.h"
#include "module-load.h"
enum driver_state {DRIVER_SELECTED=1,DRIVER_SKIPPED,DRIVER_LIVE,DRIVER_FAILED};
enum driver_error {DRIVER_OK,DRIVER_CONFIG,DRIVER_MEMORY,DRIVER_FILE,DRIVER_MANIFEST,DRIVER_LOAD};
struct driver_record {struct driver_record *next;char name[32];uint32_t state,error;uint64_t devices;struct module_load_result result;};
struct driver_table {uint64_t count;struct driver_record *first;uint32_t started,finished,error,reserved;};
int bootstrap_loader_init(const struct kernel_surfaces *,uintptr_t);
struct kernel_surface_result bootstrap_surface_load(MODULE_ARGS);
struct kernel_surface_result bootstrap_surface_driver_info(MODULE_ARGS);
#endif
