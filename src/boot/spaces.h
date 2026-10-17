#ifndef CLOUDBERRY_BOOT_SPACES_H
#define CLOUDBERRY_BOOT_SPACES_H
#include "memory.h"
#define BOOT_USER_LIMIT (UINT64_C(1)<<47)
enum boot_user_flags {BOOT_USER_WRITE=1,BOOT_USER_EXEC=2};
/* Caller serializes ledger/page tables and acknowledges TLB invalidation before
 * mapped frames can be released. All roots share supervisor-only upper slots. */
bool boot_spaces_prepare(struct boot_pages *,uint64_t root);
uint64_t boot_space_create(struct boot_pages *,uintptr_t owner);
bool boot_space_map(struct boot_pages *,uint64_t root,uintptr_t owner,uintptr_t va,uint64_t physical,unsigned flags);
bool boot_space_unmap(struct boot_pages *,uint64_t root,uintptr_t owner,uintptr_t va);
bool boot_space_protect(struct boot_pages *,uint64_t root,uintptr_t va,unsigned flags);
bool boot_space_query(struct boot_pages *,uint64_t root,uintptr_t va,uint64_t *physical,unsigned *flags);
/* Inactive, unleased root only. One bounded leaf/table disposal per step;
 * 1=more, 2=root released, 0=retained inconsistent state. Does not free data. */
unsigned boot_space_reap(struct boot_pages *,uint64_t root,uintptr_t owner);
#endif
