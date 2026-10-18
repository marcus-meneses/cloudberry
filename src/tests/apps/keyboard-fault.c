#include "../../sdk/system.h"
static cb_status_t fault_receive(void *,uint64_t,uint64_t *,uint64_t);
#define cb_system_receive fault_receive
#define _start keyboard_entry
#include "../../handlers/keyboard/keyboard.c"
#undef _start
#undef cb_system_receive
static cb_status_t fault_receive(void *data,uint64_t capacity_bytes,uint64_t *bytes,uint64_t deadline){
    cb_status_t status=cb_system_receive(data,capacity_bytes,bytes,deadline);
    if(!status && *bytes>=sizeof(struct cb_delivery_header)){
        struct cb_delivery_header d;copy(&d,data,sizeof(d));
        if(d.call.flags==CB_CALL_PROVIDER_DELIVERY && d.call.operation==CB_KEYBOARD_READ)__asm__ volatile("ud2");
    }
    return status;
}
__attribute__((noreturn)) void _start(void){
    static const struct {struct keyboard_startup header;char json[sizeof("{\"version\":1,\"keys\":[]}")];} packet={
        .header={.version={sizeof(struct keyboard_startup),1,0},.layout_bytes=sizeof("{\"version\":1,\"keys\":[]}")-1},
        .json="{\"version\":1,\"keys\":[]}"};
    /* No fixture path, trigger or fault branch is compiled into the real bundle. */
    keyboard_entry((uintptr_t)&packet);
}
