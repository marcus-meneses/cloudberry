#ifndef CLOUDBERRY_INTERFACE_REGISTRY_H
#define CLOUDBERRY_INTERFACE_REGISTRY_H
#include "installations.h"
/* Supplemental registrations: explicit little-endian u64 fields. Header is
 * magic/version 0x314745524243, total bytes, generation, count (32 bytes).
 * Each entry is owner UID, reserved zero, then the existing 64-byte installation
 * record and its name/path/manifest bytes. UINT64_MAX means system-wide.
 * Built-in entries never appear here and cannot be replaced. */
#define CB_REGISTRY_MAGIC UINT64_C(0x314745524243)
#define CB_REGISTRY_SYSTEM UINT64_MAX
static inline uint64_t cb_reg_u64(const void *p){const unsigned char *b=(const unsigned char *)p;uint64_t n=0;for(unsigned i=0;i<8;++i)n|=(uint64_t)b[i]<<(i*8);return n;}
static inline void cb_reg_put(void *p,uint64_t n){unsigned char *b=(unsigned char *)p;for(unsigned i=0;i<8;++i)b[i]=n>>(8*i);}
enum {CB_REGISTRY_VERSION=1,CB_REGISTRY_LIST,CB_REGISTRY_CONTEXT,CB_REGISTRY_BEGIN,CB_REGISTRY_STAGE,CB_REGISTRY_PREPARE,CB_REGISTRY_COMMIT,CB_REGISTRY_ABORT,CB_REGISTRY_SOURCE};
struct cb_registry_request {uint64_t operation,flags,actor,token,offset,bytes;};
/* LIST: offset ordinal, token optional expected generation; flags=1 selects a
 * UID in actor for identity/installer only, else actor is authenticated PID.
 * Returns cb_registry_entry + inline name. Zero bytes is EOF. */
struct cb_registry_entry {uint64_t generation,cursor,name_bytes,owner;uint8_t installation[16];uint32_t flags,dynamic;};
struct cb_install_context {uint64_t uid,gid,home_bytes,administrator;};
/* CONTEXT returns this header + canonical home bytes, no NUL. BEGIN takes
 * expected generation in token, total staging bytes in bytes. STAGE appends
 * inline chunks at exact offsets. PREPARE validates and gates dynamic launches;
 * COMMIT swaps the prepared immutable snapshot without allocating. ABORT discards
 * staging. Only the installed installer may mutate, and only its caller's scope.
 * SOURCE takes actor PID and inline canonical path (bytes); it checks package
 * read access using the requesting app's policy, never installer privileges.
 * Publication tokens are idempotent for the most recent successful COMMIT. */
#endif
