#ifndef CLOUDBERRY_INTERFACE_SYSTEM_H
#define CLOUDBERRY_INTERFACE_SYSTEM_H
#include "handle.h"
/* Local x86-64 gate descriptor, copied by the kernel; pointers exist only here,
 * never in provider messages. int80: RAX=1, RDI=&gate. RAX=0 is process exit. */
enum {CB_SYSTEM_ACQUIRE=1,CB_SYSTEM_INVOKE,CB_SYSTEM_RELEASE,CB_SYSTEM_PUBLISH,CB_SYSTEM_RECEIVE,CB_SYSTEM_REPLY};
enum {CB_FRAME_LIMIT=16384};
struct cb_system_gate {uint32_t bytes,operation;uint64_t input,input_bytes,output,output_capacity,result_bytes,value,deadline_ns;uint32_t status,reserved;};
struct cb_acquire {struct cb_interface_id interface;uint16_t major,minor;uint32_t reserved;uint64_t name_bytes;};
#endif
