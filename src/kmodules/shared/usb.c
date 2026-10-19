#include "usb.h"
#include "usb-descriptors.h"
#include "xhci.h"
#include "hwenum.h"
#include "threads.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
#define IO_TIMEOUT UINT64_C(1000000000)
struct usb_host;
struct watcher {struct watcher *next;uint64_t thread;};
struct device {
    struct usb_device record;struct device *next,*parent;struct usb_host *host;
    struct usb_configuration *configs;unsigned config_count,busy,active_endpoint,cancelling,present,notified_ready;
    uint64_t token,pending,attempt_generation,io_thread,cleanup_waiter,hub_owner;unsigned hub_ports,hub_interface;unsigned char *buffer;
};
struct topology_waiter {struct topology_waiter *next;uint64_t thread;};
struct usb_host {struct usb_host *next;uint64_t hardware,id,thread;uint32_t running,stop,failed,scanned;uint64_t topology_owner;struct topology_waiter *topology_waiters;};
static struct {
    const struct kernel_surfaces *hub;const struct hwenum_table *hw;uintptr_t heap;
    struct usb_table table;struct watcher *watchers;struct usb_host *hosts;struct device *devices;uint64_t serial,wakes;unsigned managing;
    kernel_surface_call cpu,alloc,free,thread,create,clock,irq_context;
    kernel_surface_call open,close,info,port,reset,device,endpoint,transfer,result,wait,cancel,remove,watch,hub_config,disconnect;
} state;
static struct kernel_surface_result failure(void) {return module_result(0,0);}
static int bsp(void) {return state.hub && !CALL(state.cpu,CPU_CURRENT,0,0).value;}
static uint64_t lock(void) {return CALL(state.cpu,CPU_ENTER,0,0).value;}
static void unlock(uint64_t flags) {CALL(state.cpu,CPU_LEAVE,flags,0);}
static int foreground(void) {
    uint64_t flags;__asm__ volatile("pushfq; popq %0":"=r"(flags));
    return bsp() && (flags&512) && !CALL(state.cpu,CPU_DEPTH,0,0).value && !CALL(state.irq_context,0,0,0).value && CALL(state.thread,THREAD_CURRENT,0,0).value;
}
static uint64_t now(void) {struct kernel_surface_result r=CALL(state.clock,0,0,0);return r.status?UINT64_MAX:r.value;}
static void *allocate(void *ctx,size_t bytes) {
    (void)ctx;struct kernel_surface_result r=CALL(state.alloc,state.heap,1,bytes);return r.status?NULL:(void *)r.value;
}
static void release(void *ctx,void *p) {(void)ctx;if(p)CALL(state.free,state.heap,p,0);}
static struct usb_allocator allocator;
static void copy(void *to,const void *from,size_t n) {unsigned char *out=to;const unsigned char *in=from;while(n--)*out++=*in++;}
static unsigned word(const unsigned char *p) {return p[0]|((unsigned)p[1]<<8);}
/* Serialize attachment/removal per bus, including entire downstream trees.
 * A blocked topology request never holds a device's transfer ownership. */
static int topology_enter(struct usb_host *host) {
    struct topology_waiter waiter={.thread=CALL(state.thread,THREAD_CURRENT,0,0).value};
    if(!waiter.thread)return 0;
    int linked=0;
    for(;;){uint64_t flags=lock();
        if(!host->topology_owner || host->topology_owner==waiter.thread) {
            int ok=!host->topology_owner;
            if(linked){struct topology_waiter **p=&host->topology_waiters;while(*p && *p!=&waiter)p=&(*p)->next;if(*p)*p=waiter.next;}
            if(ok)host->topology_owner=waiter.thread;
            unlock(flags);return ok;
        }
        if(!linked){waiter.next=host->topology_waiters;host->topology_waiters=&waiter;linked=1;}
        unlock(flags);
        if(CALL(state.thread,THREAD_BLOCK,0,0).status){flags=lock();struct topology_waiter **p=&host->topology_waiters;while(*p && *p!=&waiter)p=&(*p)->next;if(*p)*p=waiter.next;unlock(flags);return 0;}
    }
}
static void topology_leave(struct usb_host *host) {
    uint64_t flags=lock();host->topology_owner=0;
    for(struct topology_waiter *p=host->topology_waiters;p;p=p->next)CALL(state.thread,THREAD_WAKE,p->thread,0);
    unlock(flags);
}
static struct device *find(const struct usb_ref *ref) {
    if(!ref)return NULL;
    for(struct device *p=state.devices;p;p=p->next)if(p->record.identity.id==ref->id && p->record.identity.generation==ref->generation)return p;
    return NULL;
}
static struct device *acquire(const struct usb_ref *ref) {
    if(!foreground())return NULL;
    uint64_t flags=lock();struct device *dev=find(ref);
    if(!dev || dev->busy || dev->host->stop || dev->record.state!=USB_CONFIGURED)dev=NULL;
    if(dev)dev->busy=1;
    unlock(flags);return dev;
}
static void notify_changes(void);
static void finish(struct device *dev) {uint64_t flags=lock();dev->busy=0;if(dev->record.state==USB_CONFIGURED && !dev->notified_ready){dev->notified_ready=1;notify_changes();}CALL(state.thread,THREAD_WAKE,dev->host->thread,0);if(dev->cleanup_waiter)CALL(state.thread,THREAD_WAKE,dev->cleanup_waiter,0);unlock(flags);}
/* Called only under the short CPU lock, from foreground/deferred ownership
 * transitions. Clients receive wake credits, never synchronous callbacks. */
static void notify_changes(void) {
    if(state.table.changes!=UINT64_MAX)++state.table.changes;
    for(struct watcher *w=state.watchers;w;w=w->next)CALL(state.thread,THREAD_WAKE,w->thread,0);
}
static void set_state(struct device *dev,unsigned status,unsigned error) {
    uint64_t flags=lock();uint32_t present=status==USB_CONFIGURED?DEVICE_PRESENT|DEVICE_ENABLED|DEVICE_FUNCTIONING:status==USB_DETACHED || !dev->present?0:DEVICE_PRESENT;
    int changed=dev->record.state!=status || dev->record.error!=error || dev->record.identity.flags!=present;
    dev->record.state=status;dev->record.error=error;dev->record.identity.flags=present;
    if(changed)notify_changes();
    unlock(flags);
}
static void free_configs(struct device *dev) {
    for(unsigned i=0;i<dev->config_count;++i){unsigned char *raw=dev->configs[i].raw;usb_free_configuration(&allocator,&dev->configs[i]);release(NULL,raw);}
    release(NULL,dev->configs);dev->configs=NULL;dev->config_count=0;
}
/* The transport retains only this module's permanent bounce buffer. Even when
 * cancellation cannot prove quiescence, no caller stack/buffer escapes return. */
static int perform(struct device *dev,uint32_t endpoint,const uint8_t *setup,void *buffer,unsigned bytes,uint64_t deadline,struct host_transfer_result *out) {
    *out=(struct host_transfer_result){.state=HOST_FAILED};if(bytes>4096 || (bytes && !buffer) || dev->pending)return 0;
    int input=endpoint?(endpoint&128):setup[0]&128;
    if(bytes && !input)copy(dev->buffer,buffer,bytes);
    struct host_transfer_request request={.device=dev->token,.endpoint=endpoint,.bytes=bytes,.buffer=dev->buffer};
    if(setup)copy(request.setup,setup,8);
    uint64_t time=now();if(time==UINT64_MAX || (deadline && deadline<=time))return 0;
    if(dev->host->stop)return 0;
    if(!deadline){if(time>UINT64_MAX-IO_TIMEOUT)return 0;deadline=time+IO_TIMEOUT;}
    dev->active_endpoint=endpoint;
    struct kernel_surface_result submitted=CALL(state.transfer,dev->host->id,&request,0);if(submitted.status)return 0;
    dev->pending=submitted.value;dev->io_thread=CALL(state.thread,THREAD_CURRENT,0,0).value;
    if(dev->host->stop)CALL(state.thread,THREAD_WAKE,dev->host->thread,0);
    struct kernel_surface_result waited=state.wait(dev->host->id,dev->pending,(uintptr_t)out,deadline,0,0);
    /* Stop Endpoint may emit a Stopped transfer event before its command
     * completion. Do not reap that intermediate event while a shutdown cancel
     * is still establishing the final endpoint/result state. */
    while(dev->cancelling)if(CALL(state.thread,THREAD_BLOCK,0,0).status)return 0;
    if(waited.status) {
        if(state.result(dev->host->id,dev->pending,(uintptr_t)out,0,0,0).status)return 0;
        if(out->state==HOST_PENDING && CALL(state.cancel,dev->host->id,dev->token,endpoint).status) {set_state(dev,USB_FAILED,USB_QUIESCENCE);return 0;}
    }
    if(state.result(dev->host->id,dev->pending,(uintptr_t)out,1,0,0).status || out->state==HOST_PENDING){set_state(dev,USB_FAILED,USB_QUIESCENCE);return 0;}
    dev->pending=0;
    if(out->state==HOST_FAILED && CALL(state.cancel,dev->host->id,dev->token,endpoint).status)set_state(dev,USB_FAILED,USB_QUIESCENCE);
    if(out->state==HOST_DISCONNECTED)set_state(dev,USB_DETACHED,0);
    if(out->state!=HOST_COMPLETE || out->bytes>bytes)return 0;
    if(input && out->bytes)copy(buffer,dev->buffer,out->bytes);
    return 1;
}
static int descriptor(struct device *dev,unsigned type,unsigned index,void *buffer,unsigned bytes,unsigned *actual) {
    uint8_t setup[8]={0x80,6,index,type,0,0,bytes,bytes>>8};struct host_transfer_result result;
    int ok=perform(dev,0,setup,buffer,bytes,0,&result);if(ok)*actual=result.bytes;return ok;
}
static int cleanup(struct device *dev) {
    if(dev->pending) {
        struct host_transfer_result result;
        if(state.result(dev->host->id,dev->pending,(uintptr_t)&result,0,0,0).status)return 0;
        if(result.state==HOST_PENDING)return 0;
        if(state.result(dev->host->id,dev->pending,(uintptr_t)&result,1,0,0).status)return 0;
        dev->pending=0;
    }
    if(dev->token) {
        if(CALL(state.remove,dev->host->id,dev->token,0).status)return 0;
        dev->token=0;
    }
    for(unsigned i=0;i<dev->config_count;++i)for(size_t j=0;j<dev->configs[i].interface_count;++j)dev->configs[i].interfaces[j].info.owner=0;
    return 1;
}
static struct device *port_device(struct usb_host *host,struct device *parent,unsigned port) {
    for(struct device *dev=state.devices;dev;dev=dev->next)if(dev->host==host && dev->parent==parent && dev->record.port==port)return dev;
    struct device *dev=allocate(NULL,sizeof(*dev));if(!dev)return NULL;
    dev->buffer=allocate(NULL,4096);if(!dev->buffer){release(NULL,dev);return NULL;}
    dev->host=host;dev->parent=parent;dev->present=1;dev->record.port=port;dev->record.state=USB_DETACHED;
    uint64_t flags=lock();dev->next=state.devices;state.devices=dev;dev->record.next=state.table.first;state.table.first=&dev->record;++state.table.count;unlock(flags);return dev;
}
static int sleep_for(uint64_t duration) {
    uint64_t time=now();if(time==UINT64_MAX || duration>UINT64_MAX-time)return 0;
    uint64_t deadline=time+duration;
    do {if(CALL(state.thread,THREAD_BLOCK_UNTIL,deadline,0).status)return 0;time=now();}while(time<deadline);
    return time!=UINT64_MAX;
}
static void enumerate(struct device *dev,struct xhci_port port) {
    unsigned error=USB_TRANSPORT,actual=0;uint8_t first[18];struct usb_host *host=dev->host;
    uint64_t busy_flags=lock();dev->busy=1;unlock(busy_flags);
    free_configs(dev);dev->notified_ready=0;dev->hub_owner=dev->hub_ports=0;
    uint64_t flags=lock();uint64_t generation=dev->record.identity.generation;
    if(state.serial==UINT64_C(0x0fffffffffffffff) || generation==UINT64_MAX){unlock(flags);set_state(dev,USB_FAILED,USB_MEMORY);finish(dev);return;}
    dev->record=(struct usb_device){.next=dev->record.next,.identity={.id=DEVICE_USB_ID(++state.serial),.generation=generation+1,.parent_id=dev->parent?dev->parent->record.identity.id:DEVICE_PCI_ID(host->hardware),.bus=DEVICE_BUS_USB},.controller=host->hardware,.port=port.number,.root_port=dev->parent?dev->parent->record.root_port:port.number,.depth=dev->parent?dev->parent->record.depth+1:0,.port_generation=port.generation,.state=USB_ENUMERATING};
    dev->attempt_generation=port.generation;unlock(flags);set_state(dev,USB_ENUMERATING,0);
    /* USB2 attachment debounce, then reset recovery; this worker sleeps rather
     * than spinning while hardware stabilizes. A changed generation aborts. */
    if(!dev->parent && (!sleep_for(100000000) || CALL(state.port,host->id,port.number,&port).status || !(port.status&1) || port.generation!=dev->attempt_generation))goto failed;
    if(!dev->parent && (CALL(state.reset,host->id,port.number,0).status || !sleep_for(10000000) || CALL(state.port,host->id,port.number,&port).status || (port.status&3)!=3 || port.generation!=dev->attempt_generation))goto failed;
    unsigned speed=usb_port_speed(port.protocol_major,(port.status>>10)&15,port.speed_ids,port.speed_id_count);
    if(!speed){error=USB_UNSUPPORTED_SPEED;goto failed;}
    dev->record.speed=speed;
    struct host_device_request binding={.port=dev->record.root_port,.port_generation=port.generation,.max_packet=speed==USB_SUPER?512:speed==USB_HIGH?64:8};
    if(dev->parent) {
        struct xhci_port root;if(CALL(state.port,host->id,dev->record.root_port,&root).status)goto failed;
        binding.port_generation=root.generation;binding.parent=dev->parent->token;binding.downstream_port=port.number;binding.speed_id=(port.status>>10)&15;
    }
    if(CALL(state.device,host->id,&binding,&dev->token).status)goto failed;
    if(!descriptor(dev,1,0,first,8,&actual) || actual!=8)goto failed;
    error=USB_DESCRIPTOR;unsigned packet=usb_ep0_packet(speed,first[7]);
    if(first[0]!=18 || first[1]!=1 || !packet)goto failed;
    if(packet!=binding.max_packet) {
        struct host_endpoint_request ep0={.device=dev->token,.type=HOST_CONTROL,.max_packet=packet};
        error=USB_TRANSPORT;if(CALL(state.endpoint,host->id,&ep0,0).status)goto failed;
    }
    if(!descriptor(dev,1,0,first,18,&actual) || actual!=18)goto failed;
    error=USB_DESCRIPTOR;
    if(first[0]!=18 || first[1]!=1 || !first[17] || usb_ep0_packet(speed,first[7])!=packet || (speed==USB_SUPER && word(first+2)<0x300))goto failed;
    dev->record.vendor=word(first+8);dev->record.product=word(first+10);dev->record.usb_version=word(first+2);dev->record.device_version=word(first+12);
    dev->record.class_code=first[4];dev->record.subclass=first[5];dev->record.protocol=first[6];dev->record.manufacturer=first[14];dev->record.product_string=first[15];dev->record.serial_string=first[16];dev->record.configurations=first[17];
    dev->configs=allocate(NULL,first[17]*sizeof(*dev->configs));if(!dev->configs){error=USB_MEMORY;goto failed;}
    for(unsigned i=0;i<first[17];++i) {
        uint8_t head[9];error=USB_TRANSPORT;if(!descriptor(dev,2,i,head,9,&actual) || actual!=9)goto failed;
        error=USB_DESCRIPTOR;unsigned total=word(head+2);if(head[0]!=9 || head[1]!=2 || total<9)goto failed;
        if(total>4096){error=USB_TRANSPORT_LIMIT;goto failed;}
        unsigned char *raw=allocate(NULL,total);if(!raw){error=USB_MEMORY;goto failed;}
        int ok=descriptor(dev,2,i,raw,total,&actual);
        int parsed=ok && actual==total?usb_parse_configuration(raw,total,speed,&allocator,&dev->configs[i]):0;
        if(!ok || actual!=total || parsed!=1){release(NULL,raw);error=!ok?USB_TRANSPORT:parsed<0?USB_MEMORY:USB_DESCRIPTOR;goto failed;}
        ++dev->config_count;
        for(unsigned j=0;j<i;++j)if(dev->configs[j].value==dev->configs[i].value){error=USB_DESCRIPTOR;goto failed;}
    }
    struct usb_configuration *cfg=&dev->configs[0];
    dev->record.hub=first[4]==9;
    for(size_t i=0;i<cfg->interface_count;++i)if(cfg->interfaces[i].info.class_code==9 && !cfg->interfaces[i].info.alternate)dev->record.hub=1;
    uint8_t setup[8]={0,9,cfg->value,0,0,0,0,0};struct host_transfer_result result;
    error=USB_TRANSPORT;if(!perform(dev,0,setup,NULL,0,0,&result))goto failed;
    flags=lock();dev->record.configuration=cfg->value;dev->record.interfaces=cfg->distinct_interfaces;unlock(flags);
    set_state(dev,USB_CONFIGURED,0);finish(dev);return;
failed:
    if(dev->record.error!=USB_QUIESCENCE)set_state(dev,USB_FAILED,error);
    if(!cleanup(dev))set_state(dev,USB_FAILED,USB_QUIESCENCE);
    finish(dev);
}
static int descendant(struct device *dev,struct device *root) {
    for(unsigned i=0;i<=5 && dev;++i,dev=dev->parent)if(dev==root)return 1;
    return 0;
}
static int detach_tree(struct device *root) {
    uint64_t current=CALL(state.thread,THREAD_CURRENT,0,0).value;
    for(struct device *dev=state.devices;dev;dev=dev->next)if(descendant(dev,root)) {dev->present=0;set_state(dev,USB_DETACHED,0);}
    if(root->token && CALL(state.disconnect,root->host->id,root->token,0).status)return 0;
    for(int depth=5;depth>=0;--depth)for(struct device *dev=state.devices;dev;dev=dev->next)if(descendant(dev,root) && dev->record.depth==(unsigned)depth) {
        uint64_t flags=lock();dev->cleanup_waiter=current;unlock(flags);
        while(dev->busy)if(CALL(state.thread,THREAD_BLOCK,0,0).status)return 0;
        flags=lock();dev->cleanup_waiter=0;unlock(flags);
        if(!cleanup(dev))return 0;
        dev->hub_owner=dev->hub_ports=0;
    }
    return 1;
}
static int host_scan(struct usb_host *host,unsigned ports) {
    for(unsigned i=1;i<=ports;++i) {
        if(host->stop)break;
        struct xhci_port port;if(CALL(state.port,host->id,i,&port).status)return 0;
        struct device *dev=NULL;for(struct device *p=state.devices;p;p=p->next)if(p->host==host && !p->parent && p->record.port==i){dev=p;break;}
        if(dev){uint64_t flags=lock();dev->present=!!(port.status&1);unlock(flags);}
        if(dev && (port.generation!=dev->attempt_generation || !(port.status&1))) {
            if(!detach_tree(dev)){set_state(dev,USB_FAILED,USB_QUIESCENCE);continue;}
            dev->present=!!(port.status&1);
        }
        if(port.status&1) {
            if(!dev)dev=port_device(host,NULL,i);
            if(!dev)return 0;
            if(port.generation!=dev->attempt_generation && !dev->busy && !dev->pending && !dev->token)enumerate(dev,port);
        }
    }
    return 1;
}
struct kernel_surface_result usb_surface_worker(MODULE_ARGS) {
    MODULE_UNUSED();struct usb_host *host=(void *)a;int known=0;
    for(struct usb_host *p=state.hosts;p;p=p->next)if(p==host)known=1;
    if(!known || !foreground())return failure();
    while(!host->thread)if(CALL(state.thread,THREAD_BLOCK,0,0).status)return failure();
    if(CALL(state.thread,THREAD_CURRENT,0,0).value!=host->thread)return failure();
    struct xhci_info info={0};int ok=1;
    if(!host->stop) {
        struct kernel_surface_result opened=CALL(state.open,host->hardware,&info,0);
        host->id=opened.value;if(opened.status){ok=0;goto done;}
        if(CALL(state.watch,host->id,host->thread,0).status){ok=0;goto close;}
        while(!host->stop) {
            if(CALL(state.info,host->id,&info,0).status || info.state!=XHCI_ONLINE || !topology_enter(host)){ok=0;break;}
            int scanned=host_scan(host,info.ports);topology_leave(host);if(!scanned){ok=0;break;}
            if(!host->scanned){uint64_t f=lock();host->scanned=1;notify_changes();unlock(f);}
            if(host->stop)break;
            if(CALL(state.thread,THREAD_BLOCK,0,0).status){ok=0;break;}
            ++state.wakes;
        }
        CALL(state.watch,host->id,0,0);
    }
close:
    if(!topology_enter(host)){ok=0;goto done;}
    for(struct device *dev=state.devices;dev;dev=dev->next)if(dev->host==host && !dev->parent && dev->token)CALL(state.disconnect,host->id,dev->token,0);
    for(int depth=5;depth>=0;--depth)for(struct device *dev=state.devices;dev;dev=dev->next)if(dev->host==host && dev->record.depth==(unsigned)depth) {
        dev->present=0;set_state(dev,USB_DETACHED,0);
        while(dev->busy) {
            struct host_transfer_result terminal={0};
            int pending=dev->pending && !state.result(host->id,dev->pending,(uintptr_t)&terminal,0,0,0).status && terminal.state==HOST_PENDING;
            if(pending) {
                uint64_t flags=lock();dev->cancelling=1;unlock(flags);
                struct kernel_surface_result cancelled=CALL(state.cancel,host->id,dev->token,dev->active_endpoint);
                flags=lock();dev->cancelling=0;if(dev->io_thread)CALL(state.thread,THREAD_WAKE,dev->io_thread,0);unlock(flags);
                if(cancelled.status){ok=0;goto done;}
            }
            if(CALL(state.thread,THREAD_BLOCK,0,0).status){ok=0;goto done;}
        }
        if(!cleanup(dev))ok=0;
    }
    if(host->id) {
        uint64_t deadline=now()+IO_TIMEOUT;
        while(CALL(state.close,host->id,0,0).status) {
            if(now()>=deadline || CALL(state.thread,THREAD_YIELD,0,0).status){ok=0;goto done;}
        }
        host->id=0;
        for(struct device *dev=state.devices;dev;dev=dev->next)if(dev->host==host)dev->attempt_generation=0;
    }
done:
    if(host->topology_owner==CALL(state.thread,THREAD_CURRENT,0,0).value)topology_leave(host);
    {uint64_t flags=lock();host->failed=!ok;host->running=0;notify_changes();unlock(flags);}
    return module_result(1,ok);
}
struct kernel_surface_result usb_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)a;
    if(state.hub)return module_result(1,state.hub==hub && state.heap==b);
    if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC || !b)return failure();
    /* Resolve in place: large automatic pointer arrays can be folded into
     * absolute static initializers, which a format3 PIC image cannot relocate. */
#define RESOLVE(field,name) do {state.field=kernel_surface_find_version(hub,name,1,0);if(!state.field)return failure();} while(0)
    RESOLVE(cpu,"cpu.control");
    RESOLVE(alloc,"mem.calloc");
    RESOLVE(free,"mem.free");
    RESOLVE(thread,"threads.control");
    RESOLVE(create,"threads.create");
    RESOLVE(clock,"clock.now_ns");
    RESOLVE(irq_context,"irq.context");
    RESOLVE(open,"xhci.open");
    RESOLVE(close,"xhci.close");
    RESOLVE(info,"xhci.info");
    RESOLVE(port,"xhci.port");
    RESOLVE(reset,"xhci.reset_port");
    RESOLVE(device,"xhci.device");
    RESOLVE(endpoint,"xhci.endpoint");
    RESOLVE(transfer,"xhci.transfer");
    RESOLVE(result,"xhci.result");
    RESOLVE(wait,"xhci.wait");
    RESOLVE(cancel,"xhci.cancel");
    RESOLVE(remove,"xhci.remove");
    RESOLVE(watch,"xhci.watch");
    RESOLVE(hub_config,"xhci.hub");
    RESOLVE(disconnect,"xhci.disconnect");
#undef RESOLVE
    if(CALL(state.cpu,CPU_CURRENT,0,0).value)return failure();
    const struct kernel_hub_table *hw=kernel_hub_find_table(hub,HWENUM_TABLE_NAME);kernel_surface_call publish=kernel_surface_find(hub,"hub.publish_table");
    if(!hw || !publish)return failure();
    allocator=(struct usb_allocator){.allocate=allocate,.release=release};
    state.heap=b;state.hw=(const void *)hw->address;
    for(size_t i=0;i<state.hw->count;++i) {
        const struct hwenum_descriptor *d=&state.hw->entries[i];
        if(d->kind!=HWENUM_PCI || d->data.pci.class_code!=12 || d->data.pci.subclass!=3 || d->data.pci.programming_interface!=0x30)continue;
        struct usb_host *host=allocate(NULL,sizeof(*host));if(!host)goto fail;
        host->hardware=i;host->next=state.hosts;state.hosts=host;
    }
    state.table=(struct usb_table){.magic=USB_TABLE_MAGIC,.major=1,.record_bytes=sizeof(struct usb_device)};
    struct kernel_hub_table table={.owner=kernel_surface_owner(hub,"usb.init"),.name=USB_TABLE_NAME,.address=(uintptr_t)&state.table,.bytes=sizeof(state.table),.count=1,.entry_bytes=sizeof(state.table)};
    if(CALL(publish,&table,0,0).status)goto fail;
    state.hub=hub;return module_result(1,1);
fail:
    while(state.hosts){struct usb_host *p=state.hosts;state.hosts=p->next;release(NULL,p);}return failure();
}
static int manage_begin(void) {
    if(!bsp() || CALL(state.cpu,CPU_DEPTH,0,0).value || CALL(state.irq_context,0,0,0).value)return 0;
    uint64_t flags=lock();int ok=!state.managing;if(ok)state.managing=1;unlock(flags);return ok;
}
static struct kernel_surface_result manage_end(uintptr_t value,int ok) {
    uint64_t flags=lock();state.managing=0;unlock(flags);return module_result(value,ok);
}
struct kernel_surface_result usb_surface_start(MODULE_ARGS) {
    MODULE_UNUSED();if(!manage_begin())return failure();
    kernel_surface_call worker=kernel_surface_find_version(state.hub,"usb.worker",1,0);if(!worker)return manage_end(0,0);
    unsigned count=0;
    for(struct usb_host *host=state.hosts;host;host=host->next) {
        if(host->running){if(host->stop)return manage_end(0,0);++count;continue;}
        if(host->id)return manage_end(0,0); /* Uncertain old controller ownership is retained. */
        if(host->thread && CALL(state.thread,THREAD_REAP,host->thread,0).status)return manage_end(0,0);
        host->thread=0;host->stop=host->failed=host->scanned=0;
        struct thread_request request={.abi=1,.cpu=0,.stack_bytes=32768,.entry=(uintptr_t)worker,.major=1};request.payload.arguments[0]=(uintptr_t)host;
        uint64_t flags=lock();host->running=1;unlock(flags);
        struct kernel_surface_result created=CALL(state.create,&request,0,0);
        flags=lock();
        if(!created.status){host->thread=created.value;++count;CALL(state.thread,THREAD_WAKE,host->thread,0);}
        else {host->failed=1;host->running=0;}
        unlock(flags);if(created.status)return manage_end(0,0);
    }
    return manage_end(count,1);
}
struct kernel_surface_result usb_surface_stop(MODULE_ARGS) {
    MODULE_UNUSED();if(!manage_begin())return failure();
    for(struct usb_host *host=state.hosts;host;host=host->next) {
        uint64_t flags=lock();host->stop=1;
        if(host->running)CALL(state.thread,THREAD_WAKE,host->thread,0);
        unlock(flags);
        if(!host->running && host->thread) {
            if(!CALL(state.thread,THREAD_REAP,host->thread,0).status)host->thread=0;
        }
    }
    return manage_end(1,1);
}
struct kernel_surface_result usb_surface_watch(MODULE_ARGS) {
    MODULE_UNUSED();if(!foreground() || a>1)return failure();
    uint64_t current=CALL(state.thread,THREAD_CURRENT,0,0).value;
    uint64_t flags=lock();struct watcher **link=&state.watchers;
    while(*link && (*link)->thread!=current)link=&(*link)->next;
    struct watcher *existing=*link;
    if(!a && existing)*link=existing->next;
    unlock(flags);
    if(!a){release(NULL,existing);return module_result(current,1);}
    if(existing)return module_result(current,1);
    struct watcher *w=allocate(NULL,sizeof(*w));if(!w)return failure();w->thread=current;
    flags=lock();w->next=state.watchers;state.watchers=w;unlock(flags);return module_result(current,1);
}
struct kernel_surface_result usb_surface_info(MODULE_ARGS) {
    MODULE_UNUSED();if(!bsp() || !a)return failure();struct usb_info info={0};uint64_t flags=lock();
    for(struct usb_host *host=state.hosts;host;host=host->next){++info.controllers;info.running+=host->running;info.failed+=host->failed;}
    for(struct device *dev=state.devices;dev;dev=dev->next){++info.records;info.connected+=dev->present;info.configured+=dev->record.state==USB_CONFIGURED;}
    info.worker_wakes=state.wakes;info.changes=state.table.changes;*(struct usb_info *)a=info;unlock(flags);return module_result(1,1);
}
struct kernel_surface_result usb_surface_device(MODULE_ARGS) {
    MODULE_UNUSED();if(!bsp() || !b)return failure();uint64_t flags=lock();struct device *dev=find((const void *)a);
    if(dev)*(struct usb_device *)b=dev->record;
    unlock(flags);return module_result(1,dev!=NULL);
}
struct kernel_surface_result usb_surface_configuration(MODULE_ARGS) {
    MODULE_UNUSED();if(!bsp() || !e || (!c && d))return failure();uint64_t flags=lock();struct device *dev=find((const void *)a);
    int ok=dev && !dev->busy && b<dev->config_count;
    if(ok){*(size_t *)e=dev->configs[b].bytes;ok=d>=dev->configs[b].bytes;if(ok)copy((void *)c,dev->configs[b].raw,dev->configs[b].bytes);}
    unlock(flags);return module_result(1,ok);
}
static struct usb_parsed_interface *interface_find(struct device *dev,unsigned number) {
    if(!dev->config_count)return NULL;
    struct usb_configuration *cfg=&dev->configs[0];for(size_t i=0;i<cfg->interface_count;++i)if(cfg->interfaces[i].info.number==number && !cfg->interfaces[i].info.alternate)return &cfg->interfaces[i];return NULL;
}
struct kernel_surface_result usb_surface_interface(MODULE_ARGS) {
    MODULE_UNUSED();if(!bsp() || !c)return failure();uint64_t flags=lock();struct device *dev=find((const void *)a);
    int ok=dev && !dev->busy && dev->config_count && b<dev->configs[0].interface_count;
    if(ok)*(struct usb_interface *)c=dev->configs[0].interfaces[b].info;
    unlock(flags);return module_result(1,ok);
}
static struct usb_endpoint *endpoint_find(struct device *dev,struct usb_parsed_interface *interface,unsigned address) {
    if(!interface)return NULL;
    for(unsigned i=0;i<interface->info.endpoints;++i){struct usb_endpoint *ep=&dev->configs[0].endpoints[interface->first+i];if(ep->address==address)return ep;}return NULL;
}
struct kernel_surface_result usb_surface_endpoint(MODULE_ARGS) {
    MODULE_UNUSED();if(!bsp() || !d)return failure();uint64_t flags=lock();struct device *dev=find((const void *)a);struct usb_endpoint *ep=dev && !dev->busy?endpoint_find(dev,interface_find(dev,b),c):NULL;
    if(ep)*(struct usb_endpoint *)d=*ep;
    unlock(flags);return module_result(1,ep!=NULL);
}
struct kernel_surface_result usb_surface_claim(MODULE_ARGS) {
    MODULE_UNUSED();if(!c)return failure();struct device *dev=acquire((const void *)a);if(!dev)return failure();
    struct usb_parsed_interface *interface=interface_find(dev,b);int ok=interface && (!interface->info.owner || interface->info.owner==c);
    if(ok)for(unsigned i=0;i<interface->info.endpoints;++i)if(!dev->configs[0].endpoints[interface->first+i].supported)ok=0;
    if(ok)for(unsigned i=0;i<interface->info.endpoints;++i) {
        size_t index=interface->first+i;struct usb_endpoint *ep=&dev->configs[0].endpoints[index];if(dev->configs[0].configured[index])continue;
        struct host_endpoint_request request={.device=dev->token,.address=ep->address,.type=ep->host_type,.max_packet=ep->max_packet,.interval=ep->interval,.max_burst=ep->max_burst,.max_esit=ep->max_esit};
        if(CALL(state.endpoint,dev->host->id,&request,0).status){ok=0;break;}dev->configs[0].configured[index]=1;
    }
    if(ok)interface->info.owner=c;
    finish(dev);return module_result(1,ok);
}
struct kernel_surface_result usb_surface_release(MODULE_ARGS) {
    MODULE_UNUSED();struct device *dev=acquire((const void *)a);if(!dev)return failure();
    struct usb_parsed_interface *interface=interface_find(dev,b);int ok=interface && c && interface->info.owner==c;
    if(ok)interface->info.owner=0;
    finish(dev);return module_result(1,ok);
}
static struct kernel_surface_result io(const struct usb_io *request,struct host_transfer_result *out,int control) {
    if(!request || !out || request->reserved || request->bytes>4096 || (request->bytes && !request->buffer))return failure();
    struct device *dev=acquire(&request->device);if(!dev)return failure();
    struct usb_parsed_interface *interface=interface_find(dev,request->interface);int ok=interface && request->owner && interface->info.owner==request->owner;
    if(control) {
        unsigned recipient=request->setup[0]&31,type=(request->setup[0]>>5)&3,index=word(request->setup+4);
        ok=ok && !request->endpoint && type!=3 && word(request->setup+6)==request->bytes;
        if(recipient==1)ok=ok && index==request->interface;
        else if(recipient==2)ok=ok && endpoint_find(dev,interface,index);
        else if((recipient==0 || recipient==3) && type==1 && interface && interface->info.class_code==9 && dev->record.hub) {
            if(recipient==3)ok=ok && dev->hub_owner==request->owner && index && index<=dev->hub_ports;
        } else ok=0;
        if(type==0)ok=ok && ((request->setup[0]&128) || (recipient==2 && request->setup[1]==1 && !word(request->setup+2) && !request->bytes));
    } else ok=ok && endpoint_find(dev,interface,request->endpoint);
    if(ok) {
        uint64_t deadline=request->deadline_ns;
        /* An idle interrupt-IN endpoint has nothing to report. Keep its receive
         * armed until an event, disconnect or explicit cancellation instead of
         * periodically stopping/restarting a healthy keyboard or hub. */
        if(!control && !deadline && (request->endpoint&128) && endpoint_find(dev,interface,request->endpoint)->transfer_type==3)deadline=UINT64_MAX;
        ok=perform(dev,control?0:request->endpoint,control?request->setup:NULL,request->buffer,request->bytes,deadline,out);

    }
    finish(dev);return module_result(ok?out->bytes:0,ok);
}
struct kernel_surface_result usb_surface_control(MODULE_ARGS) {MODULE_UNUSED();return io((const void *)a,(void *)b,1);}
struct kernel_surface_result usb_surface_transfer(MODULE_ARGS) {MODULE_UNUSED();return io((const void *)a,(void *)b,0);}

struct kernel_surface_result usb_surface_hub_configure(MODULE_ARGS) {
    MODULE_UNUSED();const struct usb_hub_config *r=(void *)a;if(!r)return failure();
    struct device *dev=acquire(&r->device);if(!dev)return failure();
    struct usb_parsed_interface *interface=interface_find(dev,r->interface);
    int ok=dev->record.hub && dev->record.depth<5 && interface && interface->info.class_code==9 && r->owner && interface->info.owner==r->owner;
    struct host_hub_request request={.device=dev->token,.ports=r->ports,.multi_tt=r->multi_tt,.think_time=r->think_time};
    if(ok)ok=!CALL(state.hub_config,dev->host->id,&request,0).status;
    if(ok){dev->hub_owner=r->owner;dev->hub_ports=r->ports;dev->hub_interface=r->interface;}
    finish(dev);return module_result(1,ok);
}
struct kernel_surface_result usb_surface_downstream(MODULE_ARGS) {
    MODULE_UNUSED();const struct usb_downstream *r=(void *)a;if(!r || !foreground() || !r->generation || !r->port || r->connected>1 || r->reserved || (r->connected && (r->speed<USB_LOW || r->speed>USB_SUPER)))return failure();
    struct device *parent=find(&r->parent);if(!parent || !topology_enter(parent->host))return failure();
    int ok=0;
    if(!find(&r->parent) || parent->host->stop || parent->record.state!=USB_CONFIGURED || !r->owner || parent->hub_owner!=r->owner || !interface_find(parent,parent->hub_interface) || interface_find(parent,parent->hub_interface)->info.owner!=r->owner || r->port>parent->hub_ports)goto done;
    struct device *dev=port_device(parent->host,parent,r->port);if(!dev)goto done;
    if(dev->attempt_generation==r->generation && dev->record.state==USB_CONFIGURED && r->connected){ok=1;goto done;}
    if(!detach_tree(dev))goto done;
    dev->attempt_generation=r->generation;
    if(!r->connected){ok=1;goto done;}
    dev->present=1;
    unsigned id=r->speed==USB_LOW?2:r->speed==USB_FULL?1:r->speed==USB_HIGH?3:4;
    struct xhci_port port={.number=r->port,.generation=r->generation,.protocol_major=r->speed==USB_SUPER?3:2,.status=3|(id<<10)};
    enumerate(dev,port);
    /* A rejected child is a device-local failure, not a dead upstream hub.
     * Only uncertain transport quiescence makes the topology request fail. */
    ok=dev->record.state==USB_CONFIGURED || (dev->record.state==USB_FAILED && dev->record.error!=USB_QUIESCENCE);
done:
    topology_leave(parent->host);return module_result(1,ok);
}

struct kernel_surface_result usb_surface_cancel(MODULE_ARGS) {
    MODULE_UNUSED();const struct usb_io *r=(void *)a;if(!r || !foreground())return failure();
    struct device *dev=find(&r->device);struct usb_parsed_interface *interface=dev?interface_find(dev,r->interface):NULL;
    if(!dev || !interface || !r->owner || interface->info.owner!=r->owner || (r->endpoint && !endpoint_find(dev,interface,r->endpoint)))return failure();
    uint64_t flags=lock();int ok=dev->pending && dev->active_endpoint==r->endpoint && !dev->cancelling;
    if(ok)dev->cancelling=1;
    unlock(flags);if(!ok)return failure();
    struct kernel_surface_result result=CALL(state.cancel,dev->host->id,dev->token,r->endpoint);
    flags=lock();dev->cancelling=0;if(dev->io_thread)CALL(state.thread,THREAD_WAKE,dev->io_thread,0);unlock(flags);return result;
}

struct kernel_surface_result usb_surface_ready(MODULE_ARGS){
    MODULE_UNUSED();if(!bsp())return failure();uint64_t flags=lock();int ready=1,ok=1;
    for(struct usb_host *h=state.hosts;h;h=h->next){if(h->failed)ok=0;if(!h->running || !h->scanned)ready=0;}
    for(struct device *dev=state.devices;dev;dev=dev->next)if(dev->present && dev->record.state==USB_ENUMERATING)ready=0;
    unlock(flags);return module_result(ready,ok);
}
