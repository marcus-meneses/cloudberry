#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kmodules/shared/io-process.c"
#include "../interfaces/admission.h"
static long blocks,budget=-1;static unsigned reaped,reject_admission;static const char *expected_path;
static struct kernel_surface_result alloc_stub(MODULE_ARGS){MODULE_UNUSED();if(!budget)return module_result(0,0);if(budget>0)--budget;void *p=calloc(b,c);if(p)++blocks;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result free_stub(MODULE_ARGS){MODULE_UNUSED();assert(blocks>0);--blocks;free((void *)b);return module_result(1,1);}
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result load_stub(MODULE_ARGS){MODULE_UNUSED();const struct process_request *r=(const void *)a;assert(r->abi==2 && !strcmp(r->path,expected_path) && r->filesystem==7 && r->startup_bytes==24);return module_result(99,1);}
static struct kernel_surface_result admit_stub(MODULE_ARGS){MODULE_UNUSED();const struct handle_policy *p=(const void *)a;assert(p->process==99 && p->bytes && p->identity.installation_id[0]);return module_result(1,!reject_admission);}
static struct kernel_surface_result reap_stub(MODULE_ARGS){MODULE_UNUSED();assert(a==99);++reaped;return module_result(1,1);}
int main(int argc,char **argv){
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);assert(!fseek(file,0,SEEK_END));long count=ftell(file);assert(count>0);rewind(file);unsigned char *data=malloc((size_t)count),*bad=malloc((size_t)count);assert(data && bad && fread(data,1,(size_t)count,file)==(size_t)count);fclose(file);
    size_t bytes=(size_t)count;assert(cb_install_valid(data,bytes));assert(!cb_install_valid(data,bytes-1));memcpy(bad,data,bytes);bad[0]=0;assert(!cb_install_valid(bad,bytes));
    struct cb_install_record first,second;memcpy(&first,data+24,64);memcpy(&second,data+24+first.bytes,64);memcpy(bad,data,bytes);memcpy(bad+24+first.bytes+32,first.installation,16);assert(!cb_install_valid(bad,bytes));
    struct cb_application_launch h={.version={sizeof(h),1,0},.name_bytes=4,.argc=1,.arguments_bytes=5,.cwd_bytes=1};unsigned char frame[50];memcpy(frame,&h,sizeof(h));memcpy(frame+sizeof(h),"truetrue\0/",10);assert(cb_application_launch_valid(frame,sizeof(frame)));h.argc=2;memcpy(frame,&h,sizeof(h));assert(!cb_application_launch_valid(frame,sizeof(frame)));h.argc=1;memcpy(frame,&h,sizeof(h));frame[sizeof(h)]='/';assert(!cb_application_launch_valid(frame,sizeof(frame)));
    const char context[]="{\"input_stream\":\"boot:1\",\"grant_ns\":123,\"process\":42,\"installation\":\"00112233445566778899aabbccddeeff\"}";struct cb_admission admission;assert(cb_admission_parse(context,sizeof(context)-1,&admission) && admission.process==42 && admission.grant_ns==123 && admission.installation[15]==255);assert(!cb_admission_parse(context,sizeof(context),&admission));
    struct kernel_surfaces hub={0};runtime.hub=&hub;runtime.heap=1;runtime.alloc=alloc_stub;runtime.free=free_stub;runtime.cpu=cpu_stub;runtime.load=load_stub;runtime.admit=admit_stub;runtime.reap=reap_stub;
    budget=0;assert(io_surface_installations(7,(uintptr_t)data,bytes,0,0,0).status && !blocks && !runtime.catalog);budget=-1;
    assert(!io_surface_installations(7,(uintptr_t)data,bytes,0,0,0).status && blocks==1);assert(!io_surface_installations(7,(uintptr_t)data,bytes,0,0,0).status && blocks==1);assert(io_surface_installations(8,(uintptr_t)data,bytes,0,0,0).status);
    struct cb_install_record terminal;assert(cb_install_find(runtime.catalog,"terminal",8,NULL,&terminal));
    assert(io_installation_data(terminal.installation,"/system/root/_apps/terminal.data/history"));
    assert(!io_installation_data(terminal.installation,"/system/root/_apps/echo.data/history"));
    assert(!io_installation_data(terminal.installation,"/system/root/_apps/terminal.data"));
    assert(!io_installation_data(terminal.installation,"/system/root/_apps/_index"));
    assert(!io_installation_data(terminal.installation,"/system/apps/terminal"));
    struct cb_startup packet={.version={24,1,0},.bytes=24};expected_path="/system/handlers/applications";
    budget=0;assert(io_surface_installed_load((uintptr_t)"applications",(uintptr_t)&packet,sizeof(packet),0,0,0).status && !runtime.manager && blocks==1);budget=-1;
    reject_admission=1;assert(io_surface_installed_load((uintptr_t)"applications",(uintptr_t)&packet,sizeof(packet),0,0,0).status && reaped==1 && !runtime.manager && blocks==1);reject_admission=0;
    assert(!io_surface_installed_load((uintptr_t)"applications",(uintptr_t)&packet,sizeof(packet),0,0,0).status && runtime.manager==99 && blocks==1);
    assert(io_surface_installed_load((uintptr_t)"applications",(uintptr_t)&packet,sizeof(packet),0,0,0).status);
    struct io_invocation invocation={.process=100,.operation=CB_IO_PROCESS_REAP};assert(io_process_invoke(&invocation) && invocation.status==CB_DENIED);
    assert(io_process_exit(99)==CB_OK && !runtime.manager);release(runtime.catalog);runtime.catalog=NULL;assert(!blocks);free(data);free(bad);
    puts("PASS installation contract: framing, duplicate identity, startup bounds, authenticated context, immutable binding, authority denial and OOM rollback");return 0;
}
