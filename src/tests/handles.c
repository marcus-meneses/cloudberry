#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../kmodules/shared/handles.c"
static long remaining=-1,blocks;
static unsigned native_calls;
static struct kernel_surface_result io_stub(MODULE_ARGS){MODULE_UNUSED();struct io_invocation *r=(void *)a;assert(r->capacity>=8);++native_calls;uint64_t value=123;memcpy(r->output,&value,8);r->actual=8;r->status=CB_OK;return module_result(1,1);}
static struct kernel_surface_result heap_alloc(MODULE_ARGS){MODULE_UNUSED();if(remaining==0)return module_result(0,0);if(remaining>0)--remaining;void *p=calloc(b,c);if(p)++blocks;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result heap_free(MODULE_ARGS){MODULE_UNUSED();assert(b && blocks>0);free((void *)b);--blocks;return module_result(1,1);}
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result info_stub(MODULE_ARGS){MODULE_UNUSED();*(struct process_info *)b=(struct process_info){.id=a,.state=PROCESS_READY};return module_result(1,1);}
static struct kernel_surface_result control_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result copy_stub(MODULE_ARGS){MODULE_UNUSED();if(e)memcpy((void *)b,(const void *)c,d);else memcpy((void *)c,(const void *)b,d);return module_result(d,1);}
int main(void){
    struct kernel_surfaces hub={0};state.hub=&hub;state.heap=1;state.alloc=heap_alloc;state.free=heap_free;state.cpu=cpu_stub;state.process_info=info_stub;state.control=control_stub;state.clock=cpu_stub;
    const char server_manifest[]="{\"version\":1,\"resources\":[{\"name\":\"@host\",\"publish\":true,\"operations\":[\"read\",\"secret\"]}]}";
    const char client_manifest[]="{\"version\":1,\"resources\":[{\"name\":\"@host\",\"operations\":[\"read\"]}]}";
    struct handle_policy admission={.process=1,.manifest=server_manifest,.bytes=sizeof(server_manifest)-1};assert(!CALL(handles_surface_admit,&admission,0,0).status);admission=(struct handle_policy){.process=2,.manifest=client_manifest,.bytes=sizeof(client_manifest)-1};assert(!CALL(handles_surface_admit,&admission,0,0).status);
    /* Small gate inputs remain usable under allocator exhaustion; a larger
       frame still fails cleanly without leaking or freeing stack storage. */
    state.copy=copy_stub;state.check=cpu_stub;
    unsigned char gate_input[257]={0};
    struct cb_system_gate gate={.bytes=sizeof(gate),.operation=UINT32_MAX,.input=(uintptr_t)gate_input};
    long gate_blocks=blocks;
    remaining=0;
    assert(handles_surface_gate(2,(uintptr_t)&gate,0,0,0,0).value==CB_BAD_REQUEST);
    gate.input_bytes=256;
    assert(handles_surface_gate(2,(uintptr_t)&gate,0,0,0,0).value==CB_BAD_REQUEST);
    gate.input_bytes=257;
    assert(handles_surface_gate(2,(uintptr_t)&gate,0,0,0,0).value==CB_RESOURCE_FAILURE);
    remaining=-1;
    assert(handles_surface_gate(2,(uintptr_t)&gate,0,0,0,0).value==CB_BAD_REQUEST && blocks==gate_blocks);
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
    struct {struct cb_call_header h;struct cb_describe_request d;} reflected_call={.h={.version={sizeof(struct cb_call_header),1,0},.handle=cap->id,.interface=CB_HANDLE_INTERFACE_ID,.operation=CB_HANDLE_DESCRIBE,.payload_bytes=sizeof(struct cb_describe_request)},.d={.version={sizeof(struct cb_describe_request),1,0},.interface=interface}};
    long request_blocks=blocks;unsigned char *response=NULL;size_t response_bytes=0;
    remaining=0;assert(invoke(policy(2),(void *)&reflected_call,sizeof(reflected_call),&response,&response_bytes,0)==CB_RESOURCE_FAILURE && blocks==request_blocks);remaining=-1;
    assert(invoke(policy(2),(void *)&reflected_call,sizeof(reflected_call),&response,&response_bytes,0)==CB_OK);
    struct request *owned=state.requests;assert(owned && owned->input==(unsigned char *)(owned+1));
    reflected_call.d.interface.bytes[0]^=1;struct cb_describe_request captured;copy_bytes(&captured,owned->input+sizeof(struct cb_call_header),sizeof(captured));assert(captured.interface.bytes[0]==interface.bytes[0]);reflected_call.d.interface.bytes[0]^=1;
    release(response);drop_request(owned);assert(blocks==request_blocks);
    remaining=1;assert(invoke(policy(2),(void *)&reflected_call,sizeof(reflected_call),&response,&response_bytes,0)==CB_RESOURCE_FAILURE);remaining=-1;
    assert(state.requests && state.requests->failure==CB_RESOURCE_FAILURE);drop_request(state.requests);assert(blocks==request_blocks);
    struct {struct cb_call_header h;uint64_t payload;} native={.h={.version={sizeof(struct cb_call_header),1,0},.handle=cap->id,.interface=interface,.operation=1,.payload_bytes=8}};
    cap->publication->kernel=2;state.io=io_stub;
    remaining=1;assert(invoke(policy(2),(void *)&native,sizeof(native),&response,&response_bytes,0)==CB_RESOURCE_FAILURE && !native_calls);remaining=-1;assert(state.requests && !state.requests->dispatched);drop_request(state.requests);assert(blocks==request_blocks);
    remaining=3;assert(invoke(policy(2),(void *)&native,sizeof(native),&response,&response_bytes,0)==CB_OK && native_calls==1 && remaining==0);remaining=-1;release(response);drop_request(state.requests);assert(blocks==request_blocks);cap->publication->kernel=0;
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
    /* A dispatched provider BAD_REQUEST must complete, including replacement
       of a provisional timeout. */
    struct request *bad_request=allocate(sizeof(*bad_request)+sizeof(captured_call));assert(bad_request);bad_request->input=(void *)(bad_request+1);bad_request->bytes=sizeof(captured_call);copy_bytes(bad_request->input,captured_call,sizeof(captured_call));bad_request->publication=cap->publication;++cap->publication->refs;bad_request->pid=2;bad_request->cap=cap->id;bad_request->provider=1;bad_request->id=(struct cb_request_id){2,serial()};bad_request->dispatched=1;state.requests=state.request_tail=bad_request;
    assert(error_reply(bad_request,CB_TIMEOUT,CB_OUTCOME_UNKNOWN)==CB_TIMEOUT);
    struct cb_reply_header provider_error={.version={sizeof(provider_error),1,0},.request=bad_request->id,.status=CB_BAD_REQUEST,.outcome=CB_NOT_COMMITTED,.provider_epoch=cap->publication->epoch};
    assert(respond(policy(1),(const void *)&provider_error,sizeof(provider_error))==CB_OK && bad_request->completed && bad_request->done && !bad_request->failure);
    drop_request(bad_request);
    /* New stream/filesystem statuses are provider results, not malformed replies. */
    for(unsigned status=CB_WOULD_BLOCK;status<CB_STATUS_COUNT;++status){
        struct request *r=allocate(sizeof(*r)+sizeof(captured_call));assert(r);r->input=(void *)(r+1);r->bytes=sizeof(captured_call);copy_bytes(r->input,captured_call,sizeof(captured_call));r->publication=cap->publication;++cap->publication->refs;r->pid=2;r->cap=cap->id;r->provider=1;r->id=(struct cb_request_id){2,serial()};r->dispatched=1;state.requests=state.request_tail=r;
        provider_error.request=r->id;provider_error.status=status;assert(respond(policy(1),(const void *)&provider_error,sizeof(provider_error))==CB_OK && r->completed && r->done && !r->failure);struct cb_reply_header delivered;copy_bytes(&delivered,r->reply,sizeof(delivered));assert(delivered.status==status);drop_request(r);
    }

    /* Closing a session revokes queued work and new calls while retaining a
     * dispatched payload until its provider replies. No cap/code is reclaimed. */
    struct request *queued=allocate(sizeof(*queued)+sizeof(captured_call)),*active=allocate(sizeof(*active)+sizeof(captured_call));assert(queued && active);
    struct request *requests[2]={queued,active};for(unsigned i=0;i<2;++i){struct request *r=requests[i];r->input=(void *)(r+1);r->bytes=sizeof(captured_call);copy_bytes(r->input,captured_call,sizeof(captured_call));r->publication=cap->publication;++cap->publication->refs;r->pid=2;r->cap=cap->id;r->provider=i?1:0;r->id=(struct cb_request_id){2,serial()};r->dispatched=i;}
    queued->next=active;state.requests=queued;state.request_tail=active;assert(!handles_surface_revoke(2,0,0,0,0,0).status && policy(2)->revoked && queued->done && queued->completed && !active->done && !active->completed);
    struct cb_reply_header revoked;copy_bytes(&revoked,queued->reply,sizeof(revoked));assert(revoked.status==CB_REVOKED && revoked.outcome==CB_NOT_DISPATCHED);
    provider_error.request=active->id;provider_error.status=CB_OK;provider_error.outcome=CB_COMMITTED;assert(respond(policy(1),(const void *)&provider_error,sizeof(provider_error))==CB_OK && active->done && active->completed);
    drop_request(queued);drop_request(active);
    struct request *oom=allocate(sizeof(*oom)+sizeof(captured_call));assert(oom);oom->input=(void *)(oom+1);oom->bytes=sizeof(captured_call);copy_bytes(oom->input,captured_call,sizeof(captured_call));oom->publication=cap->publication;++cap->publication->refs;oom->pid=2;oom->cap=cap->id;oom->id=(struct cb_request_id){2,serial()};state.requests=state.request_tail=oom;remaining=0;assert(!handles_surface_revoke(2,0,0,0,0,0).status && oom->done && oom->completed && oom->failure==CB_RESOURCE_FAILURE);remaining=-1;drop_request(oom);
    struct cb_call_header denied_call={.version={sizeof(denied_call),1,0},.handle=cap->id,.interface=interface,.operation=1};unsigned char *denied_reply=NULL;size_t denied_bytes=0;
    assert(invoke(policy(2),(void *)&denied_call,sizeof(denied_call),&denied_reply,&denied_bytes,0)==CB_REVOKED);struct cb_reply_header denied_header;copy_bytes(&denied_header,denied_reply,sizeof(denied_header));assert(denied_header.status==CB_REVOKED && denied_header.request.serial && owned_request(2,denied_header.request)->completed);release(denied_reply);drop_request(owned_request(2,denied_header.request));
    struct stream *closed_pipe=allocate(sizeof(*closed_pipe)+CB_PIPE_CAPACITY);assert(closed_pipe);struct capability *closed_reader=stream_endpoint(policy(2),cap->publication,closed_pipe,CB_STREAM_READ);assert(closed_reader);struct cb_call_header closed_call={.handle=closed_reader->id,.operation=CB_STREAM_READ,.payload_bytes=sizeof(struct cb_stream_read)};struct cb_stream_read closed_read={1,0};size_t actual=0;unsigned char read_byte;
    assert(stream_call(2,&closed_call,&closed_read,&read_byte,&actual)==CB_REVOKED);drop_capability(closed_reader);

    admission=(struct handle_policy){.process=4,.manifest=client_manifest,.bytes=sizeof(client_manifest)-1};assert(!CALL(handles_surface_admit,&admission,0,0).status);
    assert(acquire(policy(4),ask,sizeof(ask),&token)==CB_OK);struct capability *old_cap=capability(4,token);uint64_t old_epoch=old_cap->publication->epoch;
    assert(!handles_surface_decommission(1,0,0,0,0,0).status && cap->publication->dead);
    assert(receive(policy(1),0,&notification,&notification_bytes,CB_FRAME_LIMIT)==CB_OK);copy_bytes(&delivered,notification,sizeof(delivered));assert(!delivered.call.handle && delivered.call.operation==CB_HANDLE_RELEASE && delivered.call.flags==(CB_CALL_SYSTEM_EVENT|CB_CALL_PROVIDER_DELIVERY));release(notification);
    assert(!CALL(handles_surface_exit,1,0,0).status && cap->publication->dead);
    admission=(struct handle_policy){.process=3,.manifest=server_manifest,.bytes=sizeof(server_manifest)-1};assert(!CALL(handles_surface_admit,&admission,0,0).status);
    assert(publish(policy(3),bytes,offset,&token)==CB_OK && acquire(policy(4),ask,sizeof(ask),&token)==CB_OK);struct capability *fresh=capability(4,token);
    assert(fresh && fresh->publication->pid==3 && fresh->publication->epoch!=old_epoch && old_cap->publication->pid==1 && old_cap->publication->dead && permitted(fresh->operations,"read",4) && !permitted(fresh->operations,"secret",6));
    struct cb_call_header stale={.version={sizeof(stale),1,0},.handle=old_cap->id,.interface=interface,.operation=1};unsigned char *stale_reply=NULL;size_t stale_bytes;
    assert(invoke(policy(4),(void *)&stale,sizeof(stale),&stale_reply,&stale_bytes,0)==CB_PROVIDER_FAILED);struct cb_reply_header stale_header;copy_bytes(&stale_header,stale_reply,sizeof(stale_header));assert(stale_header.outcome==CB_NOT_DISPATCHED && stale_header.provider_epoch==old_epoch);release(stale_reply);drop_request(owned_request(4,stale_header.request));
    assert(!CALL(handles_surface_exit,2,0,0).status && !CALL(handles_surface_exit,3,0,0).status && !CALL(handles_surface_exit,4,0,0).status);assert(!state.policies && !state.caps && !state.publications && !state.requests && blocks==0);
    puts("PASS handles: publication policy, duplicate names, scoped reflection, cross-process token rejection, OOM rollback, fresh restart epoch/stale-cap failure and owner cleanup");return 0;
}
