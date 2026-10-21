/* Exercise the production broker with deterministic scheduler handoffs. */
#include "../sdk/service.h"
#define main existing_handles_main
#include "handles.c"
#undef main
static uint64_t clock_ns=100;
static unsigned blocks_waited,wakes;
static void (*handoff)(void);
static struct kernel_surface_result clock_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(clock_ns,1);}
static struct kernel_surface_result wait_control(MODULE_ARGS){MODULE_UNUSED();
    if(a==THREAD_CURRENT)return module_result(77,1);
    if(a==THREAD_WAKE){assert(b==77);++wakes;return module_result(1,1);}
    if(a==THREAD_BLOCK || a==THREAD_BLOCK_UNTIL){assert(handle_waiters && handoff);++blocks_waited;void (*run)(void)=handoff;handoff=NULL;run();return module_result(1,1);}
    return module_result(1,1);
}
static struct request *pending;
static struct capability *pipe_writer,*pipe_reader;
static void answer(void){assert(pending && !pending->done);unsigned char reply[sizeof(struct cb_reply_header)+8];struct cb_reply_header h={.version={sizeof(h),1,0},.request=pending->id,.provider_epoch=pending->publication->epoch,.result_bytes=8,.outcome=CB_COMMITTED};memcpy(reply,&h,sizeof(h));uint64_t result=321;memcpy(reply+sizeof(h),&result,8);pending->dispatched=1;pending->provider=2;assert(respond(policy(2),reply,sizeof(reply))==CB_OK);}
static void write_pipe(void){struct {uint64_t flags;char text[3];} __attribute__((packed)) payload={CB_STREAM_NONBLOCK,"abc"};uint64_t actual;size_t n;struct cb_call_header call={.handle=pipe_writer->id,.operation=CB_STREAM_WRITE,.payload_bytes=sizeof(payload)};assert(stream_call(1,&call,&payload,&actual,&n)==CB_OK && actual==3);}
static void close_writer(void){drop_capability(pipe_writer);pipe_writer=NULL;}
static void close_reader(void){drop_capability(pipe_reader);pipe_reader=NULL;}
static void expire(void){clock_ns=200;}
static void spurious(void){assert(handoff==NULL);handoff=answer;}
static cb_status_t gate(unsigned pid,unsigned op,const void *input,size_t n,void *output,size_t capacity,uint64_t *bytes){struct cb_system_gate g={.bytes=sizeof(g),.operation=op,.input=(uintptr_t)input,.input_bytes=n,.output=(uintptr_t)output,.output_capacity=capacity};cb_status_t status=handles_surface_gate(pid,(uintptr_t)&g,0,0,0,0).value;if(bytes)*bytes=g.result_bytes;return status;}
cb_status_t cb_system_collect(struct cb_request_id id,void *out,uint64_t cap,uint64_t *n){return gate(1,CB_SYSTEM_COLLECT,&id,sizeof(id),out,cap,n);}
cb_status_t cb_system_request_release(struct cb_request_id id){return gate(1,CB_SYSTEM_FORGET,&id,sizeof(id),NULL,0,NULL);}
cb_status_t cb_system_abandon(struct cb_request_id id,struct cb_abandon_result *out){return gate(1,CB_SYSTEM_ABANDON,&id,sizeof(id),out,sizeof(*out),NULL);}
static cb_status_t answer_status;
static void provider_error(void){struct cb_reply_header h={.version={sizeof(h),1,0},.request=pending->id,.provider_epoch=pending->publication->epoch,.status=answer_status,.outcome=CB_NOT_COMMITTED};pending->dispatched=1;pending->provider=2;assert(respond(policy(2),(void *)&h,sizeof(h))==CB_OK);}
static void abandon_waiting(void){struct cb_abandon_result result;assert(cb_system_abandon(pending->id,&result)==CB_OK && result.phase==CB_REQUEST_QUEUED);}
static void submit_again(struct cb_call_header *call,struct cb_request_id *id){assert(gate(1,CB_SYSTEM_SUBMIT,call,sizeof(*call),id,sizeof(*id),NULL)==CB_OK);pending=owned_request(1,*id);assert(pending);}
int main(void){
    struct publication monitor_io={.kernel=2};assert(kernel_reply_capacity(&monitor_io,CB_IO_PROCESS_SAMPLE)==sizeof(struct cb_process_sample));
    struct kernel_surfaces hub={0};state.hub=&hub;state.heap=1;state.alloc=heap_alloc;state.free=heap_free;state.cpu=cpu_stub;state.process_info=info_stub;state.control=wait_control;state.clock=clock_stub;state.copy=copy_stub;state.check=cpu_stub;
    const char manifest[]="{\"version\":1,\"resources\":[{\"name\":\"@events\",\"operations\":[\"read\"]},{\"name\":\"/@streams\",\"operations\":[\"pipe\",\"read\",\"write\"]}]}";
    for(unsigned pid=1;pid<=2;++pid){struct handle_policy p={.process=pid,.manifest=manifest,.bytes=sizeof(manifest)-1};assert(!CALL(handles_surface_admit,&p,0,0).status);}
    const struct cb_interface_id id={{1}};const char *ops[]={"read","secret"};assert(kernel_publication("@events",&id,ops,2,0));struct publication *provider=state.publications;provider->pid=2;
    struct capability *cap=new_capability(policy(1),provider,grant(policy(1),"@events"),0);assert(cap);
    struct cb_call_header call={.version={sizeof(call),1,0},.handle=cap->id,.interface=id,.operation=1};struct cb_request_id request;uint64_t n;
    long initial=blocks;remaining=0;assert(gate(1,CB_SYSTEM_SUBMIT,&call,sizeof(call),&request,sizeof(request),&n)==CB_RESOURCE_FAILURE);remaining=-1;assert(blocks==initial);
    assert(gate(1,CB_SYSTEM_SUBMIT,&call,sizeof(call),&request,1,&n)==CB_BUFFER_TOO_SMALL && !state.requests);
    assert(gate(1,CB_SYSTEM_SUBMIT,&call,sizeof(call),&request,sizeof(request),&n)==CB_OK && n==sizeof(request));pending=state.requests;assert(pending && !pending->done && !pending->dispatched && !blocks_waited);
    unsigned char result[256];assert(gate(2,CB_SYSTEM_COLLECT,&request,sizeof(request),result,sizeof(result),&n)==CB_BAD_HANDLE);assert(gate(1,CB_SYSTEM_COLLECT,&request,sizeof(request),result,sizeof(result),&n)==CB_WOULD_BLOCK);
    uint64_t timer_ready=0;assert(wait_any(policy(1),NULL,0,clock_ns,&timer_ready)==CB_TIMEOUT && timer_ready==UINT64_MAX);assert(wait_any(policy(1),NULL,0,0,&timer_ready)==CB_BAD_REQUEST);
    struct cb_wait_item item={.kind=CB_WAIT_REQUEST,.request=request};uint64_t ready;
    assert(wait_any(policy(2),&item,sizeof(item),0,&ready)==CB_BAD_HANDLE && !handle_waiters);
    handoff=spurious;assert(wait_any(policy(1),&item,sizeof(item),0,&ready)==CB_OK && ready==0 && blocks_waited==2 && wakes && !handle_waiters);
    assert(gate(1,CB_SYSTEM_COLLECT,&request,sizeof(request),result,1,&n)==CB_BUFFER_TOO_SMALL && n==sizeof(struct cb_reply_header)+8);
    remaining=0;assert(gate(1,CB_SYSTEM_COLLECT,&request,sizeof(request),result,sizeof(result),&n)==CB_RESOURCE_FAILURE && !n && owned_request(1,request)==pending);remaining=-1;
    assert(gate(1,CB_SYSTEM_COLLECT,&request,sizeof(request),result,sizeof(result),&n)==CB_OK);uint64_t value;memcpy(&value,result+sizeof(struct cb_reply_header),8);assert(value==321);
    assert(wait_any(policy(1),&item,sizeof(item),0,&ready)==CB_OK && blocks_waited==2);drop_request(pending);
    /* Provider status is distinct from collection transport readiness. */
    for(unsigned i=0;i<2;++i){submit_again(&call,&request);static struct cb_client client;memset(&client,0,sizeof(client));client.pending=request;
        assert(cb_collect(&client)==CB_WOULD_BLOCK && !client.collected && client.pending.serial);
        answer_status=i?CB_BUFFER_TOO_SMALL:CB_WOULD_BLOCK;provider_error();assert(cb_collect(&client)==answer_status && client.collected && !client.pending.serial && !owned_request(1,request));
    }
    struct cb_abandon_result abandoned;
    submit_again(&call,&request);long queued_blocks=blocks;
    assert(gate(2,CB_SYSTEM_ABANDON,&request,sizeof(request),&abandoned,sizeof(abandoned),NULL)==CB_BAD_HANDLE);
    assert(gate(1,CB_SYSTEM_ABANDON,&request,sizeof(request),&abandoned,1,NULL)==CB_BUFFER_TOO_SMALL && owned_request(1,request));
    remaining=0;assert(gate(1,CB_SYSTEM_ABANDON,&request,sizeof(request),&abandoned,sizeof(abandoned),NULL)==CB_RESOURCE_FAILURE && owned_request(1,request));remaining=-1;
    assert(cb_system_abandon(request,&abandoned)==CB_OK && abandoned.phase==CB_REQUEST_QUEUED && abandoned.outcome==CB_NOT_DISPATCHED && !owned_request(1,request) && blocks<queued_blocks);
    submit_again(&call,&request);item=(struct cb_wait_item){.kind=CB_WAIT_REQUEST,.request=request};handoff=abandon_waiting;assert(wait_any(policy(1),&item,sizeof(item),0,&ready)==CB_BAD_HANDLE && !handle_waiters && !state.requests);
    submit_again(&call,&request);pending->dispatched=1;pending->provider=2;pending->waiter=77;assert(cb_system_abandon(request,&abandoned)==CB_BUSY && !pending->abandoned);pending->waiter=0;
    struct cb_client abandoned_client={.pending=request};assert(cb_abandon(&abandoned_client,&abandoned)==CB_OK && !abandoned_client.pending.serial && abandoned.phase==CB_REQUEST_DISPATCHED && abandoned.outcome==CB_OUTCOME_UNKNOWN && pending->abandoned && !owned_request(1,request));
    /* Late completion is accepted and frees the tombstone even under OOM. */
    struct cb_reply_header late={.version={sizeof(late),1,0},.request=request,.provider_epoch=provider->epoch,.outcome=CB_COMMITTED};remaining=0;assert(respond(policy(2),(void *)&late,sizeof(late))==CB_OK);remaining=-1;assert(!state.requests);
    submit_again(&call,&request);answer();assert(cb_system_abandon(request,&abandoned)==CB_OK && abandoned.phase==CB_REQUEST_COMPLETE && abandoned.outcome==CB_COMMITTED && !state.requests);
    submit_again(&call,&request);pending->dispatched=1;pending->provider=2;assert(cb_system_abandon(request,&abandoned)==CB_OK);retire_publication(provider,1);assert(!state.requests);provider->dead=0;
    const struct cb_interface_id stream_id=CB_STREAM_INTERFACE_ID;const char *stream_ops[]={"pipe","read","write"};assert(kernel_publication(CB_STREAM_NAME,&stream_id,stream_ops,3,3));struct capability *factory=new_capability(policy(1),state.publications,grant(policy(1),CB_STREAM_NAME),0);struct cb_stream_pair pair;size_t actual;call=(struct cb_call_header){.handle=factory->id,.operation=CB_STREAM_PIPE};assert(stream_call(1,&call,NULL,&pair,&actual)==CB_OK);pipe_reader=capability(1,pair.reader);pipe_writer=capability(1,pair.writer);
    struct cb_wait_item both[2]={{.kind=CB_WAIT_READ,.handle=pair.reader},{.kind=CB_WAIT_WRITE,.handle=pair.writer}};
    assert(wait_any(policy(1),both,sizeof(both),0,&ready)==CB_OK && ready==1);
    item=both[0];assert(wait_any(policy(1),&item,sizeof(item),100,&ready)==CB_TIMEOUT && !handle_waiters);handoff=expire;assert(wait_any(policy(1),&item,sizeof(item),200,&ready)==CB_TIMEOUT && !handle_waiters);
    handoff=write_pipe;unsigned before_wait=blocks_waited;assert(wait_any(policy(1),&item,sizeof(item),0,&ready)==CB_OK && ready==0 && blocks_waited==before_wait+1);
    struct cb_stream_read read={4096,CB_STREAM_NONBLOCK};call=(struct cb_call_header){.handle=pair.reader,.operation=CB_STREAM_READ,.payload_bytes=sizeof(read)};assert(stream_call(1,&call,&read,result,&actual)==CB_OK && actual==3 && !memcmp(result,"abc",3));
    handoff=close_writer;assert(wait_any(policy(1),&item,sizeof(item),0,&ready)==CB_OK);assert(stream_call(1,&call,&read,result,&actual)==CB_OK && !actual);
    both[1].handle=UINT64_MAX;assert(wait_any(policy(1),both,sizeof(both),0,&ready)==CB_BAD_HANDLE); /* validate entire set, even behind ready EOF */
    item.kind=CB_WAIT_WRITE;assert(wait_any(policy(1),&item,sizeof(item),0,&ready)==CB_DENIED);item.kind=CB_WAIT_READ;
    drop_capability(pipe_reader);assert(wait_any(policy(1),&item,sizeof(item),0,&ready)==CB_BAD_HANDLE);
    call=(struct cb_call_header){.handle=factory->id,.operation=CB_STREAM_PIPE};assert(stream_call(1,&call,NULL,&pair,&actual)==CB_OK);pipe_reader=capability(1,pair.reader);pipe_writer=capability(1,pair.writer);
    /* Full writer must wake when the last reader disappears (broken pipe). */
    pipe_writer->stream->used=CB_PIPE_CAPACITY;item=(struct cb_wait_item){.kind=CB_WAIT_WRITE,.handle=pair.writer};handoff=close_reader;assert(wait_any(policy(1),&item,sizeof(item),0,&ready)==CB_OK);drop_capability(pipe_writer);
    assert(!handles_surface_exit(1,0,0,0,0,0).status && !handles_surface_exit(2,0,0,0,0,0).status);assert(!state.caps && !state.requests && !state.policies && !handle_waiters);while(state.publications){struct publication *p=state.publications;assert(!p->refs);p->dead=1;p->kernel=0;drop_publication(p);}assert(!blocks);
    puts("PASS wait-any: deferred replies, spurious wakes, ownership, full-set validation, deadlines, pipe progress/EOF/broken pipe and cleanup");return 0;
}
