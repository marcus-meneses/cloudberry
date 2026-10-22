/* Real kernel registry admission behind the provider's host transport fixture. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../kmodules/shared/applications.c"
static unsigned live_allocations;
static struct {struct kernel_surfaces hub;struct kernel_surface surfaces[6];} fixture;
static struct kernel_surface_result alloc_stub(MODULE_ARGS){MODULE_UNUSED();void *p=calloc(b,c);if(p)++live_allocations;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result free_stub(MODULE_ARGS){MODULE_UNUSED();if(b){assert(live_allocations);--live_allocations;free((void *)b);}return module_result(1,1);}
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result active_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(1,1);}
static struct kernel_surface_result credentials_stub(MODULE_ARGS){MODULE_UNUSED();if(b==3)return module_result(a==1 || a==20,1);*(struct cb_credentials *)c=(struct cb_credentials){.uid=a==2?100:a==3?101:0,.gid=0};return module_result(sizeof(struct cb_credentials),1);}
static struct kernel_surface_result home_stub(MODULE_ARGS){MODULE_UNUSED();const char *home=a==2?"/home/alice":a==3?"/home/bob":"/system/root";strcpy((char *)b,home);return module_result(strlen(home),1);}
static struct kernel_surface_result identity_stub(MODULE_ARGS){MODULE_UNUSED();memset((void *)b,0,c);return module_result(1,1);}
static struct kernel_surface_result process_stub(MODULE_ARGS){MODULE_UNUSED();*(struct process_info *)b=(struct process_info){.id=a,.state=PROCESS_EXITED};return module_result(1,1);}
static struct kernel_surface_result file_stub(MODULE_ARGS){MODULE_UNUSED();struct filesystem_request *r=(void *)b;r->status=CB_OK;return module_result(1,1);}
void installer_registry_reset(const unsigned char *catalog){
    registry_drop(runtime.staging);registry_drop(runtime.registry);assert(!live_allocations);memset(&runtime,0,sizeof(runtime));memset(&fixture,0,sizeof(fixture));
    runtime.catalog=(unsigned char *)catalog;runtime.cpu=cpu_stub;runtime.alloc=alloc_stub;runtime.free=free_stub;runtime.info=process_stub;runtime.manager=20;runtime.hub=&fixture.hub;
    fixture.hub.count=6;fixture.hub.methods_address=(uintptr_t)fixture.surfaces;
    const char *names[]={"process.credentials","process.home","process.active","handles.identity","process.info","filesystem.file"};
    kernel_surface_call calls[]={credentials_stub,home_stub,active_stub,identity_stub,process_stub,file_stub};
    for(unsigned i=0;i<6;++i){strcpy(fixture.surfaces[i].name,names[i]);fixture.surfaces[i].call=calls[i];}
}
cb_status_t installer_registry_invoke(const void *input,size_t bytes,void *output,size_t capacity,size_t *actual){
    struct io_invocation invocation={.process=20,.operation=CB_IO_REGISTRY,.input=input,.bytes=bytes,.output=output,.capacity=capacity,.authority=1};
    cb_status_t status=registry_invoke(&invocation);*actual=invocation.actual;return status;
}
uint64_t installer_registry_generation(void){return registry_generation();}
uint64_t installer_registry_count(void){return runtime.registry?cb_reg_u64(runtime.registry->data+24):0;}
