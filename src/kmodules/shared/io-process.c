#include "io-process.h"
#include "process.h"
#include "threads.h"
#include "handles.h"
#include "../../interfaces/startup.h"
#include "../../interfaces/installations.h"
#include "../../kernel/cpu.h"
struct child {struct child *next;uint64_t process,worker,channel,thread,owner;unsigned published,cancelled,reaped;};
static struct {const struct kernel_surfaces *hub;uintptr_t heap;unsigned char *catalog;size_t bytes;uint64_t filesystem,manager,serial;struct io_process_info stats;struct child *children;
    kernel_surface_call alloc,free,cpu,load,start,stop,info,reap,admit,create,control,channel,post,unsubscribe;} runtime;
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static uint64_t lock(void){return CALL(runtime.cpu,CPU_ENTER,0,0).value;}
static void unlock(uint64_t flags){CALL(runtime.cpu,CPU_LEAVE,flags,0);}
static void *allocate(size_t bytes){struct kernel_surface_result r=CALL(runtime.alloc,runtime.heap,1,bytes);return r.status?NULL:(void *)r.value;}
static void release(void *p){if(p)CALL(runtime.free,runtime.heap,p,0);}
static size_t length(const char *p){size_t n=0;while(p[n])++n;return n;}
int io_process_init(const struct kernel_surfaces *hub,uintptr_t heap){
    if(runtime.hub)return runtime.hub==hub;
#define BIND(field,name) do{runtime.field=kernel_surface_find(hub,name);if(!runtime.field)return 0;}while(0)
    BIND(alloc,"mem.calloc");BIND(free,"mem.free");BIND(cpu,"cpu.control");BIND(load,"process.load");BIND(start,"process.start");BIND(stop,"process.stop");BIND(info,"process.info");BIND(reap,"process.reap");BIND(admit,"handles.admit");BIND(create,"threads.create");BIND(control,"threads.control");BIND(channel,"handles.channel");BIND(post,"handles.post");BIND(unsubscribe,"handles.unsubscribe");
#undef BIND
    runtime.hub=hub;runtime.heap=heap;return 1;
}
struct kernel_surface_result io_surface_installations(MODULE_ARGS){
    MODULE_UNUSED();if(!runtime.hub || !a || !b || !cb_install_valid((const void *)b,c))return module_result(0,0);
    if(runtime.catalog)return module_result(1,runtime.filesystem==a && runtime.bytes==c && cb_install_equal(runtime.catalog,(const void *)b,c));
    unsigned char *catalog=allocate(c);if(!catalog)return module_result(0,0);cb_install_copy(catalog,(const void *)b,c);runtime.catalog=catalog;runtime.bytes=c;runtime.filesystem=a;return module_result(1,1);
}
/* Trusted kernel entry. Ordinary callers can only reach the manager's narrower
 * process_launch operation, which cannot load provider/manager records. */
struct kernel_surface_result io_surface_installed_load(MODULE_ARGS){
    MODULE_UNUSED();if(!runtime.catalog || !a || !b || !c || CALL(runtime.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);
    struct cb_install_record record;const unsigned char *name=cb_install_find(runtime.catalog,(const void *)a,length((const void *)a),NULL,&record);if(!name || (record.flags==CB_INSTALL_MANAGER && runtime.manager))return module_result(0,0);
    char *path=allocate(record.path_bytes+1);if(!path)return module_result(0,0);cb_install_copy(path,name+record.name_bytes,record.path_bytes);
    struct process_request request={.abi=2,.filesystem=runtime.filesystem,.path=path,.cpu=record.cpu,.stack_bytes=record.stack_bytes,.startup=(const void *)b,.startup_bytes=c};
    struct kernel_surface_result loaded=CALL(runtime.load,&request,0,0);release(path);if(loaded.status){if(loaded.value)(void)CALL(runtime.reap,loaded.value,0,0);return module_result(0,0);}
    struct handle_policy policy={.process=loaded.value,.manifest=name+record.name_bytes+record.path_bytes,.bytes=record.manifest_bytes};cb_install_copy(policy.identity.installation_id,record.installation,16);
    if(CALL(runtime.admit,&policy,0,0).status){(void)CALL(runtime.reap,loaded.value,0,0);return module_result(0,0);}
    if(record.flags==CB_INSTALL_MANAGER)runtime.manager=loaded.value;
    return module_result(loaded.value,1);
}
struct kernel_surface_result io_surface_process_worker(MODULE_ARGS){
    MODULE_UNUSED();struct child *child=(void *)a;if(!child)return module_result(0,0);
    while(!__atomic_load_n(&child->published,__ATOMIC_ACQUIRE))if(CALL(runtime.control,THREAD_BLOCK,0,0).status)return module_result(0,0);
    if(child->cancelled)return module_result(1,1);
    if(CALL(runtime.control,THREAD_WAIT,child->thread,0).status)return module_result(0,0);
    struct process_info info;if(CALL(runtime.info,child->process,&info,0).status)return module_result(0,0);
    struct cb_io_process_event event={.kind=CB_IO_PROCESS_COMPLETE,.owner=__atomic_load_n(&child->owner,__ATOMIC_ACQUIRE),.status={.process=child->process,.exit_code=info.exit_code,.fault_vector=info.fault_vector,.state=info.state==PROCESS_FAULTED?CB_APPLICATION_FAULTED:CB_APPLICATION_EXITED,.stop_requested=info.stop_requested}};
    const struct cb_interface_id interface=CB_IO_INTERFACE_ID;
    (void)runtime.post(runtime.manager,child->channel,(uintptr_t)&event,sizeof(event),(uintptr_t)&interface,CB_IO_PROCESS_EVENT);
    return module_result(1,1);
}
static cb_status_t dispose(struct child *child,int wait){
    if(child->worker){
        if(wait && CALL(runtime.control,THREAD_WAIT,child->worker,0).status)return CB_RESOURCE_FAILURE;
        struct thread_info worker;if(CALL(runtime.control,THREAD_INFO,child->worker,&worker).status || worker.state!=THREAD_EXITED)return CB_BUSY;
    }
    if(!child->reaped){struct process_info info;if(CALL(runtime.info,child->process,&info,0).status || CALL(runtime.reap,child->process,0,0).status)return CB_BUSY;
        uint64_t flags=lock();++runtime.stats.reaped;runtime.stats.last_exit=info.exit_code;if(info.state==PROCESS_FAULTED)++runtime.stats.faulted;unlock(flags);
    }
    child->reaped=1;
    if(child->worker && CALL(runtime.control,THREAD_REAP,child->worker,0).status)return CB_BUSY;
    child->worker=0;(void)CALL(runtime.unsubscribe,runtime.manager,child->channel,0);return CB_OK;
}
static void release_child(struct child *child){
    (void)CALL(runtime.unsubscribe,runtime.manager,child->channel+1,0);
    uint64_t flags=lock();struct child **at=&runtime.children;while(*at!=child)at=&(*at)->next;*at=child->next;unlock(flags);release(child);
}
static cb_status_t launch(struct io_invocation *invocation){
    if(invocation->bytes<8 || !cb_application_launch_valid((const unsigned char *)invocation->input+8,invocation->bytes-8) || invocation->capacity<8)return CB_BAD_REQUEST;
    uint64_t owner;cb_install_copy(&owner,invocation->input,8);struct process_info owner_info;if(owner && CALL(runtime.info,owner,&owner_info,0).status)return CB_BAD_REQUEST;
    struct cb_application_launch h;const unsigned char *payload=(const unsigned char *)invocation->input+8;size_t payload_bytes=invocation->bytes-8;cb_install_copy(&h,payload,sizeof(h));
    struct cb_install_record record;if(!cb_install_find(runtime.catalog,payload+sizeof(h),h.name_bytes,NULL,&record) || !(record.flags&CB_INSTALL_APPLICATION))return CB_DENIED;
    char *name=allocate(h.name_bytes+1);struct child *child=allocate(sizeof(*child));size_t total=sizeof(struct cb_startup)+sizeof(struct cb_startup_file)+11+payload_bytes;unsigned char *packet=allocate(total);
    if(!name || !child || !packet){release(name);release(child);release(packet);return CB_RESOURCE_FAILURE;}
    cb_install_copy(name,payload+sizeof(h),h.name_bytes);struct cb_startup startup={.version={sizeof(startup),1,0},.bytes=total,.files=1};struct cb_startup_file file={11,payload_bytes};cb_install_copy(packet,&startup,sizeof(startup));cb_install_copy(packet+sizeof(startup),&file,sizeof(file));cb_install_copy(packet+sizeof(startup)+sizeof(file),"application",11);cb_install_copy(packet+sizeof(startup)+sizeof(file)+11,payload,payload_bytes);
    struct kernel_surface_result loaded=io_surface_installed_load((uintptr_t)name,(uintptr_t)packet,total,0,0,0);release(name);release(packet);if(loaded.status){release(child);return CB_RESOURCE_FAILURE;}child->process=loaded.value;child->owner=owner;
    uint64_t flags=lock();if(runtime.serial>UINT64_MAX/2-2){unlock(flags);CALL(runtime.reap,child->process,0,0);release(child);return CB_RESOURCE_FAILURE;}runtime.serial+=2;child->channel=(UINT64_C(1)<<63)|(runtime.serial-1);unlock(flags);
    if(CALL(runtime.channel,runtime.manager,child->channel,sizeof(struct cb_io_process_event)).status || CALL(runtime.channel,runtime.manager,child->channel+1,sizeof(struct cb_io_process_event)).status){CALL(runtime.unsubscribe,runtime.manager,child->channel,0);CALL(runtime.reap,child->process,0,0);release(child);return CB_RESOURCE_FAILURE;}
    struct thread_request worker={.abi=1,.cpu=0,.stack_bytes=32768,.entry=(uintptr_t)kernel_surface_find(runtime.hub,"io.process_worker"),.major=1,.payload={.arguments={(uintptr_t)child}}};struct kernel_surface_result created=CALL(runtime.create,&worker,0,0);
    if(created.status){CALL(runtime.unsubscribe,runtime.manager,child->channel,0);CALL(runtime.unsubscribe,runtime.manager,child->channel+1,0);CALL(runtime.reap,child->process,0,0);release(child);return CB_RESOURCE_FAILURE;}child->worker=created.value;
    flags=lock();child->next=runtime.children;runtime.children=child;unlock(flags);
    struct kernel_surface_result started=CALL(runtime.start,child->process,0,0);struct process_info info;
    if(started.status || CALL(runtime.info,child->process,&info,0).status){child->cancelled=1;__atomic_store_n(&child->published,1,__ATOMIC_RELEASE);CALL(runtime.control,THREAD_WAKE,child->worker,0);if(dispose(child,1)==CB_OK)release_child(child);return CB_RESOURCE_FAILURE;}
    flags=lock();++runtime.stats.launched;unlock(flags);child->thread=info.thread;__atomic_store_n(&child->published,1,__ATOMIC_RELEASE);CALL(runtime.control,THREAD_WAKE,child->worker,0);cb_install_copy(invocation->output,&child->process,8);invocation->actual=8;return CB_OK;
}
int io_process_invoke(struct io_invocation *r){
    if(r->operation<CB_IO_PROCESS_LAUNCH || r->operation>CB_IO_PROCESS_RELEASE)return 0;
    r->status=CB_DENIED;if(!runtime.catalog || r->process!=runtime.manager || CALL(runtime.cpu,CPU_CURRENT,0,0).value)return 1;
    if(r->operation==CB_IO_PROCESS_LAUNCH){r->status=launch(r);return 1;}
    if(r->bytes!=sizeof(struct cb_application_ref)){r->status=CB_BAD_REQUEST;return 1;}struct cb_application_ref ref;cb_install_copy(&ref,r->input,sizeof(ref));
    uint64_t flags=lock();struct child *child=runtime.children;while(child && child->process!=ref.process)child=child->next;unlock(flags);if(!child){r->status=CB_BAD_REQUEST;return 1;}
    if(r->operation==CB_IO_PROCESS_RELEASE){r->status=child->reaped && !child->worker?CB_OK:CB_BUSY;if(r->status==CB_OK)release_child(child);return 1;}
    if(r->operation==CB_IO_PROCESS_STOP)r->status=CALL(runtime.stop,child->process,0,0).status?CB_RESOURCE_FAILURE:CB_OK;
    else {struct process_info info;if(!child->reaped && (CALL(runtime.info,child->process,&info,0).status || (info.state!=PROCESS_EXITED && info.state!=PROCESS_FAULTED))){r->status=CB_BUSY;return 1;}r->status=dispose(child,1);}return 1;
}
cb_status_t io_process_exit(uint64_t pid){
    if(pid!=runtime.manager){
        uint64_t flags=lock();for(struct child *child=runtime.children;child;child=child->next)if(child->owner==pid){struct cb_io_process_event event={.kind=CB_IO_PROCESS_OWNER_EXIT,.owner=pid,.status={.process=child->process}};const struct cb_interface_id interface=CB_IO_INTERFACE_ID;(void)runtime.post(runtime.manager,child->channel+1,(uintptr_t)&event,sizeof(event),(uintptr_t)&interface,CB_IO_PROCESS_EVENT);__atomic_store_n(&child->owner,0,__ATOMIC_RELEASE);}unlock(flags);return CB_OK;
    }
    /* Stop every surviving child, even when an earlier one does not cooperate.
       Running mappings stay owned until quiescence; there is no forced unwind. */
    for(struct child *child=runtime.children;child;child=child->next)if(!child->reaped){struct process_info info;if(!CALL(runtime.info,child->process,&info,0).status && info.state==PROCESS_RUNNING)(void)CALL(runtime.stop,child->process,0,0);}
    for(;;){uint64_t flags=lock();struct child *child=runtime.children;unlock(flags);if(!child)break;
        struct process_info info={0};if(!child->reaped && CALL(runtime.info,child->process,&info,0).status)return CB_RESOURCE_FAILURE;
        if(info.state==PROCESS_RUNNING){(void)CALL(runtime.stop,child->process,0,0);return CB_BUSY;}
        cb_status_t status=dispose(child,0);if(status)return status;release_child(child);
    }
    runtime.manager=0;return CB_OK;
}

struct kernel_surface_result io_surface_process_info(MODULE_ARGS){
    MODULE_UNUSED();if(!a)return module_result(0,0);uint64_t flags=lock();struct io_process_info info=runtime.stats;info.manager=runtime.manager;info.children=0;for(struct child *p=runtime.children;p;p=p->next)++info.children;*(struct io_process_info *)a=info;unlock(flags);return module_result(1,1);
}
