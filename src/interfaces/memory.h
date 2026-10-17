#ifndef CLOUDBERRY_INTERFACE_MEMORY_H
#define CLOUDBERRY_INTERFACE_MEMORY_H
#include "handle.h"
#define CB_MEMORY_NAME "/@memory"
#define CB_MEMORY_INTERFACE_ID {{0x43,0x42,0x4d,0x45,0x4d,0x31,0x46,0x3b,0x91,0xe1,0x40,0x0a,0x1c,0x87,0xc0,0x04}}
enum {CB_MEMORY_ALLOCATE=1,CB_MEMORY_FREE,CB_MEMORY_RESIZE};
struct cb_memory_request {uint64_t address,bytes;};
struct cb_memory_result {uint64_t address,bytes;};
#endif
