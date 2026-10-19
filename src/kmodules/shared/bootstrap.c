#include "bootstrap.h"
#include "bootstrap-apps.h"
#include "bootstrap-load.h"
#include "boot-origin.h"
#include "block-util.h"
#include "config.h"
#include "partition.h"
#include "nvme.h"
#include "sata.h"
#include "usb-storage.h"
#include "hwenum.h"
#include "threads.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
struct candidate {struct candidate *next;struct block_device disk,part;uint64_t cookie,index;uint32_t transport,nsid,port;struct usb_ref usb;uint8_t disk_guid[16];};
static struct {const struct kernel_surfaces *hub;uintptr_t heap;uint64_t worker;struct bootstrap_info info;struct candidate *first;
    kernel_surface_call cpu,thread,clock,irq,alloc,free,select,identity,close_part,mount,fsinfo;
} state;
static void *allocate(size_t bytes){struct kernel_surface_result r=CALL(state.alloc,state.heap,1,bytes);return r.status?NULL:(void *)r.value;}
static uint64_t lock(void){return CALL(state.cpu,CPU_ENTER,0,0).value;}
static void unlock(uint64_t f){CALL(state.cpu,CPU_LEAVE,f,0);}
static int foreground(void){uint64_t f;__asm__ volatile("pushfq;pop %0":"=r"(f));return state.hub && (f&512) && !CALL(state.cpu,CPU_CURRENT,0,0).value && !CALL(state.cpu,CPU_DEPTH,0,0).value && !CALL(state.irq,0,0,0).value && CALL(state.thread,THREAD_CURRENT,0,0).value!=0;}
static const struct config_value *config(const char *name,unsigned type){const struct kernel_hub_table *t=kernel_hub_find_table(state.hub,name);const struct config_value *v=t?(const void *)t->address:NULL;return v && v->major==1 && v->type==type && v->data?v:NULL;}
static void log_text(const char *text){kernel_surface_call log=kernel_surface_find(state.hub,"hardware.log");size_t bytes=0;while(text[bytes])++bytes;if(log)CALL(log,text,bytes,0);}
static void report(void){
    if(state.info.error){log_text("bootstrap: driver volume unavailable: ");switch(state.info.error){case BOOTSTRAP_CONFIG:log_text("configuration");break;case BOOTSTRAP_MEMORY:log_text("memory");break;case BOOTSTRAP_TRANSPORT:log_text("transport/cleanup");break;case BOOTSTRAP_GPT:log_text("invalid GPT");break;case BOOTSTRAP_NOT_FOUND:log_text("GUID not found");break;case BOOTSTRAP_DUPLICATE:log_text("duplicate GUID");break;case BOOTSTRAP_FILESYSTEM:log_text("filesystem");break;case BOOTSTRAP_TOPOLOGY:log_text("topology changed");break;default:log_text("discovery timeout");break;}log_text("\n");}
    else {log_text(state.info.filesystem.readonly?"bootstrap: read-only driver volume ready; boot origin ":"bootstrap: writable driver volume ready; boot origin ");log_text(state.info.origin==BOOTSTRAP_ORIGIN_MATCH?"matched\n":state.info.origin==BOOTSTRAP_ORIGIN_DIFFERENT?"different\n":"unknown\n");}
}
static void fail(unsigned error){uint64_t flags=lock();state.info.error=error;unlock(flags);}
static struct candidate *record(unsigned transport,uint64_t index,unsigned sub){struct candidate *p=allocate(sizeof(*p));if(!p){fail(BOOTSTRAP_MEMORY);return NULL;}p->transport=transport;p->index=index;p->nsid=transport==BOOTSTRAP_NVME?sub:0;p->port=transport==BOOTSTRAP_SATA?sub:0;p->next=state.first;state.first=p;return p;}
static kernel_surface_call closer(unsigned transport){return kernel_surface_find(state.hub,transport==BOOTSTRAP_NVME?"nvme.close":transport==BOOTSTRAP_SATA?"sata.close":"usb_storage.close");}
static int discard(struct candidate *p){
    if(p->part.cookie && CALL(state.close_part,p->part.cookie,0,0).status)return 0;
    p->part.cookie=0;
    if(p->cookie && CALL(closer(p->transport),p->cookie,0,0).status)return 0;
    p->cookie=0;return 1;
}
static int examine(struct candidate *p,const void *guid){
    ++state.info.examined;
    uint8_t sector[4096];struct kernel_surface_result r=p->disk.read(p->disk.cookie,1,1,(uintptr_t)sector,sizeof(sector),0);if(r.status || r.value!=p->disk.block_bytes){fail(BOOTSTRAP_TRANSPORT);return 0;}
    if(!disk_equal(sector,"EFI PART",8))return 1; /* Blank/non-GPT disks are not candidates. */
    r=state.select((uintptr_t)&p->disk,(uintptr_t)guid,(uintptr_t)&p->part,sizeof(p->part),0,0);
    if(r.status){if(r.value==PARTITION_NOT_FOUND)return 1;fail(BOOTSTRAP_GPT);return 0;}
    if(CALL(state.identity,&p->disk,p->disk_guid,0).status){fail(BOOTSTRAP_GPT);return 0;}
    ++state.info.matches;return 1;
}
static int discover_pci(const void *guid){
    const struct kernel_hub_table *ref=kernel_hub_find_table(state.hub,HWENUM_TABLE_NAME);if(!ref)return 0;const struct hwenum_table *hw=(const void *)ref->address;
    kernel_surface_call no=kernel_surface_find(state.hub,"nvme.open"),nb=kernel_surface_find(state.hub,"nvme.block"),nn=kernel_surface_find(state.hub,"nvme.next"),so=kernel_surface_find(state.hub,"sata.open"),sp=kernel_surface_find(state.hub,"sata.ports");
    for(uint64_t i=0;i<hw->count;++i){const struct hwenum_descriptor *h=&hw->entries[i];if(h->kind!=HWENUM_PCI || h->data.pci.class_code!=1)continue;
        if(h->data.pci.subclass==8 && h->data.pci.programming_interface==2){
            uint32_t nsid=0;do{struct candidate *p=record(BOOTSTRAP_NVME,i,nsid);struct nvme_info info;if(!p)return 0;
                struct kernel_surface_result r=no(i,(uintptr_t)&info,nsid,(uintptr_t)&p->cookie,0,0);if(r.status){fail(BOOTSTRAP_TRANSPORT);return 0;}p->cookie=r.value;p->nsid=info.namespace_id;
                if(CALL(nb,&p->disk,sizeof(p->disk),p->cookie).status || !examine(p,guid))return 0;
                if(CALL(nn,i,info.namespace_id,&nsid).status){fail(BOOTSTRAP_TRANSPORT);return 0;}
            }while(nsid);
        }else if(h->data.pci.subclass==6 && h->data.pci.programming_interface==1){uint32_t ports=0;if(CALL(sp,i,&ports,0).status){fail(BOOTSTRAP_TRANSPORT);return 0;}
            for(unsigned port=0;port<32;++port)if(ports&(1u<<port)){struct candidate *p=record(BOOTSTRAP_SATA,i,port);if(!p)return 0;struct sata_open request={.hardware_index=i,.port=port};
                struct kernel_surface_result r=so((uintptr_t)&request,(uintptr_t)&p->disk,sizeof(p->disk),(uintptr_t)&p->cookie,0,0);if(r.status){fail(BOOTSTRAP_TRANSPORT);return 0;}p->cookie=r.value;if(!examine(p,guid))return 0;}
        }
    }return 1;
}
static int discover_usb(const void *guid){
    const struct kernel_hub_table *ref=kernel_hub_find_table(state.hub,USB_TABLE_NAME);if(!ref)return 0;const struct usb_table *table=(const void *)ref->address;
    kernel_surface_call inspect=kernel_surface_find(state.hub,"usb.interface"),open=kernel_surface_find(state.hub,"usb_storage.open");
    const struct usb_device *d=table->first;
    while(d){uint64_t flags=lock();struct usb_device snapshot=*d;d=d->next;unlock(flags);if(snapshot.state!=USB_CONFIGURED || snapshot.hub || snapshot.class_code==9)continue;
        struct usb_ref dev={snapshot.identity.id,snapshot.identity.generation};
        for(unsigned ordinal=0;ordinal<snapshot.interfaces;++ordinal){struct usb_interface intf;if(CALL(inspect,&dev,ordinal,&intf).status){fail(BOOTSTRAP_TOPOLOGY);return 0;}if(intf.class_code!=8 || intf.subclass!=6 || intf.protocol!=0x50)continue;
            struct candidate *p=record(BOOTSTRAP_USB,snapshot.controller,0);if(!p)return 0;p->usb=dev;struct usb_storage_open request={.device=dev,.interface=intf.number};
            struct kernel_surface_result r=CALL(open,&request,&p->disk,sizeof(p->disk));p->cookie=p->disk.cookie;if(r.status){fail(BOOTSTRAP_TRANSPORT);return 0;}if(!examine(p,guid))return 0;
        }
    }return 1;
}
static int ready(uint64_t *changes){
    kernel_surface_call usb=kernel_surface_find(state.hub,"usb.ready"),hubs=kernel_surface_find(state.hub,"usb_hub.ready");const struct config_value *timeout=config("config.storage.timeout_ms",CONFIG_UINT);
    if(!usb || !hubs || !timeout || timeout->bytes!=8)return 0;
    uint64_t start=CALL(state.clock,0,0,0).value,span=*(const uint64_t *)timeout->data*1000000;
    /* Initial discovery includes firmware handoff and hub debounce/reset. */
    if(span<UINT64_C(10000000000))span=UINT64_C(10000000000);
    if(start>UINT64_MAX-span)return 0;
    uint64_t end=start+span;
    for(;;){struct kernel_surface_result a=CALL(usb,0,0,0),b=CALL(hubs,0,0,0);if(a.status || b.status)return 0;if(a.value && b.value){const struct kernel_hub_table *t=kernel_hub_find_table(state.hub,USB_TABLE_NAME);uint64_t f=lock();*changes=((const struct usb_table *)t->address)->changes;unlock(f);return 1;}
        uint64_t time=CALL(state.clock,0,0,0).value;if(time>=end || time>UINT64_MAX-1000000 || CALL(state.thread,THREAD_BLOCK_UNTIL,time+1000000,0).status)return 0;}
}
struct kernel_surface_result bootstrap_surface_select(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground())return module_result(0,0);uint64_t flags=lock();if(state.info.state!=BOOTSTRAP_IDLE){unlock(flags);return module_result(0,0);}state.info.state=BOOTSTRAP_DISCOVERING;unlock(flags);
    const struct config_value *guid=config("config.drivers.partition_guid",CONFIG_GUID);uint64_t changes=0;struct candidate *chosen=NULL;
    if(!guid || guid->bytes!=16){fail(BOOTSTRAP_CONFIG);goto done;}disk_copy(state.info.partition_guid,guid->data,16);
    if(!ready(&changes)){fail(BOOTSTRAP_TIMEOUT);goto done;}
    if(!discover_pci(guid->data) || !discover_usb(guid->data)){if(!state.info.error)fail(BOOTSTRAP_TRANSPORT);goto done;}
    const struct kernel_hub_table *usb=kernel_hub_find_table(state.hub,USB_TABLE_NAME);if(((const struct usb_table *)usb->address)->changes!=changes){fail(BOOTSTRAP_TOPOLOGY);goto done;}
    if(state.info.matches!=1){fail(state.info.matches?BOOTSTRAP_DUPLICATE:BOOTSTRAP_NOT_FOUND);goto done;}
    for(chosen=state.first;chosen && !chosen->part.cookie;chosen=chosen->next){}
    const struct config_value *write_config=config("config.filesystem.writable",CONFIG_BOOL);
    unsigned writable=write_config && *(const uint8_t *)write_config->data && chosen && block_writable(&chosen->part);
    if(!chosen || CALL(state.mount,&chosen->part,&state.info.filesystem,writable?EXT4_MOUNT_WRITE:0).status){fail(BOOTSTRAP_FILESYSTEM);chosen=NULL;goto done;}
    state.info.disk=chosen->disk;state.info.partition=chosen->part;state.info.transport=chosen->transport;state.info.hardware_index=chosen->transport==BOOTSTRAP_USB?UINT64_MAX:chosen->index;state.info.usb_controller=chosen->transport==BOOTSTRAP_USB?chosen->index:0;state.info.namespace_id=chosen->nsid;state.info.port=chosen->port;state.info.usb=chosen->usb;disk_copy(state.info.disk_guid,chosen->disk_guid,16);
    const struct kernel_hub_table *o=kernel_hub_find_table(state.hub,"boot.origin");const struct boot_origin *origin=o?(const void *)o->address:NULL;uint8_t zero[16]={0};
    if(origin && origin->present && !disk_equal(origin->disk_guid,zero,16))state.info.origin=disk_equal(origin->disk_guid,chosen->disk_guid,16)?BOOTSTRAP_ORIGIN_MATCH:BOOTSTRAP_ORIGIN_DIFFERENT;
done:
    for(struct candidate *p=state.first;p;p=p->next)if(p!=chosen){unsigned retry=0;while(!discard(p) && retry++<128)CALL(state.thread,THREAD_YIELD,0,0);if(p->cookie || p->part.cookie)fail(BOOTSTRAP_TRANSPORT);}
    /* Keep selected and uncertain cleanup records; free completed bookkeeping. */
    struct candidate **link=&state.first;while(*link){struct candidate *p=*link;if(p==chosen || p->cookie || p->part.cookie){link=&p->next;continue;}*link=p->next;CALL(state.free,state.heap,p,0);}
    flags=lock();state.info.state=state.info.error?BOOTSTRAP_FAILED:BOOTSTRAP_READY;unlock(flags);report();if(!state.info.error)(void)bootstrap_surface_load(0,0,0,0,0,0);return module_result(state.info.error,!state.info.error);
}
struct kernel_surface_result bootstrap_surface_init(MODULE_ARGS){
    MODULE_UNUSED();const struct kernel_surfaces *hub=(void *)a;if(state.hub)return module_result(1,hub==state.hub && b==state.heap);if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC || !b)return module_result(0,0);
#define BIND(field,name) state.field=kernel_surface_find(hub,name);if(!state.field)return module_result(0,0)
    BIND(cpu,"cpu.control");BIND(thread,"threads.control");BIND(clock,"clock.now_ns");BIND(irq,"irq.context");BIND(alloc,"mem.calloc");BIND(free,"mem.free");BIND(select,"partition.find");BIND(identity,"partition.identity");BIND(close_part,"partition.close");BIND(mount,"ext4_fs.mount");BIND(fsinfo,"ext4_fs.info");
#undef BIND
    const char required[][32]={"nvme.open","nvme.block","nvme.next","nvme.close","sata.open","sata.ports","sata.close","usb.interface","usb_storage.open","usb_storage.close","usb.ready","usb_hub.ready","threads.create"};
    for(size_t i=0;i<sizeof(required)/sizeof(required[0]);++i){uint16_t minor=i==0 || i==1 || i==4?2:0;if(!kernel_surface_find_version(hub,required[i],1,minor))return module_result(0,0);}
    state.hub=hub;state.heap=b;struct kernel_hub_table table={.name="bootstrap.volume",.address=(uintptr_t)&state.info,.bytes=sizeof(state.info),.count=1,.entry_bytes=sizeof(state.info),.owner=kernel_surface_owner(hub,"bootstrap.init")};kernel_surface_call publish=kernel_surface_find(hub,"hub.publish_table");if(!publish || CALL(publish,&table,0,0).status || !bootstrap_loader_init(hub,b) || !bootstrap_apps_init(hub,b))return module_result(0,0);return module_result(1,1);
}
struct kernel_surface_result bootstrap_surface_start(MODULE_ARGS){
    MODULE_UNUSED();if(!state.hub || CALL(state.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);if(state.worker || state.info.state!=BOOTSTRAP_IDLE)return module_result(state.worker,1);
    kernel_surface_call create=kernel_surface_find(state.hub,"threads.create"),entry=kernel_surface_find(state.hub,"bootstrap.select");if(!create || !entry)return module_result(0,0);
    struct thread_request r={.abi=1,.cpu=0,.stack_bytes=32768,.entry=(uintptr_t)entry,.major=1};struct kernel_surface_result result=CALL(create,&r,0,0);if(!result.status)state.worker=result.value;return result;
}
struct kernel_surface_result bootstrap_surface_info(MODULE_ARGS){
    MODULE_UNUSED();if(!state.hub || !a || CALL(state.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);
    uint64_t flags=lock();if(state.info.state==BOOTSTRAP_READY){struct ext4_info info;if(CALL(state.fsinfo,state.info.filesystem.cookie,&info,0).status)state.info.state=BOOTSTRAP_DISCONNECTED;}*(struct bootstrap_info *)a=state.info;uint64_t worker=state.worker;unlock(flags);
    uint64_t interrupts;__asm__ volatile("pushfq;pop %0":"=r"(interrupts));
    if(worker && (interrupts&512) && !CALL(state.cpu,CPU_DEPTH,0,0).value){struct thread_info t;if(!CALL(state.thread,THREAD_INFO,worker,&t).status && t.state==THREAD_EXITED && !CALL(state.thread,THREAD_REAP,worker,0).status)state.worker=0;}
    return module_result(1,1);
}
