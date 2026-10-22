/* One authoritative supplemental registry. Readers pin immutable snapshots;
 * publication drops only the publisher reference, never a borrowed reader. */
#include "manifest.h"
struct registry_snapshot {uint64_t refs,token,bytes;
    unsigned char data[];
    };
struct installation_ref {struct cb_install_record record;
    const unsigned char *name;
    struct registry_snapshot *snapshot;
    uint64_t owner;
    };
static uint64_t registry_generation(void){return runtime.registry?cb_reg_u64(runtime.registry->data+16):1;
    }
static struct registry_snapshot *registry_pin(void){uint64_t f=lock();
    struct registry_snapshot *p=runtime.registry;
    if(p)++p->refs;
    unlock(f);
    return p;
    }
static void registry_drop(struct registry_snapshot *p){if(!p)return;
    uint64_t f=lock();
    int last=!--p->refs;
    unlock(f);
    if(last)release(p);
    }
static cb_status_t registry_context(uint64_t pid,struct cb_install_context *out,char home[4096]){
    kernel_surface_call get=kernel_surface_find(runtime.hub,"process.credentials"),path=kernel_surface_find(runtime.hub,"process.home"),live=kernel_surface_find(runtime.hub,"process.active");
    struct cb_credentials who;
    if(!pid || !get || !path || !live || !CALL(live,pid,0,0).value || get(pid,0,(uintptr_t)&who,sizeof(who),0,0).status)return CB_REVOKED;
    struct kernel_surface_result got=path(pid,(uintptr_t)home,4096,0,0,0);
    if(got.status || got.value>4095)return CB_UNAVAILABLE;
    *out=(struct cb_install_context){who.uid,who.gid,got.value,!who.uid || CALL(get,pid,3,1).value};
    return CB_OK;
    }
static uint64_t registry_uid(uint64_t pid){struct cb_credentials who;
    kernel_surface_call get=kernel_surface_find(runtime.hub,"process.credentials");
    return get && !get(pid,0,(uintptr_t)&who,sizeof(who),0,0).status?who.uid:UINT64_MAX-1;
    }
static int installation_lookup(uint64_t uid,const void *name,size_t bytes,const uint8_t *identity,struct installation_ref *out){
    *out=(struct installation_ref){.owner=CB_REGISTRY_SYSTEM};
    out->snapshot=registry_pin();
    const unsigned char *global=NULL;
    if(out->snapshot)for(size_t at=32;at<out->snapshot->bytes;){const unsigned char *p=out->snapshot->data+at;
        struct cb_install_record r;
        cb_install_copy(&r,p+16,sizeof(r));
        const unsigned char *n=p+80;
        uint64_t owner=cb_reg_u64(p);
        if((owner==uid || owner==CB_REGISTRY_SYSTEM) && (identity?cb_install_equal(identity,r.installation,16):(r.name_bytes==bytes && cb_install_equal(n,name,bytes)))){if(owner==uid){out->record=r;
                out->name=n;
                out->owner=owner;
                return 1;
                }global=p;
            }
        at+=16+r.bytes;
        }
    if(global){out->owner=CB_REGISTRY_SYSTEM;
        cb_install_copy(&out->record,global+16,sizeof(out->record));
        out->name=global+80;
        return 1;
        }
    out->name=runtime.catalog?cb_install_find(runtime.catalog,name,bytes,identity,&out->record):NULL;
    if(out->name)return 1;
    registry_drop(out->snapshot);
    out->snapshot=NULL;
    return 0;
    }
static void installation_drop(struct installation_ref *r){registry_drop(r->snapshot);
    r->snapshot=NULL;
    }
static int installation_process(uint64_t pid,struct installation_ref *out){uint8_t id[24];uint64_t scope;
    kernel_surface_call get=kernel_surface_find(runtime.hub,"handles.identity");
    if(!get || get(pid,(uintptr_t)id,sizeof(id),0,0,0).status)return 0;
    cb_install_copy(&scope,id+16,8);
    return installation_lookup(scope,NULL,0,id,out);
    }
static const unsigned char *registry_find(struct registry_snapshot *s,uint64_t uid,const unsigned char id[16]){if(!s)return NULL;
    for(size_t at=32;at<s->bytes;){const unsigned char *p=s->data+at;
        struct cb_install_record r;
        cb_install_copy(&r,p+16,sizeof(r));
        if(cb_reg_u64(p)==uid && cb_install_equal(id,r.installation,16))return p;
        at+=16+r.bytes;
        }return NULL;
    }
static int registry_resource(void *context,const char *name,const char *scope,int publish){int *io=context;
    *io=mf_equal(name,"/@io");
    return !(publish && name[0]=='/' && name[1]=='@') && !mf_equal(scope,"process-manager") && !mf_equal(scope,"display") && !mf_equal(scope,"filesystem") && !mf_equal(scope,"graphics") && (!*io || mf_equal(scope,"self"));
    }
static int registry_operation(void *context,const char *name){return !*(int *)context || mf_equal(name,"now_ns");
    }
static int registry_in_use(uint64_t owner,const unsigned char identity[16]){
    for(struct child *c=runtime.children;c;c=c->next)if(!c->reaped){struct process_info info;if(CALL(runtime.info,c->process,&info,0).status)return 1;
        if(info.state==PROCESS_EXITED || info.state==PROCESS_FAULTED)continue;
        unsigned char admitted[24];kernel_surface_call get=kernel_surface_find(runtime.hub,"handles.identity");
        if(get && !get(c->process,(uintptr_t)admitted,sizeof(admitted),0,0,0).status && cb_install_equal(admitted,identity,16) && (owner==CB_REGISTRY_SYSTEM?cb_reg_u64(admitted+16)==0:cb_reg_u64(admitted+16)==owner))return 1;
    }return 0;
}
static cb_status_t registry_validate(struct registry_snapshot *s,uint64_t uid,int admin,int restore){
    if(s->bytes<32 || cb_reg_u64(s->data)!=CB_REGISTRY_MAGIC || cb_reg_u64(s->data+8)!=s->bytes || (!restore && cb_reg_u64(s->data+16)!=registry_generation()+1) || (restore && cb_reg_u64(s->data+16)<registry_generation()) || !cb_reg_u64(s->data+16))return CB_BAD_REQUEST;
    uint64_t count=0;
    for(size_t at=32;at<s->bytes;){if(s->bytes-at<80)return CB_BAD_REQUEST;
        const unsigned char *p=s->data+at;
        uint64_t owner=cb_reg_u64(p);
        struct cb_install_record r;
        cb_install_copy(&r,p+16,sizeof(r));
        if(cb_reg_u64(p+8) || r.bytes<64 || r.bytes>s->bytes-at-16 || !r.name_bytes || r.name_bytes>207 || !r.path_bytes || r.path_bytes>4095 || !cb_frame_fits(64,r.name_bytes,r.path_bytes,r.bytes) || r.manifest_bytes!=r.bytes-64-r.name_bytes-r.path_bytes || !r.manifest_bytes || (r.flags!=1 && r.flags!=3) || r.cpu || r.stack_bytes<4096)return CB_BAD_REQUEST;
        const unsigned char *name=p+80,*path=name+r.name_bytes;
        unsigned any=0;
        for(unsigned i=0;i<16;++i)any|=r.installation[i];
        if(!any)return CB_BAD_REQUEST;
        for(size_t i=0;i<r.name_bytes;++i)if(!((name[i]>='a'&&name[i]<='z') || (name[i]>='A'&&name[i]<='Z') || (name[i]>='0'&&name[i]<='9') || (i && (name[i]=='-' || name[i]=='_'))))return CB_BAD_REQUEST;
        struct cb_install_record builtin;
        if(cb_install_find(runtime.catalog,name,r.name_bytes,NULL,&builtin) || cb_install_find(runtime.catalog,NULL,0,r.installation,&builtin))return CB_DENIED;
        if(path[0]!='/' || (owner==CB_REGISTRY_SYSTEM?(r.path_bytes<14 || !cb_install_equal(path,"/system/apps/",13)):(r.path_bytes<15 || !cb_install_equal(path,"/home/",6))))return CB_DENIED;
        size_t component=1,last=1;
        for(size_t i=1;i<=r.path_bytes;++i){if(i==r.path_bytes || path[i]=='/'){size_t n=i-component;
                if(!n || (n==1 && path[component]=='.') || (n==2 && path[component]=='.'&&path[component+1]=='.'))return CB_BAD_REQUEST;
                last=component;
                component=i+1;
                }else if(!path[i])return CB_BAD_REQUEST;
            }
        if(owner!=CB_REGISTRY_SYSTEM && (last<7 || !cb_install_equal(path+last-7,"/_apps/",7)))return CB_DENIED;
        if(r.path_bytes-last!=34+r.name_bytes || path[last]!='_' || path[last+33]!='.' || !cb_install_equal(path+last+34,name,r.name_bytes))return CB_BAD_REQUEST;
        static const char hex[]="0123456789abcdef";
        for(unsigned i=0;i<16;++i)if(path[last+1+i*2]!=hex[r.installation[i]>>4] || path[last+2+i*2]!=hex[r.installation[i]&15])return CB_BAD_REQUEST;
        for(size_t prev=32;prev<at;){struct cb_install_record q;
            cb_install_copy(&q,s->data+prev+16,64);
            if((cb_reg_u64(s->data+prev)==owner || owner==CB_REGISTRY_SYSTEM || cb_reg_u64(s->data+prev)==CB_REGISTRY_SYSTEM) && ((q.name_bytes==r.name_bytes && cb_install_equal(s->data+prev+80,name,r.name_bytes)) || cb_install_equal(q.installation,r.installation,16)))return CB_BAD_REQUEST;
            prev+=16+q.bytes;
            }
        const unsigned char *old=registry_find(runtime.registry,owner,r.installation);
        int changed=!old || cb_reg_u64(old+16)!=r.bytes || !cb_install_equal(old,p,16+r.bytes);
        if(!restore && changed && !(owner==uid || (owner==CB_REGISTRY_SYSTEM && admin)))return CB_DENIED;
        char *json=allocate(r.manifest_bytes+1);
        if(!json)return CB_RESOURCE_FAILURE;
        cb_install_copy(json,path+r.path_bytes,r.manifest_bytes);
        int io=0;
        struct manifest_parser parser={json,json+r.manifest_bytes,&io,registry_resource,registry_operation};
        int valid=manifest_parse(&parser);
        release(json);
        if(!valid)return CB_DENIED;
        at+=16+r.bytes;
        ++count;
        }
    if(count!=cb_reg_u64(s->data+24))return CB_BAD_REQUEST;
    if(runtime.registry)for(size_t at=32;at<runtime.registry->bytes;){const unsigned char *old=runtime.registry->data+at;
        struct cb_install_record r;
        cb_install_copy(&r,old+16,64);
        uint64_t owner=cb_reg_u64(old);
        const unsigned char *next=registry_find(s,owner,r.installation);
        int changed=!next || cb_reg_u64(next+16)!=r.bytes || !cb_install_equal(old,next,16+r.bytes);
        if(changed){if(!restore && !(owner==uid || (owner==CB_REGISTRY_SYSTEM && admin)))return CB_DENIED;
            uint64_t guard=lock();
            for(struct child *c=runtime.children;c;c=c->next)if(!c->reaped){struct process_info info;
                if(CALL(runtime.info,c->process,&info,0).status || info.state==PROCESS_EXITED || info.state==PROCESS_FAULTED)continue;
                uint8_t id[24];
                kernel_surface_call get=kernel_surface_find(runtime.hub,"handles.identity");
                if(get && !get(c->process,(uintptr_t)id,sizeof(id),0,0,0).status && cb_install_equal(id,r.installation,16) && (owner==CB_REGISTRY_SYSTEM?cb_reg_u64(id+16)==0:cb_reg_u64(id+16)==owner)){unlock(guard);
                    return CB_BUSY;
                    }}unlock(guard);
            }
        at+=16+r.bytes;
        }
    return CB_OK;
    }
static cb_status_t registry_invoke(struct io_invocation *r){
    if(r->bytes<sizeof(struct cb_registry_request))return CB_BAD_REQUEST;
    struct cb_registry_request q;
    cb_install_copy(&q,r->input,sizeof(q));
    if(r->process!=runtime.manager && !r->authority)return CB_DENIED;
    if(q.operation==CB_REGISTRY_VERSION){if(r->bytes!=sizeof(q) || r->capacity<8)return CB_BAD_REQUEST;
        uint64_t f=lock(),g=registry_generation();
        unlock(f);
        cb_install_copy(r->output,&g,8);
        r->actual=8;
        return CB_OK;
        }
    if(q.operation==CB_REGISTRY_CONTEXT){if(r->authority!=1 || r->bytes!=sizeof(q))return CB_DENIED;
        char path[4096];
        struct cb_install_context c;
        cb_status_t status=registry_context(q.actor?q.actor:r->process,&c,path);
        if(status)return status;
        if(r->capacity<sizeof(c)+c.home_bytes)return CB_BUFFER_TOO_SMALL;
        cb_install_copy(r->output,&c,sizeof(c));
        cb_install_copy((unsigned char *)r->output+sizeof(c),path,c.home_bytes);
        r->actual=sizeof(c)+c.home_bytes;
        return CB_OK;
        }
    if(q.operation==CB_REGISTRY_SOURCE){
        if(r->authority!=1 || !q.bytes || q.bytes>4095 || r->bytes!=sizeof(q)+q.bytes)return CB_DENIED;
        char path[4096],home[4096];cb_install_copy(path,(const unsigned char *)r->input+sizeof(q),q.bytes);path[q.bytes]=0;
        if(length(path)!=q.bytes || path[0]!='/')return CB_BAD_REQUEST;
        struct cb_install_context context;cb_status_t status=registry_context(q.actor,&context,home);if(status)return status;
        struct filesystem_request access={.operation=FS_PREPARE,.mode=CB_FILE_READ,.path=path,.process=q.actor};
        kernel_surface_call file=kernel_surface_find(runtime.hub,"filesystem.file");
        return !file || CALL(file,runtime.mount,&access,0).status?CB_UNAVAILABLE:access.status;
        }
    if(q.operation==CB_REGISTRY_LIST){if(r->bytes!=sizeof(q) || q.flags>1 || (q.flags && !r->authority))return CB_DENIED;
        uint64_t uid=q.flags?q.actor:registry_uid(q.actor);
        struct registry_snapshot *s=registry_pin();
        uint64_t generation=s?cb_reg_u64(s->data+16):1;
        if(q.token && q.token!=generation){registry_drop(s);
            return CB_BUSY;
            }struct cb_install_record record;
        const unsigned char *name=NULL;
        uint64_t owner=CB_REGISTRY_SYSTEM,index=0;
        unsigned dynamic=0;
        struct cb_install_catalog h;
        cb_install_copy(&h,runtime.catalog,sizeof(h));
        for(size_t at=sizeof(h);at<h.bytes;){struct cb_install_record v;
            cb_install_copy(&v,runtime.catalog+at,64);
            if(v.flags&CB_INSTALL_APPLICATION){if(index++==q.offset){record=v;
                    name=runtime.catalog+at+64;
                    break;
                    }}at+=v.bytes;
            }
        if(!name && s)for(size_t at=32;at<s->bytes;){struct cb_install_record v;
            cb_install_copy(&v,s->data+at+16,64);
            uint64_t u=cb_reg_u64(s->data+at);
            if(u==uid || u==CB_REGISTRY_SYSTEM){if(index++==q.offset){record=v;
                    name=s->data+at+80;
                    owner=u;
                    dynamic=1;
                    break;
                    }}at+=16+v.bytes;
            }
        cb_status_t status=CB_OK;
        if(name){struct cb_registry_entry e={generation,q.offset+1,record.name_bytes,owner,{0},record.flags,dynamic};
            cb_install_copy(e.installation,record.installation,16);
            if(r->capacity<sizeof(e)+record.name_bytes)status=CB_BUFFER_TOO_SMALL;
            else {cb_install_copy(r->output,&e,sizeof(e));
                cb_install_copy((unsigned char *)r->output+sizeof(e),name,record.name_bytes);
                r->actual=sizeof(e)+record.name_bytes;
                }}registry_drop(s);
        return status;
        }
    if(r->authority!=1)return CB_DENIED;
    uint64_t f=lock();
    cb_status_t status=CB_BAD_REQUEST;
    if(q.operation==CB_REGISTRY_BEGIN){if(runtime.staging){status=CB_BUSY;
            goto done;
            }if(q.flags>1 || (q.flags && ((runtime.registry && !runtime.registry_recovery_required) || q.actor)) || q.token!=registry_generation() || q.bytes<32 || q.bytes>SIZE_MAX-sizeof(struct registry_snapshot) || runtime.registry_serial==UINT64_MAX){status=CB_BUSY;
            goto done;
            }if(r->capacity<8)goto done;
        unlock(f);
        struct registry_snapshot *s=allocate(sizeof(*s)+q.bytes);
        if(!s)return CB_RESOURCE_FAILURE;
        f=lock();
        if(runtime.staging || q.token!=registry_generation() || runtime.registry_serial==UINT64_MAX || (q.flags && ((runtime.registry && !runtime.registry_recovery_required) || q.actor))){unlock(f);release(s);return CB_BUSY;}
        s->refs=1;
        s->bytes=q.bytes;
        s->token=++runtime.registry_serial;
        runtime.staging=s;
        runtime.registry_writer=r->process;
        runtime.registry_actor=q.actor;
        runtime.registry_written=0;
        runtime.registry_prepared=q.flags?2:0;
        cb_install_copy(r->output,&s->token,8);
        r->actual=8;
        status=CB_OK;
        goto done;
        }
    if(q.operation==CB_REGISTRY_COMMIT && q.token==runtime.registry_committed && q.token){status=CB_OK;
        goto done;
        }
    struct registry_snapshot *s=runtime.staging;
    if(!s || s->token!=q.token || runtime.registry_writer!=r->process)goto done;
    if(q.operation==CB_REGISTRY_STAGE){size_t n=r->bytes-sizeof(q);
        if(runtime.registry_prepared==1 || q.offset!=runtime.registry_written || n>s->bytes-runtime.registry_written)goto done;
        cb_install_copy(s->data+q.offset,(const unsigned char *)r->input+sizeof(q),n);
        runtime.registry_written+=n;
        status=CB_OK;
        }
    else if(q.operation==CB_REGISTRY_ABORT){runtime.staging=NULL;
        runtime.registry_prepared=0;
        unlock(f);registry_drop(s);return CB_OK;
        }
    else if(q.operation==CB_REGISTRY_PREPARE){
        if(q.flags>1 || r->bytes!=sizeof(q)+(q.flags?sizeof(struct cb_registry_target):0))goto done;
        if(q.flags){struct cb_registry_target target;cb_install_copy(&target,(const unsigned char *)r->input+sizeof(q),sizeof(target));
            if(!registry_find(runtime.registry,target.owner,target.installation))goto done;
            if(registry_in_use(target.owner,target.installation)){status=CB_BUSY;goto done;}}
        if(runtime.registry_written!=s->bytes)goto done;
        if(runtime.registry_prepared==1){status=CB_OK;
            goto done;
            }if(runtime.registry_loaders || (runtime.registry && runtime.registry->refs>1)){status=CB_BUSY;
            goto done;
            }struct cb_install_context c;
        char home[4096];
        int restore=runtime.registry_prepared==2;
        status=registry_context(runtime.registry_actor?runtime.registry_actor:r->process,&c,home);
        if(status)goto done;
        runtime.registry_prepared=3;
        struct registry_snapshot *old=runtime.registry;
        if(old)++old->refs;
        unlock(f);
        status=registry_validate(s,c.uid,c.administrator,restore);
        f=lock();
        registry_drop(old);
        runtime.registry_prepared=status?(restore?2:0):1;
        }
    else if(q.operation==CB_REGISTRY_COMMIT && runtime.registry_prepared==1){struct registry_snapshot *old=runtime.registry;
        runtime.registry=s;
        runtime.staging=NULL;
        runtime.registry_prepared=0;
        runtime.registry_committed=q.token;
        runtime.registry_recovery_required=0;
        unlock(f);registry_drop(old);return CB_OK;
        }
done:unlock(f);
    return status;
    }
