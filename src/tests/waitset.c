/* Exercise the production broker with deterministic scheduler handoffs. */
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
static void answer(void){assert(pending && !pending->done);unsigned char reply[sizeof(struct cb_reply_header)+8];struct cb_reply_header h={.version={sizeof(h),1,0},.request=pending->id,.provider_epoch=pending->publication->epoch,.result_bytes=8};memcpy(reply,&h,sizeof(h));uint64_t result=321;memcpy(reply+sizeof(h),&result,8);pending->dispatched=1;pending->provider=2;assert(respond(policy(2),reply,sizeof(reply))==CB_OK);}
static void write_pipe(void){struct {uint64_t flags;char text[3];} __attribute__((packed)) payload={CB_STREAM_NONBLOCK,"abc"};uint64_t actual;size_t n;struct cb_call_header call={.handle=pipe_writer->id,.operation=CB_STREAM_WRITE,.payload_bytes=sizeof(payload)};assert(stream_call(1,&call,&payload,&actual,&n)==CB_OK && actual==3);}
static void close_writer(void){drop_capability(pipe_writer);pipe_writer=NULL;}
static void close_reader(void){drop_capability(pipe_reader);pipe_reader=NULL;}
static void expire(void){clock_ns=200;}
static void spurious(void){assert(handoff==NULL);handoff=answer;}
static cb_status_t gate(unsigned pid,unsigned op,const void *input,size_t n,void *output,size_t capacity,uint64_t *bytes){struct cb_system_gate g={.bytes=sizeof(g),.operation=op,.input=(uintptr_t)input,.input_bytes=n,.output=(uintptr_t)output,.output_capacity=capacity};cb_status_t status=handles_surface_gate(pid,(uintptr_t)&g,0,0,0,0).value;if(bytes)*bytes=g.result_bytes;return status;}
int main(void){
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
    struct cb_wait_item item={.kind=CB_WAIT_REQUEST,.request=request};uint64_t ready;
    assert(wait_any(policy(2),&item,sizeof(item),0,&ready)==CB_BAD_HANDLE && !handle_waiters);
    handoff=spurious;assert(wait_any(policy(1),&item,sizeof(item),0,&ready)==CB_OK && ready==0 && blocks_waited==2 && wakes && !handle_waiters);
    assert(gate(1,CB_SYSTEM_COLLECT,&request,sizeof(request),result,1,&n)==CB_BUFFER_TOO_SMALL && n==sizeof(struct cb_reply_header)+8);
    remaining=0;assert(gate(1,CB_SYSTEM_COLLECT,&request,sizeof(request),result,sizeof(result),&n)==CB_RESOURCE_FAILURE && !n && owned_request(1,request)==pending);remaining=-1;
    assert(gate(1,CB_SYSTEM_COLLECT,&request,sizeof(request),result,sizeof(result),&n)==CB_OK);uint64_t value;memcpy(&value,result+sizeof(struct cb_reply_header),8);assert(value==321);
    assert(wait_any(policy(1),&item,sizeof(item),0,&ready)==CB_OK && blocks_waited==2);drop_request(pending);
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
