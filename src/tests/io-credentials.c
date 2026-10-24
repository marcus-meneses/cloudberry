#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/io.c"
static unsigned replacements,watch_calls,gpu_calls;
static struct kernel_surface_result gpu_stub(MODULE_ARGS){MODULE_UNUSED();struct io_invocation *r=(void *)a;assert(b==22 && r->process==22 && r->operation==CB_IO_GPU);++gpu_calls;r->status=CB_OK;return module_result(1,1);}
static struct kernel_surface_result watch_stub(MODULE_ARGS){MODULE_UNUSED();assert((a==10 || a==22) && b==9 && c==77 && d==1);++watch_calls;return module_result(1,1);}
static struct kernel_surface_result credential_stub(MODULE_ARGS){MODULE_UNUSED();if(b==1){assert(a==9);++replacements;return module_result(1,1);}assert(a==8 || a==9);*(struct cb_credentials *)c=(struct cb_credentials){.uid=a,.gid=2,.generation=1};return module_result(sizeof(struct cb_credentials),1);}
static unsigned session_mutations;
static struct kernel_surface_result session_stub(MODULE_ARGS){MODULE_UNUSED();if(b){++session_mutations;return module_result(1,1);}*(struct cb_session *)c=(struct cb_session){(a==9 || a==10)?100:200,0,0,a};return module_result(1,1);}
static unsigned revoked_pid;
static struct kernel_surface_result ancestor_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(a!=revoked_pid && b!=revoked_pid && (a==b || (a==8 && b==9)),1);}
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
int main(void){
    struct {struct kernel_surfaces h;struct kernel_surface entry[5];} table={0};table.h.count=4;table.h.methods_address=(uintptr_t)table.entry;strcpy(table.entry[0].name,"process.credentials");table.entry[0].call=credential_stub;strcpy(table.entry[1].name,"process.session");table.entry[1].call=session_stub;strcpy(table.entry[2].name,"process.ancestor");table.entry[2].call=ancestor_stub;strcpy(table.entry[3].name,"cpu.control");table.entry[3].call=cpu_stub;hub=&table.h;identity_owner=7;
    uint64_t pid=9;struct cb_credentials result;struct io_invocation call={.process=8,.operation=CB_IO_CREDENTIALS,.input=&pid,.bytes=8,.output=&result,.capacity=sizeof(result)};
    io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED);pid=0;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK && result.uid==8);
    struct {uint64_t pid;struct cb_credentials value;} request={.pid=9,.value={.uid=0}};call.operation=CB_IO_IDENTITY_SET;call.input=&request;call.bytes=sizeof(request);io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED && !replacements);call.process=7;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK && replacements==1);
    struct cb_io_session session={0};struct cb_session info;call=(struct io_invocation){.process=8,.operation=CB_IO_SESSION,.input=&session,.bytes=sizeof(session),.output=&info,.capacity=sizeof(info)};io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK && info.id==200);
    session.process=9;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED);session.process=0;session.operation=1;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED && !session_mutations);
    sessions_owner=6;call.process=6;session.process=9;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK && session_mutations==1);
    uint64_t args[2]={8,9},yes=0;call=(struct io_invocation){.process=8,.operation=CB_IO_INPUT_ANCESTOR,.input=args,.bytes=16,.output=&yes,.capacity=8};io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED);keyboard_owner=8;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK && yes);
    args[0]=9;args[1]=1;call.operation=CB_IO_INPUT_FOCUS_SET;call.output=NULL;call.capacity=0;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK && focus_pid==9 && focus_generation==1);io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_BAD_REQUEST);
    struct cb_io_input_focus focused;pid=8;call=(struct io_invocation){.process=10,.operation=CB_IO_INPUT_FOCUS,.input=&pid,.bytes=8,.output=&focused,.capacity=sizeof(focused)};io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED);mouse_owner=10;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK && focused.session==100 && focused.caller_session==200);
    text_owner=20;graphics_owner=21;display_owner=22;
    struct cb_io_console_access access={8,CB_CONSOLE_TEXT,0};
    call=(struct io_invocation){.process=19,.operation=CB_IO_CONSOLE_ACCESS,.input=&access,.bytes=sizeof(access)};
    io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED);
    call.process=20;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED); /* foreign session */
    access.process=9;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK);
    access.process=10;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED); /* same session, unrelated lineage */
    access.process=9;revoked_pid=9;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_REVOKED);revoked_pid=0;
    access.process=20;access.layer=CB_CONSOLE_GRAPHICS;call.process=21;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK);
    access.process=21;access.layer=CB_CONSOLE_DISPLAY;call.process=22;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK);
    access.process=9;access.reserved=1;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_BAD_REQUEST);access.reserved=0;
    focus_pid=0;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED);
    call=(struct io_invocation){.process=21,.operation=CB_IO_GPU};
    io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED && !gpu_calls);
    call.process=22;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_UNAVAILABLE && !gpu_calls);
    strcpy(table.entry[4].name,"gpu.invoke");table.entry[4].call=gpu_stub;table.h.count=5;
    io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK && gpu_calls==1);
    access=(struct cb_io_console_access){21,CB_CONSOLE_RENDER_BACKEND,0};
    call=(struct io_invocation){.process=22,.operation=CB_IO_CONSOLE_ACCESS,.input=&access,.bytes=sizeof(access)};
    io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK);
    access.process=9;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED); /* focused apps cannot use raw backend */
    access.process=21;call.process=21;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED); /* only display checks */
    /* Administrative mechanics are inaccessible even with an IO handle. */
    session=(struct cb_io_session){99,4,0};call=(struct io_invocation){.process=8,.operation=CB_IO_SESSION,.input=&session,.bytes=sizeof(session)};io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED);
    struct cb_io_owner_watch owner_watch={9,77,1};watch=watch_stub;call=(struct io_invocation){.process=11,.operation=CB_IO_OWNER_WATCH,.input=&owner_watch,.bytes=sizeof(owner_watch)};io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_DENIED && !watch_calls);call.process=10;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK && watch_calls==1);
    call.process=22;io_surface_invoke((uintptr_t)&call,0,0,0,0,0);assert(call.status==CB_OK && watch_calls==2);
    puts("PASS IO credential authority: own queries, foreign-query refusal and installed-identity-only replacement");
}
