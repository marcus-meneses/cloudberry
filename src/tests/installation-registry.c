#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kmodules/shared/applications.c"
static unsigned allocations,oom;
static unsigned char appid[16]={0x99};
static struct kernel_surface_result alloc_stub(MODULE_ARGS){MODULE_UNUSED();void *p=oom?NULL:calloc(b,c);if(p)++allocations;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result free_stub(MODULE_ARGS){MODULE_UNUSED();if(b){assert(allocations);--allocations;free((void *)b);}return module_result(1,1);}
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result credentials_stub(MODULE_ARGS){MODULE_UNUSED();if(b==3)return module_result(a==2,1);*(struct cb_credentials *)c=(struct cb_credentials){.uid=a==2?0:a==3?1:2,.gid=2};return module_result(sizeof(struct cb_credentials),1);}
static struct kernel_surface_result home_stub(MODULE_ARGS){MODULE_UNUSED();const char *p=a==2?"/system/root":a==3 || a==99?"/home/alice":"/home/bob";strcpy((char *)b,p);return module_result(strlen(p),1);}
static struct kernel_surface_result active_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(a!=404,1);}
static unsigned running;
static uint64_t source_actor;
static struct kernel_surface_result filesystem_stub(MODULE_ARGS){MODULE_UNUSED();struct filesystem_request *r=(void *)b;assert(r->operation==FS_PREPARE && r->mode==CB_FILE_READ);source_actor=r->process;r->status=CB_DENIED;return module_result(1,1);}
static struct kernel_surface_result info_stub(MODULE_ARGS){MODULE_UNUSED();*(struct process_info *)b=(struct process_info){.id=a,.state=running?PROCESS_RUNNING:PROCESS_EXITED};return module_result(1,1);}
static struct kernel_surface_result identity_stub(MODULE_ARGS){MODULE_UNUSED();memcpy((void *)b,appid,16);if(c==24){uint64_t scope=1;memcpy((unsigned char *)b+16,&scope,8);}return module_result(1,1);}
static cb_status_t invoke(unsigned op,uint64_t actor,uint64_t token,uint64_t offset,const void *data,size_t bytes,uint64_t total,unsigned authority,uint64_t *out){unsigned char buffer[8192],result[8192];struct cb_registry_request q={.operation=op,.actor=actor,.token=token,.offset=offset,.bytes=total};assert(sizeof(q)+bytes<=sizeof(buffer));memcpy(buffer,&q,sizeof(q));if(bytes)memcpy(buffer+sizeof(q),data,bytes);struct io_invocation r={.process=20,.operation=CB_IO_REGISTRY,.input=buffer,.bytes=sizeof(q)+bytes,.output=result,.capacity=sizeof(result),.authority=authority};cb_status_t status=registry_invoke(&r);if(!status && out && r.actual>=8)memcpy(out,result,8);return status;}
static uint64_t prepare(unsigned char *data,size_t bytes,uint64_t actor){uint64_t token=0;assert(invoke(CB_REGISTRY_BEGIN,actor,registry_generation(),0,NULL,0,bytes,1,&token)==CB_OK);assert(invoke(CB_REGISTRY_STAGE,0,token,1,data,bytes,0,1,NULL)==CB_BAD_REQUEST);assert(invoke(CB_REGISTRY_STAGE,0,token,0,data,bytes,0,1,NULL)==CB_OK);return token;}
static cb_status_t guarded(uint64_t token){struct {struct cb_registry_request q;struct cb_registry_target target;} frame={{.operation=CB_REGISTRY_PREPARE,.flags=1,.token=token},{1,{0}}};memcpy(frame.target.installation,appid,16);struct io_invocation call={.process=20,.operation=CB_IO_REGISTRY,.input=&frame,.bytes=sizeof(frame),.authority=1};return registry_invoke(&call);}
int main(int argc,char **argv){assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t bytes=ftell(f);rewind(f);unsigned char *base=malloc(bytes);assert(fread(base,1,bytes,f)==bytes);fclose(f);runtime.catalog=base;runtime.cpu=cpu_stub;runtime.alloc=alloc_stub;runtime.free=free_stub;runtime.info=info_stub;runtime.manager=20;
    struct {struct kernel_surfaces h;struct kernel_surface methods[6];} hub={0};hub.h.count=6;hub.h.methods_address=(uintptr_t)hub.methods;const char *names[]={"process.credentials","process.home","process.active","handles.identity","process.info","filesystem.file"};kernel_surface_call calls[]={credentials_stub,home_stub,active_stub,identity_stub,info_stub,filesystem_stub};for(unsigned i=0;i<6;++i){strcpy(hub.methods[i].name,names[i]);hub.methods[i].call=calls[i];}runtime.hub=&hub.h;
    char path[]="/home/alice/_apps/_99000000000000000000000000000000.sample",name[]="sample",manifest[]="{\"version\":1,\"resources\":[]}";struct cb_install_record record={.bytes=64+strlen(name)+strlen(path)+strlen(manifest),.name_bytes=strlen(name),.path_bytes=strlen(path),.manifest_bytes=strlen(manifest),.flags=1,.stack_bytes=262144};memcpy(record.installation,appid,16);size_t size=32+16+record.bytes;unsigned char *data=calloc(1,size);cb_reg_put(data,CB_REGISTRY_MAGIC);cb_reg_put(data+8,size);cb_reg_put(data+16,2);cb_reg_put(data+24,1);cb_reg_put(data+32,1);memcpy(data+48,&record,64);memcpy(data+112,name,strlen(name));memcpy(data+112+strlen(name),path,strlen(path));memcpy(data+112+strlen(name)+strlen(path),manifest,strlen(manifest));
    assert(invoke(CB_REGISTRY_SOURCE,3,0,0,path,strlen(path),strlen(path),1,NULL)==CB_DENIED && source_actor==3);
    assert(invoke(CB_REGISTRY_BEGIN,3,1,0,NULL,0,size,0,NULL)==CB_DENIED);oom=1;assert(invoke(CB_REGISTRY_BEGIN,3,1,0,NULL,0,size,1,NULL)==CB_RESOURCE_FAILURE);oom=0;
    uint64_t token=prepare(data,size,4);assert(invoke(CB_REGISTRY_PREPARE,0,token,0,NULL,0,0,1,NULL)==CB_DENIED);assert(invoke(CB_REGISTRY_ABORT,0,token,0,NULL,0,0,1,NULL)==CB_OK);
    token=prepare(data,size,3);assert(invoke(CB_REGISTRY_PREPARE,0,token,0,NULL,0,0,1,NULL)==CB_OK);assert(invoke(CB_REGISTRY_COMMIT,0,token,0,NULL,0,0,1,NULL)==CB_OK);assert(registry_generation()==2);assert(invoke(CB_REGISTRY_COMMIT,0,token,0,NULL,0,0,1,NULL)==CB_OK);
    struct installation_ref ref;assert(installation_lookup(1,name,strlen(name),NULL,&ref) && ref.owner==1);char own[4096];assert(installation_data_path(3,&ref,own)==CB_OK && strstr(own,"_99000000000000000000000000000000.sample.data"));installation_drop(&ref);assert(!installation_lookup(2,name,strlen(name),NULL,&ref));
    struct installation_ref launcher;assert(installation_lookup(1,"terminal",8,NULL,&launcher));
    assert(installation_access(3,path,FS_EXEC,&launcher)==CB_OK);
    assert(installation_access(3,path,FS_READ,&launcher)==CB_DENIED);
    assert(installation_access(4,path,FS_EXEC,&launcher)==CB_DENIED);installation_drop(&launcher);
    /* Reauthentication changes credentials, never the admitted installation. */
    assert(installation_process(99,&ref) && ref.owner==1);installation_drop(&ref);
    /* Resource/code-only upgrade: identical registry record still requires idle. */
    struct child guarded_child={.process=99};runtime.children=&guarded_child;running=1;cb_reg_put(data+16,3);token=prepare(data,size,3);assert(guarded(token)==CB_BUSY);running=0;assert(guarded(token)==CB_OK);assert(invoke(CB_REGISTRY_ABORT,0,token,0,NULL,0,0,1,NULL)==CB_OK);runtime.children=NULL;
    unsigned char empty[32];cb_reg_put(empty,CB_REGISTRY_MAGIC);cb_reg_put(empty+8,32);cb_reg_put(empty+16,3);cb_reg_put(empty+24,0);
    token=prepare(empty,32,3);struct registry_snapshot *old=registry_pin();assert(invoke(CB_REGISTRY_PREPARE,0,token,0,NULL,0,0,1,NULL)==CB_BUSY);registry_drop(old);
    struct child child={.process=99};runtime.children=&child;running=1;assert(invoke(CB_REGISTRY_PREPARE,0,token,0,NULL,0,0,1,NULL)==CB_BUSY);running=0;assert(invoke(CB_REGISTRY_PREPARE,0,token,0,NULL,0,0,1,NULL)==CB_OK);old=registry_pin();assert(invoke(CB_REGISTRY_COMMIT,0,token,0,NULL,0,0,1,NULL)==CB_OK && registry_generation()==3 && cb_reg_u64(old->data+16)==2);registry_drop(old);
    cb_reg_put(empty+16,4);token=prepare(empty,32,3);assert(invoke(CB_REGISTRY_PREPARE,0,token,0,NULL,0,0,1,NULL)==CB_OK);runtime.manager=99;runtime.children=NULL;assert(applications_exit(20)==CB_OK && !runtime.staging && runtime.registry_recovery_required);runtime.children=NULL;
    registry_drop(runtime.registry);runtime.registry=NULL;assert(!allocations);free(base);free(data);puts("PASS installation registry: issuer authority, scope, OOM, exact staging, publication generation, private data, active removal, pinned snapshot retirement and idempotent commit");
}
