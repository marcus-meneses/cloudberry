#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../kmodules/shared/handles.c"
static long remaining=-1,blocks;
static unsigned native_calls;static size_t last_allocation;
#ifdef CB_HANDLES_PROFILE
static unsigned pause_on_io,pause_on_copy;static uint64_t probe_ticks=1000;
static struct kernel_surface_result probe_clock(MODULE_ARGS){MODULE_UNUSED();probe_ticks+=100;return module_result(probe_ticks,1);}
#endif
static struct kernel_surface_result io_stub(MODULE_ARGS){MODULE_UNUSED();struct io_invocation *r=(void *)a;assert(r->capacity>=8);++native_calls;
#ifdef CB_HANDLES_PROFILE
    if(pause_on_io){uint64_t unused;pause_on_io=0;assert(!handles_surface_profile(HP_PAUSE,(uintptr_t)&unused,0,0,0,0).status);}
#endif
uint64_t value=123;memcpy(r->output,&value,8);r->actual=8;r->status=CB_OK;return module_result(1,1);}
static struct kernel_surface_result gpu_reply_stub(MODULE_ARGS){MODULE_UNUSED();struct io_invocation *r=(void *)a;++native_calls;assert(r->operation==CB_IO_GPU);assert(r->capacity==gpu_reply_capacity(r->input,r->bytes));if(r->capacity)memset(r->output,0x5a,r->capacity);r->actual=r->capacity;r->status=CB_OK;return module_result(1,1);}
static struct kernel_surface_result heap_alloc(MODULE_ARGS){MODULE_UNUSED();if(remaining==0)return module_result(0,0);if(remaining>0)--remaining;last_allocation=b*c;void *p=calloc(b,c);if(p)++blocks;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result heap_free(MODULE_ARGS){MODULE_UNUSED();assert(b && blocks>0);free((void *)b);--blocks;return module_result(1,1);}
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result info_stub(MODULE_ARGS){MODULE_UNUSED();*(struct process_info *)b=(struct process_info){.id=a,.state=PROCESS_READY};return module_result(1,1);}
static struct kernel_surface_result control_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result copy_stub(MODULE_ARGS){MODULE_UNUSED();
#ifdef CB_HANDLES_PROFILE
    if(pause_on_copy){uint64_t unused;pause_on_copy=0;assert(!handles_surface_profile(HP_PAUSE,(uintptr_t)&unused,0,0,0,0).status);}
#endif
if(e)memcpy((void *)b,(const void *)c,d);else memcpy((void *)c,(const void *)b,d);return module_result(d,1);}
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
#ifdef CB_HANDLES_PROFILE
    struct hp_report probe={.actors=1,.pids={2}};struct hp_call active_probe;
    assert(!handles_surface_profile(HP_BEGIN,(uintptr_t)&probe,0,0,0,0).status);
    assert(hp_begin(policy(2),CB_SYSTEM_INVOKE,(struct hp_entry_stamp){0,hp.generation},&active_probe) && hp.active==1);
    assert(!handles_surface_profile(HP_PAUSE,(uintptr_t)&probe,0,0,0,0).status && !hp_enabled() && hp.active==1);
    assert(handles_surface_profile(HP_END,(uintptr_t)&probe,0,0,0,0).status);
    assert(handles_surface_profile(HP_BEGIN,(uintptr_t)&probe,0,0,0,0).status); /* Cannot replace a draining report. */
    hp_finish(policy(2),&active_probe,0,0);
    assert(!handles_surface_profile(HP_END,(uintptr_t)&probe,0,0,0,0).status && probe.rows[0][CB_SYSTEM_INVOKE].count==1);
#endif
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
#ifdef CB_HANDLES_PROFILE
    /* Pause inside a real native gate: the admitted call must retain its output
     * timestamp after new admissions close. A zero clock used to hide this. */
    for(unsigned pause_stage=0;pause_stage<2;++pause_stage){
    state.clock=probe_clock;probe=(struct hp_report){.actors=1,.pids={2}};
    assert(!handles_surface_profile(HP_BEGIN,(uintptr_t)&probe,0,0,0,0).status);
    cap->publication->kernel=2;pause_on_io=pause_stage==0;pause_on_copy=pause_stage==1;unsigned char paused_output[sizeof(struct cb_reply_header)+8];
    struct cb_system_gate paused_gate={.bytes=sizeof(paused_gate),.operation=CB_SYSTEM_INVOKE,.input=(uintptr_t)&native,.input_bytes=sizeof(native),.output=(uintptr_t)paused_output,.output_capacity=sizeof(paused_output)};
    assert(handles_surface_gate(2,(uintptr_t)&paused_gate,0,0,0,0).value==CB_OK && !pause_on_io && !hp_enabled());
    assert(!handles_surface_profile(HP_END,(uintptr_t)&probe,0,0,0,0).status);
    assert(probe.clock_errors==0 && probe.rows[0][CB_SYSTEM_INVOKE].count==1);
    drop_request(state.requests);cap->publication->kernel=0;state.clock=cpu_stub;
    }
#endif
#ifdef CB_HANDLES_PROFILE
    state.clock=probe_clock;struct hp_entry_stamp inactive=hp_stamp();probe=(struct hp_report){.actors=1,.pids={2}};
    assert(!handles_surface_profile(HP_BEGIN,(uintptr_t)&probe,0,0,0,0).status);
    assert(!hp_begin(policy(2),CB_SYSTEM_INVOKE,inactive,&active_probe));struct hp_entry_stamp stale_stamp=hp_stamp();
    assert(!handles_surface_profile(HP_END,(uintptr_t)&probe,0,0,0,0).status);
    assert(!handles_surface_profile(HP_BEGIN,(uintptr_t)&probe,0,0,0,0).status);
    assert(!hp_begin(policy(2),CB_SYSTEM_INVOKE,stale_stamp,&active_probe));
    assert(!handles_surface_profile(HP_END,(uintptr_t)&probe,0,0,0,0).status);state.clock=cpu_stub;
#endif
    /* GPU replies include 96-byte info and chunked readbacks, beyond the
       original 64-byte native reply. Reserve before invoking the driver. */
    struct publication gpu_publication={.kernel=2};
    struct cb_io_gpu_request gpu={0};
    for(unsigned op=CB_GPU_INFO;op<=CB_GPU_SELECTED;++op){gpu=(struct cb_io_gpu_request){.operation=op,.bytes=123};size_t want=0;
        switch(op){case CB_GPU_INFO:case CB_GPU_OPEN:case CB_GPU_NEXT:case CB_GPU_SELECTED:want=sizeof(struct cb_io_gpu_info);break;case CB_GPU_MODES:want=sizeof(struct cb_io_gpu_modes);break;case CB_GPU_CREATE:case CB_GPU_TEXTURE:want=sizeof(struct cb_io_gpu_resource);break;case CB_GPU_CONTEXT:want=4;break;case CB_GPU_READ:want=123;break;case CB_GPU_CAPSET:want=sizeof(struct cb_io_gpu_capset)+123;break;}
        assert(kernel_reply_capacity(&gpu_publication,CB_IO_GPU,&gpu,sizeof(gpu))==want);
    }
    gpu=(struct cb_io_gpu_request){.operation=CB_GPU_CONTEXT,.context=1};assert(!gpu_reply_capacity(&gpu,sizeof(gpu)));
    gpu=(struct cb_io_gpu_request){.operation=CB_GPU_READ,.bytes=UINT64_MAX};assert(gpu_reply_capacity(&gpu,sizeof(gpu))==CB_FRAME_LIMIT-1024);
    gpu.operation=CB_GPU_CAPSET;assert(gpu_reply_capacity(&gpu,sizeof(gpu))==CB_FRAME_LIMIT-1024);
    assert(!gpu_reply_capacity(&gpu,sizeof(gpu)-1));gpu.operation=UINT32_MAX;assert(!gpu_reply_capacity(&gpu,sizeof(gpu)));
    state.io=gpu_reply_stub;gpu=(struct cb_io_gpu_request){.operation=CB_GPU_WRITE};
    struct cb_call_header gpu_call={.operation=CB_IO_GPU,.payload_bytes=sizeof(gpu)};
    unsigned calls_before_gpu=native_calls;long blocks_before_gpu=blocks;
    assert(kernel_call(2,&gpu_publication,&gpu_call,(const void *)&gpu,&response,&response_bytes)==CB_OK && native_calls==calls_before_gpu+1 && response_bytes==sizeof(struct cb_reply_header) && last_allocation==sizeof(struct cb_reply_header));release(response);
    gpu.operation=CB_GPU_READ;gpu.bytes=123;
    assert(kernel_call(2,&gpu_publication,&gpu_call,(const void *)&gpu,&response,&response_bytes)==CB_OK && response_bytes==sizeof(struct cb_reply_header)+123 && last_allocation==response_bytes);release(response);
    gpu.operation=CB_GPU_INFO;
    assert(kernel_call(2,&gpu_publication,&gpu_call,(const void *)&gpu,&response,&response_bytes)==CB_OK && response_bytes==sizeof(struct cb_reply_header)+sizeof(struct cb_io_gpu_info));release(response);
    remaining=0;calls_before_gpu=native_calls;assert(kernel_call(2,&gpu_publication,&gpu_call,(const void *)&gpu,&response,&response_bytes)==CB_RESOURCE_FAILURE && native_calls==calls_before_gpu && blocks==blocks_before_gpu);remaining=-1;state.io=io_stub;
    /* Repeated no-payload operations no longer reserve two 15 KiB bodies. */
    printf("GPU_REPLY_WORK iterations=32 old_body_bytes=%u new_body_bytes=0\n",32*2*(CB_FRAME_LIMIT-1024));
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
