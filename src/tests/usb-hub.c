/* Production hub port policy with deterministic class-control responses. */
#include "../kmodules/shared/usb-hub.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint64_t ticks;
static uint32_t port_status;
static unsigned attached,detached,powered_off,reset_selector,inject_edge;
static unsigned descriptor_reads,descriptor_short;static int descriptor_corrupt=-1;
static unsigned char descriptor_source[255];
static struct kernel_surface_result clock_call(MODULE_ARGS){MODULE_UNUSED();return module_result(ticks,1);}
static struct kernel_surface_result thread_call(MODULE_ARGS){MODULE_UNUSED();if(a==THREAD_BLOCK_UNTIL)ticks=b;return module_result(1,1);}
static struct kernel_surface_result downstream_call(MODULE_ARGS){
    MODULE_UNUSED();struct usb_downstream *r=(void *)a;assert(r->port==1 && r->generation);
    if(r->connected){++attached;assert(r->speed==USB_FULL || r->speed==USB_SUPER);if(inject_edge)port_status|=1u<<16;}
    else ++detached;
    return module_result(1,1);
}
static struct kernel_surface_result control_call(MODULE_ARGS){
    MODULE_UNUSED();struct usb_io *io=(void *)a;unsigned feature=io->setup[2];
    if(io->setup[1]==6) {
        ++descriptor_reads;assert(io->setup[4]==0 && io->bytes<=sizeof(descriptor_source));
        memcpy(io->buffer,descriptor_source,io->bytes);
        if(descriptor_reads==2 && descriptor_corrupt>=0)((unsigned char *)io->buffer)[descriptor_corrupt]^=1;
        unsigned actual=io->bytes-(descriptor_reads==2 && descriptor_short);
        *(struct host_transfer_result *)b=(struct host_transfer_result){.state=HOST_COMPLETE,.bytes=actual};return module_result(actual,1);
    }
    assert(io->setup[4]==1);
    if(io->setup[1]==0){assert(io->bytes==4);memcpy(io->buffer,&port_status,4);}
    else if(io->setup[1]==3){assert(feature==4 || feature==28);reset_selector=feature;port_status|=2|(1u<<20);}
    else {assert(io->setup[1]==1);if(feature==8)++powered_off;else {unsigned bit=feature-16;assert(bit<8);port_status&=~(1u<<(16+bit));}}
    *(struct host_transfer_result *)b=(struct host_transfer_result){.state=HOST_COMPLETE,.bytes=io->bytes};return module_result(io->bytes,1);
}
int main(void){
    struct hub hub={0};unsigned char descriptor[12]={11,0x29,8};
    assert(descriptor_valid(&hub,descriptor));descriptor[0]=10;assert(!descriptor_valid(&hub,descriptor));
    hub.qemu_legacy=1;assert(descriptor_valid(&hub,descriptor));descriptor[2]=7;descriptor[0]=8;assert(!descriptor_valid(&hub,descriptor));
    descriptor[2]=0;assert(!descriptor_valid(&hub,descriptor));
    hub.record.superspeed=1;descriptor[0]=12;descriptor[1]=0x2a;descriptor[2]=4;assert(descriptor_valid(&hub,descriptor));
    descriptor[0]=11;assert(!descriptor_valid(&hub,descriptor));hub.record.superspeed=0;
    state.clock=clock_call;state.thread=thread_call;state.control=control_call;state.downstream=downstream_call;
    unsigned char full[255];memcpy(descriptor_source,(unsigned char[]){11,0x29,8,0x0a,0,1,0},7);
    descriptor_reads=0;assert(read_descriptor(&hub,full) && descriptor_reads==2 && full[2]==8);
    /* Length, type, port count and all operational prefix fields must agree. */
    for(descriptor_corrupt=0;descriptor_corrupt<7;++descriptor_corrupt){descriptor_reads=0;assert(!read_descriptor(&hub,full) && descriptor_reads==2);}
    descriptor_corrupt=-1;descriptor_short=1;descriptor_reads=0;assert(!read_descriptor(&hub,full));descriptor_short=0;
    descriptor_source[2]=0;descriptor_reads=0;assert(!read_descriptor(&hub,full) && descriptor_reads==1);descriptor_source[2]=8;
    descriptor_source[0]=10;descriptor_reads=0;assert(read_descriptor(&hub,full));
    hub.qemu_legacy=0;descriptor_reads=0;assert(!read_descriptor(&hub,full) && descriptor_reads==1);
    hub.record.superspeed=1;descriptor_source[0]=12;descriptor_source[1]=0x2a;descriptor_source[2]=4;
    descriptor_reads=0;assert(read_descriptor(&hub,full) && descriptor_reads==1);hub.record.superspeed=0;
    struct hub_port port={0};hub.ports=&port;port_status=1|(1u<<16);inject_edge=1;
    assert(scan_port(&hub,1,1) && attached==1 && port.connected && reset_selector==4);
    /* An edge arriving during enumeration remains pending for the next read. */
    assert(port_status&(1u<<16));inject_edge=0;
    assert(scan_port(&hub,1,0) && detached==1 && attached==2 && !(port_status>>16));
    port_status=1u<<16;assert(scan_port(&hub,1,0) && detached==2 && !port.connected);
    hub.record.superspeed=1;port_status=1|(6u<<5)|(1u<<16);
    assert(scan_port(&hub,1,0) && attached==3 && reset_selector==28);
    port_status=1|8;assert(!scan_port(&hub,1,0) && powered_off==1);
    puts("PASS USB hubs: complete descriptor consistency/short reads/quirk, debounce/reset, late connection edge, detach, SuperSpeed warm reset and overcurrent");
}
