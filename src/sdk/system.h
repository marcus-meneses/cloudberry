#ifndef CLOUDBERRY_SDK_SYSTEM_H
#define CLOUDBERRY_SDK_SYSTEM_H
#include "../interfaces/system.h"
#include "../interfaces/memory.h"
#include "../interfaces/stream.h"
cb_status_t cb_system_gate(struct cb_system_gate *);
__attribute__((noreturn)) void cb_exit(uint64_t);
static inline struct cb_call_header cb_call(cb_handle_t cap,struct cb_interface_id interface,uint32_t operation,uint64_t bytes){return (struct cb_call_header){.version={sizeof(struct cb_call_header),1,0},.handle=cap,.interface=interface,.operation=operation,.payload_bytes=bytes};}
/* Named process-relative stream acquisition; caller owns each returned handle.
 * Zero means the binding is absent or acquisition failed. */
static inline cb_handle_t cb_standard(unsigned index){
    static const char *const names[]={CB_STDIN_NAME,CB_STDOUT_NAME,CB_STDERR_NAME};
    static const size_t bytes[]={sizeof(CB_STDIN_NAME)-1,sizeof(CB_STDOUT_NAME)-1,sizeof(CB_STDERR_NAME)-1};
    if(index>=3)return 0;
    cb_handle_t handle=0;const struct cb_interface_id interface=CB_STREAM_INTERFACE_ID;return cb_system_get_handle(names[index],bytes[index],&interface,1,0,&handle)==CB_OK?handle:0;
}
#endif
