#ifndef CLOUDBERRY_KMODULE_IO_H
#define CLOUDBERRY_KMODULE_IO_H
#include "module.h"
#include "../../interfaces/io.h"
struct io_provider_info {uint64_t thread,id,device,generation;uint32_t active,claimed,stopped,waiting;};
struct io_invocation {uint64_t process;uint32_t operation,status;const void *input;size_t bytes;void *output;size_t capacity,actual;};
struct kernel_surface_result io_surface_init(MODULE_ARGS);
struct kernel_surface_result io_surface_invoke(MODULE_ARGS);
struct kernel_surface_result io_surface_exit(MODULE_ARGS);
#endif
