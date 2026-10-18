#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../kmodules/shared/handles.c"
static long remaining=-1,blocks;
static struct kernel_surface_result heap_alloc(MODULE_ARGS){MODULE_UNUSED();if(remaining==0)return module_result(0,0);if(remaining>0)--remaining;void *p=calloc(b,c);if(p)++blocks;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result heap_free(MODULE_ARGS){MODULE_UNUSED();assert(b && blocks>0);free((void *)b);--blocks;return module_result(1,1);}
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result info_stub(MODULE_ARGS){MODULE_UNUSED();*(struct process_info *)b=(struct process_info){.id=a,.state=PROCESS_READY};return module_result(1,1);}
static struct kernel_surface_result control_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
int main(void){
    struct kernel_surfaces hub={0};state.hub=&hub;state.heap=1;state.alloc=heap_alloc;state.free=heap_free;state.cpu=cpu_stub;state.process_info=info_stub;state.control=control_stub;state.clock=cpu_stub;
    const char server_manifest[]="{\"version\":1,\"resources\":[{\"name\":\"@host\",\"publish\":true,\"operations\":[\"read\",\"secret\"]}]}";
    const char client_manifest[]="{\"version\":1,\"resources\":[{\"name\":\"@host\",\"operations\":[\"read\"]}]}";
    struct handle_policy admission={.process=1,.manifest=server_manifest,.bytes=sizeof(server_manifest)-1};assert(!CALL(handles_surface_admit,&admission,0,0).status);admission=(struct handle_policy){.process=2,.manifest=client_manifest,.bytes=sizeof(client_manifest)-1};assert(!CALL(handles_surface_admit,&admission,0,0).status);
    unsigned char bytes[1024]={0};struct cb_interface_id interface={{1}};
    struct cb_publication h={.version={sizeof(h),1,0},.interface=interface,.interface_major=1,.name_bytes=5};
    struct cb_description d={.version={sizeof(d),1,0},.interface=interface,.interface_major=1,.operation_count=2,.facts_bytes=2};size_t offset=sizeof(h)+5+sizeof(d);
    const char *names[]={"read","secret"};for(unsigned i=0;i<2;++i){struct cb_operation_description op={.version={sizeof(op),1,0},.operation=i+1,.name_bytes=strlen(names[i]),.request_schema_bytes=2,.reply_schema_bytes=2};copy_bytes(bytes+offset,&op,sizeof(op));offset+=sizeof(op);copy_bytes(bytes+offset,names[i],op.name_bytes);offset+=op.name_bytes;copy_bytes(bytes+offset,"{}{}",4);offset+=4;}
    d.operations_bytes=offset-sizeof(h)-5-sizeof(d);copy_bytes(bytes+offset,"{}",2);offset+=2;h.description_bytes=offset-sizeof(h)-5;copy_bytes(bytes,&h,sizeof(h));copy_bytes(bytes+sizeof(h),"@host",5);copy_bytes(bytes+sizeof(h)+5,&d,sizeof(d));
    uint64_t token;assert(publish(policy(1),bytes,offset,&token)==CB_OK);assert(publish(policy(1),bytes,offset,&token)==CB_NAME_IN_USE);assert(publish(policy(2),bytes,offset,&token)==CB_DENIED);
    unsigned char ask[sizeof(struct cb_acquire)+5];struct cb_acquire q={.interface=interface,.major=1,.name_bytes=5};copy_bytes(ask,&q,sizeof(q));copy_bytes(ask+sizeof(q),"@host",5);
    long before=blocks;remaining=1;assert(acquire(policy(2),ask,sizeof(ask),&token)==CB_RESOURCE_FAILURE);remaining=-1;assert(blocks==before);
    assert(acquire(policy(2),ask,sizeof(ask),&token)==CB_OK);assert(!capability(1,token));struct capability *cap=capability(2,token);assert(cap && permitted(cap->operations,"read",4) && !permitted(cap->operations,"secret",6));
    unsigned char *reflection;size_t reflected;assert(reflect(cap,&reflection,&reflected)==CB_OK);copy_bytes(&d,reflection+sizeof(struct cb_reply_header),sizeof(d));assert(d.operation_count==1 && d.facts_bytes==16);assert(!memcmp(reflection+reflected-d.facts_bytes,"{\"scope\":\"self\"}",16));release(reflection);
    assert(!handles_surface_channel(2,88,32,0,0,0).status);uint64_t data[2]={123,456};const struct cb_interface_id io=CB_IO_INTERFACE_ID;
    long mailbox_blocks=blocks;assert(!handles_surface_post(2,88,(uintptr_t)data,sizeof(data),(uintptr_t)&io,1).status);assert(blocks==mailbox_blocks);assert(handles_surface_post(2,88,(uintptr_t)data,sizeof(data),(uintptr_t)&io,1).status);
    unsigned char *notification=NULL;size_t notification_bytes=0;assert(receive(policy(2),0,&notification,&notification_bytes,1)==CB_BUFFER_TOO_SMALL && !notification);
    assert(receive(policy(2),0,&notification,&notification_bytes,CB_FRAME_LIMIT)==CB_OK);struct cb_delivery_header delivered;copy_bytes(&delivered,notification,sizeof(delivered));assert(delivered.call.flags==(CB_CALL_SYSTEM_EVENT|CB_CALL_PROVIDER_DELIVERY) && !delivered.call.request.serial && delivered.call.handle==88);release(notification);
    assert(!handles_surface_post(2,88,(uintptr_t)data,sizeof(data),(uintptr_t)&io,1).status);
    struct cb_call_header original={.version={sizeof(original),1,0},.payload_bytes=3};unsigned char captured_call[sizeof(original)+3];copy_bytes(captured_call,&original,sizeof(original));copy_bytes(captured_call+sizeof(original),"abc",3);
    struct request failure={.publication=cap->publication,.input=captured_call,.bytes=sizeof(captured_call)};uint64_t anchor=991;
    assert(error_result(&failure,CB_INPUT_RESYNC,CB_NOT_COMMITTED,&anchor,sizeof(anchor))==CB_INPUT_RESYNC);
    unsigned char *retained=failure.reply;remaining=0;assert(error_result(&failure,CB_PROVIDER_FAILED,CB_OUTCOME_UNKNOWN,NULL,0)==CB_RESOURCE_FAILURE && failure.failure==CB_RESOURCE_FAILURE && failure.reply==retained);remaining=-1;
    struct cb_reply_header normalized;copy_bytes(&normalized,failure.reply,sizeof(normalized));assert(normalized.result_bytes==8 && !memcmp(failure.reply+sizeof(normalized),&anchor,8));
    struct cb_error_header detail;copy_bytes(&detail,failure.reply+sizeof(normalized)+8,sizeof(detail));assert(detail.original_payload_bytes==3 && !memcmp(failure.reply+sizeof(normalized)+8+sizeof(detail),"abc",3) && detail.trace_bytes);release(failure.reply);
    assert(!handles_surface_decommission(1,0,0,0,0,0).status && cap->publication->dead);
    assert(receive(policy(1),0,&notification,&notification_bytes,CB_FRAME_LIMIT)==CB_OK);copy_bytes(&delivered,notification,sizeof(delivered));assert(!delivered.call.handle && delivered.call.operation==CB_HANDLE_RELEASE && delivered.call.flags==(CB_CALL_SYSTEM_EVENT|CB_CALL_PROVIDER_DELIVERY));release(notification);
    assert(!CALL(handles_surface_exit,1,0,0).status && cap->publication->dead);assert(!CALL(handles_surface_exit,2,0,0).status);assert(!state.policies && !state.caps && !state.publications && !state.requests && blocks==0);
    puts("PASS handles: publication policy, duplicate names, scoped reflection, cross-process token rejection, OOM rollback and owner cleanup");return 0;
}
