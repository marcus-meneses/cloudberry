#include "bootstrap-apps.h"
#include "bootstrap.h"
#include "bootstrap-load.h"
#include "config.h"
#include "process.h"
#include "handles.h"
#include "threads.h"
#include "provider-definition.h"
#include "render-selection.h"
#include "../../interfaces/startup.h"
#include "../../interfaces/installations.h"
#include "../../kernel/cpu.h"
struct application {struct application *next;uint64_t process;uint32_t state,error;char *bundle,*ready;};
static struct {struct application *first;const struct kernel_surfaces *hub;uintptr_t heap;unsigned operation;struct bootstrap_apps_info info;
    void *catalog;
    kernel_surface_call alloc,free,stat,read,load,admit,start,reap,process_info,thread,create,clock,cpu,inspect;} apps;
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void *allocate(size_t n){struct kernel_surface_result r=CALL(apps.alloc,apps.heap,1,n);return r.status?NULL:(void *)r.value;}
static void release(void *p){if(p)CALL(apps.free,apps.heap,p,0);}
static size_t length(const char *p){size_t n=0;while(p[n])++n;return n;}
static void copy(void *dst,const void *src,size_t n){cb_startup_copy(dst,src,n);}
static char *string(const char *s){size_t n=length(s);char *p=allocate(n+1);if(p)copy(p,s,n+1);return p;}
static int enter(void){unsigned expected=0;return apps.hub && !CALL(apps.cpu,CPU_CURRENT,0,0).value && __atomic_compare_exchange_n(&apps.operation,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);}
static void leave(void){__atomic_store_n(&apps.operation,0,__ATOMIC_RELEASE);}
static const struct config_value *config(const char *name,unsigned type){const struct kernel_hub_table *t=kernel_hub_find_table(apps.hub,name);const struct config_value *v=t?(const void *)t->address:NULL;return v && v->major==1 && v->type==type && v->data?v:NULL;}
static char *path(const char *directory,const char *suffix){size_t n=length(directory),m=length(suffix);while(n && directory[n-1]=='/')--n;if(n>SIZE_MAX-m-2)return NULL;char *p=allocate(n+m+2);if(p){copy(p,directory,n);p[n]='/';copy(p+n+1,suffix,m+1);}return p;}
static struct render_selection render_status;
static unsigned render_started;
static void log_text(const char *s);
#ifdef CB_BOOT_TIMING
static void boot_trace(const char *phase,const char *name){log_text("BOOT-TIME provider ");log_text(phase);log_text(" ");log_text(name);log_text("\n");}
#else
#define boot_trace(phase,name) ((void)0)
#endif
static void *file(uint64_t volume,const char *name,size_t *bytes){boot_trace("file",name?name:"<null>");struct ext4_stat st;if(!name || CALL(apps.stat,volume,name,&st).status || (st.mode&0170000)!=0100000 || st.bytes>=SIZE_MAX)return NULL;void *data=allocate(st.bytes+1);if(!data)return NULL;struct kernel_surface_result got=apps.read(volume,(uintptr_t)name,0,(uintptr_t)data,st.bytes,0);if(got.status || got.value!=st.bytes){release(data);return NULL;}*bytes=st.bytes;boot_trace("file_done",name);return data;}
static void *bundle_file(uint64_t volume,const char *directory,const char *name,size_t *bytes){char *p=path(directory,name);void *data=p?file(volume,p,bytes):NULL;release(p);return data;}
struct startup_part {struct startup_part *next;char *name;void *data;size_t bytes;};
static void *startup(uint64_t volume,const char *directory,char *list,size_t *bytes){
    struct startup_part *first=NULL,**tail=&first;size_t total=sizeof(struct cb_startup),count=0;void *packet=NULL;
    for(char *cursor=list;cursor && *cursor;){char *name=cursor;while(*cursor && *cursor!=',')++cursor;if(*cursor){*cursor++=0;if(!*cursor)goto done;}
        char *source=name;for(char *p=name;*p;++p)if(*p=='='){*p=0;source=p+1;break;}
        if(!pd_path(name,0) || (source!=name && (*source!='/' || !pd_path(source+1,0))))goto done;
        for(struct startup_part *p=first;p;p=p->next)if(pd_same(p->name,name))goto done;
        struct startup_part *part=allocate(sizeof(*part));if(!part)goto done;part->name=name;*tail=part;tail=&part->next;part->data=source!=name?file(volume,source,&part->bytes):bundle_file(volume,directory,name,&part->bytes);if(!part->data)goto done;
        size_t n=length(name);if(n>SIZE_MAX-sizeof(struct cb_startup_file) || part->bytes>SIZE_MAX-sizeof(struct cb_startup_file)-n || total>SIZE_MAX-sizeof(struct cb_startup_file)-n-part->bytes)goto done;
        total+=sizeof(struct cb_startup_file)+n+part->bytes;++count;
    }
    packet=allocate(total);if(!packet)goto done;
    struct cb_startup h={.version={sizeof(h),1,0},.bytes=total,.files=count};copy(packet,&h,sizeof(h));size_t at=sizeof(h);
    for(struct startup_part *p=first;p;p=p->next){struct cb_startup_file f={length(p->name),p->bytes};copy((unsigned char *)packet+at,&f,sizeof(f));at+=sizeof(f);copy((unsigned char *)packet+at,p->name,f.name_bytes);at+=f.name_bytes;copy((unsigned char *)packet+at,p->data,f.data_bytes);at+=f.data_bytes;}
    *bytes=total;
done:while(first){struct startup_part *p=first;first=p->next;release(p->data);release(p);}return packet;
}
int bootstrap_apps_init(const struct kernel_surfaces *hub,uintptr_t heap){
    if(apps.hub)return apps.hub==hub && apps.heap==heap;
#define BIND(field,name) do{apps.field=kernel_surface_find(hub,name);if(!apps.field)return 0;}while(0)
    BIND(alloc,"mem.calloc");BIND(free,"mem.free");BIND(stat,"ext4_fs.stat");BIND(read,"ext4_fs.read");BIND(load,"process.load");BIND(admit,"handles.admit");BIND(start,"process.start");BIND(reap,"process.reap");BIND(process_info,"process.info");BIND(thread,"threads.control");BIND(create,"threads.create");BIND(clock,"clock.now_ns");BIND(cpu,"cpu.control");BIND(inspect,"handles.inspect");
#undef BIND
    apps.hub=hub;apps.heap=heap;struct kernel_hub_table table={.owner=kernel_surface_owner(hub,"bootstrap.init"),.name="bootstrap.applications",.address=(uintptr_t)&apps.info,.bytes=sizeof(apps.info),.count=1,.entry_bytes=sizeof(apps.info)};
    kernel_surface_call publish=kernel_surface_find(hub,"hub.publish_table");if(!publish || CALL(publish,&table,0,0).status)return 0;
    render_status=(struct render_selection){.bytes=sizeof(render_status),.major=1,.hardware_index=UINT64_MAX};
    table=(struct kernel_hub_table){.owner=kernel_surface_owner(hub,"bootstrap.init"),.name="graphics.backend",.address=(uintptr_t)&render_status,.bytes=sizeof(render_status),.count=1,.entry_bytes=sizeof(render_status)};
    return !CALL(publish,&table,0,0).status;
}
static void refresh(struct application *record){
    struct process_info p;struct handle_publication_info publication;
    if(CALL(apps.process_info,record->process,&p,0).status || p.state!=PROCESS_RUNNING || p.stop_requested){record->state=4;record->error=2;}
    else if(!*record->ready || (!CALL(apps.inspect,record->ready,&publication,0).status && publication.process==record->process)){record->state=3;record->error=0;}
    else if(record->state==3){record->state=4;record->error=3;}
}
static int installations(uint64_t filesystem){
    if(apps.catalog)return 1;
    const struct config_value *base=config("config.drivers.directory",CONFIG_STRING);if(!base)return 0;
    size_t bytes=0;void *catalog=file(filesystem,"/system/state/installed.bin",&bytes);
    if(!catalog)return 1; /* Trusted low-level fixture/provider loading remains available. */
    kernel_surface_call install=kernel_surface_find(apps.hub,"applications.installations");
    kernel_surface_call adopt=kernel_surface_find(apps.hub,"filesystem.adopt"),backend=kernel_surface_find(apps.hub,"ext4_fs.fs_backend");
    struct kernel_surface_result mount=adopt && backend?adopt((uintptr_t)backend,filesystem,(uintptr_t)"/",0,0,0):module_result(0,0);
    if(!install || !cb_install_valid(catalog,bytes) || mount.status){release(catalog);return 0;}
    /* The selected system volume is not evidence of boot provenance by itself.
     * Mount only when Limine's disk identity matches the retained parent. */
    struct bootstrap_info volume;struct kernel_surface_result known=bootstrap_surface_info((uintptr_t)&volume,0,0,0,0,0);
    if(!known.status && volume.state==BOOTSTRAP_READY && volume.origin==BOOTSTRAP_ORIGIN_MATCH){
        kernel_surface_call attach=kernel_surface_find(apps.hub,"filesystem.mount");
        struct kernel_surface_result boot=attach?attach((uintptr_t)&volume.disk,(uintptr_t)"/boot",0,0,0,0):module_result(0,0);
        if(boot.status){release(catalog);return 0;}
    }else {kernel_surface_call log=kernel_surface_find(apps.hub,"hardware.log");static const char message[]="bootstrap: boot filesystem not mounted; boot disk identity unverified\n";if(log)CALL(log,message,sizeof(message)-1,0);}
    if(install(filesystem,(uintptr_t)catalog,bytes,mount.value,0,0).status){release(catalog);return 0;}apps.catalog=catalog;return 1;
}
static struct kernel_surface_result launch(uint64_t filesystem,const char *bundle){
    if(!installations(filesystem) || !bundle || !pd_path(bundle,1))return module_result(0,0);
    for(const struct application *p=apps.first;p;p=p->next)if(pd_same(p->bundle,bundle))return module_result(0,0);
    const struct config_value *base=config("config.drivers.directory",CONFIG_STRING);if(!base)return module_result(0,0);
    struct cb_install_record catalog_record;const unsigned char *catalog_name=apps.catalog?cb_install_find(apps.catalog,bundle,length(bundle),NULL,&catalog_record):NULL;
    int flat=catalog_name && catalog_record.path_bytes>17 && cb_install_equal(catalog_name+catalog_record.name_bytes,"/system/handlers/",17);
    char *directory=flat?string("/system/handlers"):path(base->data,bundle),*definition=NULL,*program=NULL;void *manifest=NULL,*packet=NULL;struct application *record=NULL;
    size_t definition_bytes=0,manifest_bytes=0,packet_bytes=0;uint64_t pid=0;int ok=0;unsigned error=1;
    if(!directory)goto done;
    error=2;char *definition_name=NULL;
    if(flat){size_t n=length(bundle);definition_name=allocate(n+14);if(!definition_name)goto done;copy(definition_name,bundle,n);copy(definition_name+n,".provider.cnf",14);}
    definition=bundle_file(filesystem,directory,flat?definition_name:"provider.cnf",&definition_bytes);release(definition_name);struct provider_definition d;
    if(!definition || !provider_definition_parse(definition,definition_bytes,&d))goto done;
    error=3;program=path(directory,d.executable);manifest=bundle_file(filesystem,directory,d.manifest,&manifest_bytes);packet=startup(filesystem,directory,d.files,&packet_bytes);record=allocate(sizeof(*record));
    if(!record)goto done;
    record->bundle=string(bundle);record->ready=string(d.ready?d.ready:"");record->state=2;
    if(!program || !manifest || !manifest_bytes || !packet || !record->bundle || !record->ready)goto done;
    error=4;struct cb_install_record installed;const unsigned char *installed_name=apps.catalog?cb_install_find(apps.catalog,bundle,length(bundle),NULL,&installed):NULL;
    if(installed_name){
        if((installed.flags!=CB_INSTALL_PROVIDER && installed.flags!=CB_INSTALL_MANAGER) || installed.cpu!=d.cpu || installed.stack_bytes!=d.stack_bytes || !cb_install_equal(installed.installation,d.identity,16) || installed.path_bytes!=length(program) || !cb_install_equal(installed_name+installed.name_bytes,program,installed.path_bytes) || installed.manifest_bytes!=manifest_bytes || !cb_install_equal(installed_name+installed.name_bytes+installed.path_bytes,manifest,manifest_bytes))goto done;
        kernel_surface_call installed_load=kernel_surface_find(apps.hub,"applications.installed_load");if(!installed_load)goto done;boot_trace("process",bundle);struct kernel_surface_result loaded=CALL(installed_load,bundle,packet,packet_bytes);boot_trace("process_done",bundle);pid=loaded.value;if(loaded.status)goto done;
    }else{
        struct process_request request={.abi=2,.cpu=d.cpu,.filesystem=filesystem,.path=program,.stack_bytes=d.stack_bytes,.startup=packet,.startup_bytes=packet_bytes};struct kernel_surface_result loaded=CALL(apps.load,&request,0,0);pid=loaded.value;if(loaded.status)goto done;
        error=5;struct handle_policy policy={.process=pid,.manifest=manifest,.bytes=manifest_bytes};copy(policy.identity.installation_id,d.identity,16);
        if(CALL(apps.admit,&policy,0,0).status)goto done;
    }
    error=6;if(CALL(apps.start,pid,0,0).status)goto done;
    ok=1;
done:
    if(!ok && pid && !CALL(apps.reap,pid,0,0).status)pid=0;
    if(record && pid){record->process=pid;if(!ok){record->state=4;record->error=1;}record->next=apps.first;apps.first=record;++apps.info.count;apps.info.state=2;}
    else if(record){release((void *)record->bundle);release((void *)record->ready);release(record);}
    if(!ok){apps.info.state=4;apps.info.error=error;}
    release(directory);release(definition);release(program);release(manifest);release(packet);return module_result(pid,ok);
}
struct kernel_surface_result bootstrap_surface_launch(MODULE_ARGS){MODULE_UNUSED();if(!enter())return module_result(0,0);struct kernel_surface_result result=CALL(apps.thread,THREAD_CURRENT,0,0).value?launch(a,(const void *)b):module_result(0,0);leave();return result;}
struct kernel_surface_result bootstrap_surface_release(MODULE_ARGS){
    MODULE_UNUSED();if(!enter())return module_result(0,0);struct process_info p;struct application **at=&apps.first;while(*at && (*at)->process!=a)at=&(*at)->next;
    struct application *record=*at;if(!record || !CALL(apps.process_info,a,&p,0).status){leave();return module_result(0,0);}*at=record->next;--apps.info.count;release((void *)record->bundle);release((void *)record->ready);release(record);if(!apps.info.count && !apps.info.worker){apps.info.state=0;apps.info.error=0;}leave();return module_result(1,1);
}
struct kernel_surface_result bootstrap_surface_apps(MODULE_ARGS){
    MODULE_UNUSED();if(!enter())return module_result(0,0);if(apps.info.worker || apps.info.state){uint64_t worker=apps.info.worker;leave();return module_result(worker,1);}
    const struct config_value *providers=config("config.boot.providers",CONFIG_STRING);if(!providers){leave();return module_result(0,0);}if(!*(const char *)providers->data){leave();return module_result(0,1);}
    struct thread_request r={.abi=1,.cpu=0,.stack_bytes=65536,.entry=(uintptr_t)kernel_surface_find(apps.hub,"bootstrap.apps_worker"),.major=1};apps.info.state=1;struct kernel_surface_result result=CALL(apps.create,&r,0,0);if(result.status){apps.info.state=4;apps.info.error=1;}else apps.info.worker=result.value;leave();return result;
}
static void log_text(const char *s){kernel_surface_call log=kernel_surface_find(apps.hub,"hardware.log");if(log)CALL(log,s,length(s),0);}
static void log_number(uint64_t n){char text[21];unsigned at=20;text[at]=0;do{text[--at]='0'+n%10;n/=10;}while(n);log_text(text+at);}
/* Storage selection and synchronous image loading have their own bounded I/O.
 * A provider's publication deadline starts only once that provider is running;
 * earlier boot work must not consume its readiness allowance. */
static int wait_provider(const char **phase){
    struct kernel_surface_result stamp=CALL(apps.clock,0,0,0);
    if(stamp.status || stamp.value>UINT64_MAX-UINT64_C(30000000000)){*phase="readiness clock";return 0;}
    uint64_t limit=stamp.value+UINT64_C(30000000000);
    for(;;){struct bootstrap_apps_info status;
        if(bootstrap_surface_apps_info((uintptr_t)&status,0,0,0,0,0).status){*phase="readiness inspection";return 0;}
        if(status.state==4){*phase="provider failure";return 0;}
        if(status.state==3)return 1;
        stamp=CALL(apps.clock,0,0,0);if(stamp.status){*phase="readiness clock";return 0;}
        if(stamp.value>=limit){*phase="publication timeout";return 0;}
        uint64_t wake=limit-stamp.value<1000000?limit:stamp.value+1000000;
        if(CALL(apps.thread,THREAD_BLOCK_UNTIL,wake,0).status){*phase="readiness wait";return 0;}
    }
}
/* Provider pairs are exclusive: stop/reap a failed accelerated pair before
 * binding the VGA roles. Never race two publishers for /@display or /@graphics. */
static int render_discard(struct application *marker){
    kernel_surface_call terminate=kernel_surface_find(apps.hub,"process.terminate");
    struct kernel_surface_result now=CALL(apps.clock,0,0,0);if(now.status || now.value>UINT64_MAX-UINT64_C(5000000000))return 0;
    uint64_t deadline=now.value+UINT64_C(5000000000);
    while(apps.first!=marker){struct application *p=apps.first;if(!p)return 0;
        struct process_info info;if(!CALL(apps.process_info,p->process,&info,0).status && info.state==PROCESS_RUNNING && (!terminate || CALL(terminate,p->process,0,0).status))return 0;
        while(CALL(apps.reap,p->process,0,0).status){
            now=CALL(apps.clock,0,0,0);if(now.status||now.value>=deadline)return 0;
            if(CALL(apps.thread,THREAD_BLOCK_UNTIL,now.value+1000000,0).status)return 0;
        }
        /* Reap must have removed the old publications and in-flight readers. */
        struct handle_publication_info pub;if(*p->ready&&!CALL(apps.inspect,p->ready,&pub,0).status&&pub.process==p->process)return 0;
        apps.first=p->next;--apps.info.count;release(p->bundle);release(p->ready);release(p);
    }
    apps.info.state=2;apps.info.error=0;return 1;
}
static int render_pair(uint64_t filesystem,const char *display,const char *graphics,const char **phase){
    const char *names[]={display,graphics};for(unsigned i=0;i<2;i++){
        *phase=names[i];if(bootstrap_surface_launch(filesystem,(uintptr_t)names[i],0,0,0,0).status || !wait_provider(phase))return 0;
    }return 1;
}
static int render_start(uint64_t filesystem,const char **phase){
    if(render_started)return 0;
    if(!installations(filesystem))return 0;
    kernel_surface_call discover=kernel_surface_find_version(apps.hub,"gpu_virtio.discover",1,0);
    if(discover){struct kernel_surface_result discovered=CALL(discover,0,0,0);log_text("bootstrap: virtio-gpu discovery ");if(discovered.status)log_text("unavailable");else log_number(discovered.value);log_text("\n");}
    const struct kernel_hub_table *inventory=kernel_hub_find_table(apps.hub,HWENUM_TABLE_NAME);
    kernel_surface_call probe=kernel_surface_find_version(apps.hub,RENDER_PROBE_SURFACE,1,0),drop=kernel_surface_find_version(apps.hub,RENDER_RELEASE_SURFACE,1,0);
    struct cb_install_record r;int providers=apps.catalog && cb_install_find(apps.catalog,"display-opengl",14,NULL,&r) && r.flags==CB_INSTALL_PROVIDER && cb_install_find(apps.catalog,"graphics-opengl",15,NULL,&r) && r.flags==CB_INSTALL_PROVIDER;
    unsigned backend=render_select(inventory?(const void *)inventory->address:NULL,inventory?inventory->bytes:0,probe,drop!=NULL,providers,&render_status);
    log_text("bootstrap: GPU discovery: ");log_number(render_status.devices);log_text(" display device(s), ");log_number(render_status.candidates);log_text(" qualified accelerator(s)\n");
    if(backend==RENDER_OPENGL){struct application *marker=apps.first;
        log_text("bootstrap: trying display-opengl + graphics-opengl\n");
        if(render_pair(filesystem,"display-opengl","graphics-opengl",phase)){render_started=1;render_status.state=RENDER_ACTIVE;return 1;}
        render_status.reason=RENDER_START_FAILED;
        if(!render_discard(marker)||drop(render_status.hardware_index,0,0,0,0,0).status){render_status.state=RENDER_FAILED;render_status.reason=RENDER_CLEANUP_FAILED;return 0;}
    }
    render_status.backend=RENDER_VGA;
    log_text("bootstrap: selecting display-vga + graphics-vga (fallback reason ");log_number(render_status.reason);log_text(")\n");
    if(!render_pair(filesystem,"display-vga","graphics-vga",phase)){render_status.state=RENDER_FAILED;return 0;}
    render_status.state=RENDER_ACTIVE;render_started=1;return 1;
}
/* Volume discovery precedes selected-module loading. Providers must not race
 * that loader for filesystem initialization or observe its partial hub state. */
static int wait_drivers(kernel_surface_call snapshot,const char **phase){
    *phase="selected driver readiness";
    struct kernel_surface_result stamp=CALL(apps.clock,0,0,0);
    if(!snapshot || stamp.status || stamp.value>UINT64_MAX-UINT64_C(30000000000))return 0;
    uint64_t limit=stamp.value+UINT64_C(30000000000);
    for(;;){
        struct driver_table status={0};
        if(CALL(snapshot,&status,0,0).status)return 0;
        if(status.finished){if(status.error){*phase="selected driver failure";return 0;}return 1;}
        stamp=CALL(apps.clock,0,0,0);if(stamp.status || stamp.value>=limit)return 0;
        uint64_t wake=limit-stamp.value<1000000?limit:stamp.value+1000000;
        if(CALL(apps.thread,THREAD_BLOCK_UNTIL,wake,0).status)return 0;
    }
}
struct kernel_surface_result bootstrap_surface_apps_worker(MODULE_ARGS){
    MODULE_UNUSED();while(__atomic_load_n(&apps.operation,__ATOMIC_ACQUIRE))if(CALL(apps.thread,THREAD_YIELD,0,0).status)return module_result(0,0);
    const char *active="volume",*phase="volume readiness";
    kernel_surface_call info=kernel_surface_find(apps.hub,"bootstrap.info");uint64_t time=CALL(apps.clock,0,0,0).value;if(time>UINT64_MAX-UINT64_C(30000000000))goto failed;uint64_t limit=time+UINT64_C(30000000000);struct bootstrap_info volume;char *list=NULL;
    for(;;){if(CALL(info,&volume,0,0).status || volume.state==BOOTSTRAP_FAILED || volume.state==BOOTSTRAP_DISCONNECTED)goto failed;if(volume.state==BOOTSTRAP_READY)break;time=CALL(apps.clock,0,0,0).value;if(time>=limit || CALL(apps.thread,THREAD_BLOCK_UNTIL,time+10000000,0).status)goto failed;}
    if(!wait_drivers(kernel_surface_find(apps.hub,"bootstrap.driver_info"),&phase))goto failed_list;
    const struct config_value *providers=config("config.boot.providers",CONFIG_STRING);if(!providers || !(list=string(providers->data)))goto failed;
    for(char *p=list;*p;){char *name=p;active=name;phase="launch";while(*p && *p!=',')++p;if(*p){*p++=0;if(!*p)goto failed_list;}
        /* Logical roles stay in boot.providers; implementation names are selected
         * together before dependent mouse/text providers may start. */
        if(pd_same(name,"display")){if(!render_start(volume.filesystem.cookie,&phase))goto failed_list;continue;}
        if(pd_same(name,"graphics")){if(!render_started)goto failed_list;continue;}
        boot_trace("begin",name);if(bootstrap_surface_launch(volume.filesystem.cookie,(uintptr_t)name,0,0,0,0).status)goto failed_list;
        boot_trace("loaded",name);
        /* Creation order is also readiness order for startup dependencies. */
        phase="publication readiness";
        if(!wait_provider(&phase))goto failed_list;
        boot_trace("ready",name);
    }
    release(list);list=NULL;
    for(const struct application *p=apps.first;p;p=p->next){log_text("bootstrap: provider ready: ");log_text(p->bundle);log_text("\n");}return module_result(apps.info.count,1);
failed_list:
    log_text("bootstrap: failed provider=");log_text(active);log_text(" phase=");log_text(phase);log_text(" error=");log_number(apps.info.error);
    for(const struct application *p=apps.first;p;p=p->next)if(p->state==4 || pd_same(p->bundle,active)){
        log_text(" record=");log_text(p->bundle);log_text(" record_error=");log_number(p->error);log_text(" pid=");log_number(p->process);struct process_info status;
        if(!CALL(apps.process_info,p->process,&status,0).status){log_text(" state=");log_number(status.state);log_text(" exit=");log_number(status.exit_code);log_text(" fault_vector=");log_number(status.fault_vector);log_text(" fault_rip=");log_number(status.fault_rip);}
    }
    log_text("\n");release(list);
failed:apps.info.state=4;apps.info.error=1;log_text("bootstrap: provider startup failed\n");return module_result(0,0);
}
struct kernel_surface_result bootstrap_surface_apps_info(MODULE_ARGS){
    MODULE_UNUSED();if(!a || !enter())return module_result(0,0);uint64_t worker=apps.info.worker;if(worker){struct thread_info t;if(!CALL(apps.thread,THREAD_INFO,worker,&t).status && t.state==THREAD_EXITED && !CALL(apps.thread,THREAD_REAP,worker,0).status)apps.info.worker=0;}
    unsigned status=3,error=0;for(struct application *p=apps.first;p;p=p->next){refresh(p);if(p->state==4){status=4;error=p->error;}else if(p->state!=3 && status!=4)status=2;}
    if(apps.info.count){apps.info.state=status;apps.info.error=error;}
    *(struct bootstrap_apps_info *)a=apps.info;leave();return module_result(1,1);
}

static struct kernel_surface_result application_snapshot(uint64_t id,struct bootstrap_application_info *out,int next){
    if(!out || !enter())return module_result(0,0);
    struct application *found=NULL;for(struct application *p=apps.first;p;p=p->next)if(next?(p->process>id && (!found || p->process<found->process)):p->process==id)found=p;
    if(found){refresh(found);*out=(struct bootstrap_application_info){found->process,found->state,found->error};}uint64_t process=found?found->process:0;leave();return module_result(process,found!=NULL);
}
struct kernel_surface_result bootstrap_surface_application_info(MODULE_ARGS){MODULE_UNUSED();return application_snapshot(a,(void *)b,0);}
struct kernel_surface_result bootstrap_surface_application_next(MODULE_ARGS){MODULE_UNUSED();return application_snapshot(a,(void *)b,1);}
