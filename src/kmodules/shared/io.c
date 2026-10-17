#include "io.h"
#include "process.h"
#include "threads.h"
#include "usb.h"
#include "../../kernel/cpu.h"
struct subscription {struct subscription *next;uint64_t pid,id,thread,skip_id;uint32_t stop,waiting,ack,claimed,negotiated,active,ready;struct usb_ref ref;uint32_t interface,endpoint;struct usb_io transfer;unsigned char report[8];};
static struct {const struct kernel_surfaces *hub;uintptr_t heap;uint64_t serial;struct subscription *first;
    kernel_surface_call alloc,free,cpu,thread,create,clock,device,interface,endpoint,claim,release,control,transfer,cancel,watch,channel,post,unsubscribe,process_info;} state;
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static uint64_t lock(void){return CALL(state.cpu,CPU_ENTER,0,0).value;}
static void unlock(uint64_t flags){CALL(state.cpu,CPU_LEAVE,flags,0);}
static void copy(void *dst,const void *src,size_t n){unsigned char *d=dst;const unsigned char *s=src;for(size_t i=0;i<n;++i)d[i]=s[i];}
static struct subscription *find(uint64_t pid,uint64_t id){for(struct subscription *s=state.first;s;s=s->next)if(s->pid==pid && s->id==id)return s;return NULL;}
static int stopped(struct subscription *s){return __atomic_load_n(&s->stop,__ATOMIC_ACQUIRE)!=0;}
static int event(struct subscription *s,uint32_t kind,uint32_t status){
    struct cb_io_keyboard_event e={.subscription=s->id,.device_id=s->ref.id,.timestamp_ns=CALL(state.clock,0,0,0).value,.kind=kind,.status=status};if(kind==CB_IO_REPORT)copy(e.report,s->report,8);
    uint64_t flags=lock();s->waiting=1;s->ack=0;unlock(flags);
    const struct cb_interface_id interface=CB_IO_INTERFACE_ID;
    if(state.post(s->pid,s->id,(uintptr_t)&e,sizeof(e),(uintptr_t)&interface,CB_IO_KEYBOARD_EVENT).status)return 0;
    while(!stopped(s) && !__atomic_load_n(&s->ack,__ATOMIC_ACQUIRE))if(CALL(state.thread,THREAD_BLOCK,0,0).status)return 0;
    flags=lock();s->waiting=0;unlock(flags);return !stopped(s);
}
static int discover(struct subscription *s){
    const struct kernel_hub_table *entry=kernel_hub_find_table(state.hub,USB_TABLE_NAME);if(!entry)return 0;const struct usb_table *table=(const void *)entry->address;uint64_t seen=0;
    for(;;){struct usb_device snapshot={0};uint64_t flags=lock();for(struct usb_device *d=table->first;d;d=d->next)if(d->identity.id>seen && (!snapshot.identity.id || d->identity.id<snapshot.identity.id))snapshot=*d;unlock(flags);if(!snapshot.identity.id)return 0;seen=snapshot.identity.id;if(snapshot.state!=USB_CONFIGURED || seen==s->skip_id)continue;
        struct usb_ref ref={snapshot.identity.id,snapshot.identity.generation};
        for(unsigned i=0;i<snapshot.interfaces;++i){struct usb_interface interface;if(CALL(state.interface,&ref,i,&interface).status || interface.alternate || interface.owner || interface.class_code!=3 || interface.subclass!=1 || interface.protocol!=1)continue;
            for(unsigned ep=129;ep<144;++ep){struct usb_endpoint endpoint;if(state.endpoint((uintptr_t)&ref,interface.number,ep,(uintptr_t)&endpoint,0,0).status || endpoint.transfer_type!=3 || !endpoint.supported || endpoint.max_packet<8)continue;
                if(CALL(state.claim,&ref,interface.number,s).status)break;
                flags=lock();s->ref=ref;s->interface=interface.number;s->endpoint=ep;s->claimed=1;s->negotiated=0;unlock(flags);return 1;}
        }
    }
}
static int release_claim(struct subscription *s){
    if(!s->claimed)return 1;
    struct usb_device d={0};
    if(!CALL(state.device,&s->ref,&d,0).status && d.state==USB_CONFIGURED){if(CALL(state.release,&s->ref,s->interface,s).status)return 0;}
    else if(d.state==USB_FAILED && d.error==USB_QUIESCENCE)return 0;
    s->claimed=0;return 1;
}
struct kernel_surface_result io_surface_worker(MODULE_ARGS){
    MODULE_UNUSED();struct subscription *s=(void *)a;if(!s)return module_result(0,0);if(CALL(state.watch,1,0,0).status){__atomic_store_n(&s->ready,2,__ATOMIC_RELEASE);return module_result(0,0);}
    __atomic_store_n(&s->ready,1,__ATOMIC_RELEASE);
    int ok=1;
    while(!stopped(s)){
        if(!discover(s)){if(CALL(state.thread,THREAD_BLOCK,0,0).status){ok=0;break;}continue;}
        if(!event(s,CB_IO_DEVICE_ADDED,CB_OK))break;
        if(!s->negotiated){(void)event(s,CB_IO_ERROR,CB_UNAVAILABLE);s->skip_id=s->ref.id;if(!release_claim(s)){ok=0;break;}continue;}
        while(!stopped(s)){
            uint64_t flags=lock();s->transfer=(struct usb_io){.device=s->ref,.owner=(uintptr_t)s,.interface=s->interface,.endpoint=s->endpoint,.bytes=8,.buffer=s->report};s->active=1;unlock(flags);
            struct host_transfer_result result={0};struct kernel_surface_result read=CALL(state.transfer,&s->transfer,&result,0);
            flags=lock();s->active=0;unlock(flags);if(stopped(s))break;
            if(!read.status && result.state==HOST_COMPLETE && result.bytes==8){if(!event(s,CB_IO_REPORT,CB_OK)){ok=0;break;}continue;}
            struct usb_device device={0};int alive=!CALL(state.device,&s->ref,&device,0).status && device.state==USB_CONFIGURED;
            /* Existing USB transactions retain their one-second health deadline.
             * Empty interrupt endpoints are rearmed after safe cancellation;
             * applications wait on notifications rather than polling the bus. */
            if(alive && result.state==HOST_CANCELLED)continue;
            if(!event(s,alive?CB_IO_ERROR:CB_IO_DEVICE_REMOVED,alive?CB_UNAVAILABLE:CB_OK)){ok=0;break;}
            if(alive)s->skip_id=s->ref.id;
            break;
        }
        if(!release_claim(s)){ok=0;break;}
    }
    (void)CALL(state.watch,0,0,0);if(!release_claim(s))ok=0;
    uint64_t flags=lock();for(struct subscription *other=state.first;other;other=other->next)if(other!=s && other->thread)CALL(state.thread,THREAD_WAKE,other->thread,0);unlock(flags);
    return module_result(ok,ok);
}
struct kernel_surface_result io_surface_init(MODULE_ARGS){
    MODULE_UNUSED();if(state.hub)return module_result(1,state.hub==(const void *)a);const struct kernel_surfaces *hub=(const void *)a;if(!hub || !b)return module_result(0,0);
#define BIND(field,name) do{state.field=kernel_surface_find(hub,name);if(!state.field)return module_result(0,0);}while(0)
    BIND(alloc,"mem.calloc");BIND(free,"mem.free");BIND(cpu,"cpu.control");BIND(thread,"threads.control");BIND(create,"threads.create");BIND(clock,"clock.now_ns");BIND(device,"usb.device");BIND(interface,"usb.interface");BIND(endpoint,"usb.endpoint");BIND(claim,"usb.claim");BIND(release,"usb.release");BIND(control,"usb.control");BIND(transfer,"usb.transfer");BIND(cancel,"usb.cancel");BIND(watch,"usb.watch");BIND(channel,"handles.channel");BIND(post,"handles.post");BIND(unsubscribe,"handles.unsubscribe");BIND(process_info,"process.info");
#undef BIND
    state.hub=hub;state.heap=b;return module_result(1,1);
}
static cb_status_t close_subscription(struct subscription *s,int wait){
    uint64_t flags=lock();__atomic_store_n(&s->stop,1,__ATOMIC_RELEASE);uint64_t tid=s->thread;int active=s->active;unlock(flags);
    if(active)(void)CALL(state.cancel,&s->transfer,0,0);
    if(tid)CALL(state.thread,THREAD_WAKE,tid,0);
    for(;;){struct thread_info t;if(!tid)break;if(CALL(state.thread,THREAD_INFO,tid,&t).status)return CB_RESOURCE_FAILURE;if(t.state==THREAD_EXITED)break;if(!wait)return CB_BUSY;
        uint64_t time=CALL(state.clock,0,0,0).value;if(time>UINT64_MAX-1000000 || CALL(state.thread,THREAD_BLOCK_UNTIL,time+1000000,0).status)return CB_RESOURCE_FAILURE;}
    if(s->claimed)return CB_RESOURCE_FAILURE;
    if(tid && CALL(state.thread,THREAD_REAP,tid,0).status)return CB_BUSY;
    (void)CALL(state.unsubscribe,s->pid,s->id,0);flags=lock();struct subscription **at=&state.first;while(*at!=s)at=&(*at)->next;*at=s->next;unlock(flags);CALL(state.free,state.heap,s,0);return CB_OK;
}
struct kernel_surface_result io_surface_invoke(MODULE_ARGS){
    MODULE_UNUSED();struct io_invocation *r=(void *)a;if(!r || !state.hub)return module_result(0,0);r->status=CB_BAD_REQUEST;r->actual=0;if((r->bytes && !r->input) || (r->capacity && !r->output))return module_result(1,1);
    if(r->operation==CB_IO_NOW_NS){if(!r->bytes && r->capacity>=8){uint64_t time=CALL(state.clock,0,0,0).value;copy(r->output,&time,8);r->actual=8;r->status=CB_OK;}return module_result(1,1);}
    struct process_info process;if(CALL(state.process_info,r->process,&process,0).status || process.cpu || CALL(state.cpu,CPU_CURRENT,0,0).value){r->status=CB_DENIED;return module_result(1,1);}
    if(r->operation==CB_IO_KEYBOARD_SUBSCRIBE && !r->bytes && r->capacity>=8){
        uint64_t flags=lock();for(struct subscription *s=state.first;s;s=s->next)if(s->pid==r->process){unlock(flags);r->status=CB_BUSY;return module_result(1,1);}if(state.serial==UINT64_MAX){unlock(flags);r->status=CB_RESOURCE_FAILURE;return module_result(1,1);}uint64_t id=++state.serial;unlock(flags);
        struct kernel_surface_result allocated=CALL(state.alloc,state.heap,1,sizeof(struct subscription));if(allocated.status){r->status=CB_RESOURCE_FAILURE;return module_result(1,1);}struct subscription *s=(void *)allocated.value;s->pid=r->process;s->id=id;
        if(CALL(state.channel,s->pid,s->id,sizeof(struct cb_io_keyboard_event)).status){CALL(state.free,state.heap,s,0);r->status=CB_RESOURCE_FAILURE;return module_result(1,1);}
        flags=lock();s->next=state.first;state.first=s;unlock(flags);
        struct thread_request thread={.abi=1,.cpu=0,.stack_bytes=32768,.entry=(uintptr_t)kernel_surface_find(state.hub,"io.worker"),.major=1,.payload={.arguments={(uintptr_t)s}}};struct kernel_surface_result created=CALL(state.create,&thread,0,0);
        if(created.status){(void)close_subscription(s,0);r->status=CB_RESOURCE_FAILURE;return module_result(1,1);}s->thread=created.value;
        while(!__atomic_load_n(&s->ready,__ATOMIC_ACQUIRE)){uint64_t time=CALL(state.clock,0,0,0).value;if(time>UINT64_MAX-1000000 || CALL(state.thread,THREAD_BLOCK_UNTIL,time+1000000,0).status){r->status=CB_RESOURCE_FAILURE;return module_result(1,1);}}
        if(s->ready!=1){(void)close_subscription(s,1);r->status=CB_RESOURCE_FAILURE;return module_result(1,1);}
        copy(r->output,&id,8);r->actual=8;r->status=CB_OK;return module_result(1,1);
    }
    if(r->bytes!=(r->operation==CB_IO_KEYBOARD_PROTOCOL?sizeof(struct cb_io_protocol):sizeof(struct cb_io_subscription)))return module_result(1,1);
    uint64_t id;copy(&id,r->input,8);uint64_t flags=lock();struct subscription *s=find(r->process,id);if(!s){unlock(flags);r->status=CB_BAD_HANDLE;return module_result(1,1);}
    if(r->operation==CB_IO_KEYBOARD_ACK){if(s->waiting){__atomic_store_n(&s->ack,1,__ATOMIC_RELEASE);CALL(state.thread,THREAD_WAKE,s->thread,0);r->status=CB_OK;}unlock(flags);return module_result(1,1);}
    if(r->operation==CB_IO_KEYBOARD_PROTOCOL){struct cb_io_protocol protocol;copy(&protocol,r->input,sizeof(protocol));if(protocol.reserved || protocol.protocol || !s->claimed || !s->waiting || s->active || stopped(s)){unlock(flags);return module_result(1,1);}struct usb_io control={.device=s->ref,.owner=(uintptr_t)s,.interface=s->interface,.setup={0x21,0x0b,0,0,s->interface,0,0,0}};unlock(flags);struct host_transfer_result result;
        struct kernel_surface_result configured=CALL(state.control,&control,&result,0);flags=lock();if(!configured.status){s->negotiated=1;r->status=CB_OK;}else r->status=CB_UNAVAILABLE;unlock(flags);return module_result(1,1);}
    unlock(flags);if(r->operation==CB_IO_KEYBOARD_CLOSE)r->status=close_subscription(s,1);return module_result(1,1);
}
struct kernel_surface_result io_surface_exit(MODULE_ARGS){
    MODULE_UNUSED();if(!state.hub)return module_result(1,1);uint64_t flags=lock();struct subscription *s=state.first;while(s && s->pid!=a)s=s->next;unlock(flags);if(!s)return module_result(1,1);
    if(CALL(state.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);
    cb_status_t result=close_subscription(s,0);return module_result(result,result==CB_OK);
}
