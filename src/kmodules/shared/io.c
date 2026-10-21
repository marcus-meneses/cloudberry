#include "io.h"
#include "../../interfaces/display.h"
#include "process.h"
#include "../../kernel/cpu.h"
static const struct kernel_surfaces *hub;
static uint64_t installer_owner,text_owner,display_owner,graphics_owner,keyboard_owner,identity_owner,sessions_owner,mouse_owner,focus_pid,focus_generation;
static kernel_surface_call hid,display,filesystem,applications,clock,watch,hid_exit,applications_exit;
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
struct kernel_surface_result io_surface_init(MODULE_ARGS){
    MODULE_UNUSED();if(hub)return module_result(1,hub==(const void *)a);if(!a || !b)return module_result(0,0);const struct kernel_surfaces *candidate=(const void *)a;
#define BIND(field,name) field=kernel_surface_find(candidate,name);if(!field)return module_result(0,0)
    BIND(hid,"hid.invoke");BIND(display,"display.invoke");BIND(filesystem,"filesystem.invoke");BIND(applications,"applications.invoke");BIND(clock,"clock.now_ns");BIND(watch,"handles.owner_watch");BIND(hid_exit,"hid.exit");BIND(applications_exit,"applications.exit");
#undef BIND
    static const char init[][24]={"applications.init","hid.init","display.init","filesystem.init"};
    for(unsigned i=0;i<sizeof(init)/sizeof(init[0]);++i){kernel_surface_call start=kernel_surface_find(candidate,init[i]);if(!start)return module_result(0,0);struct kernel_surface_result result=start(a,b,c,0,0,0);if(result.status || !result.value)return module_result(0,0);}
    hub=candidate;return module_result(1,1);
}
struct kernel_surface_result io_surface_bind(MODULE_ARGS){MODULE_UNUSED();if(!a)return module_result(0,0);if(kernel_hub_name_equal((const char *)a,"installer"))installer_owner=b;else if(kernel_hub_name_equal((const char *)a,"text"))text_owner=b;else if(kernel_hub_name_equal((const char *)a,"display"))display_owner=b;else if(kernel_hub_name_equal((const char *)a,"graphics"))graphics_owner=b;else if(kernel_hub_name_equal((const char *)a,"keyboard"))keyboard_owner=b;else if(kernel_hub_name_equal((const char *)a,"identity"))identity_owner=b;else if(kernel_hub_name_equal((const char *)a,"sessions"))sessions_owner=b;else if(kernel_hub_name_equal((const char *)a,"mouse"))mouse_owner=b;return module_result(1,1);}
/* Authorize screen access before a frontend mutates its state. Each backend
 * trusts only the bound upstream renderer, never a claimed identity in payload. */
static cb_status_t console_access(uint64_t provider,const struct cb_io_console_access *r){
    if(r->reserved || !r->process || r->layer<CB_CONSOLE_TEXT || r->layer>CB_CONSOLE_DISPLAY)return CB_BAD_REQUEST;
    uint64_t expected=r->layer==CB_CONSOLE_TEXT?text_owner:r->layer==CB_CONSOLE_GRAPHICS?graphics_owner:display_owner;
    if(!expected || provider!=expected)return CB_DENIED;
    if((r->layer==CB_CONSOLE_GRAPHICS && text_owner && r->process==text_owner) || (r->layer==CB_CONSOLE_DISPLAY && graphics_owner && r->process==graphics_owner))return CB_OK;
    kernel_surface_call cpu=kernel_surface_find(hub,"cpu.control"),session=kernel_surface_find(hub,"process.session"),ancestor=kernel_surface_find(hub,"process.ancestor");
    if(!cpu || !session || !ancestor)return CB_UNAVAILABLE;
    if(!CALL(ancestor,r->process,r->process,0).value)return CB_REVOKED;
    uint64_t flags=CALL(cpu,CPU_ENTER,0,0).value,focused=focus_pid,generation=focus_generation;CALL(cpu,CPU_LEAVE,flags,0);
    struct cb_session actor={0},active={0};if(session(r->process,0,(uintptr_t)&actor,sizeof(actor),0,0).status)return CB_REVOKED;
    /* Pre-console boot diagnostics only. Once focus was established, absence
     * of an owner fails closed rather than reopening the screen to everyone. */
    if(!focused)return !generation && !actor.id?CB_OK:CB_DENIED;
    if(!CALL(ancestor,focused,focused,0).value || session(focused,0,(uintptr_t)&active,sizeof(active),0,0).status)return CB_REVOKED;
    if(actor.id!=active.id)return CB_DENIED;
    if(!CALL(ancestor,focused,r->process,0).value && !CALL(ancestor,r->process,focused,0).value)return CB_DENIED;
    /* Reject a claim that changed while the ancestry/session checks ran. */
    flags=CALL(cpu,CPU_ENTER,0,0).value;int unchanged=focused==focus_pid && generation==focus_generation;CALL(cpu,CPU_LEAVE,flags,0);return unchanged?CB_OK:CB_BUSY;
}
struct kernel_surface_result io_surface_invoke(MODULE_ARGS){
    MODULE_UNUSED();struct io_invocation *r=(void *)a;if(!r || !hub)return module_result(0,0);r->status=CB_BAD_REQUEST;r->actual=0;if((r->bytes && !r->input) || (r->capacity && !r->output))return module_result(1,1);
    if(r->operation==CB_IO_REGISTRY){r->authority=installer_owner && r->process==installer_owner?1:identity_owner && r->process==identity_owner?2:0;(void)CALL(applications,r,0,0);return module_result(1,1);}
    if(r->operation==CB_IO_CONSOLE_ACCESS){if(r->bytes==sizeof(struct cb_io_console_access)){struct cb_io_console_access request;__builtin_memcpy(&request,r->input,sizeof(request));r->status=console_access(r->process,&request);}return module_result(1,1);}
    if(r->operation==CB_IO_NOW_NS){if(!r->bytes && r->capacity>=8){uint64_t time=CALL(clock,0,0,0).value;for(unsigned i=0;i<8;++i)((unsigned char *)r->output)[i]=((unsigned char *)&time)[i];r->actual=8;r->status=CB_OK;}return module_result(1,1);}
    if(r->operation==CB_IO_OWNER_WATCH){r->status=CB_DENIED;if((!graphics_owner || r->process!=graphics_owner) && (!keyboard_owner || r->process!=keyboard_owner) && (!identity_owner || r->process!=identity_owner) && (!sessions_owner || r->process!=sessions_owner))return module_result(1,1);r->status=CB_BAD_REQUEST;if(r->bytes!=sizeof(struct cb_io_owner_watch))return module_result(1,1);struct cb_io_owner_watch request;for(size_t i=0;i<sizeof(request);++i)((unsigned char *)&request)[i]=((const unsigned char *)r->input)[i];if(request.enable<=1)r->status=watch(r->process,request.process,request.token,request.enable,0,0).status?CB_RESOURCE_FAILURE:CB_OK;return module_result(1,1);}
    if(r->operation==CB_IO_CREDENTIALS || r->operation==CB_IO_IDENTITY_SET){
        r->status=CB_DENIED;kernel_surface_call credentials=kernel_surface_find(hub,"process.credentials");if(!credentials)return module_result(1,1);
        if(r->operation==CB_IO_CREDENTIALS){if(r->bytes!=8 || r->capacity<sizeof(struct cb_credentials)){r->status=CB_BAD_REQUEST;return module_result(1,1);}uint64_t pid;__builtin_memcpy(&pid,r->input,8);if(!pid)pid=r->process;if(pid!=r->process && (!identity_owner || r->process!=identity_owner) && (!sessions_owner || r->process!=sessions_owner))return module_result(1,1);struct kernel_surface_result result=credentials(pid,0,(uintptr_t)r->output,r->capacity,0,0);r->status=result.status?CB_BAD_REQUEST:result.value>r->capacity?CB_BUFFER_TOO_SMALL:CB_OK;if(!r->status)r->actual=result.value;}
        else if(identity_owner && r->process==identity_owner){if(r->bytes<8+sizeof(struct cb_credentials)){r->status=CB_BAD_REQUEST;return module_result(1,1);}uint64_t pid;__builtin_memcpy(&pid,r->input,8);r->status=credentials(pid,1,(uintptr_t)r->input+8,r->bytes-8,0,0).status?CB_RESOURCE_FAILURE:CB_OK;}
        return module_result(1,1);
    }
    if(r->operation==CB_IO_IDENTITY_HOME){r->status=CB_DENIED;if(!identity_owner || r->process!=identity_owner)return module_result(1,1);if(r->bytes<8+sizeof(struct cb_identity_home)){r->status=CB_BAD_REQUEST;return module_result(1,1);}uint64_t pid;__builtin_memcpy(&pid,r->input,8);kernel_surface_call credentials=kernel_surface_find(hub,"process.credentials");r->status=!credentials || credentials(pid,4,(uintptr_t)r->input+8,r->bytes-8,0,0).status?CB_RESOURCE_FAILURE:CB_OK;return module_result(1,1);}
    if(r->operation==CB_IO_SESSION){
        r->status=CB_DENIED;if(r->bytes!=sizeof(struct cb_io_session))return module_result(1,1);struct cb_io_session request;__builtin_memcpy(&request,r->input,sizeof(request));if(!request.process && request.operation<4)request.process=r->process;
        if((request.process!=r->process || request.operation) && (!sessions_owner || r->process!=sessions_owner))return module_result(1,1);
        kernel_surface_call session=kernel_surface_find(hub,"process.session");if(!session || request.operation>5 || ((!request.operation || (request.operation==3 || request.operation==5)) && r->capacity<((request.operation==3 || request.operation==5)?sizeof(struct cb_session_status):sizeof(struct cb_session)))){r->status=CB_BAD_REQUEST;return module_result(1,1);}
        struct kernel_surface_result got=session(request.process,request.operation,(request.operation==2 || request.operation==4)?request.exclude:(uintptr_t)r->output,r->capacity,0,0);r->status=got.status?CB_BAD_REQUEST:(request.operation==2 || request.operation==4) && got.value?CB_RESOURCE_FAILURE:CB_OK;if(!r->status && (!request.operation || (request.operation==3 || request.operation==5)))r->actual=(request.operation==3 || request.operation==5)?sizeof(struct cb_session_status):sizeof(struct cb_session);return module_result(1,1);
    }
    if(r->operation==CB_IO_INPUT_ANCESTOR){r->status=CB_DENIED;if(!keyboard_owner || r->process!=keyboard_owner)return module_result(1,1);if(r->bytes!=16 || r->capacity<8){r->status=CB_BAD_REQUEST;return module_result(1,1);}uint64_t args[2];__builtin_memcpy(args,r->input,16);kernel_surface_call ancestor=kernel_surface_find(hub,"process.ancestor");struct kernel_surface_result got=ancestor?CALL(ancestor,args[0],args[1],0):module_result(0,0);r->status=got.status?CB_BAD_REQUEST:CB_OK;if(!r->status){__builtin_memcpy(r->output,&got.value,8);r->actual=8;}return module_result(1,1);}
    if(r->operation==CB_IO_INPUT_FOCUS_SET){r->status=CB_DENIED;if(!keyboard_owner || r->process!=keyboard_owner)return module_result(1,1);if(r->bytes!=16){r->status=CB_BAD_REQUEST;return module_result(1,1);}uint64_t args[2];__builtin_memcpy(args,r->input,16);kernel_surface_call cpu=kernel_surface_find(hub,"cpu.control");if(!cpu){r->status=CB_UNAVAILABLE;return module_result(1,1);}uint64_t flags=CALL(cpu,CPU_ENTER,0,0).value;if(args[1]<=focus_generation){CALL(cpu,CPU_LEAVE,flags,0);r->status=CB_BAD_REQUEST;return module_result(1,1);}focus_pid=args[0];focus_generation=args[1];CALL(cpu,CPU_LEAVE,flags,0);r->status=CB_OK;return module_result(1,1);}
    if(r->operation==CB_IO_INPUT_FOCUS){r->status=CB_DENIED;if((!mouse_owner || r->process!=mouse_owner) && (!keyboard_owner || r->process!=keyboard_owner))return module_result(1,1);if(r->bytes!=8 || r->capacity<sizeof(struct cb_io_input_focus)){r->status=CB_BAD_REQUEST;return module_result(1,1);}uint64_t pid;__builtin_memcpy(&pid,r->input,8);kernel_surface_call cpu=kernel_surface_find(hub,"cpu.control");if(!cpu){r->status=CB_UNAVAILABLE;return module_result(1,1);}uint64_t flags=CALL(cpu,CPU_ENTER,0,0).value;uint64_t focused=focus_pid,generation=focus_generation;CALL(cpu,CPU_LEAVE,flags,0);kernel_surface_call live=kernel_surface_find(hub,"process.ancestor");if(!live || !CALL(live,pid,pid,0).value){r->status=CB_REVOKED;return module_result(1,1);}kernel_surface_call session=kernel_surface_find(hub,"process.session");struct cb_session active={0},caller={0};if(!session || session(pid,0,(uintptr_t)&caller,sizeof(caller),0,0).status || (focused && session(focused,0,(uintptr_t)&active,sizeof(active),0,0).status)){r->status=CB_REVOKED;return module_result(1,1);}struct cb_io_input_focus result={focused,generation,active.id,caller.id};__builtin_memcpy(r->output,&result,sizeof(result));r->actual=sizeof(result);r->status=CB_OK;return module_result(1,1);}
    if(r->operation==CB_IO_RANDOM){r->status=CB_BAD_REQUEST;if(r->bytes!=8)return module_result(1,1);uint64_t n;__builtin_memcpy(&n,r->input,8);if(n>64 || n>r->capacity)return module_result(1,1);uint32_t eax=1,ebx,ecx,edx;__asm__ volatile("cpuid":"+a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx));if(!(ecx&(1u<<30))){r->status=CB_UNAVAILABLE;return module_result(1,1);}for(size_t at=0;at<n;){uint64_t value=0;unsigned char good=0;for(unsigned tries=0;tries<64 && !good;++tries)__asm__ volatile("rdrand %0; setc %1":"=r"(value),"=qm"(good));if(!good){for(size_t i=0;i<n;++i)((volatile unsigned char *)r->output)[i]=0;r->status=CB_UNAVAILABLE;return module_result(1,1);}for(unsigned i=0;i<8 && at<n;++i,++at)((unsigned char *)r->output)[at]=value>>(8*i);}r->actual=n;r->status=CB_OK;return module_result(1,1);
    }
    kernel_surface_call backend=NULL;
    if((r->operation>=CB_IO_KEYBOARD_SUBSCRIBE && r->operation<=CB_IO_KEYBOARD_CLOSE) || (r->operation>=CB_IO_HID_SUBSCRIBE && r->operation<=CB_IO_HID_SET_FEATURE))backend=hid;
    else if((r->operation>=CB_IO_DISPLAY_INFO && r->operation<=CB_IO_DISPLAY_BLIT) || r->operation==CB_IO_DISPLAY_COPY)backend=display;
    else if(r->operation==CB_IO_FILESYSTEM)backend=filesystem;
    else if(r->operation>=CB_IO_PROCESS_LAUNCH && r->operation<=CB_IO_PROCESS_RELEASE)backend=applications;
    else if(r->operation==CB_IO_PROCESS_SAMPLE || r->operation==CB_IO_PROCESS_DATA)backend=applications;
    if(backend)(void)CALL(backend,r,0,0);
    return module_result(1,1);
}
struct kernel_surface_result io_surface_exit(MODULE_ARGS){MODULE_UNUSED();if(!hub)return module_result(1,1);struct kernel_surface_result result=CALL(applications_exit,a,0,0);if(result.status)return result;return CALL(hid_exit,a,0,0);}
static struct kernel_surface_result forward(const char *name,uintptr_t a,uintptr_t b,uintptr_t c){kernel_surface_call call=hub?kernel_surface_find(hub,name):NULL;return call?CALL(call,a,b,c):module_result(0,0);}
struct kernel_surface_result io_surface_info(MODULE_ARGS){MODULE_UNUSED();return forward("hid.info",a,b,c);}
struct kernel_surface_result io_surface_installations(MODULE_ARGS){MODULE_UNUSED();kernel_surface_call call=hub?kernel_surface_find(hub,"applications.installations"):NULL;return call?call(a,b,c,d,e,f):module_result(0,0);}
struct kernel_surface_result io_surface_installed_load(MODULE_ARGS){MODULE_UNUSED();return forward("applications.installed_load",a,b,c);}
struct kernel_surface_result io_surface_process_info(MODULE_ARGS){MODULE_UNUSED();return forward("applications.info",a,b,c);}
