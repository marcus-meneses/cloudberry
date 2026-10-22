#include "../../sdk/stream.h"
#include "../../interfaces/io.h"
#include "../../interfaces/identity.h"
#include "../../interfaces/identity-format.h"
#include "../../kmodules/shared/process-elf.h"
#include "../../kmodules/shared/manifest.h"
#include "bundle.h"
static struct cb_client client,responder;
static cb_handle_t memory,filesystem,io,identity,publication;
static unsigned char input[CB_FRAME_LIMIT],output[CB_FRAME_LIMIT];
static unsigned char trusted_key[32];
static const unsigned char *builtin;
static unsigned char *registrations;
static size_t registration_bytes;
static unsigned failed;
static uint64_t phase,last_status;
struct blob {unsigned char *data;
    size_t bytes;
    };
static void *allocate(size_t bytes){struct cb_memory_request r={.bytes=bytes};
    if(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&r,sizeof(r),0))return NULL;
    struct cb_memory_result out;
    cb_copy(&out,cb_result(&client),sizeof(out));
    return (void *)(uintptr_t)out.address;
    }
static void release(void *p){if(!p)return;
    struct cb_memory_request r={.address=(uintptr_t)p};
    CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,&r,sizeof(r),0)==CB_OK);
    }
static cb_status_t read_file(const char *path,struct blob *out){*out=(struct blob){0};
    cb_status_t status=cb_fs_call(&client,filesystem,CB_FILESYSTEM_STAT,path,0,0);
    if(status)return status;
    struct cb_filesystem_stat s;
    cb_copy(&s,cb_result(&client),sizeof(s));
    if(s.directory || s.bytes>SIZE_MAX)return CB_BAD_REQUEST;
    unsigned char *data=s.bytes?allocate(s.bytes):NULL;
    if(s.bytes && !data)return CB_RESOURCE_FAILURE;
    status=cb_fs_call(&client,filesystem,CB_FILESYSTEM_OPEN,path,CB_FILE_READ,0);
    if(status){release(data);
        return status;
        }cb_handle_t file;
    cb_copy(&file,cb_result(&client),8);
    size_t at=0;
    while(at<s.bytes){size_t n=0;
        status=cb_stream_read(&client,file,data+at,s.bytes-at,&n,0);
        if(status || !n){if(!status)status=CB_RESOURCE_FAILURE;
            break;
            }at+=n;
        }cb_status_t closed=cb_system_release(file);
    if(!status)status=closed;
    if(status)release(data);
    else *out=(struct blob){data,s.bytes};
    return status;
    }
static cb_status_t write_file(const char *path,const void *data,size_t bytes,unsigned mode){cb_status_t status=cb_fs_call(&client,filesystem,CB_FILESYSTEM_OPEN,path,mode,0);
    if(status)return status;
    cb_handle_t file;
    cb_copy(&file,cb_result(&client),8);
    status=cb_write_all(&client,file,data,bytes);
    cb_status_t closed=cb_system_release(file);
    return status?status:closed;
    }
static cb_status_t metadata(const char *path,struct cb_filesystem_metadata *m){cb_status_t s=cb_fs_call(&client,filesystem,CB_FILESYSTEM_METADATA,path,0,0);
    if(!s)cb_copy(m,cb_result(&client),sizeof(*m));
    return s;
    }
static cb_status_t assign(const char *path,uint64_t uid,uint64_t gid,unsigned mode){cb_status_t s=cb_fs_call(&client,filesystem,CB_FILESYSTEM_CHMOD,path,mode,0);
    return s?s:cb_fs_chown(&client,filesystem,path,uid,gid);
    }
static int under(const char *path,const char *base){size_t n=cb_length(base);
    return cb_length(path)>=n && cb_install_equal(path,base,n) && (!path[n] || path[n]=='/');
    }
static cb_status_t registry(unsigned op,uint64_t actor,uint64_t token,uint64_t offset,uint64_t bytes,unsigned flags,const void *data,size_t n){if(n>CB_FRAME_LIMIT-sizeof(struct cb_registry_request)-1024)return CB_BAD_REQUEST;
    unsigned char frame[CB_FRAME_LIMIT];
    struct cb_registry_request r={op,flags,actor,token,offset,bytes};
    cb_copy(frame,&r,sizeof(r));
    cb_copy(frame+sizeof(r),data,n);
    return cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_REGISTRY,frame,sizeof(r)+n,0);
    }
static cb_status_t context(uint64_t pid,struct cb_install_context *c,char home[4096]){cb_status_t status=registry(CB_REGISTRY_CONTEXT,pid,0,0,0,0,NULL,0);
    if(status)return status;
    struct cb_reply_header h;
    cb_copy(&h,client.result,sizeof(h));
    if(h.result_bytes<sizeof(*c))return CB_BAD_REQUEST;
    cb_copy(c,cb_result(&client),sizeof(*c));
    if(c->home_bytes>4095 || h.result_bytes!=sizeof(*c)+c->home_bytes)return CB_BAD_REQUEST;
    cb_copy(home,(unsigned char *)cb_result(&client)+sizeof(*c),c->home_bytes);
    home[c->home_bytes]=0;
    return CB_OK;
    }
static int manifest_resource(void *ctx,const char *name,const char *scope,int publish){int *is_io=ctx;
    *is_io=mf_equal(name,"/@io");
    return !(publish && name[0]=='/' && name[1]=='@') && !mf_equal(scope,"process-manager") && !mf_equal(scope,"display") && !mf_equal(scope,"filesystem") && !mf_equal(scope,"graphics") && (!*is_io || mf_equal(scope,"self"));
    }
static int manifest_operation(void *ctx,const char *name){return !*(int *)ctx || (mf_equal(name,"now_ns") || mf_equal(name,"utc") || mf_equal(name,"random"));
    }
static cb_status_t verify(struct blob *file,struct bundle *b){cb_status_t status=bundle_decode(b,file->data,file->bytes,trusted_key);
    if(status)return status;
    unsigned char *code=allocate(b->code),*json=allocate(b->manifest+1);
    if(!code || !json){release(code);
        release(json);
        return CB_RESOURCE_FAILURE;
        }cb_copy(code,b->data+b->header+b->name+b->manifest,b->code);
    cb_copy(json,b->data+b->header+b->name,b->manifest);
    int is_io=0;
    struct manifest_parser p={(char *)json,(char *)json+b->manifest,&is_io,manifest_resource,manifest_operation};
    int valid=process_elf_valid(code,b->code) && manifest_parse(&p);
    release(code);
    release(json);
    return valid?CB_OK:CB_BAD_REQUEST;
    }
#include "permissions.inc"
#include "health.inc"
#include "files.inc"
#include "store.inc"
#include "operations.inc"
__attribute__((noreturn)) void _start(const struct cb_startup *packet){
    const void *key,*catalog;
    size_t kb,cb;
    CB_REQUIRE(packet && cb_startup_find(packet,packet->bytes,"trusted-key.bin",&key,&kb) && kb==32 && cb_startup_find(packet,packet->bytes,"catalog.bin",&catalog,&cb) && cb_install_valid(catalog,cb));
    cb_copy(trusted_key,key,32);
    builtin=catalog;
    memory=cb_acquire(CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID);
    filesystem=cb_acquire(CB_FILESYSTEM_NAME,(struct cb_interface_id)CB_FILESYSTEM_INTERFACE_ID);
    io=cb_acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);
    identity=cb_acquire(CB_IDENTITY_NAME,(struct cb_interface_id)CB_IDENTITY_INTERFACE_ID);
    CB_REQUIRE(initialize_store()==CB_OK);
    static const char *names[]={"inspect","install","remove","list","status","diagnostics","upgrade","repair","permissions","approve","metadata"};
    publication=cb_publish(&client,CB_INSTALLER_NAME,(struct cb_interface_id)CB_INSTALLER_INTERFACE_ID,names,11);
    for(;;){uint64_t bytes;
        CB_REQUIRE(cb_system_receive(input,sizeof(input),&bytes,0)==CB_OK && bytes>=sizeof(struct cb_delivery_header));
        struct cb_delivery_header d;
        cb_copy(&d,input,sizeof(d));
        CB_REQUIRE(d.context_bytes<=bytes-sizeof(d) && d.call.payload_bytes==bytes-sizeof(d)-d.context_bytes);
        if(d.call.flags==(CB_CALL_PROVIDER_DELIVERY|CB_CALL_SYSTEM_EVENT)){if(d.call.operation==CB_HANDLE_RELEASE && !d.call.handle){release(registrations);clear_rejected();
                cb_system_release(publication);
                cb_exit(0);
                }continue;
            }
        struct cb_admission actor;
        size_t result=0;
        unsigned was_degraded=failed;
        cb_status_t status=CB_DENIED;
        if(d.call.flags==CB_CALL_PROVIDER_DELIVERY && d.call.handle==publication && cb_admission_parse(input+sizeof(d),d.context_bytes,&actor))status=operate(actor.process,d.call.operation,input+sizeof(d)+d.context_bytes,d.call.payload_bytes,&result);
        if((!was_degraded || !failed) && d.call.operation!=CB_INSTALLER_STATUS && d.call.operation!=CB_INSTALLER_DIAGNOSTICS && d.call.operation!=CB_INSTALLER_LIST && d.call.operation!=CB_INSTALLER_PERMISSIONS && d.call.operation!=CB_INSTALLER_INSPECT && d.call.operation!=CB_INSTALLER_METADATA){last_status=status;if(!status)phase=0;}
        cb_status_t sent=cb_respond(&responder,&d,status,output,result);
        CB_REQUIRE(sent==CB_OK || sent==CB_BAD_REQUEST);
        }
}
