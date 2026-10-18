#include "bootstrap-apps.h"
#include "bootstrap.h"
#include "config.h"
#include "process.h"
#include "handles.h"
#include "threads.h"
#include "../../handlers/keyboard/startup.h"
#include "../../kernel/cpu.h"
static struct {const struct kernel_surfaces *hub;uintptr_t heap;unsigned loading;struct bootstrap_apps_info info;kernel_surface_call alloc,free,stat,read,load,admit,start,reap,process_info,thread,create,clock,cpu,inspect;} apps;
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void *allocate(size_t n){struct kernel_surface_result r=CALL(apps.alloc,apps.heap,1,n);return r.status?NULL:(void *)r.value;}
static void release(void *p){if(p)CALL(apps.free,apps.heap,p,0);}
static size_t length(const char *p){size_t n=0;while(p[n])++n;return n;}
static void copy(void *dst,const void *src,size_t bytes){unsigned char *d=dst;const unsigned char *s=src;for(size_t i=0;i<bytes;++i)d[i]=s[i];}
static const struct config_value *config(const char *name,unsigned type){const struct kernel_hub_table *t=kernel_hub_find_table(apps.hub,name);const struct config_value *v=t?(const void *)t->address:NULL;return v && v->major==1 && v->type==type && v->data?v:NULL;}
static char *path(const char *directory,const char *suffix){size_t n=length(directory),m=length(suffix);while(n && directory[n-1]=='/')--n;if(n>SIZE_MAX-m-2)return NULL;char *p=allocate(n+m+2);if(p){copy(p,directory,n);p[n]='/';copy(p+n+1,suffix,m+1);}return p;}
static void *file(uint64_t volume,const char *path,size_t *bytes){struct ext4_stat st;if(!path || CALL(apps.stat,volume,path,&st).status || (st.mode&0170000)!=0100000 || !st.bytes || st.bytes>SIZE_MAX)return NULL;void *data=allocate(st.bytes);if(!data)return NULL;struct kernel_surface_result got=apps.read(volume,(uintptr_t)path,0,(uintptr_t)data,st.bytes,0);if(got.status || got.value!=st.bytes){release(data);return NULL;}*bytes=st.bytes;return data;}
int bootstrap_apps_init(const struct kernel_surfaces *hub,uintptr_t heap){
    if(apps.hub)return apps.hub==hub && apps.heap==heap;
#define BIND(field,name) do{apps.field=kernel_surface_find(hub,name);if(!apps.field)return 0;}while(0)
    BIND(alloc,"mem.calloc");BIND(free,"mem.free");BIND(stat,"ext4_fs.stat");BIND(read,"ext4_fs.read");BIND(load,"process.load");BIND(admit,"handles.admit");BIND(start,"process.start");BIND(reap,"process.reap");BIND(process_info,"process.info");BIND(thread,"threads.control");BIND(create,"threads.create");BIND(clock,"clock.now_ns");BIND(cpu,"cpu.control");BIND(inspect,"handles.inspect");
#undef BIND
    apps.hub=hub;apps.heap=heap;struct kernel_hub_table table={.owner=kernel_surface_owner(hub,"bootstrap.init"),.name="bootstrap.applications",.address=(uintptr_t)&apps.info,.bytes=sizeof(apps.info),.count=1,.entry_bytes=sizeof(apps.info)};
    kernel_surface_call publish=kernel_surface_find(hub,"hub.publish_table");return publish && !CALL(publish,&table,0,0).status;
}
static struct kernel_surface_result keyboard_load(uintptr_t a,uintptr_t b){
    struct process_info old;if(apps.info.keyboard && !CALL(apps.process_info,apps.info.keyboard,&old,0).status)return module_result(0,0);
    apps.info.keyboard=0;
    const struct config_value *directory=config("config.drivers.directory",CONFIG_STRING),*layout=config("config.keyboard.layout",CONFIG_STRING);if(!directory || !layout)return module_result(0,0);
    const char *selected=b?(const void *)b:layout->data;if(!*selected)return module_result(0,0);for(const char *p=selected;*p;++p)if(!((*p>='a'&&*p<='z') || (*p>='A'&&*p<='Z') || (*p>='0'&&*p<='9') || *p=='_' || *p=='-'))return module_result(0,0);
    char *program=path(directory->data,"keyboard/keyboard.elf"),*manifest_path=path(directory->data,"keyboard/manifest.json"),*layout_suffix=allocate(length(selected)+23),*layout_path=NULL;
    void *manifest=NULL,*definition=NULL,*packet=NULL;uint64_t pid=0;size_t manifest_bytes=0,layout_bytes=0;int ok=0;
    if(!program || !manifest_path || !layout_suffix)goto done;
    copy(layout_suffix,"keyboard/layouts/",17);copy(layout_suffix+17,selected,length(selected));copy(layout_suffix+17+length(selected),".json",6);layout_path=path(directory->data,layout_suffix);
    manifest=file(a,manifest_path,&manifest_bytes);definition=file(a,layout_path,&layout_bytes);if(!manifest || !definition || layout_bytes>SIZE_MAX-sizeof(struct keyboard_startup))goto done;
    packet=allocate(sizeof(struct keyboard_startup)+layout_bytes);if(!packet)goto done;struct keyboard_startup header={.version={sizeof(header),1,0},.layout_bytes=layout_bytes};copy(packet,&header,sizeof(header));copy((unsigned char *)packet+sizeof(header),definition,layout_bytes);
    struct process_request request={.abi=2,.cpu=0,.filesystem=a,.path=program,.stack_bytes=262144,.startup=packet,.startup_bytes=sizeof(header)+layout_bytes};struct kernel_surface_result loaded=CALL(apps.load,&request,0,0);pid=loaded.value;if(loaded.status)goto done;
    struct handle_policy policy={.process=pid,.manifest=manifest,.bytes=manifest_bytes};static const unsigned char identity[16]={'c','l','o','u','d','b','e','r','r','y','.','k','b','d','1',0};copy(policy.identity.installation_id,identity,16);
    if(CALL(apps.admit,&policy,0,0).status || CALL(apps.start,pid,0,0).status)goto done;
    apps.info.keyboard=pid;apps.info.state=2;apps.info.error=0;ok=1;
done:
    if(!ok && pid){if(!CALL(apps.reap,pid,0,0).status)pid=0;else apps.info.keyboard=pid;}
    release(program);release(manifest_path);release(layout_suffix);release(layout_path);release(manifest);release(definition);release(packet);return module_result(pid,ok);
}
struct kernel_surface_result bootstrap_surface_keyboard(MODULE_ARGS){
    MODULE_UNUSED();if(!apps.hub || CALL(apps.cpu,CPU_CURRENT,0,0).value || !CALL(apps.thread,THREAD_CURRENT,0,0).value)return module_result(0,0);
    unsigned expected=0;if(!__atomic_compare_exchange_n(&apps.loading,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return module_result(0,0);
    struct kernel_surface_result result=keyboard_load(a,b);__atomic_store_n(&apps.loading,0,__ATOMIC_RELEASE);return result;
}
struct kernel_surface_result bootstrap_surface_apps(MODULE_ARGS){
    MODULE_UNUSED();if(!apps.hub || apps.info.worker || apps.info.state)return module_result(apps.info.worker,apps.hub!=NULL);
    const struct config_value *enabled=config("config.keyboard.enabled",CONFIG_BOOL);if(!enabled || enabled->bytes!=1)return module_result(0,0);if(!*(const uint8_t *)enabled->data)return module_result(0,1);
    struct thread_request r={.abi=1,.cpu=0,.stack_bytes=65536,.entry=(uintptr_t)kernel_surface_find(apps.hub,"bootstrap.apps_worker"),.major=1};apps.info.state=1;struct kernel_surface_result result=CALL(apps.create,&r,0,0);if(result.status){apps.info.state=4;apps.info.error=1;}else apps.info.worker=result.value;return result;
}
static void log_text(const char *s){kernel_surface_call log=kernel_surface_find(apps.hub,"hardware.log");if(log)CALL(log,s,length(s),0);}
struct kernel_surface_result bootstrap_surface_apps_worker(MODULE_ARGS){
    MODULE_UNUSED();kernel_surface_call info=kernel_surface_find(apps.hub,"bootstrap.info");uint64_t limit=CALL(apps.clock,0,0,0).value+UINT64_C(30000000000);struct bootstrap_info volume;
    for(;;){if(CALL(info,&volume,0,0).status || volume.state==BOOTSTRAP_FAILED || volume.state==BOOTSTRAP_DISCONNECTED)goto failed;if(volume.state==BOOTSTRAP_READY)break;uint64_t time=CALL(apps.clock,0,0,0).value;if(time>=limit || CALL(apps.thread,THREAD_BLOCK_UNTIL,time+10000000,0).status)goto failed;}
    apps.info.state=2;if(bootstrap_surface_keyboard(volume.filesystem.cookie,0,0,0,0,0).status)goto failed;
    for(;;){struct handle_publication_info provider;if(!CALL(apps.inspect,"/@keyboard",&provider,0).status && provider.process==apps.info.keyboard){apps.info.state=3;log_text("bootstrap: /@keyboard provider ready from ext4\n");return module_result(apps.info.keyboard,1);}struct process_info process;if(CALL(apps.process_info,apps.info.keyboard,&process,0).status || process.state==PROCESS_EXITED || process.state==PROCESS_FAULTED)goto failed;uint64_t time=CALL(apps.clock,0,0,0).value;if(time>=limit || CALL(apps.thread,THREAD_BLOCK_UNTIL,time+1000000,0).status)goto failed;}
failed:apps.info.state=4;apps.info.error=1;log_text("bootstrap: keyboard provider unavailable\n");return module_result(0,0);
}
struct kernel_surface_result bootstrap_surface_apps_info(MODULE_ARGS){
    MODULE_UNUSED();if(!a || !apps.hub || CALL(apps.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);uint64_t worker=apps.info.worker;if(worker){struct thread_info t;if(!CALL(apps.thread,THREAD_INFO,worker,&t).status && t.state==THREAD_EXITED && !CALL(apps.thread,THREAD_REAP,worker,0).status)apps.info.worker=0;}if(apps.info.keyboard){struct process_info p;struct handle_publication_info publication;
        if(CALL(apps.process_info,apps.info.keyboard,&p,0).status || p.state!=PROCESS_RUNNING || p.stop_requested){apps.info.state=4;apps.info.error=2;}
        else if(!CALL(apps.inspect,"/@keyboard",&publication,0).status && publication.process==apps.info.keyboard){apps.info.state=3;apps.info.error=0;}
        else if(apps.info.state==3){apps.info.state=4;apps.info.error=3;}
    }
    *(struct bootstrap_apps_info *)a=apps.info;return module_result(1,1);
}
