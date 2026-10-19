/* Production USB core with modeled host transport; hardware tested separately. */
#include "../kmodules/shared/usb.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned allocations,fail_allocate,phase,endpoint_calls;static int fail_after=-1;
static uint64_t ticks,current_thread=1,sequence,last_wait_deadline;
static unsigned connected=1,generation=1,restarting;
static struct host_transfer_request inflight;
static struct host_transfer_result completion;
static struct usb_ref original;
static unsigned char config_keyboard[]={9,2,25,0,1,1,0,0x80,50,9,4,0,0,1,3,1,1,0,7,5,0x81,3,8,0,10};
static unsigned char config_storage[]={9,2,32,0,1,1,0,0x80,50,9,4,0,0,2,8,6,0x50,0,7,5,0x81,2,0,2,0,7,5,2,2,0,2,0};
static struct {struct kernel_surfaces hub;struct kernel_surface methods[26];struct kernel_hub_table tables[2];} hub;
static struct {struct hwenum_table table;struct hwenum_descriptor entries[1];} hardware;
static struct kernel_surface_result cpu(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result mem(MODULE_ARGS){MODULE_UNUSED();if(fail_allocate)return failure();void *p=calloc(b,c);if(p)++allocations;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result mem_free(MODULE_ARGS){MODULE_UNUSED();free((void *)b);--allocations;return module_result(1,1);}
static struct kernel_surface_result clock_call(MODULE_ARGS){MODULE_UNUSED();ticks+=1000;return module_result(ticks,1);}
static struct kernel_surface_result info_call(MODULE_ARGS){MODULE_UNUSED();*(struct xhci_info *)b=(struct xhci_info){.id=1,.ports=2,.state=XHCI_ONLINE};return module_result(1,1);}
static struct kernel_surface_result open_call(MODULE_ARGS){MODULE_UNUSED();info_call(1,b,0,0,0,0);return module_result(1,1);}
static struct kernel_surface_result success(MODULE_ARGS){MODULE_UNUSED();return module_result(1,1);}
static struct kernel_surface_result create_call(MODULE_ARGS){MODULE_UNUSED();return module_result(current_thread,1);}
static struct kernel_surface_result port_call(MODULE_ARGS){MODULE_UNUSED();*(struct xhci_port *)c=(struct xhci_port){.number=b,.generation=b==1?generation:1,.protocol_major=2,.status=(b==1?(connected?3:0):3)|((b==1?1:3)<<10)};return module_result(1,1);}
static struct kernel_surface_result device_call(MODULE_ARGS){MODULE_UNUSED();*(uint64_t *)c=((struct host_device_request *)b)->port;return module_result(1,1);}
static struct kernel_surface_result endpoint_call(MODULE_ARGS){MODULE_UNUSED();struct host_endpoint_request *r=(void *)b;assert(r->max_packet && r->max_packet<=512);++endpoint_calls;return module_result(1,1);}
static struct kernel_surface_result transfer_call(MODULE_ARGS){MODULE_UNUSED();inflight=*(struct host_transfer_request *)b;completion=(struct host_transfer_result){.id=++sequence,.state=HOST_COMPLETE,.bytes=inflight.bytes};return module_result(sequence,1);}
static struct kernel_surface_result result_call(MODULE_ARGS){MODULE_UNUSED();assert(b==completion.id);
    if(d && completion.state==HOST_COMPLETE && inflight.bytes) {
        unsigned char descriptor[18]={18,1,0,2,0,0,0,inflight.device==1?8:64,0x34,0x12,0x78,0x56,0,1,0,0,0,1};
        const unsigned char *source=NULL;size_t bytes=0;
        if(inflight.setup[1]==6 && inflight.setup[3]==1){source=descriptor;bytes=18;}
        else if(inflight.setup[1]==6 && inflight.setup[3]==2){source=inflight.device==1?config_keyboard:config_storage;bytes=inflight.device==1?sizeof(config_keyboard):sizeof(config_storage);}
        else {static const unsigned char report[]={1,2,3,4};source=report;bytes=sizeof(report);}
        if(bytes<completion.bytes)completion.bytes=bytes;
        memcpy(inflight.buffer,source,completion.bytes);
    }
    *(struct host_transfer_result *)c=completion;return module_result(1,1);
}
static struct kernel_surface_result wait_call(MODULE_ARGS){MODULE_UNUSED();last_wait_deadline=d;*(struct host_transfer_result *)c=completion;return module_result(1,1);}
static void inspect(void) {
    struct usb_info info;assert(!usb_surface_info((uintptr_t)&info,0,0,0,0,0).status && info.records==2 && !info.failed);
    struct device *keyboard=NULL,*storage=NULL;
    for(struct device *p=state.devices;p;p=p->next)if(p->record.port==1)keyboard=p;else storage=p;
    assert(keyboard && storage && storage->record.state==USB_CONFIGURED);
    struct usb_ref ref={keyboard->record.identity.id,keyboard->record.identity.generation};
    if(restarting) {
        assert(info.configured==2 && keyboard->record.identity.id!=original.id && storage->record.state==USB_CONFIGURED);
        state.hosts->stop=1;return;
    }
    if(phase==0) {
        assert(info.configured==2 && keyboard->record.speed==USB_FULL && storage->record.speed==USB_HIGH);
        original=ref;struct usb_interface interface;
        assert(!usb_surface_interface((uintptr_t)&ref,0,(uintptr_t)&interface,0,0,0).status && interface.class_code==3);
        assert(!usb_surface_claim((uintptr_t)&ref,0,42,0,0,0).status);
        assert(usb_surface_claim((uintptr_t)&ref,0,99,0,0,0).status);
        unsigned char output[8];memset(output,0xcc,sizeof(output));struct host_transfer_result result;
        struct usb_io io={.device=ref,.owner=99,.interface=0,.bytes=4,.buffer=output,.setup={0x81,6,0,0x22,0,0,4,0}};
        assert(usb_surface_control((uintptr_t)&io,(uintptr_t)&result,0,0,0,0).status && output[0]==0xcc);
        io.owner=42;assert(!usb_surface_control((uintptr_t)&io,(uintptr_t)&result,0,0,0,0).status && result.bytes==4 && output[0]==1 && output[4]==0xcc);
        io.setup[0]=0;io.setup[1]=9;io.bytes=0;io.setup[6]=0;assert(usb_surface_control((uintptr_t)&io,(uintptr_t)&result,0,0,0,0).status);
        assert(!usb_surface_release((uintptr_t)&ref,0,42,0,0,0).status);
        assert(!usb_surface_claim((uintptr_t)&ref,0,99,0,0,0).status && endpoint_calls==1);
        io=(struct usb_io){.device=ref,.owner=99,.interface=0,.endpoint=129,.bytes=4,.buffer=output};
        assert(!usb_surface_transfer((uintptr_t)&io,(uintptr_t)&result,0,0,0,0).status && last_wait_deadline==UINT64_MAX);
        io.deadline_ns=ticks+1000000000;assert(!usb_surface_transfer((uintptr_t)&io,(uintptr_t)&result,0,0,0,0).status && last_wait_deadline==io.deadline_ns);
        connected=0;++generation;
    } else if(phase==1) {assert(keyboard->record.state==USB_DETACHED && !keyboard->token);connected=1;++generation;}
    else {assert(keyboard->record.state==USB_CONFIGURED && ref.id!=original.id);struct usb_device out;assert(usb_surface_device((uintptr_t)&original,(uintptr_t)&out,0,0,0,0).status);state.hosts->stop=1;}
    ++phase;
}
static struct kernel_surface_result thread_call(MODULE_ARGS){MODULE_UNUSED();if(a==THREAD_CURRENT)return module_result(current_thread,1);
    if(a==THREAD_BLOCK_UNTIL)ticks=b;
    if(a==THREAD_BLOCK)inspect();
    return module_result(1,1);
}
static struct kernel_surface_result publish(MODULE_ARGS){MODULE_UNUSED();hub.tables[1]=*(struct kernel_hub_table *)a;hub.hub.table_count=2;return module_result(1,1);}
static void *parse_alloc(void *ctx,size_t bytes){(void)ctx;if(fail_allocate || fail_after==0)return NULL;if(fail_after>0)--fail_after;void *p=calloc(1,bytes);if(p)++allocations;return p;}
static void parse_free(void *ctx,void *p){(void)ctx;free(p);--allocations;}
int main(void) {
    struct usb_allocator a={.allocate=parse_alloc,.release=parse_free};struct usb_configuration cfg={0};
    assert(usb_parse_configuration(config_keyboard,sizeof(config_keyboard),USB_FULL,&a,&cfg));assert(cfg.endpoints[0].interval==6 && cfg.endpoints[0].max_esit==8);usb_free_configuration(&a,&cfg);assert(!allocations);
    for(size_t n=0;n<sizeof(config_keyboard);++n)assert(!usb_parse_configuration(config_keyboard,n,USB_FULL,&a,&cfg));
    unsigned char bad[sizeof(config_storage)];memcpy(bad,config_storage,sizeof(bad));bad[27]=0x81;assert(!usb_parse_configuration(bad,sizeof(bad),USB_HIGH,&a,&cfg));assert(!allocations);
    memcpy(bad,config_storage,sizeof(bad));bad[4]=2;assert(!usb_parse_configuration(bad,sizeof(bad),USB_HIGH,&a,&cfg));
    fail_allocate=1;assert(usb_parse_configuration(config_keyboard,sizeof(config_keyboard),USB_FULL,&a,&cfg)==-1);fail_allocate=0;
    for(fail_after=0;fail_after<3;) {
        int budget=fail_after;
        assert(usb_parse_configuration(config_keyboard,sizeof(config_keyboard),USB_FULL,&a,&cfg)==-1 && !allocations);
        fail_after=budget+1;
    }
    fail_after=-1;
    unsigned char ss[]={9,2,31,0,1,1,0,0x80,50,9,4,0,0,1,3,1,1,0,7,5,0x81,3,0,4,5,6,48,1,0,0,8};
    assert(usb_parse_configuration(ss,sizeof(ss),USB_SUPER,&a,&cfg)==1 && cfg.endpoints[0].max_packet==1024 && cfg.endpoints[0].max_esit==2048 && cfg.endpoints[0].max_burst==1 && cfg.endpoints[0].interval==4);
    usb_free_configuration(&a,&cfg);ss[26]=47;assert(!usb_parse_configuration(ss,sizeof(ss),USB_SUPER,&a,&cfg));
    assert(usb_ep0_packet(USB_SUPER,9)==512 && !usb_ep0_packet(USB_SUPER,64) && !usb_ep0_packet(USB_LOW,64));
    uint32_t psi=(480u<<16)|(2u<<4)|3;assert(usb_port_speed(2,3,&psi,1)==USB_HIGH && !usb_port_speed(2,4,&psi,1));
    const char *names[]={"cpu.control","mem.calloc","mem.free","threads.control","threads.create","clock.now_ns","irq.context","xhci.open","xhci.close","xhci.info","xhci.port","xhci.reset_port","xhci.device","xhci.endpoint","xhci.transfer","xhci.result","xhci.wait","xhci.cancel","xhci.remove","xhci.watch","hub.publish_table","usb.worker","xhci.hub","xhci.disconnect"};
    kernel_surface_call calls[]={cpu,mem,mem_free,thread_call,create_call,clock_call,cpu,open_call,success,info_call,port_call,success,device_call,endpoint_call,transfer_call,result_call,wait_call,success,success,success,publish,usb_surface_worker,success,success};
    hub.hub=(struct kernel_surfaces){.magic=KERNEL_SURFACES_MAGIC,.major=KERNEL_HUB_ABI,.count=24,.header_bytes=offsetof(__typeof__(hub),methods),.tables_offset=offsetof(__typeof__(hub),tables),.table_count=1};
    for(unsigned i=0;i<24;++i){strcpy(hub.methods[i].name,names[i]);hub.methods[i].major=1;hub.methods[i].call=calls[i];}
    hardware.table.count=1;hardware.entries[0].kind=HWENUM_PCI;hardware.entries[0].data.pci.class_code=12;hardware.entries[0].data.pci.subclass=3;hardware.entries[0].data.pci.programming_interface=0x30;
    strcpy(hub.tables[0].name,HWENUM_TABLE_NAME);hub.tables[0].address=(uintptr_t)&hardware;
    assert(!usb_surface_init((uintptr_t)&hub.hub,1,0,0,0,0).status && !state.table.count);
    assert(!usb_surface_watch(1,0,0,0,0,0).status);
    unsigned watched=allocations;assert(!usb_surface_watch(1,0,0,0,0,0).status && allocations==watched);
    assert(!usb_surface_start(0,0,0,0,0,0).status);
    assert(!usb_surface_worker((uintptr_t)state.hosts,0,0,0,0,0).status && phase==3 && !state.hosts->running && !state.hosts->id);
    assert(!usb_surface_stop(0,0,0,0,0,0).status && !state.hosts->thread);
    restarting=1;generation=1;
    assert(!usb_surface_start(0,0,0,0,0,0).status);
    assert(!usb_surface_worker((uintptr_t)state.hosts,0,0,0,0,0).status && state.table.count==2);
    assert(!usb_surface_stop(0,0,0,0,0,0).status);
    assert(state.table.changes && !usb_surface_watch(0,0,0,0,0,0).status);
    /* A malformed downstream device must not take its healthy hub offline. */
    struct device *parent=state.devices;parent->record.state=USB_CONFIGURED;parent->record.hub=1;
    parent->hub_owner=77;parent->hub_ports=2;parent->hub_interface=0;
    parent->configs[0].interfaces[0].info.class_code=9;parent->configs[0].interfaces[0].info.owner=77;
    state.hosts->stop=0;state.hosts->id=1;
    struct usb_downstream child={.parent={parent->record.identity.id,parent->record.identity.generation},.owner=99,.generation=1,.port=2,.speed=USB_LOW,.connected=1};
    assert(usb_surface_downstream((uintptr_t)&child,0,0,0,0,0).status);
    child.owner=77;assert(!usb_surface_downstream((uintptr_t)&child,0,0,0,0,0).status);
    struct device *failed_child=state.devices;assert(failed_child->parent==parent && failed_child->record.state==USB_FAILED && failed_child->record.error==USB_DESCRIPTOR && !failed_child->token && parent->record.state==USB_CONFIGURED);
    child.connected=0;++child.generation;assert(!usb_surface_downstream((uintptr_t)&child,0,0,0,0,0).status && failed_child->record.state==USB_DETACHED);
    while(state.devices){struct device *p=state.devices;state.devices=p->next;free_configs(p);release(NULL,p->buffer);release(NULL,p);}
    release(NULL,state.hosts);assert(!allocations);
    puts("PASS USB: descriptors, malformed/OOM rollback, speed/interval normalization, root enumeration, scoped claims/control, detach/rebind, stale refs and shutdown");return 0;
}
