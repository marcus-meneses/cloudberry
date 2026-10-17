#ifndef CLOUDBERRY_SDK_SYSTEM_H
#define CLOUDBERRY_SDK_SYSTEM_H
#include "../interfaces/system.h"
#include "../interfaces/memory.h"
cb_status_t cb_system_gate(struct cb_system_gate *);
__attribute__((noreturn)) void cb_exit(uint64_t);
static inline struct cb_call_header cb_call(cb_handle_t cap,struct cb_interface_id interface,uint32_t operation,uint64_t bytes){return (struct cb_call_header){.version={sizeof(struct cb_call_header),1,0},.handle=cap,.interface=interface,.operation=operation,.payload_bytes=bytes};}
#endif
