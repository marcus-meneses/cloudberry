#include "../../sdk/system.h"
#include "../../interfaces/io.h"
#include "../../interfaces/startup.h"
#include "../../interfaces/admission.h"
#include "../../interfaces/installations.h"
struct waiter {struct waiter *next;struct cb_delivery_header delivery;};
struct child {struct child *next;uint64_t owner;struct cb_application_status status;struct waiter *waiters;unsigned terminal;};
static struct child *children;
static const void *catalog;
static cb_handle_t memory,io,publication;
static unsigned stopping;
static unsigned char incoming[CB_FRAME_LIMIT],outgoing[CB_FRAME_LIMIT],io_frame[CB_FRAME_LIMIT],io_output[CB_FRAME_LIMIT];
#define CHECK(x) do{if(!(x))cb_exit(__LINE__);}while(0)
#define COPY cb_install_copy
static cb_status_t forget_result(void){struct cb_reply_header h;COPY(&h,io_output,sizeof(h));return h.request.serial?cb_system_request_release(h.request):CB_OK;}
static cb_status_t invoke_io(unsigned operation,const void *payload,size_t bytes){
    if(bytes>sizeof(io_frame)-512-sizeof(struct cb_call_header))return CB_BAD_REQUEST;
    struct cb_call_header call=cb_call(io,(struct cb_interface_id)CB_IO_INTERFACE_ID,operation,bytes);COPY(io_frame,&call,sizeof(call));COPY(io_frame+sizeof(call),payload,bytes);uint64_t result_bytes;
    for(size_t i=0;i<sizeof(struct cb_reply_header);++i)io_output[i]=0;
    cb_status_t status=cb_system_invoke(io_frame,sizeof(call)+bytes,io_output,sizeof(io_output),&result_bytes);if(forget_result()!=CB_OK)cb_exit(CB_RESOURCE_FAILURE);return status;
}
static void *allocate(size_t bytes){struct {struct cb_call_header c;struct cb_memory_request m;} r={.c=cb_call(memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,sizeof(r.m)),.m={.bytes=bytes}};uint64_t n;if(cb_system_invoke(&r,sizeof(r),io_output,sizeof(io_output),&n)!=CB_OK)return NULL;struct cb_memory_result result;COPY(&result,io_output+sizeof(struct cb_reply_header),sizeof(result));CHECK(forget_result()==CB_OK);return (void *)(uintptr_t)result.address;}
static void release(void *p){if(!p)return;struct {struct cb_call_header c;struct cb_memory_request m;} r={.c=cb_call(memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,sizeof(r.m)),.m={.address=(uintptr_t)p}};uint64_t n;CHECK(cb_system_invoke(&r,sizeof(r),io_output,sizeof(io_output),&n)==CB_OK && forget_result()==CB_OK);}
static void reply(const struct cb_delivery_header *delivery,cb_status_t status,const void *result,size_t bytes){struct cb_reply_header h={.version={sizeof(h),1,0},.request=delivery->call.request,.provider_epoch=delivery->provider_epoch,.status=status,.outcome=status?CB_NOT_COMMITTED:CB_COMMITTED,.result_bytes=bytes};COPY(outgoing,&h,sizeof(h));COPY(outgoing+sizeof(h),result,bytes);cb_status_t sent=cb_system_reply(outgoing,sizeof(h)+bytes);CHECK(sent==CB_OK || sent==CB_BAD_REQUEST);}
static void publish(void){
    static const char *names[]={"launch","info","wait","stop","release","list","processes"};struct cb_publication p={.version={sizeof(p),1,0},.interface=CB_APPLICATIONS_INTERFACE_ID,.interface_major=1,.name_bytes=sizeof(CB_APPLICATIONS_NAME)-1};struct cb_description d={.version={sizeof(d),1,0},.interface=CB_APPLICATIONS_INTERFACE_ID,.interface_major=1,.operation_count=7,.facts_bytes=2};size_t at=sizeof(p)+p.name_bytes+sizeof(d);
    for(unsigned i=0;i<7;++i){size_t n=0;while(names[i][n])++n;struct cb_operation_description op={.version={sizeof(op),1,0},.operation=i+1,.name_bytes=n,.request_schema_bytes=2,.reply_schema_bytes=2};COPY(outgoing+at,&op,sizeof(op));at+=sizeof(op);COPY(outgoing+at,names[i],n);at+=n;COPY(outgoing+at,"{}{}",4);at+=4;}
    d.operations_bytes=at-sizeof(p)-p.name_bytes-sizeof(d);COPY(outgoing+at,"{}",2);at+=2;p.description_bytes=sizeof(d)+d.operations_bytes+2;COPY(outgoing,&p,sizeof(p));COPY(outgoing+sizeof(p),CB_APPLICATIONS_NAME,p.name_bytes);COPY(outgoing+sizeof(p)+p.name_bytes,&d,sizeof(d));CHECK(cb_system_publish(outgoing,at,&publication)==CB_OK);
}
static struct child *find(uint64_t pid){for(struct child *c=children;c;c=c->next)if(c->status.process==pid)return c;return NULL;}
static void remove_child(struct child *child){struct cb_application_ref ref={child->status.process};CHECK(invoke_io(CB_IO_PROCESS_RELEASE,&ref,sizeof(ref))==CB_OK);struct child **at=&children;while(*at!=child)at=&(*at)->next;*at=child->next;release(child);}
static cb_status_t launch(uint64_t owner,const uint8_t *identity,const void *payload,size_t bytes,uint64_t *process){
    if(!cb_application_launch_valid(payload,bytes) || bytes>CB_FRAME_LIMIT-512-sizeof(struct cb_call_header)-8)return CB_BAD_REQUEST;
    struct cb_install_record caller,target;
    if(owner && (!cb_install_find(catalog,NULL,0,identity,&caller) || !(caller.flags&CB_INSTALL_LAUNCHER)))return CB_DENIED;
    struct cb_application_launch request;COPY(&request,payload,sizeof(request));if(!cb_install_find(catalog,(const unsigned char *)payload+request.version.bytes,request.name_bytes,NULL,&target) || !(target.flags&CB_INSTALL_APPLICATION))return CB_DENIED;
    struct child *child=allocate(sizeof(*child));if(!child)return CB_RESOURCE_FAILURE;
    /* io_frame is separate from incoming. Its copy source must not overlap. */
    unsigned char *framed=allocate(bytes+8);if(!framed){release(child);return CB_RESOURCE_FAILURE;}COPY(framed,&owner,8);COPY(framed+8,payload,bytes);cb_status_t result=invoke_io(CB_IO_PROCESS_LAUNCH,framed,bytes+8);uint64_t pid=0;if(result==CB_OK)COPY(&pid,io_output+sizeof(struct cb_reply_header),8);release(framed);if(result!=CB_OK){release(child);return result;}
    child->owner=owner;child->status=(struct cb_application_status){.process=pid,.state=CB_APPLICATION_RUNNING};child->next=children;children=child;*process=pid;return CB_OK;
}
static void event(const struct cb_io_process_event *event){
    struct child *child=find(event->status.process);if(!child)return;
    if(event->kind==CB_IO_PROCESS_OWNER_EXIT){if(child->owner!=event->owner)return;child->owner=0;if(child->terminal)remove_child(child);return;}
    if(event->kind!=CB_IO_PROCESS_COMPLETE || child->terminal)return;
    child->status=event->status;struct cb_application_ref ref={child->status.process};CHECK(invoke_io(CB_IO_PROCESS_REAP,&ref,sizeof(ref))==CB_OK);child->terminal=1;
    while(child->waiters){struct waiter *w=child->waiters;child->waiters=w->next;reply(&w->delivery,CB_OK,&child->status,sizeof(child->status));release(w);}
    if(!child->owner)remove_child(child);
}
__attribute__((noreturn)) void _start(const struct cb_startup *packet){
    const void *initial;size_t catalog_bytes,initial_bytes;
    CHECK(packet && cb_startup_find(packet,packet->bytes,"catalog.bin",&catalog,&catalog_bytes) && cb_install_valid(catalog,catalog_bytes));
    CHECK(cb_startup_find(packet,packet->bytes,"initial.bin",&initial,&initial_bytes));
    CHECK(cb_system_get_handle(CB_MEMORY_NAME,sizeof(CB_MEMORY_NAME)-1,&(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,1,0,&memory)==CB_OK);
    CHECK(cb_system_get_handle(CB_IO_NAME,sizeof(CB_IO_NAME)-1,&(struct cb_interface_id)CB_IO_INTERFACE_ID,1,0,&io)==CB_OK);publish();
    if(initial_bytes){uint64_t pid;CHECK(launch(0,NULL,initial,initial_bytes,&pid)==CB_OK);}
    for(;;){uint64_t bytes;CHECK(cb_system_receive(incoming,sizeof(incoming),&bytes,0)==CB_OK && bytes>=sizeof(struct cb_delivery_header));struct cb_delivery_header d;COPY(&d,incoming,sizeof(d));CHECK(d.call.version.bytes==sizeof(d) && d.context_bytes<=bytes-sizeof(d) && d.call.payload_bytes==bytes-sizeof(d)-d.context_bytes);const unsigned char *payload=incoming+sizeof(d)+d.context_bytes;
        if(d.call.flags==(CB_CALL_PROVIDER_DELIVERY|CB_CALL_SYSTEM_EVENT)){
            if(d.call.operation==CB_HANDLE_RELEASE && !d.call.handle){stopping=1;struct child *c=children;while(c){struct child *next=c->next;c->owner=0;if(c->terminal)remove_child(c);else {struct cb_application_ref ref={c->status.process};CHECK(invoke_io(CB_IO_PROCESS_STOP,&ref,sizeof(ref))==CB_OK);}c=next;}if(!children){CHECK(cb_system_release(publication)==CB_OK);cb_exit(0);}continue;}
            if(d.call.operation==CB_IO_PROCESS_EVENT && cb_install_equal(&d.call.interface,&(struct cb_interface_id)CB_IO_INTERFACE_ID,sizeof(d.call.interface)) && d.call.payload_bytes==sizeof(struct cb_io_process_event)){struct cb_io_process_event e;COPY(&e,payload,sizeof(e));event(&e);if(stopping && !children){CHECK(cb_system_release(publication)==CB_OK);cb_exit(0);}}continue;
        }
        struct cb_admission caller;if(d.call.flags!=CB_CALL_PROVIDER_DELIVERY || d.call.handle!=publication || !cb_admission_parse(incoming+sizeof(d),d.context_bytes,&caller)){reply(&d,CB_DENIED,NULL,0);continue;}
        if(d.call.operation==CB_APPLICATION_LAUNCH){if(stopping){reply(&d,CB_REVOKED,NULL,0);continue;}uint64_t pid;cb_status_t status=launch(caller.process,caller.installation,payload,d.call.payload_bytes,&pid);reply(&d,status,status?NULL:&pid,status?0:8);continue;}
        if(d.call.operation==CB_APPLICATION_LIST){if(d.call.payload_bytes!=8){reply(&d,CB_BAD_REQUEST,NULL,0);continue;}uint64_t cursor;COPY(&cursor,payload,8);struct cb_install_catalog header;COPY(&header,catalog,sizeof(header));size_t at=sizeof(header);uint64_t index=0;unsigned char data[CB_FRAME_LIMIT];size_t size=0;while(at<header.bytes){struct cb_install_record record;COPY(&record,(const unsigned char *)catalog+at,sizeof(record));if(index++>=cursor && (record.flags&CB_INSTALL_APPLICATION)){struct cb_application_entry entry={index,record.name_bytes};COPY(data,&entry,sizeof(entry));COPY(data+sizeof(entry),(const unsigned char *)catalog+at+sizeof(record),record.name_bytes);size=sizeof(entry)+record.name_bytes;break;}at+=record.bytes;}reply(&d,CB_OK,data,size);continue;}
        if(d.call.operation==CB_APPLICATION_PROCESSES){if(d.call.payload_bytes!=sizeof(struct cb_process_query)){reply(&d,CB_BAD_REQUEST,NULL,0);continue;}cb_status_t status=invoke_io(CB_IO_PROCESS_SAMPLE,payload,d.call.payload_bytes);struct cb_reply_header result;COPY(&result,io_output,sizeof(result));reply(&d,status,status?NULL:io_output+sizeof(result),status?0:result.result_bytes);continue;}
        if(d.call.payload_bytes!=sizeof(struct cb_application_ref)){reply(&d,CB_BAD_REQUEST,NULL,0);continue;}
        struct cb_application_ref ref;COPY(&ref,payload,sizeof(ref));struct child *child=find(ref.process);if(!child || child->owner!=caller.process){reply(&d,CB_DENIED,NULL,0);continue;}
        if(d.call.operation==CB_APPLICATION_INFO)reply(&d,CB_OK,&child->status,sizeof(child->status));
        else if(d.call.operation==CB_APPLICATION_WAIT){if(child->terminal)reply(&d,CB_OK,&child->status,sizeof(child->status));else {struct waiter *w=allocate(sizeof(*w));if(!w){reply(&d,CB_RESOURCE_FAILURE,NULL,0);continue;}w->delivery=d;w->next=child->waiters;child->waiters=w;}}
        else if(d.call.operation==CB_APPLICATION_STOP){cb_status_t status=child->terminal?CB_OK:invoke_io(CB_IO_PROCESS_STOP,&ref,sizeof(ref));if(!status && !child->terminal)child->status.stop_requested=1;reply(&d,status,NULL,0);}
        else if(d.call.operation==CB_APPLICATION_RELEASE){cb_status_t status=child->terminal?CB_OK:CB_BUSY;reply(&d,status,NULL,0);if(!status)remove_child(child);}
        else reply(&d,CB_BAD_REQUEST,NULL,0);
    }
}
