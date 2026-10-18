#include "../../sdk/system.h"
#include "../../sdk/application.h"
#include "../../interfaces/io.h"
static unsigned char payload[CB_FRAME_LIMIT],frame[CB_FRAME_LIMIT],output[CB_FRAME_LIMIT];
static cb_handle_t applications;
#define COPY cb_startup_copy
#define CHECK(x) do{if(!(x))cb_exit(__LINE__);}while(0)
static size_t length(const char *p){size_t n=0;while(p[n])++n;return n;}
static int same(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static struct cb_reply_header header(void){struct cb_reply_header h;COPY(&h,output,sizeof(h));return h;}
static cb_status_t invoke(unsigned op,const void *data,size_t bytes,uint64_t deadline,struct cb_request_id request,unsigned attempt,int forget){
    struct cb_call_header h=cb_call(applications,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,op,bytes);h.deadline_ns=deadline;h.request=request;h.attempt=attempt;COPY(frame,&h,sizeof(h));COPY(frame+sizeof(h),data,bytes);uint64_t n;cb_status_t result=cb_system_invoke(frame,sizeof(h)+bytes,output,sizeof(output),&n);if(forget && header().request.serial)CHECK(cb_system_request_release(header().request)==CB_OK);return result;
}
static cb_status_t call(unsigned op,const void *data,size_t bytes){return invoke(op,data,bytes,0,(struct cb_request_id){0},0,1);}
static cb_status_t launch(const char *name,const char *const *arguments,size_t argc,const char *cwd,uint64_t *pid){size_t names=length(name),cwd_bytes=length(cwd),args=0;for(size_t i=0;i<argc;++i)args+=length(arguments[i])+1;struct cb_application_launch h={.version={sizeof(h),1,0},.name_bytes=names,.argc=argc,.arguments_bytes=args,.cwd_bytes=cwd_bytes};COPY(payload,&h,sizeof(h));size_t at=sizeof(h);COPY(payload+at,name,names);at+=names;for(size_t i=0;i<argc;++i){size_t n=length(arguments[i])+1;COPY(payload+at,arguments[i],n);at+=n;}COPY(payload+at,cwd,cwd_bytes);at+=cwd_bytes;cb_status_t result=call(CB_APPLICATION_LAUNCH,payload,at);if(!result && pid)COPY(pid,output+sizeof(struct cb_reply_header),8);return result;}
static struct cb_application_status wait_for(uint64_t pid,unsigned expected,uint64_t code){struct cb_application_ref r={pid};CHECK(call(CB_APPLICATION_WAIT,&r,sizeof(r))==CB_OK);struct cb_application_status status;COPY(&status,output+sizeof(struct cb_reply_header),sizeof(status));CHECK(status.process==pid && status.state==expected && status.exit_code==code);return status;}
static void drop(uint64_t pid){struct cb_application_ref r={pid};CHECK(call(CB_APPLICATION_RELEASE,&r,sizeof(r))==CB_OK);}
static uint64_t now(void){cb_handle_t io;CHECK(cb_system_get_handle(CB_IO_NAME,sizeof(CB_IO_NAME)-1,&(struct cb_interface_id)CB_IO_INTERFACE_ID,1,0,&io)==CB_OK);struct cb_call_header h=cb_call(io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_NOW_NS,0);uint64_t n,time;CHECK(cb_system_invoke(&h,sizeof(h),output,sizeof(output),&n)==CB_OK);COPY(&time,output+sizeof(struct cb_reply_header),8);CHECK(cb_system_request_release(header().request)==CB_OK && cb_system_release(io)==CB_OK);return time;}
__attribute__((noreturn)) void _start(const struct cb_startup *packet){
    struct cb_application_start start;CHECK(cb_application_start(packet,&start) && start.header.argc);const char *self=cb_application_argument(&start,0);
    if(same(self,"runtime-context")){CHECK(start.header.argc==3 && same(cb_application_argument(&start,1),"hello world") && same(cb_application_argument(&start,2),"CaseSensitive"));CHECK(start.header.cwd_bytes==14);for(unsigned i=0;i<14;++i)CHECK(start.cwd[i]==(unsigned char)"/example space"[i]);cb_handle_t memory;CHECK(cb_system_get_handle(CB_MEMORY_NAME,sizeof(CB_MEMORY_NAME)-1,&(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,1,0,&memory)==CB_DENIED);cb_exit(0);}
    if(same(self,"runtime-fault")){__asm__ volatile("ud2");cb_exit(99);}
    if(same(self,"runtime-cooperative")){uint64_t bytes;CHECK(cb_system_receive(frame,sizeof(frame),&bytes,0)==CB_OK);struct cb_delivery_header d;COPY(&d,frame,sizeof(d));CHECK(d.call.flags==(CB_CALL_PROVIDER_DELIVERY|CB_CALL_SYSTEM_EVENT) && !d.call.handle && d.call.operation==CB_HANDLE_RELEASE);cb_exit(7);}
    CHECK(cb_system_get_handle(CB_APPLICATIONS_NAME,sizeof(CB_APPLICATIONS_NAME)-1,&(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,1,0,&applications)==CB_OK);
    const char *context_args[]={"runtime-context","hello world","CaseSensitive"};
    if(same(self,"runtime-abandon")){uint64_t child;CHECK(launch("runtime-context",context_args,3,"/example space",&child)==CB_OK);cb_exit(0);}
    if(same(self,"runtime-attacker")){CHECK(start.header.argc==2);uint64_t pid=0;const char *p=cb_application_argument(&start,1);for(;*p;++p){CHECK(*p>='0'&&*p<='9');pid=pid*10+*p-'0';}struct cb_application_ref ref={pid};for(unsigned op=CB_APPLICATION_INFO;op<=CB_APPLICATION_RELEASE;++op)CHECK(call(op,&ref,sizeof(ref))==CB_DENIED);const char *args[]={"true"};CHECK(launch("true",args,1,"/",NULL)==CB_DENIED);cb_exit(0);}
    CHECK(same(self,"runtime-probe"));uint64_t child;
    const char *truth[]={"true"};CHECK(launch("not-installed",truth,1,"/",NULL)==CB_DENIED);CHECK(launch("applications",truth,1,"/",NULL)==CB_DENIED);CHECK(launch("../true",truth,1,"/",NULL)==CB_BAD_REQUEST);
    uint64_t peers[3];for(unsigned i=0;i<3;++i)CHECK(launch("runtime-context",context_args,3,"/example space",&peers[i])==CB_OK);for(unsigned i=0;i<3;++i){wait_for(peers[i],CB_APPLICATION_EXITED,0);drop(peers[i]);}
    const char *launcher[]={"launch","true"};CHECK(launch("launch",launcher,2,"/",&child)==CB_OK);wait_for(child,CB_APPLICATION_EXITED,0);drop(child);
    const char *fault[]={"runtime-fault"};CHECK(launch("runtime-fault",fault,1,"/",&child)==CB_OK);struct cb_application_status status=wait_for(child,CB_APPLICATION_FAULTED,134);CHECK(status.fault_vector==6);drop(child);
    const char *cooperative[]={"runtime-cooperative"};CHECK(launch("runtime-cooperative",cooperative,1,"/",&child)==CB_OK);struct cb_application_ref ref={child};CHECK(call(CB_APPLICATION_RELEASE,&ref,sizeof(ref))==CB_BUSY);
    CHECK(invoke(CB_APPLICATION_WAIT,&ref,sizeof(ref),now()+20000000,(struct cb_request_id){0},0,0)==CB_TIMEOUT);struct cb_reply_header timed=header();CHECK(timed.outcome==CB_OUTCOME_UNKNOWN && cb_system_request_release(timed.request)==CB_BUSY);
    CHECK(call(CB_APPLICATION_INFO,&ref,sizeof(ref))==CB_OK);
    char digits[24],decimal[24];unsigned count=0;uint64_t n=child;do{digits[count++]='0'+n%10;n/=10;}while(n);for(unsigned i=0;i<count;++i)decimal[i]=digits[count-1-i];decimal[count]=0;const char *attacker[]={"runtime-attacker",decimal};uint64_t attack;CHECK(launch("runtime-attacker",attacker,2,"/",&attack)==CB_OK);wait_for(attack,CB_APPLICATION_EXITED,0);drop(attack);
    CHECK(call(CB_APPLICATION_STOP,&ref,sizeof(ref))==CB_OK && call(CB_APPLICATION_STOP,&ref,sizeof(ref))==CB_OK);status=wait_for(child,CB_APPLICATION_EXITED,7);CHECK(status.stop_requested);
    CHECK(invoke(CB_APPLICATION_WAIT,&ref,sizeof(ref),0,timed.request,1,1)==CB_OK);drop(child);
    const char *abandon[]={"runtime-abandon"};CHECK(launch("runtime-abandon",abandon,1,"/",&child)==CB_OK);wait_for(child,CB_APPLICATION_EXITED,0);drop(child);
    CHECK(cb_system_release(applications)==CB_OK);cb_exit(0);
}
