#ifndef CLOUDBERRY_MEM_SPACES_H
#define CLOUDBERRY_MEM_SPACES_H
#include "module.h"
#define MEM_USER_LIMIT (UINT64_C(1)<<47)
enum mem_space_flags {MEM_SPACE_WRITE=1,MEM_SPACE_EXEC=2};
enum mem_space_state {MEM_SPACE_READY=1,MEM_SPACE_DESTROYING};
struct mem_space_info {uint64_t id,owner,root,active,leases;uint32_t state,reserved;};
struct mem_space_mapping {uint64_t physical;uint32_t flags,reserved;};
/* Trusted kernel facilities, not application handles or system calls.
 * create(owner!=0,out info); map(id,owner,va,physical,flags); unmap(id,owner,va);
 * protect(id,owner,va,flags); query(id,owner,va,out mapping); info(id,owner,out);
 * pin(id,owner,acquire); switch(id,owner) with IF=0; destroy(id,owner) with
 * IF=1 and no outer critical section. Allocation/teardown is foreground work.
 * ID zero selects the kernel root. Nonzero IDs are never reused. Switching is
 * scheduler-only: threads retain a lease and restore the kernel dispatcher root.
 * Data pages must be owned BOOT_RES_PAGES allocations; unmap/destroy releases
 * mapping references, not backing allocations. W+X, page zero and upper-half
 * mappings are rejected. Inactive/unleased destruction reclaims private tables.
 */
#endif
