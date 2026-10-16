#include "usb-hub.h"
#include "threads.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
struct hub_port {uint64_t generation;unsigned connected;};
struct hub {struct usb_hub_record record;struct hub *next;const struct usb_device *connector;struct hub_port *ports;uint64_t thread,owner;unsigned running,stop,speed,protocol,depth,qemu_legacy;};
static struct {
    const struct kernel_surfaces *hub;uintptr_t heap;const struct usb_table *usb;struct usb_hub_table table;struct hub *head;
    uint64_t manager;unsigned running,stop;
    kernel_surface_call cpu,alloc,free,create,thread,clock,watch,device,interface,endpoint,claim,release,control,transfer,configure,downstream,cancel;
} state;
static uint64_t lock(void){return CALL(state.cpu,CPU_ENTER,0,0).value;}
static void unlock(uint64_t flags){CALL(state.cpu,CPU_LEAVE,flags,0);}
static int bsp(void){return state.hub && !CALL(state.cpu,CPU_CURRENT,0,0).value;}
static void *allocate(size_t bytes){struct kernel_surface_result r=CALL(state.alloc,state.heap,1,bytes);return r.status?NULL:(void *)r.value;}
static void release(void *p){if(p)CALL(state.free,state.heap,p,0);}
static uint64_t now(void){struct kernel_surface_result r=CALL(state.clock,0,0,0);return r.status?UINT64_MAX:r.value;}
static int sleep_ms(unsigned ms){uint64_t deadline=now()+(uint64_t)ms*1000000;do{if(CALL(state.thread,THREAD_BLOCK_UNTIL,deadline,0).status)return 0;}while(now()<deadline);return 1;}
static int request(struct hub *hub,unsigned type,unsigned code,unsigned value,unsigned index,void *buffer,unsigned bytes) {
    struct usb_io io={.device=hub->record.device,.owner=hub->owner,.interface=hub->record.interface,.bytes=bytes,.buffer=buffer,.setup={type,code,value,value>>8,index,index>>8,bytes,bytes>>8}};
    struct host_transfer_result result;return !CALL(state.control,&io,&result,0).status && result.bytes==bytes;
}
static int feature(struct hub *hub,unsigned port,unsigned feature,int set){return request(hub,0x23,set?3:1,feature,port,NULL,0);}
static int status(struct hub *hub,unsigned port,uint32_t *out) {
    unsigned char bytes[4];if(!request(hub,0xa3,0,0,port,bytes,4))return 0;
    *out=bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);return 1;
}
static int downstream(struct hub *hub,unsigned port,unsigned speed,int connected) {
    struct usb_downstream r={.parent=hub->record.device,.owner=hub->owner,.port=port,.generation=hub->ports[port-1].generation,.speed=speed,.connected=connected};
    return !CALL(state.downstream,&r,0,0).status;
}
static int scan_port(struct hub *hub,unsigned port,int initial) {
    uint32_t value;if(!status(hub,port,&value))return 0;
    struct hub_port *p=&hub->ports[port-1];unsigned connected=!!(value&1),changes=value>>16;
    /* Acknowledge the observed connection edge before slow enumeration. A
     * later edge must remain pending, including unplug/replug with final CCS=1. */
    if((changes&1) && !feature(hub,port,16,0))return 0;
    if(value&8){(void)feature(hub,port,8,0);return 0;}
    if(initial || connected!=p->connected || (changes&1)) {
        if(p->generation==UINT64_MAX)return 0;
        ++p->generation;
        if(p->connected && !downstream(hub,port,0,0))return 0;
        p->connected=0;
        if(connected) {
            if(!sleep_ms(100) || !status(hub,port,&value))return 0;
            if(!(value&1))goto acknowledge;
            unsigned reset=hub->record.superspeed && (((value>>5)&15)==6 || ((value>>5)&15)==10)?28:4;
            if(!feature(hub,port,reset,1))return 0;
            uint64_t deadline=now()+1000000000;
            do {if(!sleep_ms(10) || !status(hub,port,&value) || now()>deadline)return 0;if(!(value&1))goto acknowledge;}while((value&16) || !(value&2));
            if(!sleep_ms(10))return 0;
            unsigned speed=hub->record.superspeed?USB_SUPER:(value&(1u<<9))?USB_LOW:(value&(1u<<10))?USB_HIGH:USB_FULL;
            if(!downstream(hub,port,speed,1))return 0;
            p->connected=1;
        }
    }
acknowledge:
    if(!status(hub,port,&value))return 0;
    changes=value>>16;
    for(unsigned bit=1;bit<8;++bit)if(changes&(1u<<bit)) {
        unsigned selector=16+bit;
        if(hub->record.superspeed){if(bit==1 || bit==2)continue;if(bit==5)selector=29;else if(bit==6)selector=25;else if(bit==7)selector=26;}
        else if(bit>4)continue;
        if(!feature(hub,port,selector,0))return 0;
    }
    return 1;
}
static int descriptor_valid(const struct hub *hub,const unsigned char *descriptor) {
    unsigned ports=descriptor[2],length=descriptor[0],type=hub->record.superspeed?0x2a:0x29;
    unsigned minimum=hub->record.superspeed?12u:7u+2*((ports+8)/8);
    /* QEMU 6.2's eight-port USB 1.1 hub advertises ten bytes instead of
     * eleven. Only its obsolete power-control-mask tail is incomplete. */
    return ports && descriptor[1]==type &&
           (length>=minimum || (hub->qemu_legacy && !hub->record.superspeed && ports==8 && length==10));
}
/* Reject a changing descriptor before allocation or host/port configuration. */
static int read_descriptor(struct hub *hub,unsigned char *descriptor) {
    unsigned char prefix[12];unsigned bytes=hub->record.superspeed?12:7;
    unsigned type=hub->record.superspeed?0x2a:0x29;
    if(!request(hub,0xa0,6,type<<8,0,prefix,bytes) || !descriptor_valid(hub,prefix))return 0;
    unsigned length=prefix[0];
    if(length>bytes) {
        if(!request(hub,0xa0,6,type<<8,0,descriptor,length) || !descriptor_valid(hub,descriptor))return 0;
        for(unsigned i=0;i<bytes;++i)if(descriptor[i]!=prefix[i])return 0;
    } else for(unsigned i=0;i<bytes;++i)descriptor[i]=prefix[i];
    return 1;
}
struct kernel_surface_result usb_hub_surface_worker(MODULE_ARGS) {
    MODULE_UNUSED();struct hub *hub=(void *)a;int known=0;for(struct hub *p=state.head;p;p=p->next)if(p==hub)known=1;
    if(!bsp() || !known)return module_result(0,0);
    while(!hub->thread)if(CALL(state.thread,THREAD_BLOCK,0,0).status)return module_result(0,0);
    if(CALL(state.thread,THREAD_CURRENT,0,0).value!=hub->thread)return module_result(0,0);
    unsigned error=0;int claimed=0;
#define CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    CHECK(!CALL(state.claim,&hub->record.device,hub->record.interface,hub->owner).status);claimed=1;
    unsigned char descriptor[255];CHECK(read_descriptor(hub,descriptor));
    unsigned ports=descriptor[2];
    struct usb_endpoint interrupt_endpoint;
    CHECK(!state.endpoint((uintptr_t)&hub->record.device,hub->record.interface,hub->record.endpoint,(uintptr_t)&interrupt_endpoint,0,0).status && interrupt_endpoint.max_esit>=(ports+8)/8);
    hub->record.ports=ports;hub->ports=allocate(ports*sizeof(*hub->ports));CHECK(hub->ports);
    unsigned characteristics=descriptor[3]|((unsigned)descriptor[4]<<8);
    struct usb_hub_config config={.device=hub->record.device,.owner=hub->owner,.interface=hub->record.interface,.ports=ports,.multi_tt=hub->speed==USB_HIGH && hub->protocol==2,.think_time=hub->speed==USB_HIGH?(characteristics>>5)&3:0};
    CHECK(!CALL(state.configure,&config,0,0).status);
    if(hub->record.superspeed)CHECK(request(hub,0x20,12,hub->depth,0,NULL,0));
    for(unsigned port=1;port<=ports;++port)CHECK(feature(hub,port,8,1));
    CHECK(sleep_ms(descriptor[5]*2+2));
    for(unsigned port=1;port<=ports && !hub->stop;++port)CHECK(scan_port(hub,port,1));
    hub->record.state=USB_HUB_RUNNING;
    while(!hub->stop) {
        unsigned char bitmap[32]={0};unsigned bytes=(ports+8)/8;
        struct usb_io io={.device=hub->record.device,.owner=hub->owner,.interface=hub->record.interface,.endpoint=hub->record.endpoint,.bytes=bytes,.buffer=bitmap,.deadline_ns=UINT64_MAX};
        struct host_transfer_result result;struct kernel_surface_result transferred=CALL(state.transfer,&io,&result,0);
        if(hub->stop)break;
        if(transferred.status){struct usb_device dev;if(CALL(state.device,&hub->record.device,&dev,0).status || dev.state!=USB_CONFIGURED)break;CHECK(0);}
        CHECK(result.bytes && result.bytes<=bytes);++hub->record.changes;
        if(bitmap[0]&1) {unsigned char value[4];CHECK(request(hub,0xa0,0,0,0,value,4));if(value[0]&2){for(unsigned port=1;port<=ports;++port)(void)feature(hub,port,8,0);CHECK(0);}if(value[2]&1)CHECK(request(hub,0x20,1,0,0,NULL,0));if(value[2]&2)CHECK(request(hub,0x20,1,1,0,NULL,0));}
        for(unsigned port=1;port<=ports && !hub->stop;++port)if(bitmap[port/8]&(1u<<(port%8)))CHECK(scan_port(hub,port,0));
    }
done:
    if(claimed) {
        for(unsigned port=1;port<=hub->record.ports && hub->ports;++port)if(hub->ports[port-1].connected){++hub->ports[port-1].generation;(void)downstream(hub,port,0,0);}
        CALL(state.release,&hub->record.device,hub->record.interface,hub->owner);
    }
    {uint64_t flags=lock();hub->record.error=hub->stop?0:error;hub->record.state=hub->record.error?USB_HUB_FAILED:USB_HUB_DETACHED;hub->running=0;CALL(state.thread,THREAD_WAKE,state.manager,0);unlock(flags);}
    return module_result(error,!error || hub->stop);
#undef CHECK
}
static int launch(const struct usb_device *connector,struct usb_device snapshot) {
    struct hub *hub=state.head;while(hub && hub->connector!=connector)hub=hub->next;
    /* Failed attachments stay failed until rebind; an explicit class stop is
     * different and must permit reclaiming the unchanged root attachment. */
    if(hub && hub->record.device.id==snapshot.identity.id && !hub->stop)return 1;
    if(hub && hub->running)return 1;
    struct usb_ref ref={snapshot.identity.id,snapshot.identity.generation};struct usb_interface interface={0};unsigned ordinal=0;
    while(!CALL(state.interface,&ref,ordinal++,&interface).status)if(!interface.alternate && interface.class_code==9)break;
    if(interface.class_code!=9 || interface.alternate || interface.owner)return 1;
    unsigned ep=0;struct usb_endpoint endpoint;
    for(unsigned address=0x81;address<=0x8f;++address)if(!state.endpoint((uintptr_t)&ref,interface.number,address,(uintptr_t)&endpoint,0,0).status && endpoint.host_type==HOST_INTERRUPT && endpoint.supported){ep=address;break;}
    if(!ep)return 1;
    if(!hub){hub=allocate(sizeof(*hub));if(!hub)return 0;hub->connector=connector;uint64_t flags=lock();hub->next=state.head;state.head=hub;hub->record.next=state.table.first;state.table.first=&hub->record;++state.table.count;unlock(flags);}
    if(hub->thread && CALL(state.thread,THREAD_REAP,hub->thread,0).status)return 2;
    release(hub->ports);hub->ports=NULL;hub->thread=0;hub->stop=0;hub->speed=snapshot.speed;hub->depth=snapshot.depth;hub->protocol=interface.protocol;hub->qemu_legacy=snapshot.vendor==0x0409 && snapshot.product==0x55aa && snapshot.usb_version==0x0110;hub->owner=snapshot.identity.id;
    hub->record.device=ref;hub->record.ports=0;hub->record.state=USB_HUB_STARTING;hub->record.error=0;hub->record.interface=interface.number;hub->record.endpoint=ep;hub->record.superspeed=snapshot.speed==USB_SUPER;
    kernel_surface_call worker=kernel_surface_find(state.hub,"usb_hub.worker");struct thread_request r={.abi=1,.cpu=0,.stack_bytes=32768,.entry=(uintptr_t)worker,.major=1};r.payload.arguments[0]=(uintptr_t)hub;
    hub->running=1;struct kernel_surface_result created=CALL(state.create,&r,0,0);
    if(created.status){hub->running=0;hub->record.state=USB_HUB_FAILED;return 0;}
    hub->thread=created.value;CALL(state.thread,THREAD_WAKE,hub->thread,0);return 1;
}
struct kernel_surface_result usb_hub_surface_manager(MODULE_ARGS) {
    MODULE_UNUSED();if(!bsp())return module_result(0,0);
    while(!state.manager)if(CALL(state.thread,THREAD_BLOCK,0,0).status)return module_result(0,0);
    if(CALL(state.thread,THREAD_CURRENT,0,0).value!=state.manager || CALL(state.watch,1,0,0).status)return module_result(0,0);
    while(!state.stop) {
        int retry=0;
        const struct usb_device *connector=state.usb->first;
        while(connector && !state.stop) {
            uint64_t flags=lock();struct usb_device snapshot=*connector;const struct usb_device *next=connector->next;unlock(flags);
            if(snapshot.state==USB_CONFIGURED && snapshot.hub){int launched=launch(connector,snapshot);if(!launched){state.stop=1;break;}retry|=launched==2;}
            connector=next;
        }
        if(!state.stop && CALL(state.thread,retry?THREAD_BLOCK_UNTIL:THREAD_BLOCK,retry?now()+1000000:0,0).status){state.stop=1;break;}
    }
    for(;;) {
        unsigned running=0;
        for(struct hub *hub=state.head;hub;hub=hub->next)if(hub->running) {
            hub->stop=1;++running;struct usb_io cancel={.device=hub->record.device,.owner=hub->owner,.interface=hub->record.interface,.endpoint=hub->record.endpoint};CALL(state.cancel,&cancel,0,0);CALL(state.thread,THREAD_WAKE,hub->thread,0);
        }
        if(!running)break;
        CALL(state.thread,THREAD_BLOCK_UNTIL,now()+1000000000,0);
    }
    /* running=0 is published just before a worker returns. It may still be on
     * its stack when preempted; do not report completed stop until reaped. */
    for(struct hub *hub=state.head;hub;hub=hub->next)while(hub->thread) {
        if(!CALL(state.thread,THREAD_REAP,hub->thread,0).status)hub->thread=0;
        else CALL(state.thread,THREAD_BLOCK_UNTIL,now()+1000000,0);
    }
    CALL(state.watch,0,0,0);state.running=0;return module_result(1,1);
}
struct kernel_surface_result usb_hub_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(void *)a;
    if(state.hub)return module_result(1,state.hub==hub && state.heap==b);
    if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC || !b)return module_result(0,0);
#define GET(field,name) do{state.field=kernel_surface_find_version(hub,name,1,0);if(!state.field)return module_result(0,0);}while(0)
    GET(cpu,"cpu.control");GET(alloc,"mem.calloc");GET(free,"mem.free");GET(create,"threads.create");GET(thread,"threads.control");GET(clock,"clock.now_ns");GET(watch,"usb.watch");GET(device,"usb.device");GET(interface,"usb.interface");GET(endpoint,"usb.endpoint");GET(claim,"usb.claim");GET(release,"usb.release");GET(control,"usb.control");GET(transfer,"usb.transfer");GET(configure,"usb.hub_configure");GET(downstream,"usb.downstream");GET(cancel,"usb.cancel");
#undef GET
    if(CALL(state.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);
    const struct kernel_hub_table *usb=kernel_hub_find_table(hub,USB_TABLE_NAME);kernel_surface_call publish=kernel_surface_find(hub,"hub.publish_table");if(!usb || !publish)return module_result(0,0);
    state.usb=(void *)usb->address;state.heap=b;state.table=(struct usb_hub_table){.magic=USB_HUB_TABLE_MAGIC,.major=1,.record_bytes=sizeof(struct usb_hub_record)};
    struct kernel_hub_table table={.name=USB_HUB_TABLE_NAME,.address=(uintptr_t)&state.table,.bytes=sizeof(state.table),.count=1,.entry_bytes=sizeof(state.table)};
    if(CALL(publish,&table,0,0).status)return module_result(0,0);
    state.hub=hub;return module_result(1,1);
}
struct kernel_surface_result usb_hub_surface_start(MODULE_ARGS) {
    MODULE_UNUSED();if(!bsp() || CALL(state.cpu,CPU_DEPTH,0,0).value)return module_result(0,0);
    if(state.running)return module_result(1,!state.stop);
    if(state.manager && CALL(state.thread,THREAD_REAP,state.manager,0).status)return module_result(0,0);
    state.manager=0;state.stop=0;state.running=1;
    struct thread_request r={.abi=1,.cpu=0,.stack_bytes=32768,.entry=(uintptr_t)kernel_surface_find(state.hub,"usb_hub.manager"),.major=1};
    struct kernel_surface_result created=CALL(state.create,&r,0,0);if(created.status){state.running=0;return created;}
    state.manager=created.value;CALL(state.thread,THREAD_WAKE,state.manager,0);return module_result(1,1);
}
struct kernel_surface_result usb_hub_surface_stop(MODULE_ARGS) {
    MODULE_UNUSED();if(!bsp())return module_result(0,0);state.stop=1;if(state.running)CALL(state.thread,THREAD_WAKE,state.manager,0);
    else if(state.manager && !CALL(state.thread,THREAD_REAP,state.manager,0).status)state.manager=0;
    return module_result(1,1);
}
struct kernel_surface_result usb_hub_surface_info(MODULE_ARGS) {
    MODULE_UNUSED();if(!bsp() || !a)return module_result(0,0);struct usb_hub_info info={.records=state.table.count,.manager=state.running};uint64_t flags=lock();
    for(struct hub *hub=state.head;hub;hub=hub->next){info.running+=hub->running;info.failed+=hub->record.state==USB_HUB_FAILED;}
    *(struct usb_hub_info *)a=info;unlock(flags);return module_result(1,1);
}
