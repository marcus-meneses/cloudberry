#ifndef CLOUDBERRY_PROCESS_H
#define CLOUDBERRY_PROCESS_H
#include "module.h"
enum process_state {PROCESS_LOADING=1,PROCESS_READY,PROCESS_RUNNING,PROCESS_EXITED,PROCESS_FAULTED,PROCESS_REAPING,PROCESS_CLEANUP};
struct process_request {uint32_t abi,cpu;uint64_t filesystem;const char *path;uint64_t argument,stack_bytes;};
struct process_info {uint64_t id,thread,address_space;uint32_t state,cpu;uint64_t exit_code,fault_vector,fault_address,fault_rip,switches;};
/* Trusted bootstrap facilities. Static ELF64 ET_EXEC, one pinned thread per
 * process; load(request) -> ID, start(id), info(id,out), reap(id) after exit.
 * User faults terminate only their process; kernel exceptions remain fatal.
 * copy(id,user,buffer,bytes,write) validates each user mapping and copies through
 * owned physical aliases. No caller virtual pointer is passed to a provider.
 * trap is kernel interrupt entry; process.current returns authenticated owner ID.
 */
struct kernel_surface_result process_surface_init(MODULE_ARGS);
struct kernel_surface_result process_surface_load(MODULE_ARGS);
struct kernel_surface_result process_surface_start(MODULE_ARGS);
struct kernel_surface_result process_surface_run(MODULE_ARGS);
struct kernel_surface_result process_surface_gate(MODULE_ARGS);
struct kernel_surface_result process_surface_fault(MODULE_ARGS);
struct kernel_surface_result process_surface_info(MODULE_ARGS);
struct kernel_surface_result process_surface_reap(MODULE_ARGS);
struct kernel_surface_result process_surface_current(MODULE_ARGS);
struct kernel_surface_result process_surface_copy(MODULE_ARGS);
#endif
