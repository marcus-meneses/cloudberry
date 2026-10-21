#include "applications.h"
#include "process.h"
#include "threads.h"
#include "handles.h"
#include "filesystem.h"
#include "../../interfaces/startup.h"
#include "../../interfaces/installations.h"
#include "../../kernel/cpu.h"
struct registry_snapshot;
struct child {struct child *next;uint64_t process,worker,channel,thread,owner;unsigned published,cancelled,reaped;};
static struct {const struct kernel_surfaces *hub;uintptr_t heap;unsigned char *catalog;size_t bytes;uint64_t filesystem,mount,manager,serial;struct registry_snapshot *registry,*staging;uint64_t registry_serial,registry_committed,registry_writer,registry_actor,registry_written,registry_loaders;unsigned registry_prepared,registry_recovery_required;struct applications_info stats;struct child *children;
    kernel_surface_call alloc,free,cpu,load,start,stop,info,reap,admit,create,control,channel,post,unsubscribe;} runtime;
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static uint64_t lock(void){return CALL(runtime.cpu,CPU_ENTER,0,0).value;}
static void unlock(uint64_t flags){CALL(runtime.cpu,CPU_LEAVE,flags,0);}
static void *allocate(size_t bytes){struct kernel_surface_result r=CALL(runtime.alloc,runtime.heap,1,bytes);return r.status?NULL:(void *)r.value;}
static void release(void *p){if(p)CALL(runtime.free,runtime.heap,p,0);}
static size_t length(const char *p){size_t n=0;while(p[n])++n;return n;}
#include "applications-registry.inc"
int applications_init(const struct kernel_surfaces *hub,uintptr_t heap){
    if(runtime.hub)return runtime.hub==hub;
#define BIND(field,name) do{runtime.field=kernel_surface_find(hub,name);if(!runtime.field)return 0;}while(0)
    BIND(alloc,"mem.calloc");BIND(free,"mem.free");BIND(cpu,"cpu.control");BIND(load,"process.load");BIND(start,"process.start");BIND(stop,"process.stop");BIND(info,"process.info");BIND(reap,"process.reap");BIND(admit,"handles.admit");BIND(create,"threads.create");BIND(control,"threads.control");BIND(channel,"handles.channel");BIND(post,"handles.post");BIND(unsubscribe,"handles.unsubscribe");
#undef BIND
    runtime.hub=hub;runtime.heap=heap;return 1;
}
struct kernel_surface_result applications_surface_installations(MODULE_ARGS){
    MODULE_UNUSED();if(!runtime.hub || !a || !b || !cb_install_valid((const void *)b,c))return module_result(0,0);
    if(runtime.catalog)return module_result(1,runtime.filesystem==a && runtime.mount==d && runtime.bytes==c && cb_install_equal(runtime.catalog,(const void *)b,c));
    unsigned char *catalog=allocate(c);if(!catalog)return module_result(0,0);cb_install_copy(catalog,(const void *)b,c);runtime.catalog=catalog;runtime.bytes=c;runtime.filesystem=a;runtime.mount=d;return module_result(1,1);
}
/* The filesystem provider supplies authenticated admission identity. Only that
 * installed app's root-session data subtree is writable inside /system. */
int applications_data(const unsigned char *identity,const char *path){
    if(runtime.catalog){struct cb_install_record installer;if(cb_install_find(runtime.catalog,"installer",9,NULL,&installer) && cb_install_equal(identity,installer.installation,16))return 1;}
    static const char protected[]="/system/config/identity";
    if(runtime.catalog && length(path)>=sizeof(protected)-1 && cb_install_equal(path,protected,sizeof(protected)-1) && (!path[sizeof(protected)-1] || path[sizeof(protected)-1]=='/')){
        struct cb_install_record record;return cb_install_find(runtime.catalog,"identity",8,NULL,&record) && record.flags==CB_INSTALL_PROVIDER && cb_install_equal(identity,record.installation,16);
    }
    static const char prefix[]="/system/root/_apps/";
    if(!runtime.catalog || length(path)<sizeof(prefix)-1 || !cb_install_equal(path,prefix,sizeof(prefix)-1))return 0;
    const struct cb_install_catalog *header=(const void *)runtime.catalog;
    const unsigned char *at=runtime.catalog+sizeof(*header);
    for(uint64_t i=0;i<header->count;++i){struct cb_install_record record;cb_install_copy(&record,at,sizeof(record));const unsigned char *name=at+sizeof(record);at+=record.bytes;
        if(!(record.flags&CB_INSTALL_APPLICATION) || !cb_install_equal(identity,record.installation,16))continue;
        const char *tail=path+sizeof(prefix)-1;size_t n=length(tail);
        return n>record.name_bytes+6 && cb_install_equal(tail,name,record.name_bytes) && cb_install_equal(tail+record.name_bytes,".data/",6);
    }
    struct installation_ref ref;if(installation_lookup(0,NULL,0,identity,&ref)){int allowed=0;if(ref.snapshot && registry_find(ref.snapshot,ref.owner,identity)){const unsigned char *p=ref.name+ref.record.name_bytes;size_t last=0;for(size_t i=0;i<ref.record.path_bytes;++i)if(p[i]=='/')last=i+1;size_t n=ref.record.path_bytes-last;const char *tail=path+sizeof(prefix)-1;size_t bytes=length(tail);allowed=bytes>n+6 && cb_install_equal(tail,p+last,n) && cb_install_equal(tail+n,".data/",6);}installation_drop(&ref);return allowed;}return 0;
}
#include "applications-access.inc"
/* Trusted kernel entry. Ordinary callers can only reach the manager's narrower
 * process_launch operation, which cannot load provider/manager records. */
static struct kernel_surface_result load_installation(const struct installation_ref *entry,const void *packet,size_t bytes){
    const struct cb_install_record *record=&entry->record;const unsigned char *name=entry->name;
    if(record->flags==CB_INSTALL_MANAGER && runtime.manager)return module_result(0,0);
    char *path=allocate(record->path_bytes+1);if(!path)return module_result(0,0);cb_install_copy(path,name+record->name_bytes,record->path_bytes);
    uint32_t cpu=record->cpu;if(record->flags&CB_INSTALL_APPLICATION){struct kernel_surface_result selected=CALL(runtime.control,THREAD_SELECT_CPU,0,0);if(selected.status){release(path);return module_result(0,0);}cpu=selected.value;}
    struct process_request request={.abi=2,.filesystem=runtime.filesystem,.path=path,.cpu=cpu,.stack_bytes=record->stack_bytes,.startup=packet,.startup_bytes=bytes};
    struct kernel_surface_result loaded=CALL(runtime.load,&request,0,0);release(path);if(loaded.status){if(loaded.value)(void)CALL(runtime.reap,loaded.value,0,0);return module_result(0,0);}
    struct handle_policy policy={.process=loaded.value,.manifest=name+record->name_bytes+record->path_bytes,.bytes=record->manifest_bytes,.installation_owner=entry->owner==CB_REGISTRY_SYSTEM?0:entry->owner};cb_install_copy(policy.identity.installation_id,record->installation,16);
    if(CALL(runtime.admit,&policy,0,0).status){(void)CALL(runtime.reap,loaded.value,0,0);return module_result(0,0);}
    if(record->flags==CB_INSTALL_MANAGER)runtime.manager=loaded.value;
    return loaded;
}
struct kernel_surface_result applications_surface_installed_load(MODULE_ARGS){
    MODULE_UNUSED();if(!runtime.catalog || !a || !b || !c || CALL(runtime.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);
    struct installation_ref entry={0};entry.name=cb_install_find(runtime.catalog,(const void *)a,length((const void *)a),NULL,&entry.record);if(!entry.name)return module_result(0,0);
    struct kernel_surface_result loaded=load_installation(&entry,(const void *)b,c);if(loaded.status)return loaded;
    if(entry.record.flags==CB_INSTALL_PROVIDER){static const char targets[][24]={"hid.bind","display.bind","filesystem.bind","io.bind"};for(unsigned i=0;i<sizeof(targets)/sizeof(targets[0]);++i){kernel_surface_call bind=kernel_surface_find(runtime.hub,targets[i]);if(!bind || CALL(bind,a,loaded.value,runtime.mount).status){(void)CALL(runtime.reap,loaded.value,0,0);return module_result(0,0);}}}
    return loaded;
}
struct kernel_surface_result applications_surface_worker(MODULE_ARGS){
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
    struct installation_ref caller;if(owner){if(!installation_process(owner,&caller))return CB_DENIED;int allowed=caller.record.flags&CB_INSTALL_LAUNCHER;installation_drop(&caller);if(!allowed)return CB_DENIED;}
    uint64_t gate=lock();int blocked=runtime.registry_prepared!=0 || runtime.registry_recovery_required;unlock(gate);
    struct installation_ref entry;if(!installation_lookup(owner?registry_uid(owner):0,payload+h.version.bytes,h.name_bytes,NULL,&entry))return CB_NOT_FOUND;if(!(entry.record.flags&CB_INSTALL_APPLICATION)){installation_drop(&entry);return CB_DENIED;}
    if(blocked && entry.snapshot && registry_find(entry.snapshot,entry.owner,entry.record.installation)){installation_drop(&entry);return CB_BUSY;}
    struct cb_install_record record=entry.record;
    char *name=allocate(h.name_bytes+1);struct child *child=allocate(sizeof(*child));size_t total=sizeof(struct cb_startup)+sizeof(struct cb_startup_file)+11+payload_bytes;unsigned char *packet=allocate(total);
    if(!name || !child || !packet){release(name);release(child);release(packet);installation_drop(&entry);return CB_RESOURCE_FAILURE;}
    cb_install_copy(name,payload+h.version.bytes,h.name_bytes);struct cb_startup startup={.version={sizeof(startup),1,0},.bytes=total,.files=1};struct cb_startup_file file={11,payload_bytes};cb_install_copy(packet,&startup,sizeof(startup));cb_install_copy(packet+sizeof(startup),&file,sizeof(file));cb_install_copy(packet+sizeof(startup)+sizeof(file),"application",11);cb_install_copy(packet+sizeof(startup)+sizeof(file)+11,payload,payload_bytes);cb_handle_t streams[3]={0};if(h.version.minor==1){cb_install_copy(streams,payload+sizeof(h),sizeof(streams));unsigned char *copied=packet+sizeof(startup)+sizeof(file)+11+sizeof(h);for(unsigned i=0;i<sizeof(streams);++i)copied[i]=0;}
    struct kernel_surface_result loaded=load_installation(&entry,packet,total);installation_drop(&entry);release(name);release(packet);if(loaded.status){release(child);return CB_RESOURCE_FAILURE;}child->process=loaded.value;child->owner=owner;
    if(owner){kernel_surface_call credentials=kernel_surface_find(runtime.hub,"process.credentials");if(!credentials || CALL(credentials,child->process,2,owner).status){CALL(runtime.reap,child->process,0,0);release(child);return CB_RESOURCE_FAILURE;}}
    if(owner){struct installation_ref check;if(!installation_lookup(registry_uid(owner),(const unsigned char *)invocation->input+8+h.version.bytes,h.name_bytes,NULL,&check)){CALL(runtime.reap,child->process,0,0);release(child);return CB_DENIED;}record=check.record;char *executable=allocate(record.path_bytes+1);if(!executable){installation_drop(&check);release(executable);CALL(runtime.reap,child->process,0,0);release(child);return CB_RESOURCE_FAILURE;}cb_install_copy(executable,check.name+record.name_bytes,record.path_bytes);installation_drop(&check);struct filesystem_request access={.operation=FS_EXEC,.path=executable,.process=owner};kernel_surface_call file=kernel_surface_find(runtime.hub,"filesystem.file");int denied=!file || CALL(file,runtime.mount,&access,0).status || access.status;release(executable);if(denied){CALL(runtime.reap,child->process,0,0);release(child);return CB_DENIED;}}
    if(streams[0] || streams[1] || streams[2]){kernel_surface_call bind=kernel_surface_find(runtime.hub,"handles.stream_bind");if(!owner || !bind || CALL(bind,owner,child->process,streams).status){CALL(runtime.reap,child->process,0,0);release(child);return CB_DENIED;}}
    uint64_t flags=lock();if(runtime.serial>UINT64_MAX/2-2){unlock(flags);CALL(runtime.reap,child->process,0,0);release(child);return CB_RESOURCE_FAILURE;}runtime.serial+=2;child->channel=(UINT64_C(1)<<63)|(runtime.serial-1);unlock(flags);
    if(CALL(runtime.channel,runtime.manager,child->channel,sizeof(struct cb_io_process_event)).status || CALL(runtime.channel,runtime.manager,child->channel+1,sizeof(struct cb_io_process_event)).status){CALL(runtime.unsubscribe,runtime.manager,child->channel,0);CALL(runtime.reap,child->process,0,0);release(child);return CB_RESOURCE_FAILURE;}
    struct thread_request worker={.abi=1,.cpu=0,.stack_bytes=32768,.entry=(uintptr_t)kernel_surface_find(runtime.hub,"applications.worker"),.major=1,.payload={.arguments={(uintptr_t)child}}};struct kernel_surface_result created=CALL(runtime.create,&worker,0,0);
    if(created.status){CALL(runtime.unsubscribe,runtime.manager,child->channel,0);CALL(runtime.unsubscribe,runtime.manager,child->channel+1,0);CALL(runtime.reap,child->process,0,0);release(child);return CB_RESOURCE_FAILURE;}child->worker=created.value;
    flags=lock();child->next=runtime.children;runtime.children=child;unlock(flags);
    struct kernel_surface_result started=CALL(runtime.start,child->process,0,0);struct process_info info;
    if(started.status || CALL(runtime.info,child->process,&info,0).status){child->cancelled=1;__atomic_store_n(&child->published,1,__ATOMIC_RELEASE);CALL(runtime.control,THREAD_WAKE,child->worker,0);if(dispose(child,1)==CB_OK)release_child(child);return CB_RESOURCE_FAILURE;}
    flags=lock();++runtime.stats.launched;unlock(flags);child->thread=info.thread;__atomic_store_n(&child->published,1,__ATOMIC_RELEASE);CALL(runtime.control,THREAD_WAKE,child->worker,0);cb_install_copy(invocation->output,&child->process,8);invocation->actual=8;return CB_OK;
}
int applications_invoke(struct io_invocation *r){
    if(r->operation==CB_IO_REGISTRY){r->status=registry_invoke(r);return 1;}
    if(r->operation==CB_IO_PROCESS_DATA){r->status=CB_DENIED;if(r->process!=runtime.manager || !runtime.manager)return 1;if(r->bytes!=8 && r->bytes!=16){r->status=CB_BAD_REQUEST;return 1;}uint64_t pid,kind=0;cb_install_copy(&pid,r->input,8);if(r->bytes==16)cb_install_copy(&kind,(const unsigned char *)r->input+8,8);char path[4096];r->status=CB_BAD_REQUEST;if(!kind)r->status=application_data_path(pid,path);else if(kind==1){struct installation_ref ref;if(!installation_process(pid,&ref))r->status=CB_DENIED;else{r->status=ref.record.path_bytes+11>sizeof(path)?CB_BAD_REQUEST:CB_OK;if(!r->status){cb_install_copy(path,ref.name+ref.record.name_bytes,ref.record.path_bytes);cb_install_copy(path+ref.record.path_bytes,".resources",11);}installation_drop(&ref);}}if(!r->status){size_t n=length(path);if(n>r->capacity)r->status=CB_BUFFER_TOO_SMALL;else {cb_install_copy(r->output,path,n);r->actual=n;}}return 1;}

    if(r->operation==CB_IO_PROCESS_SAMPLE){
        r->status=CB_DENIED;r->actual=0;if(r->process!=runtime.manager || !runtime.manager || CALL(runtime.cpu,CPU_CURRENT,0,0).value)return 1;
        r->status=CB_BAD_REQUEST;if(r->bytes!=sizeof(struct cb_process_query) || r->capacity<sizeof(struct cb_process_sample))return 1;
        kernel_surface_call sample=kernel_surface_find(runtime.hub,"process.sample");if(!sample){r->status=CB_UNAVAILABLE;return 1;}struct kernel_surface_result result=CALL(sample,r->input,r->output,0);r->status=result.status?CB_RESOURCE_FAILURE:CB_OK;if(!result.status && result.value)r->actual=sizeof(struct cb_process_sample);return 1;
    }
    if(r->operation<CB_IO_PROCESS_LAUNCH || r->operation>CB_IO_PROCESS_RELEASE)return 0;
    r->status=CB_DENIED;if(!runtime.catalog || r->process!=runtime.manager || CALL(runtime.cpu,CPU_CURRENT,0,0).value)return 1;
    if(r->operation==CB_IO_PROCESS_LAUNCH){uint64_t flags=lock();++runtime.registry_loaders;unlock(flags);r->status=launch(r);flags=lock();--runtime.registry_loaders;unlock(flags);return 1;}
    if(r->bytes!=sizeof(struct cb_application_ref)){r->status=CB_BAD_REQUEST;return 1;}struct cb_application_ref ref;cb_install_copy(&ref,r->input,sizeof(ref));
    uint64_t flags=lock();struct child *child=runtime.children;while(child && child->process!=ref.process)child=child->next;unlock(flags);if(!child){r->status=CB_BAD_REQUEST;return 1;}
    if(r->operation==CB_IO_PROCESS_RELEASE){r->status=child->reaped && !child->worker?CB_OK:CB_BUSY;if(r->status==CB_OK)release_child(child);return 1;}
    if(r->operation==CB_IO_PROCESS_STOP)r->status=CALL(runtime.stop,child->process,0,0).status?CB_RESOURCE_FAILURE:CB_OK;
    else {struct process_info info;if(!child->reaped && (CALL(runtime.info,child->process,&info,0).status || (info.state!=PROCESS_EXITED && info.state!=PROCESS_FAULTED))){r->status=CB_BUSY;return 1;}r->status=dispose(child,1);}return 1;
}
cb_status_t applications_exit(uint64_t pid){
    uint64_t registry_flags=lock();struct registry_snapshot *abandoned=NULL;
    if(runtime.staging && runtime.registry_writer==pid){abandoned=runtime.staging;runtime.staging=NULL;if(runtime.registry_prepared)runtime.registry_recovery_required=1;runtime.registry_prepared=0;runtime.registry_writer=0;}
    unlock(registry_flags);registry_drop(abandoned);

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

struct kernel_surface_result applications_surface_info(MODULE_ARGS){
    MODULE_UNUSED();if(!a)return module_result(0,0);uint64_t flags=lock();struct applications_info info=runtime.stats;info.manager=runtime.manager;info.children=0;for(struct child *p=runtime.children;p;p=p->next)++info.children;*(struct applications_info *)a=info;unlock(flags);return module_result(1,1);
}

struct kernel_surface_result applications_surface_init(MODULE_ARGS){MODULE_UNUSED();return module_result(1,applications_init((const void *)a,b));}
struct kernel_surface_result applications_surface_invoke(MODULE_ARGS){MODULE_UNUSED();if(!a)return module_result(0,0);return module_result(1,applications_invoke((void *)a));}
struct kernel_surface_result applications_surface_exit(MODULE_ARGS){MODULE_UNUSED();cb_status_t result=applications_exit(a);return module_result(result,result==CB_OK);}
struct kernel_surface_result applications_surface_data(MODULE_ARGS){MODULE_UNUSED();if(!a || !b)return module_result(0,0);return module_result(1,applications_data((const void *)a,(const void *)b));}
