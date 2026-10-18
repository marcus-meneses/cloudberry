#include "../interfaces/system.h"
static void copy(void *dst,const void *src,size_t bytes){unsigned char *d=dst;const unsigned char *s=src;for(size_t i=0;i<bytes;++i)d[i]=s[i];}
cb_status_t cb_system_gate(struct cb_system_gate *gate){uint64_t result;__asm__ volatile("int $0x80":"=a"(result):"a"(UINT64_C(1)),"D"(gate):"memory","cc");return result;}
cb_status_t cb_system_get_handle(const char *name,uint64_t name_bytes,const struct cb_interface_id *interface,uint16_t major,uint16_t minor,cb_handle_t *out){
    if(!name || !interface || !out || name_bytes>CB_FRAME_LIMIT-512-sizeof(struct cb_acquire))return CB_BAD_REQUEST;
    unsigned char input[sizeof(struct cb_acquire)+name_bytes];struct cb_acquire a={.interface=*interface,.major=major,.minor=minor,.name_bytes=name_bytes};copy(input,&a,sizeof(a));copy(input+sizeof(a),name,name_bytes);
    struct cb_system_gate gate={.bytes=sizeof(gate),.operation=CB_SYSTEM_ACQUIRE,.input=(uintptr_t)input,.input_bytes=sizeof(input)};cb_status_t status=cb_system_gate(&gate);if(!status)*out=gate.value;return status;
}
cb_status_t cb_system_invoke(const void *call,uint64_t bytes,void *reply,uint64_t capacity,uint64_t *required){
    struct cb_system_gate gate={.bytes=sizeof(gate),.operation=CB_SYSTEM_INVOKE,.input=(uintptr_t)call,.input_bytes=bytes,.output=(uintptr_t)reply,.output_capacity=capacity};cb_status_t status=cb_system_gate(&gate);if(required)*required=gate.result_bytes;return status;
}
cb_status_t cb_system_release(cb_handle_t handle){struct cb_system_gate gate={.bytes=sizeof(gate),.operation=CB_SYSTEM_RELEASE,.input=(uintptr_t)&handle,.input_bytes=sizeof(handle)};return cb_system_gate(&gate);}
cb_status_t cb_system_publish(const void *publication,uint64_t bytes,cb_handle_t *out){struct cb_system_gate gate={.bytes=sizeof(gate),.operation=CB_SYSTEM_PUBLISH,.input=(uintptr_t)publication,.input_bytes=bytes};cb_status_t status=cb_system_gate(&gate);if(!status && out)*out=gate.value;return status;}
cb_status_t cb_system_receive(void *call,uint64_t capacity,uint64_t *bytes,uint64_t deadline){struct cb_system_gate gate={.bytes=sizeof(gate),.operation=CB_SYSTEM_RECEIVE,.output=(uintptr_t)call,.output_capacity=capacity,.deadline_ns=deadline};cb_status_t status=cb_system_gate(&gate);if(bytes)*bytes=gate.result_bytes;return status;}
cb_status_t cb_system_reply(const void *reply,uint64_t bytes){struct cb_system_gate gate={.bytes=sizeof(gate),.operation=CB_SYSTEM_REPLY,.input=(uintptr_t)reply,.input_bytes=bytes};return cb_system_gate(&gate);}
__attribute__((noreturn)) void cb_exit(uint64_t code){__asm__ volatile("int $0x80"::"a"(UINT64_C(0)),"D"(code):"memory");__builtin_unreachable();}

cb_status_t cb_system_request_release(struct cb_request_id request){struct cb_system_gate gate={.bytes=sizeof(gate),.operation=CB_SYSTEM_FORGET,.input=(uintptr_t)&request,.input_bytes=sizeof(request)};return cb_system_gate(&gate);}
