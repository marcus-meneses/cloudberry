#include "hid-reports.h"
#include "usb.h"
#include "threads.h"
#include "process.h"
#include "../../common/hid-report.h"
#include "../../kernel/cpu.h"
/* One physical owner/receive worker per interface; independent copied mailboxes
* per class consumer. Post to every recipient before waiting for their ACKs. */
struct hid_source;
struct lifecycle_waiter {struct lifecycle_waiter *next;
    uint64_t thread;
};
struct hid_link {struct hid_link *next;
    uint64_t pid,id,parent,thread,creator,closer;
    struct hid_source *source;
    uint32_t classes;
    unsigned stop,ack,waiting,ready,started,watcher,users,failed;
};
struct hid_source {struct hid_source *next;
    struct usb_ref ref;
    uint32_t interface,endpoint,subclass,descriptor_bytes,report_bytes,classes;
    uint64_t thread;
    unsigned stop,active,claimed,dead,started,failed;
    struct usb_io_cancellable transfer;
    unsigned char *descriptor,*report;
    struct hid_report_info route;
};
static struct {const struct kernel_surfaces *hub;
    uintptr_t heap;
    uint64_t pid,keyboard_pid,serial,changes;
    unsigned opening;
    struct lifecycle_waiter *waiters;
    struct hid_link *links;
    struct hid_source *sources;
    kernel_surface_call alloc,free,cpu,threads,create,clock,device,interface,endpoint,configuration,claim,release,control,transfer,cancel,watch,channel,post,unsubscribe,process_info;
}
hid;
#define HC(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static uint64_t hlock(void){return HC(hid.cpu,CPU_ENTER,0,0).value;
}
static void hunlock(uint64_t flags){HC(hid.cpu,CPU_LEAVE,flags,0);
}
static void hcopy(void *out,const void *in,size_t n){for(size_t i=0;i<n;++i)((unsigned char *)out)[i]=((const unsigned char *)in)[i];
}
static void *halloc(size_t n){struct kernel_surface_result r=HC(hid.alloc,hid.heap,1,n);
    return r.status?NULL:(void *)r.value;
}
static void hfree(void *p){if(p)HC(hid.free,hid.heap,p,0);
}
static void *parser_alloc(void *ctx,size_t n){(void)ctx;
    return halloc(n);
}
static void parser_free(void *ctx,void *p){(void)ctx;
    hfree(p);
}
static int lifecycle_enter(int wait){
    struct lifecycle_waiter w={.thread=HC(hid.threads,THREAD_CURRENT,0,0).value};
    uint64_t flags=hlock();
    while(hid.opening){if(!wait){hunlock(flags);
            return 0;
        }
        w.next=hid.waiters;
        hid.waiters=&w;
        hunlock(flags);
        int failed=HC(hid.threads,THREAD_BLOCK,0,0).status;
        flags=hlock();
        struct lifecycle_waiter **at=&hid.waiters;
        while(*at!=&w)at=&(*at)->next;
        *at=w.next;
        if(failed){hunlock(flags);
            return 0;
        }
    }
    hid.opening=1;
    hunlock(flags);
    return 1;
}
static void lifecycle_leave(void){uint64_t flags=hlock();
    hid.opening=0;
    for(struct lifecycle_waiter *w=hid.waiters;w;w=w->next)HC(hid.threads,THREAD_WAKE,w->thread,0);
    hunlock(flags);
}
static int hstopped(struct hid_link *l){return __atomic_load_n(&l->stop,__ATOMIC_ACQUIRE)!=0;
}
static struct hid_link *hfind(uint64_t pid,uint64_t id){for(struct hid_link *l=hid.links;l;l=l->next)if(l->pid==pid && l->id==id)return l;
    return NULL;
}
static struct hid_source *hsource(struct usb_ref ref,unsigned interface){for(struct hid_source *s=hid.sources;s;s=s->next)if(s->ref.id==ref.id && s->ref.generation==ref.generation && s->interface==interface)return s;
    return NULL;
}
static void changed(void){uint64_t flags=hlock();
    ++hid.changes;
    for(struct hid_link *l=hid.links;l;l=l->next)if(l->watcher && l->thread)HC(hid.threads,THREAD_WAKE,l->thread,0);
    hunlock(flags);
}
static int hpost(struct hid_link *l,uint32_t kind,uint32_t status,const void *data,size_t bytes){
    unsigned char payload[sizeof(struct cb_io_hid_event)+CB_HID_LIMIT];
    struct hid_source *s=l->source;
    struct cb_io_hid_event event={.subscription=l->watcher?l->id:l->parent,.connection=l->watcher?0:l->id,.device=s?s->ref.id:0,.generation=s?s->ref.generation:0,.timestamp_ns=HC(hid.clock,0,0,0).value,.kind=kind,.status=status,.bytes=bytes};
    hcopy(payload,&event,sizeof(event));
    if(bytes)hcopy(payload+sizeof(event),data,bytes);
    const struct cb_interface_id interface=CB_IO_INTERFACE_ID;
    return !hid.post(l->pid,l->id,(uintptr_t)payload,sizeof(event)+bytes,(uintptr_t)&interface,CB_IO_HID_EVENT).status;
}
static int hrelease(struct hid_source *s){
    if(!s->claimed)return 1;
    struct usb_device d={0};
    if(!HC(hid.device,&s->ref,&d,0).status && d.state==USB_CONFIGURED){if(HC(hid.release,&s->ref,s->interface,s).status)return 0;
    }
    else if(d.state==USB_FAILED && d.error==USB_QUIESCENCE)return 0;
    s->claimed=0;
    return 1;
}
static int fanout(struct hid_source *s,unsigned classes,unsigned kind,unsigned status,size_t bytes,int wait){
    uint64_t cursor=0;
    for(;;){uint64_t flags=hlock();
        struct hid_link *chosen=NULL;
        for(struct hid_link *l=hid.links;l;l=l->next)if(l->source==s && l->id>cursor && !hstopped(l) && l->started && (l->classes&classes) && (!chosen || l->id<chosen->id))chosen=l;
        if(!chosen){hunlock(flags);
            break;
        }
        cursor=chosen->id;
        ++chosen->users;
        chosen->waiting=1;
        chosen->ack=0;
        hunlock(flags);
        int sent=hpost(chosen,kind,status,s->report,bytes);
        flags=hlock();
        if(!sent){chosen->waiting=0;
            __atomic_store_n(&chosen->stop,1,__ATOMIC_RELEASE);
        }
        --chosen->users;
        if(chosen->closer)HC(hid.threads,THREAD_WAKE,chosen->closer,0);
        hunlock(flags);
    }
    /* Never retain a logical-link pointer while sleeping: CLOSE can remove one
    * consumer while the other continues using the physical interface. */
    while(wait && !__atomic_load_n(&s->stop,__ATOMIC_ACQUIRE)){uint64_t flags=hlock();
        int pending=0;
        for(struct hid_link *l=hid.links;l;l=l->next)if(l->source==s && !hstopped(l) && l->waiting){pending=1;
            break;
        }
        hunlock(flags);
        if(!pending)break;
        if(HC(hid.threads,THREAD_BLOCK,0,0).status)return 0;
    }
    return 1;
}
struct kernel_surface_result hid_surface_capture_worker(MODULE_ARGS){
    MODULE_UNUSED();
    struct hid_source *s=(void *)a;
    if(!s)return module_result(0,0);
    int ok=1;
    while(!__atomic_load_n(&s->stop,__ATOMIC_ACQUIRE)){
        uint64_t flags=hlock();
        s->transfer=(struct usb_io_cancellable){.base={.device=s->ref,.owner=(uintptr_t)s,.interface=s->interface,.endpoint=s->endpoint,.bytes=s->report_bytes,.buffer=s->report,.reserved=USB_IO_CANCELLABLE},.cancelled=&s->stop};
        s->active=1;
        hunlock(flags);
        struct host_transfer_result result={0};
        struct kernel_surface_result read=HC(hid.transfer,&s->transfer,&result,0);
        flags=hlock();
        s->active=0;
        hunlock(flags);
        if(__atomic_load_n(&s->stop,__ATOMIC_ACQUIRE))break;
        if(!read.status && result.state==HOST_COMPLETE && result.bytes && result.bytes<=s->report_bytes){int classes=hid_report_route(&s->route,s->report,result.bytes);
            if(classes<0){if(!fanout(s,s->classes,CB_IO_ERROR,CB_BAD_REQUEST,0,1)){ok=0;break;}}
            else if(classes && !fanout(s,(unsigned)classes,CB_IO_REPORT,CB_OK,result.bytes,1)){ok=0;break;}
            continue;
        }
        struct usb_device device={0};
        int alive=!HC(hid.device,&s->ref,&device,0).status && device.state==USB_CONFIGURED;
        if(alive && result.state==HOST_CANCELLED)continue;
        (void)fanout(s,s->classes,alive?CB_IO_ERROR:CB_IO_DEVICE_REMOVED,alive?CB_UNAVAILABLE:CB_OK,0,1);
        break;
    }
    if(!ok && !s->stop){s->failed=1;(void)fanout(s,s->classes,CB_IO_ERROR,CB_PROVIDER_FAILED,0,0);}
    if(!hrelease(s))ok=0;
    __atomic_store_n(&s->dead,1,__ATOMIC_RELEASE);
    return module_result(ok,ok);
}
struct kernel_surface_result hid_surface_report_worker(MODULE_ARGS){
    MODULE_UNUSED();
    struct hid_link *l=(void *)a;
    if(!l || !l->watcher)return module_result(0,0);
    int ok=1;
    if(HC(hid.watch,1,0,0).status){__atomic_store_n(&l->ready,2,__ATOMIC_RELEASE);
        HC(hid.threads,THREAD_WAKE,l->creator,0);
        return module_result(0,0);
    }
    __atomic_store_n(&l->ready,1,__ATOMIC_RELEASE);
    HC(hid.threads,THREAD_WAKE,l->creator,0);
    uint64_t last=0,internal=0;
    int initial=1;
    const struct kernel_hub_table *entry=kernel_hub_find_table(hid.hub,USB_TABLE_NAME);
    if(!entry)ok=0;
    while(ok && !hstopped(l)){uint64_t flags=hlock(),changes=((const struct usb_table *)entry->address)->changes,ours=hid.changes;
        hunlock(flags);
        if(initial || changes!=last || ours!=internal){initial=0;
            last=changes;
            internal=ours;
            flags=hlock();
            l->waiting=1;
            l->ack=0;
            hunlock(flags);
            if(!hpost(l,CB_IO_HID_CHANGED,CB_OK,NULL,0)){ok=0;
                break;
            }
            while(!hstopped(l) && !__atomic_load_n(&l->ack,__ATOMIC_ACQUIRE))if(HC(hid.threads,THREAD_BLOCK,0,0).status){ok=0;
                break;
            }
            continue;
        }
        if(HC(hid.threads,THREAD_BLOCK,0,0).status){ok=0;
            break;
        }
    }
    (void)HC(hid.watch,0,0,0);
    if(!ok && !hstopped(l)){l->failed=1;(void)hpost(l,CB_IO_ERROR,CB_PROVIDER_FAILED,NULL,0);}
    return module_result(ok,ok);
}
int hid_reports_init(const struct kernel_surfaces *hub,uintptr_t heap){
    if(hid.hub)return hid.hub==hub;
#define HB(field,name) do{hid.field=kernel_surface_find(hub,name);if(!hid.field)return 0;}while(0)
    HB(alloc,"mem.calloc");
    HB(free,"mem.free");
    HB(cpu,"cpu.control");
    HB(threads,"threads.control");
    HB(create,"threads.create");
    HB(clock,"clock.now_ns");
    HB(device,"usb.device");
    HB(interface,"usb.interface");
    HB(endpoint,"usb.endpoint");
    HB(configuration,"usb.configuration");
    HB(claim,"usb.claim");
    HB(release,"usb.release");
    HB(control,"usb.control");
    HB(transfer,"usb.transfer");
    HB(cancel,"usb.cancel");
    HB(watch,"usb.watch");
    HB(channel,"handles.channel");
    HB(post,"handles.post");
    HB(unsubscribe,"handles.unsubscribe");
    HB(process_info,"process.info");
#undef HB
    hid.hub=hub;
    hid.heap=heap;
    return 1;
}
void hid_reports_bind(const char *name,uint64_t pid){const char *expected="mouse";
    size_t i=0;
    while(name[i] && name[i]==expected[i])++i;
    if(!name[i] && !expected[i])hid.pid=pid;
    expected="keyboard";
    i=0;
    while(name[i] && name[i]==expected[i])++i;
    if(!name[i] && !expected[i])hid.keyboard_pid=pid;
}
static struct hid_link *hnew(uint64_t pid){
    if(hid.serial==UINT64_C(0x7fffffffffffffff))return NULL;
    struct hid_link *l=halloc(sizeof(*l));
    if(!l)return NULL;
    l->pid=pid;
    l->id=UINT64_C(0x8000000000000000)|++hid.serial;
    l->classes=pid==hid.keyboard_pid?HID_KEYBOARD:HID_MOUSE;
    if(HC(hid.channel,pid,l->id,sizeof(struct cb_io_hid_event)+CB_HID_LIMIT).status){hfree(l);
        return NULL;
    }
    uint64_t flags=hlock();
    l->next=hid.links;
    hid.links=l;
    hunlock(flags);
    return l;
}
static cb_status_t source_close(struct hid_source *s,int wait){
    uint64_t flags=hlock();
    __atomic_store_n(&s->stop,1,__ATOMIC_RELEASE);
    int active=s->active;
    uint64_t thread=s->thread;
    hunlock(flags);
    if(active)(void)HC(hid.cancel,&s->transfer,0,0);
    if(thread)HC(hid.threads,THREAD_WAKE,thread,0);
    if(thread){struct thread_info info;
        if(HC(hid.threads,THREAD_INFO,thread,&info).status)return CB_RESOURCE_FAILURE;
        if(info.state!=THREAD_EXITED){if(!wait)return CB_BUSY;
            if(HC(hid.threads,THREAD_WAIT,thread,0).status)return CB_RESOURCE_FAILURE;
        }
        if(HC(hid.threads,THREAD_REAP,thread,0).status)return CB_RESOURCE_FAILURE;
        s->thread=0;
    }
    if(!hrelease(s))return CB_RESOURCE_FAILURE;
    flags=hlock();
    struct hid_source **at=&hid.sources;
    while(*at && *at!=s)at=&(*at)->next;
    if(*at)*at=s->next;
    hunlock(flags);
    hfree(s->descriptor);
    hfree(s->report);
    hfree(s);
    return CB_OK;
}
static cb_status_t hclose_inner(struct hid_link *l,int wait){
    uint64_t flags=hlock();
    __atomic_store_n(&l->stop,1,__ATOMIC_RELEASE);
    l->waiting=0;
    uint64_t thread=l->thread;
    struct hid_source *s=l->source;
    hunlock(flags);
    if(thread)HC(hid.threads,THREAD_WAKE,thread,0);
    if(s && s->thread)HC(hid.threads,THREAD_WAKE,s->thread,0);
    if(thread){struct thread_info info;
        if(HC(hid.threads,THREAD_INFO,thread,&info).status)return CB_RESOURCE_FAILURE;
        if(info.state!=THREAD_EXITED){if(!wait)return CB_BUSY;
            if(HC(hid.threads,THREAD_WAIT,thread,0).status)return CB_RESOURCE_FAILURE;
        }
        if(HC(hid.threads,THREAD_REAP,thread,0).status)return CB_RESOURCE_FAILURE;
        l->thread=0;
    }
    flags=hlock();
    while(l->users){if(!wait){hunlock(flags);
            return CB_BUSY;
        }
        l->closer=HC(hid.threads,THREAD_CURRENT,0,0).value;
        hunlock(flags);
        if(HC(hid.threads,THREAD_BLOCK,0,0).status)return CB_RESOURCE_FAILURE;
        flags=hlock();
    }
    l->closer=0;
    int last=s!=NULL;
    for(struct hid_link *p=hid.links;p;p=p->next)if(p!=l && p->source==s){last=0;
        break;
    }
    hunlock(flags);
    if(last){cb_status_t status=source_close(s,wait);
        if(status)return status;
        l->source=NULL;
    }
    (void)HC(hid.unsubscribe,l->pid,l->id,0);
    flags=hlock();
    struct hid_link **at=&hid.links;
    while(*at && *at!=l)at=&(*at)->next;
    if(*at)*at=l->next;
    hunlock(flags);
    hfree(l);
    return CB_OK;
}
static cb_status_t hclose(struct hid_link *l,int wait){if(!lifecycle_enter(wait))return CB_BUSY;
    cb_status_t status=hclose_inner(l,wait);
    lifecycle_leave();
    return status;
}
static cb_status_t hstart(struct hid_link *l){
    if(l->started)return CB_BUSY;
    const char *name=l->watcher?"hid.report_worker":"hid.capture_worker";
    kernel_surface_call entry=kernel_surface_find(hid.hub,name);
    if(!entry)return CB_UNAVAILABLE;
    struct hid_source *s=l->source;
    if(s && (s->dead || s->stop))return CB_UNAVAILABLE;
    l->started=1;
    if(s && s->started)return CB_OK;
    struct thread_request request={.abi=1,.cpu=0,.stack_bytes=32768,.entry=(uintptr_t)entry,.major=1,.payload={.arguments={s?(uintptr_t)s:(uintptr_t)l}
        }
    };
    struct kernel_surface_result r=HC(hid.create,&request,0,0);
    if(r.status){l->started=0;
        return CB_RESOURCE_FAILURE;
    }
    if(s){s->thread=r.value;
        s->started=1;
    }
    else l->thread=r.value;
    return CB_OK;
}
static cb_status_t hlist(struct hid_link *sub,const struct cb_io_hid_list *request,struct cb_io_hid_device *out,int *found){
    const struct kernel_hub_table *entry=kernel_hub_find_table(hid.hub,USB_TABLE_NAME);
    if(!entry)return CB_UNAVAILABLE;
    const struct usb_table *table=(void *)entry->address;
    uint64_t seen=request->device;
    int same=1;
    *found=0;
    for(;;){struct usb_device device={0};
        uint64_t flags=hlock();
        for(struct usb_device *p=table->first;p;p=p->next)if((p->identity.id>seen || (same && p->identity.id==seen)) && (!device.identity.id || p->identity.id<device.identity.id))device=*p;
        hunlock(flags);
        if(!device.identity.id)return CB_OK;
        seen=device.identity.id;
        unsigned start=same && seen==request->device?request->ordinal:0;
        same=0;
        if(device.state!=USB_CONFIGURED)continue;
        struct usb_ref ref={device.identity.id,device.identity.generation};
        for(unsigned i=start;i<device.interfaces;++i){struct usb_interface interface;
            if(HC(hid.interface,&ref,i,&interface).status || interface.alternate || interface.class_code!=3)continue;
            struct hid_source *s=hsource(ref,interface.number);
            if(interface.owner && (!s || interface.owner!=(uintptr_t)s))continue;
            if(s && (s->dead || s->stop || !(s->classes&sub->classes)))continue;
            *out=(struct cb_io_hid_device){.device=ref.id,.generation=ref.generation,.interface=interface.number,.ordinal=i+1,.vendor=device.vendor,.product=device.product,.subclass=interface.subclass,.protocol=interface.protocol};
            *found=1;
            return CB_OK;
        }
    }
}
static cb_status_t source_open(struct usb_ref ref,struct usb_interface interface,struct hid_source **out){
    struct usb_endpoint endpoint;
    unsigned ep;
    for(ep=129;ep<144;++ep)if(!hid.endpoint((uintptr_t)&ref,interface.number,ep,(uintptr_t)&endpoint,0,0).status && endpoint.transfer_type==3 && endpoint.supported && endpoint.max_packet && (endpoint.max_esit?endpoint.max_esit:endpoint.max_packet)<=CB_HID_LIMIT)break;
    if(ep==144)return CB_UNAVAILABLE;
    unsigned char *config=halloc(CB_HID_LIMIT);
    if(!config)return CB_RESOURCE_FAILURE;
    size_t bytes=0;
    struct kernel_surface_result got=hid.configuration((uintptr_t)&ref,0,(uintptr_t)config,CB_HID_LIMIT,(uintptr_t)&bytes,0);
    unsigned descriptor_bytes=0;
    int selected=0;
    if(!got.status && bytes<=CB_HID_LIMIT)for(size_t at=0;at+2<=bytes;){unsigned len=config[at];
        if(len<2 || len>bytes-at)break;
        if(config[at+1]==4)selected=len>=9 && config[at+2]==interface.number && !config[at+3];
        else if(selected && config[at+1]==0x21 && len>=6 && 6u+3u*config[at+5]<=len)for(unsigned j=0;j<config[at+5];++j){size_t p=at+6+j*3;
            if(config[p]==0x22)descriptor_bytes=config[p+1]|((unsigned)config[p+2]<<8);
        }
        at+=len;
    }
    hfree(config);
    if(!descriptor_bytes || descriptor_bytes>CB_HID_LIMIT)return CB_UNAVAILABLE;
    struct hid_source *s=halloc(sizeof(*s));
    if(!s)return CB_RESOURCE_FAILURE;
    s->ref=ref;
    s->interface=interface.number;
    s->endpoint=ep;
    s->subclass=interface.subclass;
    s->descriptor_bytes=descriptor_bytes;
    s->descriptor=halloc(descriptor_bytes);
    cb_status_t status=CB_RESOURCE_FAILURE;
    if(!s->descriptor)goto failed;
    if(HC(hid.claim,&ref,s->interface,s).status){status=CB_BUSY;
        goto failed;
    }
    s->claimed=1;
    struct usb_io control={.device=ref,.owner=(uintptr_t)s,.interface=s->interface,.bytes=descriptor_bytes,.buffer=s->descriptor,.setup={0x81,6,0,0x22,s->interface,0,descriptor_bytes&255,descriptor_bytes>>8}
    };
    struct host_transfer_result result={0};
    if(HC(hid.control,&control,&result,0).status || result.state!=HOST_COMPLETE || result.bytes!=descriptor_bytes){status=CB_UNAVAILABLE;
        goto failed;
    }
    if(!hid_report_parse(&s->route,s->descriptor,descriptor_bytes,parser_alloc,parser_free,NULL,NULL,NULL)){status=CB_UNAVAILABLE;
        goto failed;
    }
    for(unsigned id=0;id<256;++id)s->classes|=s->route.classes[0][id];
    if(!s->classes){status=CB_UNAVAILABLE;
        goto failed;
    }
    s->report_bytes=s->route.input_bytes;
    s->report=halloc(s->report_bytes);
    if(!s->report)goto failed;
    if(s->subclass==1){control.bytes=0;
        control.buffer=NULL;
        unsigned char setup[8]={0x21,0x0b,1,0,s->interface,0,0,0};
        hcopy(control.setup,setup,8);
        if(HC(hid.control,&control,&result,0).status){status=CB_UNAVAILABLE;
            goto failed;
        }
        control.setup[1]=0x0a;
        control.setup[2]=0;
        (void)HC(hid.control,&control,&result,0);
    }
    {uint64_t flags=hlock();
        s->next=hid.sources;
        hid.sources=s;
        hunlock(flags);
    }
    *out=s;
    return CB_OK;
    failed:/* Retain uncertain ownership in the source list for retryable cleanup. */
    if(!hrelease(s)){s->dead=1;
        uint64_t flags=hlock();
        s->next=hid.sources;
        hid.sources=s;
        hunlock(flags);
        return CB_RESOURCE_FAILURE;
    }
    hfree(s->descriptor);
    hfree(s->report);
    hfree(s);
    return status;
}
static cb_status_t hopen(struct hid_link *sub,const struct cb_io_hid_open *request,struct cb_io_hid_opened *out){
    struct usb_ref ref={request->device,request->generation};
    struct usb_device device;
    struct usb_interface interface;
    int found=0;
    if(request->reserved || request->interface>255 || HC(hid.device,&ref,&device,0).status || device.state!=USB_CONFIGURED)return CB_BAD_REQUEST;
    for(unsigned i=0;i<device.interfaces;++i)if(!HC(hid.interface,&ref,i,&interface).status && !interface.alternate && interface.number==request->interface && interface.class_code==3){found=1;
        break;
    }
    if(!found)return CB_BAD_REQUEST;
    if(!lifecycle_enter(1))return CB_BUSY;
    struct hid_source *s=hsource(ref,request->interface);
    cb_status_t status=CB_OK;
    int fresh=!s;
    if(!s)status=source_open(ref,interface,&s);
    if(status)goto done;
    if(s->dead || s->stop || !(s->classes&sub->classes)){status=CB_UNAVAILABLE;
        goto unused;
    }
    for(struct hid_link *p=hid.links;p;p=p->next)if(p->source==s && p->pid==sub->pid){status=CB_BUSY;
        goto done;
    }
    {struct hid_link *l=hnew(sub->pid);
        if(!l){status=CB_RESOURCE_FAILURE;
            goto unused;
        }
        l->parent=sub->id;
        l->source=s;
        *out=(struct cb_io_hid_opened){l->id,s->descriptor_bytes,s->report_bytes};
        if(fresh)changed();
    }
    goto done;
    unused:if(fresh && source_close(s,1)!=CB_OK)status=CB_RESOURCE_FAILURE;
    done:lifecycle_leave();
    return status;
}
int hid_reports_invoke(struct io_invocation *r){
    if(r->operation<CB_IO_HID_SUBSCRIBE || r->operation>CB_IO_HID_SET_FEATURE)return 0;
    r->status=CB_BAD_REQUEST;
    r->actual=0;
    struct process_info process;
    if((r->process!=hid.pid && r->process!=hid.keyboard_pid) || HC(hid.process_info,r->process,&process,0).status || process.cpu || HC(hid.cpu,CPU_CURRENT,0,0).value){r->status=CB_DENIED;
        return 1;
    }
    if(r->operation==CB_IO_HID_SUBSCRIBE){if(r->bytes || r->capacity<8)return 1;
        for(struct hid_link *p=hid.links;p;p=p->next)if(p->pid==r->process && p->watcher){r->status=CB_BUSY;
            return 1;
        }
        struct hid_link *l=hnew(r->process);
        if(!l){r->status=CB_RESOURCE_FAILURE;
            return 1;
        }
        l->watcher=1;
        l->creator=HC(hid.threads,THREAD_CURRENT,0,0).value;
        r->status=hstart(l);
        if(!r->status){while(!__atomic_load_n(&l->ready,__ATOMIC_ACQUIRE))if(HC(hid.threads,THREAD_BLOCK,0,0).status){r->status=CB_RESOURCE_FAILURE;
                break;
            }
            if(l->ready!=1)r->status=CB_RESOURCE_FAILURE;
        }
        if(r->status){(void)hclose(l,1);
            return 1;
        }
        hcopy(r->output,&l->id,8);
        r->actual=8;
        return 1;
    }
    if(r->bytes<8)return 1;
    uint64_t id;
    hcopy(&id,r->input,8);
    struct hid_link *l=hfind(r->process,id);
    if(!l || (hstopped(l) && r->operation!=CB_IO_HID_CLOSE)){r->status=CB_BAD_HANDLE;
        return 1;
    }
    struct hid_source *s=l->source;
    if(r->operation==CB_IO_HID_LIST){if(!l->watcher || r->bytes!=sizeof(struct cb_io_hid_list) || r->capacity<sizeof(struct cb_io_hid_device))return 1;
        struct cb_io_hid_list request;
        hcopy(&request,r->input,sizeof(request));
        if(request.reserved)return 1;
        struct cb_io_hid_device item;
        int found;
        r->status=hlist(l,&request,&item,&found);
        if(!r->status && found){hcopy(r->output,&item,sizeof(item));
            r->actual=sizeof(item);
        }
        return 1;
    }
    if(r->operation==CB_IO_HID_OPEN){if(!l->watcher || r->bytes!=sizeof(struct cb_io_hid_open) || r->capacity<sizeof(struct cb_io_hid_opened))return 1;
        struct cb_io_hid_open request;
        hcopy(&request,r->input,sizeof(request));
        struct cb_io_hid_opened opened;
        r->status=hopen(l,&request,&opened);
        if(!r->status){hcopy(r->output,&opened,sizeof(opened));
            r->actual=sizeof(opened);
        }
        return 1;
    }
    if(r->operation==CB_IO_HID_DESCRIPTOR){if(!s || r->bytes!=sizeof(struct cb_io_hid_data))return 1;
        struct cb_io_hid_data request;
        hcopy(&request,r->input,sizeof(request));
        if(request.offset>s->descriptor_bytes || request.bytes>s->descriptor_bytes-request.offset || request.bytes>r->capacity)return 1;
        hcopy(r->output,s->descriptor+request.offset,request.bytes);
        r->actual=request.bytes;
        r->status=CB_OK;
        return 1;
    }
    if(r->operation==CB_IO_HID_GET_FEATURE || r->operation==CB_IO_HID_SET_FEATURE){if(!s || s->dead || l->started || r->bytes<sizeof(struct cb_io_hid_feature))return 1;
        struct cb_io_hid_feature request;
        hcopy(&request,r->input,sizeof(request));
        int write=r->operation==CB_IO_HID_SET_FEATURE;
        if(request.report_id>255 || !request.bytes || request.bytes>CB_HID_LIMIT || r->bytes!=sizeof(request)+(write?request.bytes:0) || (!write && r->capacity<request.bytes) || !(s->route.classes[2][request.report_id]&l->classes) || request.bytes!=(s->route.bits[2][request.report_id]+7)/8+(s->route.numbered?1:0))return 1;
        unsigned char *buffer=halloc(request.bytes);
        if(!buffer){r->status=CB_RESOURCE_FAILURE;
            return 1;
        }
        if(write)hcopy(buffer,(const unsigned char *)r->input+sizeof(request),request.bytes);
        struct usb_io control={.device=s->ref,.owner=(uintptr_t)s,.interface=s->interface,.bytes=request.bytes,.buffer=buffer,.setup={write?0x21:0xa1,write?9:1,request.report_id,3,s->interface,0,request.bytes&255,request.bytes>>8}
        };
        struct host_transfer_result result={0};
        struct kernel_surface_result done=HC(hid.control,&control,&result,0);
        r->status=done.status || result.state!=HOST_COMPLETE || result.bytes!=request.bytes?CB_UNAVAILABLE:CB_OK;
        if(!r->status && !write){hcopy(r->output,buffer,request.bytes);
            r->actual=request.bytes;
        }
        hfree(buffer);
        return 1;
    }
    if(r->operation==CB_IO_HID_START){if(!s || l->started || r->bytes!=sizeof(struct cb_io_hid_capture))return 1;
        struct cb_io_hid_capture capture;
        hcopy(&capture,r->input,sizeof(capture));
        if(capture.reserved || capture.bytes!=s->report_bytes)return 1;
        r->status=hstart(l);
        return 1;
    }
    if(r->bytes!=sizeof(struct cb_io_subscription))return 1;
    if(r->operation==CB_IO_HID_ACK){if(l->failed || (s && s->failed)){r->status=CB_PROVIDER_FAILED;return 1;}uint64_t flags=hlock();
        if(l->waiting){l->waiting=0;
            __atomic_store_n(&l->ack,1,__ATOMIC_RELEASE);
            uint64_t thread=s?s->thread:l->thread;
            if(thread)HC(hid.threads,THREAD_WAKE,thread,0);
            r->status=CB_OK;
        }
        hunlock(flags);
        return 1;
    }
    if(r->operation==CB_IO_HID_CLOSE){if(l->watcher)for(;;){struct hid_link *p=hid.links;
            while(p && p->parent!=l->id)p=p->next;
            if(!p)break;
            r->status=hclose(p,1);
            if(r->status)return 1;
        }
        r->status=hclose(l,1);
        return 1;
    }
    return 1;
}
cb_status_t hid_reports_exit(uint64_t pid){for(;;){struct hid_link *l=hid.links;
        while(l && l->pid!=pid)l=l->next;
        if(!l)break;
        cb_status_t status=hclose(l,0);
        if(status)return status;
    }
    if(hid.pid==pid)hid.pid=0;
    if(hid.keyboard_pid==pid)hid.keyboard_pid=0;
    return CB_OK;
}
struct kernel_surface_result hid_reports_info(uint64_t pid,uintptr_t output){
    if(!output)return module_result(0,0);
    uint64_t flags=hlock();
    struct hid_link *l=hid.links;
    while(l && (l->pid!=pid || !l->source || hstopped(l)))l=l->next;
    if(l){struct hid_source *s=l->source;
        *(struct io_provider_info *)output=(struct io_provider_info){s->thread,l->id,s->ref.id,s->ref.generation,s->active,s->claimed,l->stop,l->waiting};
    }
    hunlock(flags);
    return module_result(l!=NULL,l!=NULL);
}
