#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/io.c"
static unsigned replacements;
static struct kernel_surface_result credential_stub(MODULE_ARGS){MODULE_UNUSED();if(b==1){assert(a==9);++replacements;return module_result(1,1);}assert(a==8 || a==9);*(struct cb_credentials *)c=(struct cb_credentials){.uid=a,.gid=2,.generation=1};return module_result(sizeof(struct cb_credentials),1);}
int main(void){
    struct {struct kernel_surfaces h;struct kernel_surface entry;} table={0};table.h.count=1;table.h.methods_address=(uintptr_t)&table.entry;strcpy(table.entry.name,"process.credentials");table.entry.call=credential_stub;hub=&table.h;identity_owner=7;
    uint64_t pid=9;struct cb_credentials result;struct io_invocation call={.process=8,.operation=CB_IO_CREDENTIALS,.input=&pid,.bytes=8,.output=&result,.capacity=sizeof(result)};
    io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED);pid=0;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK && result.uid==8);
    struct {uint64_t pid;struct cb_credentials value;} request={.pid=9,.value={.uid=0}};call.operation=CB_IO_IDENTITY_SET;call.input=&request;call.bytes=sizeof(request);io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED && !replacements);call.process=7;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK && replacements==1);
    puts("PASS IO credential authority: own queries, foreign-query refusal and installed-identity-only replacement");
}
