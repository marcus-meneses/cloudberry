#include "io.h"
#include "filesystem.h"
#include "process.h"
#include "threads.h"
#include "../../kernel/cpu.h"
struct backend {struct backend *next;kernel_surface_call query;struct filesystem_backend api;};
struct mount {struct mount *next;struct backend *backend;uint64_t id,cookie,refs;unsigned active,closing,owned,busy;char *path;};
struct fs_call_waiter {struct fs_call_waiter *next;uint64_t mount,thread,process;};
static struct {struct fs_call_waiter *waiters;const struct kernel_surfaces *hub;uintptr_t heap;uint64_t owner,root,serial;unsigned management;struct backend *backends;struct mount *mounts;kernel_surface_call cpu,alloc,free;} fs;
#define FC(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static uint64_t flock(void){return FC(fs.cpu,CPU_ENTER,0,0).value;}
static void funlock(uint64_t flags){FC(fs.cpu,CPU_LEAVE,flags,0);}
static void *falloc(size_t bytes){struct kernel_surface_result r=FC(fs.alloc,fs.heap,1,bytes);return r.status?NULL:(void *)r.value;}
static void ffree(void *p){if(p)FC(fs.free,fs.heap,p,0);}
static void fcopy(void *dst,const void *src,size_t n){for(size_t i=0;i<n;++i)((unsigned char *)dst)[i]=((const unsigned char *)src)[i];}
static size_t flength(const char *s){size_t n=0;while(s[n])++n;return n;}
static int prefix(const char *a,const char *b,size_t n){for(size_t i=0;i<n;++i)if(a[i]!=b[i])return 0;return 1;}
static int equal(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static int canonical(const char *path){if(!path || *path!='/')return 0;size_t bytes=flength(path);if(bytes>4095)return 0;for(size_t i=1;i<bytes;){size_t start=i;while(i<bytes && path[i]!='/')++i;size_t n=i-start;if(!n || (n==1 && path[start]=='.') || (n==2 && path[start]=='.' && path[start+1]=='.'))return 0;++i;}return 1;}
static int mount_path(const char *path){return canonical(path) && (!path[1] || path[flength(path)-1]!='/');}
static int foreground(void){return fs.hub && !FC(fs.cpu,CPU_CURRENT,0,0).value && !FC(fs.cpu,CPU_DEPTH,0,0).value;}
static struct mount *find_mount(uint64_t id){for(struct mount *m=fs.mounts;m;m=m->next)if(m->id==id)return m;return NULL;}
static struct backend *find_backend(kernel_surface_call query){for(struct backend *b=fs.backends;b;b=b->next)if(b->query==query)return b;return NULL;}
static cb_status_t backend_call(struct backend *b,uint64_t cookie,struct filesystem_request *r){r->status=CB_RESOURCE_FAILURE;r->actual=0;if(r->operation==FS_RMDIR && !b->api.minor){r->status=CB_UNAVAILABLE;return r->status;}if(b->api.invoke(cookie,(uintptr_t)r,0,0,0,0).status)r->status=CB_RESOURCE_FAILURE;return r->status;}
static struct kernel_surface_result manager_register(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground() || !a)return module_result(0,0);kernel_surface_call query=(kernel_surface_call)a;
    if(find_backend(query))return module_result(1,1);
    /* Only a published callable can supply a descriptor; pin both code owners. */
    if(kernel_hub_pin_call(fs.hub,query,1).status)return module_result(0,0);
    struct filesystem_backend api={0};struct backend *record=NULL;
    if(query((uintptr_t)&api,sizeof(api),0,0,0,0).status || api.bytes!=sizeof(api) || api.major!=2 || api.minor>1 || !api.invoke)goto fail;
    if(kernel_hub_pin_call(fs.hub,api.invoke,1).status)goto fail;
    record=falloc(sizeof(*record));if(!record){(void)kernel_hub_pin_call(fs.hub,api.invoke,0);goto fail;}
    record->query=query;record->api=api;record->next=fs.backends;fs.backends=record;return module_result(1,1);
fail:(void)kernel_hub_pin_call(fs.hub,query,0);return module_result(0,0);
}
static struct kernel_surface_result add_mount(struct backend *backend,uint64_t cookie,const char *path,unsigned owned){
    if(!cookie || !mount_path(path))return module_result(0,0);
    for(struct mount *m=fs.mounts;m;m=m->next){if(equal(m->path,path))return !owned && m->backend==backend && m->cookie==cookie?module_result(m->id,1):module_result(0,0);if(m->backend==backend && m->cookie==cookie)return module_result(0,0);}
    if(fs.serial==UINT64_MAX)return module_result(0,0);
    struct mount *m=falloc(sizeof(*m));char *copy=falloc(flength(path)+1);if(!m || !copy){ffree(copy);ffree(m);return module_result(0,0);}fcopy(copy,path,flength(path)+1);
    m->backend=backend;m->cookie=cookie;m->path=copy;m->owned=owned;m->closing=owned;m->id=++fs.serial;uint64_t flags=flock();m->next=fs.mounts;fs.mounts=m;funlock(flags);return module_result(m->id,1);
}
static struct kernel_surface_result manager_adopt(MODULE_ARGS){MODULE_UNUSED();if(!foreground() || !a || !c)return module_result(0,0);struct backend *backend=find_backend((kernel_surface_call)a);return backend?add_mount(backend,b,(const void *)c,0):module_result(0,0);}
static struct kernel_surface_result manager_mount(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground() || !a || !mount_path((const void *)b) || (c&~FS_MOUNT_WRITE))return module_result(0,0);
    for(struct mount *m=fs.mounts;m;m=m->next)if(equal(m->path,(const char *)b))return module_result(0,0);
    struct backend *selected=NULL;
    for(struct backend *backend=fs.backends;backend;backend=backend->next){struct filesystem_request r={.operation=FS_PROBE,.device=(const void *)a};cb_status_t status=backend_call(backend,0,&r);if(status==CB_NOT_FOUND)continue;if(status || selected)return module_result(status?status:CB_BAD_REQUEST,0);selected=backend;}
    if(!selected)return module_result(CB_NOT_FOUND,0);
    /* Reserve bookkeeping before touching disk, so OOM never leaks a mount. */
    struct kernel_surface_result added=add_mount(selected,UINT64_MAX,(const void *)b,1);if(added.status)return added;
    struct mount *m=find_mount(added.value);m->closing=1;
    struct filesystem_request r={.operation=FS_MOUNT,.device=(const void *)a,.flags=c};cb_status_t status=backend_call(selected,0,&r);
    if(!status && r.cookie){uint64_t flags=flock();m->cookie=r.cookie;m->closing=0;funlock(flags);return added;}
    uint64_t flags=flock();struct mount **at=&fs.mounts;while(*at!=m)at=&(*at)->next;*at=m->next;funlock(flags);ffree(m->path);ffree(m);return module_result(status?status:CB_RESOURCE_FAILURE,0);
}
static struct kernel_surface_result manager_unmount(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground())return module_result(0,0);uint64_t flags=flock();struct mount *m=find_mount(a);
    if(!m || !m->owned || m->refs || m->active || m->closing){funlock(flags);return module_result(CB_BUSY,0);}
    for(struct mount *child=fs.mounts;child;child=child->next){size_t n=flength(m->path);if(child!=m && (!m->path[1] || (flength(child->path)>n && child->path[n]=='/' && prefix(child->path,m->path,n)))){funlock(flags);return module_result(CB_BUSY,0);}}
    m->closing=1;funlock(flags);struct filesystem_request r={.operation=FS_UNMOUNT};cb_status_t status=backend_call(m->backend,m->cookie,&r);flags=flock();if(status){m->closing=0;funlock(flags);return module_result(status,0);}struct mount **at=&fs.mounts;while(*at!=m)at=&(*at)->next;*at=m->next;funlock(flags);ffree(m->path);ffree(m);return module_result(1,1);
}
struct kernel_surface_result filesystem_surface_retain(MODULE_ARGS){MODULE_UNUSED();if(!fs.hub)return module_result(0,0);uint64_t flags=flock();struct mount *m=find_mount(a);int ok=m && !m->closing && m->refs!=UINT64_MAX;if(ok)++m->refs;funlock(flags);return module_result(ok,ok);}
struct kernel_surface_result filesystem_surface_release(MODULE_ARGS){MODULE_UNUSED();if(!fs.hub)return module_result(0,0);uint64_t flags=flock();struct mount *m=find_mount(a);int ok=m && m->refs;if(ok)--m->refs;funlock(flags);return module_result(ok,ok);}
/* Re-evaluate on each operation, including calls on already-open streams.
 * Metadata-less format backends fail closed for user processes. */
static cb_status_t access_as(struct mount *m,struct filesystem_request *r,struct cb_credentials who,const uint64_t *groups){
    if(r->operation!=FS_CHOWN){r->uid=who.uid;r->gid=who.gid;}
    int admin=!who.uid || who.gid==1;for(size_t i=0;i<who.groups;++i)if(groups && groups[i]==1)admin=1;
    if(r->operation==FS_CHOWN && (!admin || r->uid>UINT32_MAX || r->gid>UINT32_MAX))return CB_DENIED;
    if(!who.uid){if(r->operation!=FS_EXEC)return CB_OK;struct filesystem_request st={.operation=FS_STAT,.path=r->path};cb_status_t status=backend_call(m->backend,m->cookie,&st);return status?status:!st.stat.valid || st.stat.type!=FS_REGULAR || !(st.stat.permissions&0111)?CB_DENIED:CB_OK;}
    struct filesystem_request target={.operation=FS_STAT,.path=r->path};cb_status_t target_status=backend_call(m->backend,m->cookie,&target);if(target_status!=CB_OK && target_status!=CB_NOT_FOUND)return target_status;
    if(!target_status && (r->operation==FS_CHMOD || r->operation==FS_CHOWN) && (!target.stat.valid || (!admin && target.stat.uid!=who.uid)))return CB_DENIED;
    int creates=target_status==CB_NOT_FOUND && r->operation==FS_PREPARE && r->mode!=CB_FILE_READ;
    char path[4096];size_t n=flength(r->path);if(n>=sizeof(path))return CB_BAD_REQUEST;fcopy(path,r->path,n+1);
    for(size_t end=0;;){
        if(!end)end=1;else while(end<n && path[end]!='/')++end;
        char saved=path[end];path[end]=0;struct filesystem_request st={.operation=FS_STAT,.path=path};cb_status_t status=backend_call(m->backend,m->cookie,&st);path[end]=saved;
        int final=end==n;unsigned needed=final?(r->flags&FS_ACCESS_SEARCH?1:r->operation==FS_STAT || r->operation==FS_REMOVE || r->operation==FS_RMDIR || r->operation==FS_CHMOD || r->operation==FS_CHOWN?0:r->operation==FS_EXEC?1:r->operation==FS_LIST?5:r->operation==FS_READ?4:r->operation==FS_PREPARE && r->mode==CB_FILE_READ?4:2):1;
        if(status==CB_NOT_FOUND && final && ((r->operation==FS_PREPARE && r->mode!=CB_FILE_READ) || r->operation==FS_MKDIR)){if(who.uid>UINT32_MAX || who.gid>UINT32_MAX)return CB_DENIED;return CB_OK;}
        if(status)return status;
        if(!st.stat.valid)return CB_DENIED;
        unsigned shift=0;if(st.stat.uid==who.uid)shift=6;else {int member=who.gid==st.stat.gid;for(size_t i=0;i<who.groups;++i)if(groups[i]==st.stat.gid)member=1;if(member)shift=3;}
        if((st.stat.permissions>>shift & needed)!=needed)return CB_DENIED;
        if(!final && st.stat.type!=FS_DIRECTORY)return CB_DENIED;
        if(final)return ((r->flags&FS_ACCESS_SEARCH) && st.stat.type!=FS_DIRECTORY) || (r->operation==FS_EXEC && st.stat.type!=FS_REGULAR)?CB_DENIED:CB_OK;
        /* Create/unlink require write+search on their parent, not target write. */
        if(end+1<=n){size_t next=end+1;while(next<n && path[next]!='/')++next;if(next==n && (r->operation==FS_REMOVE || r->operation==FS_RMDIR || r->operation==FS_MKDIR || creates)){if((st.stat.permissions>>shift & 3)!=3)return CB_DENIED;
            if((r->operation==FS_REMOVE || r->operation==FS_RMDIR) && (st.stat.permissions&01000) && st.stat.uid!=who.uid){struct filesystem_request target={.operation=FS_STAT,.path=r->path};status=backend_call(m->backend,m->cookie,&target);if(status)return status;if(!target.stat.valid || target.stat.uid!=who.uid)return CB_DENIED;}
        }}
        ++end;
    }
}
static struct mount *route(const char *,const char **);
static cb_status_t namespace_access(struct mount *m,struct filesystem_request *r,struct cb_credentials who,const uint64_t *groups){
    /* A mounted root replaces its mountpoint, but cannot bypass search access
     * to ancestors in other mounts (for example /system/root/private-mount). */
    size_t n=flength(m->path);char prefix_path[4096];
    for(size_t end=1;end<n;++end)if(end==1 || m->path[end]=='/'){
        fcopy(prefix_path,m->path,end);prefix_path[end]=0;const char *relative=NULL;struct mount *parent=route(prefix_path,&relative);if(!parent)return CB_UNAVAILABLE;
        struct filesystem_request search={.operation=FS_STAT,.flags=FS_ACCESS_SEARCH,.path=relative,.process=r->process};cb_status_t status=access_as(parent,&search,who,groups);filesystem_surface_release(parent->id,0,0,0,0,0);if(status)return status;
    }
    return access_as(m,r,who,groups);
}
static cb_status_t access_file(struct mount *m,struct filesystem_request *r){
    if(!r->process)return CB_OK;
    kernel_surface_call active=kernel_surface_find(fs.hub,"process.active");if(!active || !active(r->process,0,0,0,0,0).value)return CB_REVOKED;
    if(r->operation==FS_SYNC)return CB_DENIED;
    char absolute[4096];size_t root=flength(m->path),relative=flength(r->path);if(root==1){if(relative>4095)return CB_BAD_REQUEST;fcopy(absolute,r->path,relative+1);}else {if(root+relative>4095)return CB_BAD_REQUEST;fcopy(absolute,m->path,root);if(relative==1)absolute[root]=0;else fcopy(absolute+root,r->path,relative+1);}
    kernel_surface_call policy=kernel_surface_find(fs.hub,"applications.access");if(!policy)return CB_DENIED;struct kernel_surface_result permitted=policy(r->process,(uintptr_t)absolute,r->operation==FS_PREPARE && r->mode==CB_FILE_READ?FS_READ:r->operation,0,0,0);if(permitted.status || permitted.value)return permitted.status?CB_DENIED:permitted.value;
    if(r->operation==FS_SYNC)return CB_DENIED; /* Whole-mount sync stays kernel-only. */
    kernel_surface_call credentials=kernel_surface_find(fs.hub,"process.credentials");struct cb_credentials who;
    if(!credentials || credentials(r->process,0,(uintptr_t)&who,sizeof(who),0,0).status)return CB_DENIED;
    if(!who.groups || !who.uid)return namespace_access(m,r,who,NULL);
    if(who.groups>(SIZE_MAX-sizeof(who))/8)return CB_DENIED;
    size_t bytes=sizeof(who)+who.groups*8;struct cb_credentials *snapshot=falloc(bytes);if(!snapshot)return CB_RESOURCE_FAILURE;
    struct kernel_surface_result got=credentials(r->process,0,(uintptr_t)snapshot,bytes,0,0);
    cb_status_t status=got.status || got.value!=bytes || snapshot->generation!=who.generation?CB_REVOKED:namespace_access(m,r,*snapshot,(const void *)(snapshot+1));ffree(snapshot);return status;
}
struct kernel_surface_result filesystem_surface_file(MODULE_ARGS){
    MODULE_UNUSED();struct filesystem_request *r=(void *)b;if(!foreground() || !r)return module_result(0,0);r->status=CB_RESOURCE_FAILURE;r->actual=0;
    if(r->operation<FS_STAT || r->operation>FS_RMDIR || (r->operation!=FS_SYNC && !canonical(r->path))){r->status=CB_BAD_REQUEST;return module_result(1,1);}
    uint64_t flags=flock();struct mount *m=find_mount(a);if(!m || m->closing || m->active==UINT32_MAX){funlock(flags);return module_result(1,1);}++m->active;
    kernel_surface_call control=kernel_surface_find(fs.hub,"threads.control");
    while(m->busy){kernel_surface_call active=kernel_surface_find(fs.hub,"process.active");if(r->process && (!active || !active(r->process,0,0,0,0,0).value)){--m->active;funlock(flags);r->status=CB_REVOKED;return module_result(1,1);}if(!control){--m->active;funlock(flags);r->status=CB_BUSY;return module_result(1,1);}struct fs_call_waiter waiter={.mount=a,.process=r->process,.thread=FC(control,THREAD_CURRENT,0,0).value,.next=fs.waiters};fs.waiters=&waiter;funlock(flags);struct kernel_surface_result waited=FC(control,THREAD_BLOCK,0,0);flags=flock();struct fs_call_waiter **at=&fs.waiters;while(*at!=&waiter)at=&(*at)->next;*at=waiter.next;if(waited.status){--m->active;funlock(flags);return module_result(1,1);}}
    m->busy=1;funlock(flags);
    r->status=access_file(m,r);if(!r->status && r->operation!=FS_EXEC)(void)backend_call(m->backend,m->cookie,r);flags=flock();m->busy=0;--m->active;if(control)for(struct fs_call_waiter *w=fs.waiters;w;w=w->next)if(w->mount==a)(void)FC(control,THREAD_WAKE,w->thread,0);funlock(flags);return module_result(1,1);
}
static struct mount *route(const char *path,const char **relative){
    struct mount *best=NULL;size_t longest=0;uint64_t flags=flock();
    for(struct mount *m=fs.mounts;m;m=m->next){size_t n=flength(m->path);if(m->closing || m->refs==UINT64_MAX || n<longest)continue;if(!m->path[1] || (flength(path)>=n && prefix(path,m->path,n) && (!path[n] || path[n]=='/'))){best=m;longest=n;}}
    if(best){++best->refs;*relative=longest==1?path:path[longest]?path+longest:"/";}funlock(flags);return best;
}
static cb_status_t invoke_filesystem(struct io_invocation *r){
    if(r->process!=fs.owner || !fs.owner)return CB_DENIED;
    if(r->bytes<32+sizeof(struct cb_filesystem_request))return CB_BAD_REQUEST;
    uint64_t owner_pid,op;fcopy(&owner_pid,r->input,8);fcopy(&op,(const unsigned char *)r->input+8,8);struct cb_filesystem_request req;fcopy(&req,(const unsigned char *)r->input+32,sizeof(req));
    if(!req.path_bytes || req.path_bytes>4095 || r->bytes!=32+sizeof(req)+req.path_bytes+(op==CB_FILESYSTEM_CHOWN?sizeof(struct cb_filesystem_owner):0))return CB_BAD_REQUEST;
    char path[4096];fcopy(path,(const unsigned char *)r->input+32+sizeof(req),req.path_bytes);path[req.path_bytes]=0;if(flength(path)!=req.path_bytes || !canonical(path))return CB_BAD_REQUEST;
    int writes=(op==CB_FILESYSTEM_OPEN && req.mode!=CB_FILE_READ) || op==CB_FILESYSTEM_REMOVE || op==CB_FILESYSTEM_RMDIR || op==CB_FILESYSTEM_MKDIR || op==CB_FILESYSTEM_CHMOD || op==CB_FILESYSTEM_CHOWN;
    kernel_surface_call data=kernel_surface_find(fs.hub,"applications.data");struct kernel_surface_result grant=data?data((uintptr_t)r->input+16,(uintptr_t)path,0,0,0,0):module_result(0,0);int own_data=!grant.status && grant.value;
    const char identity_path[]="/system/config/identity";size_t identity_bytes=sizeof(identity_path)-1;
    if(req.path_bytes>=identity_bytes && prefix(path,identity_path,identity_bytes) && (!path[identity_bytes] || path[identity_bytes]=='/') && !own_data)return CB_DENIED;
    if(writes && !own_data && ((req.path_bytes>=5 && prefix(path,"/boot",5) && (!path[5] || path[5]=='/')) || (req.path_bytes>=7 && prefix(path,"/system",7) && (!path[7] || path[7]=='/'))))return CB_DENIED;
    const char *relative=NULL;struct mount *m=route(path,&relative);if(!m)return CB_UNAVAILABLE;cb_status_t status=CB_BAD_REQUEST;
    if(op==CB_FILESYSTEM_OPEN){if(r->capacity>=8 && !req.cursor && req.mode>=CB_FILE_READ && req.mode<=CB_FILE_APPEND){kernel_surface_call open=kernel_surface_find(fs.hub,"handles.file_open");struct kernel_surface_result opened=open?open(owner_pid,m->id,(uintptr_t)relative,req.mode,0,0):module_result(0,0);status=opened.status?opened.status:CB_OK;if(!opened.status){fcopy(r->output,&opened.value,8);r->actual=8;}}}
    else if((!req.mode || (op==CB_FILESYSTEM_CHMOD && !(req.mode&~0777))) && (op==CB_FILESYSTEM_LIST || !req.cursor)){
        struct filesystem_request call={.path=relative,.cursor=req.cursor,.process=owner_pid};
        if(op==CB_FILESYSTEM_STAT && r->capacity>=sizeof(struct cb_filesystem_stat)){call.operation=FS_STAT;filesystem_surface_file(m->id,(uintptr_t)&call,0,0,0,0);status=call.status;if(!status){struct cb_filesystem_stat out={call.stat.bytes,call.stat.type==FS_DIRECTORY,0};fcopy(r->output,&out,sizeof(out));r->actual=sizeof(out);}}
        else if(op==CB_FILESYSTEM_LIST){call.operation=FS_LIST;filesystem_surface_file(m->id,(uintptr_t)&call,0,0,0,0);status=call.status;if(!status && call.actual){if(call.entry.name_bytes>sizeof(call.entry.name))status=CB_RESOURCE_FAILURE;else if(r->capacity<sizeof(struct cb_filesystem_entry))status=CB_BUFFER_TOO_SMALL;else {struct cb_filesystem_entry out={.cursor=call.cursor,.directory=call.entry.type==FS_DIRECTORY,.name_bytes=call.entry.name_bytes};fcopy(out.name,call.entry.name,call.entry.name_bytes);fcopy(r->output,&out,sizeof(out));r->actual=sizeof(out);}}}
        else if(op==CB_FILESYSTEM_METADATA && r->capacity>=sizeof(struct cb_filesystem_metadata)){call.operation=FS_STAT;filesystem_surface_file(m->id,(uintptr_t)&call,0,0,0,0);status=call.status;if(!status){if(!call.stat.valid)status=CB_BAD_REQUEST;else {struct cb_filesystem_metadata out={call.stat.bytes,call.stat.uid,call.stat.gid,call.stat.permissions,call.stat.type==FS_DIRECTORY};fcopy(r->output,&out,sizeof(out));r->actual=sizeof(out);}}}
        else if(op==CB_FILESYSTEM_CHMOD || op==CB_FILESYSTEM_CHOWN){call.operation=op==CB_FILESYSTEM_CHMOD?FS_CHMOD:FS_CHOWN;call.mode=req.mode;if(op==CB_FILESYSTEM_CHOWN){struct cb_filesystem_owner owner;fcopy(&owner,(const unsigned char *)r->input+32+sizeof(req)+req.path_bytes,sizeof(owner));call.uid=owner.uid;call.gid=owner.gid;}filesystem_surface_file(m->id,(uintptr_t)&call,0,0,0,0);status=call.status;}
        else if(op==CB_FILESYSTEM_REMOVE || op==CB_FILESYSTEM_RMDIR || op==CB_FILESYSTEM_MKDIR){call.operation=op==CB_FILESYSTEM_REMOVE?FS_REMOVE:op==CB_FILESYSTEM_RMDIR?FS_RMDIR:FS_MKDIR;filesystem_surface_file(m->id,(uintptr_t)&call,0,0,0,0);status=call.status;}
    }
    filesystem_surface_release(m->id,0,0,0,0,0);return status;
}
struct kernel_surface_result filesystem_surface_init(MODULE_ARGS){
    MODULE_UNUSED();if(fs.hub)return module_result(1,fs.hub==(const void *)a);if(!a || !b)return module_result(0,0);const struct kernel_surfaces *hub=(const void *)a;
    fs.cpu=kernel_surface_find(hub,"cpu.control");fs.alloc=kernel_surface_find(hub,"mem.calloc");fs.free=kernel_surface_find(hub,"mem.free");if(!fs.cpu || !fs.alloc || !fs.free || !kernel_surface_find(hub,"applications.data"))return module_result(0,0);fs.hub=hub;fs.heap=b;
    const struct kernel_surface *entries=kernel_surface_entries(hub);const char suffix[]=".fs_backend";
    for(uint32_t i=0;i<kernel_surface_count(hub);++i){size_t n=flength(entries[i].name);if(n>=sizeof(suffix)-1 && equal(entries[i].name+n-(sizeof(suffix)-1),suffix) && filesystem_surface_register((uintptr_t)entries[i].call,0,0,0,0,0).status){while(fs.backends){struct backend *next=fs.backends->next;(void)kernel_hub_pin_call(hub,fs.backends->api.invoke,0);(void)kernel_hub_pin_call(hub,fs.backends->query,0);ffree(fs.backends);fs.backends=next;}fs.hub=NULL;return module_result(0,0);}}
    return module_result(1,1);
}
struct kernel_surface_result filesystem_surface_bind(MODULE_ARGS){MODULE_UNUSED();if(!a)return module_result(0,0);if(equal((const void *)a,"filesystem")){uint64_t flags=flock();struct mount *m=find_mount(c);int ok=m && !m->closing && equal(m->path,"/");if(ok){fs.owner=b;fs.root=c;}funlock(flags);return module_result(ok,ok);}return module_result(1,1);}
struct kernel_surface_result filesystem_surface_invoke(MODULE_ARGS){MODULE_UNUSED();if(!a || !fs.hub)return module_result(0,0);struct io_invocation *r=(void *)a;if(r->operation!=CB_IO_FILESYSTEM)return module_result(0,0);r->status=invoke_filesystem(r);return module_result(1,1);}

static int management_enter(void){if(!foreground())return 0;uint64_t flags=flock();int ok=!fs.management;if(ok)fs.management=1;funlock(flags);return ok;}
static void management_leave(void){uint64_t flags=flock();fs.management=0;funlock(flags);}
struct kernel_surface_result filesystem_surface_register(MODULE_ARGS){if(!management_enter())return module_result(CB_BUSY,0);struct kernel_surface_result result=manager_register(a,b,c,d,e,f);management_leave();return result;}
struct kernel_surface_result filesystem_surface_mount(MODULE_ARGS){if(!management_enter())return module_result(CB_BUSY,0);struct kernel_surface_result result=manager_mount(a,b,c,d,e,f);management_leave();return result;}
struct kernel_surface_result filesystem_surface_adopt(MODULE_ARGS){if(!management_enter())return module_result(CB_BUSY,0);struct kernel_surface_result result=manager_adopt(a,b,c,d,e,f);management_leave();return result;}
struct kernel_surface_result filesystem_surface_unmount(MODULE_ARGS){if(!management_enter())return module_result(CB_BUSY,0);struct kernel_surface_result result=manager_unmount(a,b,c,d,e,f);management_leave();return result;}

struct kernel_surface_result filesystem_surface_revoke(MODULE_ARGS){MODULE_UNUSED();if(!fs.hub)return module_result(0,0);uint64_t flags=flock();kernel_surface_call control=kernel_surface_find(fs.hub,"threads.control");if(control)for(struct fs_call_waiter *w=fs.waiters;w;w=w->next)if(w->process==a)(void)FC(control,THREAD_WAKE,w->thread,0);funlock(flags);return module_result(1,1);}
