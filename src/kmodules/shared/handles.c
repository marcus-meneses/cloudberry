#include "handles.h"
#include "manifest.h"
#include "io.h"
#include "../../interfaces/stream.h"
#include "../../interfaces/filesystem.h"
#include "filesystem.h"
#include "process.h"
#include "threads.h"
#include "../../kernel/cpu.h"
#include "../../interfaces/system.h"
#include "../../interfaces/memory.h"
#include "../../interfaces/applications.h"
#include "../../interfaces/gpu.h"
struct channel {struct channel *next;uint64_t pid,token,sequence,owner_watch;int ready;unsigned char *frame;size_t capacity,bytes;};
struct operation {struct operation *next;const char *name;};
struct grant {struct grant *next;const char *name,*scope;int publish;struct operation *operations;};
struct hp_call;
struct policy {
#ifdef CB_HANDLES_PROFILE
    struct hp_call *probe;
#endif
struct policy *next;unsigned revoked;uint64_t pid,receiver;cb_handle_t standard[3];char *json;struct grant *grants,*building;struct cb_provider_identity identity;uint64_t installation_owner;};
struct publication {struct publication *next;uint64_t pid,token,epoch,refs,completed,last_operation;int dead,kernel;char *name;unsigned char *description;size_t bytes;};
struct stream;
struct capability {struct capability *next;uint64_t id,pid,grant_ns;int publisher;struct stream *stream;unsigned direction;char *scope;struct publication *publication;struct operation *operations;};
struct request {
#ifdef CB_HANDLES_PROFILE
    uint64_t queued_at,delivered_at,responded_at;
#endif
struct request *next;uint64_t pid,cap,provider,waiter;struct publication *publication;struct cb_request_id id;unsigned char *input,*reply;size_t bytes,reply_bytes;int dispatched,done,completed,abandoned;cb_status_t failure;};
static struct {const struct kernel_surfaces *hub;uintptr_t heap;uint64_t serial;struct channel *channels;struct policy *policies;struct publication *publications;struct capability *caps;struct request *requests,*request_tail;
    kernel_surface_call alloc,free,cpu,process_info,copy,check,control,clock,memory_alloc,memory_free,io;} state;
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void copy_bytes(void *dst,const void *src,size_t bytes){unsigned char *d=dst;const unsigned char *s=src;for(size_t i=0;i<bytes;++i)d[i]=s[i];}
static int equal_bytes(const void *a,const void *b,size_t n){const unsigned char *x=a,*y=b;for(size_t i=0;i<n;++i)if(x[i]!=y[i])return 0;
    return 1;}
static size_t length(const char *s){size_t n=0;while(s[n])++n;return n;}
static int named(const char *s,const void *bytes,size_t n){return length(s)==n && equal_bytes(s,bytes,n);}
static int identifier(const char *s,int resource){size_t n=length(s);if(!n)return 0;if(resource){if(s[0]=='/' && s[1]=='@')s+=2;else if(s[0]=='@')++s;else return 0;if(!*s)return 0;}
    for(;*s;++s)if(!((*s>='a'&&*s<='z') || (*s>='A'&&*s<='Z') || (*s>='0'&&*s<='9') || *s=='_' || *s=='-' || *s=='.'))return 0;
    return 1;}
#ifdef CB_HANDLES_PROFILE
static struct policy *policy(uint64_t pid);
#include "../../tests/handles-profile.inc"
#else
#define HP_STAMP(name)
#define HP_ACTIVE_STAMP(name)
#define HP_BEGIN_CALL(owner,op,start)
#define HP_CAPTURE_DONE()
#define HP_FINISH(owner)
#define HP_WAIT_BEGIN(owner)
#define HP_WAIT_END(owner)
#define HP_HANDOFF(r,point)
#define HP_KERNEL_BEGIN(owner)
#define HP_KERNEL_END(owner)
#endif
static uint64_t lock(void){
#ifdef CB_HANDLES_PROFILE
    return hp_lock();
#else
    return CALL(state.cpu,CPU_ENTER,0,0).value;
#endif
}
static void unlock(uint64_t flags){
#ifdef CB_HANDLES_PROFILE
    hp_unlock(flags);
#else
    CALL(state.cpu,CPU_LEAVE,flags,0);
#endif
}
static void *allocate(size_t n){struct kernel_surface_result r=CALL(state.alloc,state.heap,1,n);
#ifdef CB_HANDLES_PROFILE
    hp_alloc(!r.status);
#endif
    return r.status?NULL:(void *)r.value;}
static void release(void *p){if(p){CALL(state.free,state.heap,p,0);
#ifdef CB_HANDLES_PROFILE
    hp_free();
#endif
}}
static uint64_t serial(void){return state.serial==UINT64_MAX?0:++state.serial;}
static struct policy *policy(uint64_t pid){for(struct policy *p=state.policies;p;p=p->next)if(p->pid==pid)return p;return NULL;}
static struct grant *grant(struct policy *p,const char *name){for(struct grant *g=p?p->grants:NULL;g;g=g->next)if(mf_equal(g->name,name))return g;return NULL;}
static struct capability *capability(uint64_t pid,uint64_t id){for(struct capability *c=state.caps;c;c=c->next)if(c->pid==pid && c->id==id)return c;return NULL;}
static int permitted(struct operation *ops,const void *name,size_t n){for(;ops;ops=ops->next)if(named(ops->name,name,n))return 1;return 0;}
static void free_operations(struct operation *op,int strings){while(op){struct operation *next=op->next;if(strings)release((void *)op->name);release(op);op=next;}}
static void free_policy(struct policy *p){while(p->grants){struct grant *g=p->grants;p->grants=g->next;free_operations(g->operations,0);release(g);}release(p->json);release(p);}
static int manifest_resource(void *context,const char *name,const char *scope,int publish){struct policy *p=context;if(!identifier(name,1) || grant(p,name) || !(mf_equal(scope,"self") || mf_equal(scope,"hid") || mf_equal(scope,"keyboard") || mf_equal(scope,"boot-input") || mf_equal(scope,"process-manager") || mf_equal(scope,"display") || mf_equal(scope,"filesystem") || mf_equal(scope,"graphics")))return 0;
    struct grant *g=allocate(sizeof(*g));if(!g)return 0;g->name=name;g->scope=scope;g->publish=publish;g->next=p->grants;p->grants=g;p->building=g;return 1;}
static int manifest_operation(void *context,const char *name){struct policy *p=context;if(!identifier(name,0) || permitted(p->building->operations,name,length(name)))return 0;
    struct operation *op=allocate(sizeof(*op));if(!op)return 0;op->name=name;op->next=p->building->operations;p->building->operations=op;return 1;}
/* These frame limits bound a transport message, not resource sizes/counts. */
static int transfer(uint64_t pid,uint64_t user,void *kernel,size_t bytes,int write){for(size_t off=0;off<bytes;){size_t n=bytes-off<4096?bytes-off:4096;if(state.copy(pid,user+off,(uintptr_t)kernel+off,n,write,0).status)return 0;off+=n;}return 1;}
static int range(uint64_t pid,uint64_t user,uint64_t bytes,int write){return !bytes || !state.check(pid,user,bytes,write,0,0).status;}
static uint64_t now(void){return CALL(state.clock,0,0,0).value;}
struct handle_waiter {struct handle_waiter *next;uint64_t pid,thread;const struct cb_wait_item *items;size_t count;};
static struct handle_waiter *handle_waiters;
static void wait_request_changed(struct request *r);
static void wait_cap_changed(struct capability *cap);
static void wait_stream_changed(struct stream *stream);
static void wake(uint64_t tid){if(tid)CALL(state.control,THREAD_WAKE,tid,0);}
static cb_status_t sleep_until(uint64_t deadline){if(deadline && now()>=deadline)return CB_TIMEOUT;struct kernel_surface_result r=CALL(state.control,deadline?THREAD_BLOCK_UNTIL:THREAD_BLOCK,deadline,0);return r.status?CB_RESOURCE_FAILURE:CB_OK;}
static struct cb_description description(struct publication *p){struct cb_description d;copy_bytes(&d,p->description,sizeof(d));return d;}
static int operation_at(struct publication *p,size_t *offset,struct cb_operation_description *op,const unsigned char **name){
    if(*offset>p->bytes || sizeof(*op)>p->bytes-*offset)return 0;
    copy_bytes(op,p->description+*offset,sizeof(*op));
    if(op->version.bytes<sizeof(*op) || op->version.major!=1 || !cb_frame_fits(op->version.bytes,op->name_bytes,op->request_schema_bytes,p->bytes-*offset) || op->reply_schema_bytes>p->bytes-*offset-op->version.bytes-op->name_bytes-op->request_schema_bytes)return 0;
    *name=p->description+*offset+op->version.bytes;*offset+=op->version.bytes+op->name_bytes+op->request_schema_bytes+op->reply_schema_bytes;return op->name_bytes!=0;
}
static int resource_operation(struct publication *p,uint32_t id,struct cb_operation_description *op,const unsigned char **name){struct cb_description d=description(p);size_t off=d.version.bytes;for(uint32_t i=0;i<d.operation_count;++i){if(!operation_at(p,&off,op,name))return 0;if(op->operation==id)return 1;}return 0;}
static int interface_equal(const struct cb_interface_id *a,const struct cb_interface_id *b){return equal_bytes(a,b,sizeof(*a));}
static void drop_publication(struct publication *p){if(p->refs)--p->refs;if(p->refs || !p->dead || p->kernel)return;struct publication **at=&state.publications;while(*at && *at!=p)at=&(*at)->next;if(*at)*at=p->next;release(p->name);release(p->description);release(p);}
static void drop_request(struct request *r){wait_request_changed(r);struct request **at=&state.requests;while(*at && *at!=r)at=&(*at)->next;if(*at)*at=r->next;if(state.request_tail==r){state.request_tail=NULL;for(struct request *tail=state.requests;tail;tail=tail->next)state.request_tail=tail;}if(r->publication)drop_publication(r->publication);release(r->reply);release(r);}
static void stream_drop(struct capability *);
static void drop_capability(struct capability *cap){wait_cap_changed(cap);struct policy *owner=policy(cap->pid);if(owner)for(unsigned i=0;i<3;++i)if(owner->standard[i]==cap->id)owner->standard[i]=0;stream_drop(cap);struct capability **at=&state.caps;while(*at && *at!=cap)at=&(*at)->next;if(*at)*at=cap->next;free_operations(cap->operations,1);release(cap->scope);drop_publication(cap->publication);release(cap);}
static struct capability *new_capability(struct policy *owner,struct publication *p,struct grant *g,int publisher){
    struct capability *cap=allocate(sizeof(*cap));if(!cap)return NULL;cap->id=serial();if(!cap->id){release(cap);return NULL;}struct kernel_surface_result granted=CALL(state.clock,0,0,0);if(granted.status){release(cap);return NULL;}cap->grant_ns=granted.value;cap->pid=owner->pid;cap->publisher=publisher;cap->publication=p;cap->scope=allocate(length(g->scope)+1);if(!cap->scope){release(cap);return NULL;}copy_bytes(cap->scope,g->scope,length(g->scope)+1);
    struct cb_description d=description(p);size_t off=d.version.bytes;
    for(uint32_t i=0;i<d.operation_count;++i){struct cb_operation_description op;const unsigned char *name;if(!operation_at(p,&off,&op,&name))goto fail;
        if(!permitted(g->operations,name,op.name_bytes))continue;
    struct operation *allowed=allocate(sizeof(*allowed));char *text=allocate(op.name_bytes+1);if(!allowed || !text){release(allowed);release(text);goto fail;}copy_bytes(text,name,op.name_bytes);allowed->name=text;allowed->next=cap->operations;cap->operations=allowed;}
    cap->next=state.caps;state.caps=cap;++p->refs;return cap;
fail:free_operations(cap->operations,1);release(cap->scope);release(cap);return NULL;
}
static int valid_description(struct publication *p,const struct cb_interface_id *interface,uint16_t major,uint16_t minor){
    if(p->bytes<sizeof(struct cb_description))return 0;
    struct cb_description d=description(p);
    if(d.facts_bytes<2 || d.version.bytes<sizeof(d) || d.version.major!=1 || !interface_equal(&d.interface,interface) || d.interface_major!=major || d.interface_minor!=minor || !cb_frame_fits(d.version.bytes,d.operations_bytes,d.facts_bytes,p->bytes) || d.version.bytes+d.operations_bytes+d.facts_bytes!=p->bytes)return 0;
    size_t off=d.version.bytes;
    for(uint32_t i=0;i<d.operation_count;++i){struct cb_operation_description op;const unsigned char *name;if(!operation_at(p,&off,&op,&name) || !op.operation || op.flags || off>d.version.bytes+d.operations_bytes)return 0;
        for(uint64_t k=0;k<op.name_bytes;++k)if(!((name[k]>='a'&&name[k]<='z') || name[k]=='_'))return 0;
        size_t prior=d.version.bytes;for(uint32_t j=0;j<i;++j){struct cb_operation_description old;const unsigned char *old_name;if(!operation_at(p,&prior,&old,&old_name) || old.operation==op.operation || (old.name_bytes==op.name_bytes && equal_bytes(old_name,name,op.name_bytes)))return 0;}}
    return off==d.version.bytes+d.operations_bytes;
}
static struct capability *stream_endpoint(struct policy *,struct publication *,struct stream *,unsigned);
static int standard_index(const void *name,size_t bytes){return named(CB_STDIN_NAME,name,bytes)?0:named(CB_STDOUT_NAME,name,bytes)?1:named(CB_STDERR_NAME,name,bytes)?2:-1;}
static cb_status_t acquire(struct policy *owner,const unsigned char *input,size_t bytes,uint64_t *value){
    if(bytes<sizeof(struct cb_acquire))return CB_BAD_REQUEST;
    struct cb_acquire a;copy_bytes(&a,input,sizeof(a));if(a.reserved || a.name_bytes!=bytes-sizeof(a) || !a.name_bytes)return CB_BAD_REQUEST;
    int standard=standard_index(input+sizeof(a),a.name_bytes);if(standard>=0){
        const struct cb_interface_id interface=CB_STREAM_INTERFACE_ID;if(!interface_equal(&a.interface,&interface) || a.major!=1 || a.minor)return CB_VERSION_MISMATCH;
        struct capability *bound=capability(owner->pid,owner->standard[standard]);if(!bound || !bound->stream)return CB_UNAVAILABLE;
        struct capability *copy=stream_endpoint(owner,bound->publication,bound->stream,bound->direction);if(!copy)return CB_RESOURCE_FAILURE;*value=copy->id;return CB_OK;
    }
    struct grant *g=NULL;for(struct grant *x=owner->grants;x;x=x->next)if(named(x->name,input+sizeof(a),a.name_bytes)){g=x;break;}if(!g)return CB_DENIED;
    if((mf_equal(g->name,CB_IO_NAME) && !(mf_equal(g->scope,"hid") || mf_equal(g->scope,"keyboard") || mf_equal(g->scope,"process-manager") || mf_equal(g->scope,"display") || mf_equal(g->scope,"filesystem") || mf_equal(g->scope,"graphics") || mf_equal(g->scope,"self"))) || (mf_equal(g->name,CB_MEMORY_NAME) && !mf_equal(g->scope,"self")) || ((mf_equal(g->name,"/@keyboard") || mf_equal(g->name,"/@mouse")) && !mf_equal(g->scope,"boot-input")))return CB_DENIED;
    struct publication *p;for(p=state.publications;p;p=p->next)if(!p->dead && named(p->name,input+sizeof(a),a.name_bytes))break;if(!p)return CB_UNAVAILABLE;
    struct cb_description d=description(p);if(!interface_equal(&a.interface,&d.interface) || a.major!=d.interface_major || a.minor>d.interface_minor)return CB_VERSION_MISMATCH;
    struct capability *cap=new_capability(owner,p,g,0);if(!cap)return CB_RESOURCE_FAILURE;*value=cap->id;return CB_OK;
}
static cb_status_t publish(struct policy *owner,const unsigned char *input,size_t bytes,uint64_t *value){
    if(bytes<sizeof(struct cb_publication))return CB_BAD_REQUEST;
    struct cb_publication h;copy_bytes(&h,input,sizeof(h));if(h.version.bytes<sizeof(h) || h.version.major!=1 || h.flags || !h.name_bytes || !cb_frame_fits(h.version.bytes,h.name_bytes,h.description_bytes,bytes) || h.version.bytes+h.name_bytes+h.description_bytes!=bytes)return CB_BAD_REQUEST;
    if(standard_index(input+h.version.bytes,h.name_bytes)>=0)return CB_DENIED;
    struct grant *g=NULL;for(struct grant *x=owner->grants;x;x=x->next)if(x->publish && named(x->name,input+h.version.bytes,h.name_bytes)){g=x;break;}if(!g)return CB_DENIED;
    for(struct publication *p=state.publications;p;p=p->next)if(!p->dead && named(p->name,input+h.version.bytes,h.name_bytes))return CB_NAME_IN_USE;
    struct publication *p=allocate(sizeof(*p));if(!p)return CB_RESOURCE_FAILURE;p->name=allocate(h.name_bytes+1);p->description=allocate(h.description_bytes);p->bytes=h.description_bytes;
    if(!p->name || !p->description){release(p->name);release(p->description);release(p);return CB_RESOURCE_FAILURE;}copy_bytes(p->name,input+h.version.bytes,h.name_bytes);copy_bytes(p->description,input+h.version.bytes+h.name_bytes,h.description_bytes);
    if(!valid_description(p,&h.interface,h.interface_major,h.interface_minor)){release(p->name);release(p->description);release(p);return CB_BAD_REQUEST;}
    p->pid=owner->pid;p->epoch=serial();if(!p->epoch){release(p->name);release(p->description);release(p);return CB_RESOURCE_FAILURE;}
    struct cb_description d=description(p);d.provider=owner->identity;d.provider_epoch=p->epoch;copy_bytes(p->description,&d,sizeof(d));
    struct capability *cap=new_capability(owner,p,g,1);if(!cap){release(p->name);release(p->description);release(p);return CB_RESOURCE_FAILURE;}p->token=cap->id;p->next=state.publications;state.publications=p;*value=cap->id;return CB_OK;
}
static const char error_location[]="handles.invoke";
static cb_status_t error_result(struct request *r,cb_status_t status,uint32_t outcome,const void *result,size_t result_bytes){
    struct cb_call_header call;copy_bytes(&call,r->input,sizeof(call));size_t payload=call.payload_bytes;
    size_t trace=sizeof(struct cb_trace_frame)+sizeof(error_location)-1;
    size_t bytes=sizeof(struct cb_reply_header)+result_bytes+sizeof(struct cb_error_header)+payload+trace;if(bytes>CB_FRAME_LIMIT)return CB_BAD_REQUEST;unsigned char *reply=allocate(bytes);if(!reply){r->failure=CB_RESOURCE_FAILURE;wake(r->waiter);wait_request_changed(r);return CB_RESOURCE_FAILURE;}
    struct cb_reply_header h={.version={sizeof(h),1,0},.request=r->id,.status=status,.outcome=outcome,.provider_epoch=r->publication?r->publication->epoch:0,.result_bytes=result_bytes,.error_bytes=sizeof(struct cb_error_header)+payload+trace};
    struct cb_error_header e={.version={sizeof(e),1,0},.request=r->id,.outcome=outcome,.original_payload_bytes=payload,.trace_bytes=trace};copy_bytes(reply,&h,sizeof(h));copy_bytes(reply+sizeof(h),result,result_bytes);copy_bytes(reply+sizeof(h)+result_bytes,&e,sizeof(e));copy_bytes(reply+sizeof(h)+result_bytes+sizeof(e),r->input+call.version.bytes,payload);
    struct cb_trace_frame frame={.version={sizeof(frame),1,0},.interface=call.interface,.request=r->id,.attempt=call.attempt,.operation=call.operation,.location_bytes=sizeof(error_location)-1,.provider_epoch=r->publication?r->publication->epoch:0};if(r->publication)frame.provider=description(r->publication).provider;
    copy_bytes(reply+sizeof(h)+result_bytes+sizeof(e)+payload,&frame,sizeof(frame));copy_bytes(reply+sizeof(h)+result_bytes+sizeof(e)+payload+sizeof(frame),error_location,sizeof(error_location)-1);release(r->reply);r->reply=reply;r->reply_bytes=bytes;r->done=1;r->failure=0;wake(r->waiter);wait_request_changed(r);return status;
}
static cb_status_t error_reply(struct request *r,cb_status_t status,uint32_t outcome){return error_result(r,status,outcome,NULL,0);}
static cb_status_t reflect(struct capability *cap,unsigned char **out,size_t *bytes){
    struct publication *p=cap->publication;struct cb_description d=description(p);unsigned char *reply=allocate(sizeof(struct cb_reply_header)+p->bytes+64);if(!reply)return CB_RESOURCE_FAILURE;
    size_t offset=d.version.bytes,end=sizeof(struct cb_reply_header)+d.version.bytes;uint32_t count=0;
    for(uint32_t i=0;i<d.operation_count;++i){size_t start=offset;struct cb_operation_description op;const unsigned char *name;if(!operation_at(p,&offset,&op,&name)){release(reply);return CB_BAD_REQUEST;}if(permitted(cap->operations,name,op.name_bytes)){copy_bytes(reply+end,p->description+start,offset-start);end+=offset-start;++count;}}
    /* Kernel-owned facts are scoped; provider facts are withheld until a scoped
     * fact schema is implemented, rather than exposing ungranted data. */
    char facts[64];static const char prefix[]="{\"scope\":\"";size_t facts_bytes=sizeof(prefix)-1;copy_bytes(facts,prefix,facts_bytes);copy_bytes(facts+facts_bytes,cap->scope,length(cap->scope));facts_bytes+=length(cap->scope);copy_bytes(facts+facts_bytes,"\"}",2);facts_bytes+=2;
    d.operation_count=count;d.operations_bytes=end-sizeof(struct cb_reply_header)-d.version.bytes;d.facts_bytes=facts_bytes;copy_bytes(reply+sizeof(struct cb_reply_header),&d,sizeof(d));copy_bytes(reply+end,facts,facts_bytes);end+=facts_bytes;
    struct cb_reply_header h={.version={sizeof(h),1,0},.status=CB_OK,.outcome=CB_COMMITTED,.provider_epoch=p->epoch,.result_bytes=end-sizeof(h)};copy_bytes(reply,&h,sizeof(h));*out=reply;*bytes=end;return CB_OK;
}
#include "handles-stream.inc"
/* Reserve result storage before dispatch. GPU writes/submits return no payload;
 * fixed records and counted reads need only their actual maximum result size.
 * The gateway remains responsible for validation and provider authority. */
static size_t gpu_reply_capacity(const void *payload,size_t bytes){
    const size_t limit=CB_FRAME_LIMIT-1024;
    if(bytes<sizeof(struct cb_io_gpu_request))return 0;
    struct cb_io_gpu_request q;copy_bytes(&q,payload,sizeof(q));
    switch(q.operation){
    case CB_GPU_INFO:case CB_GPU_OPEN:case CB_GPU_NEXT:case CB_GPU_SELECTED:return sizeof(struct cb_io_gpu_info);
    case CB_GPU_MODES:return sizeof(struct cb_io_gpu_modes);
    case CB_GPU_CREATE:case CB_GPU_TEXTURE:return sizeof(struct cb_io_gpu_resource);
    case CB_GPU_CONTEXT:return q.context?0:sizeof(uint32_t);
    case CB_GPU_READ:return q.bytes>limit?limit:(size_t)q.bytes;
    case CB_GPU_CAPSET:return q.bytes>limit-sizeof(struct cb_io_gpu_capset)?limit:sizeof(struct cb_io_gpu_capset)+(size_t)q.bytes;
    default:return 0;
    }
}
static size_t kernel_reply_capacity(struct publication *p,unsigned op,const void *payload,size_t bytes){
    if(p->kernel==2 && op==CB_IO_GPU)return gpu_reply_capacity(payload,bytes);
    return p->kernel==2 && (op==CB_IO_CREDENTIALS || op==CB_IO_PROCESS_DATA || op==CB_IO_REGISTRY)?CB_FRAME_LIMIT-1024:p->kernel==2 && op==CB_IO_RANDOM?64:p->kernel==2 && op==CB_IO_PROCESS_SAMPLE?sizeof(struct cb_process_sample):p->kernel==3 && op==CB_STREAM_READ?CB_STREAM_CHUNK:p->kernel==2 && (op==CB_IO_HID_DESCRIPTOR || op==CB_IO_HID_GET_FEATURE)?CB_HID_LIMIT:p->kernel==2 && op==CB_IO_FILESYSTEM?512:64;
}
static cb_status_t kernel_call(uint64_t pid,struct publication *p,const struct cb_call_header *call,const unsigned char *payload,unsigned char **out,size_t *bytes){
    size_t capacity=kernel_reply_capacity(p,call->operation,payload,call->payload_bytes);
    unsigned char *reply=allocate(sizeof(struct cb_reply_header)+capacity);if(!reply)return CB_RESOURCE_FAILURE;
    struct cb_reply_header h={.version={sizeof(h),1,0},.request=call->request,.status=CB_OK,.outcome=CB_COMMITTED,.provider_epoch=p->epoch};
    if(p->kernel==3){size_t actual=0;cb_status_t status=stream_call(pid,call,payload,reply+sizeof(h),&actual);if(status){release(reply);return status;}h.result_bytes=actual;
    }else if(p->kernel==2){struct io_invocation io={.process=pid,.operation=call->operation,.input=payload,.bytes=call->payload_bytes,.output=reply+sizeof(h),.capacity=capacity};
        struct kernel_surface_result result=CALL(state.io,&io,0,0);if(result.status || io.status || io.actual>capacity){release(reply);return result.status || io.actual>capacity?CB_RESOURCE_FAILURE:io.status;}h.result_bytes=io.actual;
    }else{
        struct cb_memory_request in;if(call->payload_bytes!=sizeof(in)){release(reply);return CB_BAD_REQUEST;}copy_bytes(&in,payload,sizeof(in));struct kernel_surface_result result;
        if(call->operation==CB_MEMORY_ALLOCATE && !in.address)result=CALL(state.memory_alloc,pid,in.bytes,0);
        else if(call->operation==CB_MEMORY_FREE && !in.bytes)result=CALL(state.memory_free,pid,in.address,0);
        else {release(reply);return CB_BAD_REQUEST;}
        if(result.status){release(reply);return CB_RESOURCE_FAILURE;}
        struct cb_memory_result memory={call->operation==CB_MEMORY_FREE?0:result.value,in.bytes};copy_bytes(reply+sizeof(h),&memory,sizeof(memory));h.result_bytes=sizeof(memory);
    }
    copy_bytes(reply,&h,sizeof(h));*out=reply;*bytes=sizeof(h)+h.result_bytes;return CB_OK;
}
static void retire_publication(struct publication *p,int failed){
    ++p->refs;p->dead=failed?1:2;
    for(struct request *r=state.requests,*next;r;r=next){next=r->next;if(r->publication==p && !r->done && (failed || !r->dispatched)){
        if(r->abandoned)drop_request(r);else {(void)error_reply(r,failed?CB_PROVIDER_FAILED:CB_REVOKED,r->dispatched?CB_OUTCOME_UNKNOWN:CB_NOT_DISPATCHED);r->completed=1;}
    }}drop_publication(p);
}
static int kernel_publication(const char *resource_name,const struct cb_interface_id *interface,const char *const *names,unsigned count,int kind){
    struct publication *p=allocate(sizeof(*p));if(!p)return 0;p->name=allocate(length(resource_name)+1);if(!p->name){release(p);return 0;}copy_bytes(p->name,resource_name,length(resource_name)+1);p->kernel=kind;p->epoch=serial();
    size_t bytes=sizeof(struct cb_description)+2;for(unsigned i=0;i<count;++i)bytes+=sizeof(struct cb_operation_description)+length(names[i])+4;
    p->description=allocate(bytes);if(!p->description){release(p->name);release(p);return 0;}p->bytes=bytes;
    struct cb_description desc={.version={sizeof(desc),1,0},.interface=*interface,.provider_epoch=p->epoch,.interface_major=1,.operation_count=count,.operations_bytes=bytes-sizeof(desc)-2,.facts_bytes=2};copy_bytes(p->description,&desc,sizeof(desc));size_t offset=sizeof(desc);
    for(unsigned i=0;i<count;++i){struct cb_operation_description op={.version={sizeof(op),1,0},.operation=i+1,.name_bytes=length(names[i]),.request_schema_bytes=2,.reply_schema_bytes=2};copy_bytes(p->description+offset,&op,sizeof(op));offset+=sizeof(op);copy_bytes(p->description+offset,names[i],op.name_bytes);offset+=op.name_bytes;copy_bytes(p->description+offset,"{}{}",4);offset+=4;}copy_bytes(p->description+offset,"{}",2);p->next=state.publications;state.publications=p;return 1;
}
struct kernel_surface_result handles_surface_init(MODULE_ARGS){
    MODULE_UNUSED();if(state.hub)return module_result(1,state.hub==(const void *)a);const struct kernel_surfaces *hub=(const void *)a;if(!hub || !b)return module_result(0,0);
#define BIND(field,name) do{state.field=kernel_surface_find(hub,name);if(!state.field)return module_result(0,0);}while(0)
    BIND(alloc,"mem.calloc");BIND(free,"mem.free");BIND(cpu,"cpu.control");BIND(process_info,"process.info");BIND(copy,"process.copy");BIND(check,"process.check");BIND(control,"threads.control");BIND(clock,"clock.now_ns");BIND(memory_alloc,"process.memory_alloc");BIND(memory_free,"process.memory_free");
#undef BIND
    state.heap=b;state.hub=hub;
    state.io=kernel_surface_find(hub,"io.invoke");if(!state.io)return module_result(0,0);
    const char *memory_names[]={"allocate","free"};
    /* Build relocated pointers at runtime. A large local pointer initializer
       becomes an absolute .rodata pointer table in the flat PIC module. */
    static const char io_labels[][24]={"keyboard_subscribe","keyboard_protocol","keyboard_ack","keyboard_close","now_ns","process_launch","process_stop","process_reap","process_release","display_info","display_fill","display_blit","filesystem","owner_watch","display_copy","hid_subscribe","hid_list","hid_open","hid_descriptor","hid_start","hid_ack","hid_close","hid_get_feature","hid_set_feature","process_sample","credentials","identity_set","random","session","input_ancestor","input_focus_set","input_focus","identity_home","process_data","console_access","registry","process_terminate","utc","gpu"};
    const char *io_names[sizeof(io_labels)/sizeof(io_labels[0])];for(size_t i=0;i<sizeof(io_names)/sizeof(io_names[0]);++i)io_names[i]=io_labels[i];
    const struct cb_interface_id memory=CB_MEMORY_INTERFACE_ID,io=CB_IO_INTERFACE_ID,stream=CB_STREAM_INTERFACE_ID;const char *stream_names[]={"pipe","read","write"};
    return module_result(1,kernel_publication(CB_MEMORY_NAME,&memory,memory_names,2,1) && kernel_publication(CB_IO_NAME,&io,io_names,sizeof(io_names)/sizeof(io_names[0]),2) && kernel_publication(CB_STREAM_NAME,&stream,stream_names,3,3));
}
struct kernel_surface_result handles_surface_admit(MODULE_ARGS){
    MODULE_UNUSED();const struct handle_policy *r=(const void *)a;if(!state.hub || !r || !r->manifest || !r->bytes || r->bytes==SIZE_MAX)return module_result(0,0);
    struct process_info info;if(CALL(state.process_info,r->process,&info,0).status || info.state!=PROCESS_READY || info.thread)return module_result(0,0);
    struct policy *owner=allocate(sizeof(*owner));if(!owner)return module_result(0,0);owner->pid=r->process;owner->identity=r->identity;owner->installation_owner=r->installation_owner;owner->json=allocate(r->bytes+1);if(!owner->json){release(owner);return module_result(0,0);}copy_bytes(owner->json,r->manifest,r->bytes);
    struct manifest_parser parser={.at=owner->json,.end=owner->json+r->bytes,.context=owner,.resource=manifest_resource,.operation=manifest_operation};if(!manifest_parse(&parser)){free_policy(owner);return module_result(0,0);}
    struct channel *control=allocate(sizeof(*control));if(!control){free_policy(owner);return module_result(0,0);}control->pid=r->process;control->capacity=sizeof(struct cb_delivery_header);control->frame=allocate(control->capacity);if(!control->frame){release(control);free_policy(owner);return module_result(0,0);}
    uint64_t flags=lock();if(policy(r->process) || CALL(state.process_info,r->process,&info,0).status || info.state!=PROCESS_READY || info.thread){unlock(flags);release(control->frame);release(control);free_policy(owner);return module_result(0,0);}control->next=state.channels;state.channels=control;owner->next=state.policies;state.policies=owner;unlock(flags);return module_result(1,1);
}
static cb_status_t reject_call(uint64_t pid,unsigned char *input,size_t bytes,cb_status_t status,unsigned char **out,size_t *out_bytes){
    /* Rejected calls still own their correlation/result until SDK release. A
     * temporary unregistered ID made cleanup mask the original error status. */
    if(bytes>SIZE_MAX-sizeof(struct request))return CB_RESOURCE_FAILURE;
    struct request *r=allocate(sizeof(*r)+bytes);if(!r)return CB_RESOURCE_FAILURE;r->pid=pid;r->input=(void *)(r+1);r->bytes=bytes;r->id=(struct cb_request_id){pid,serial()};if(!r->id.serial){release(r);return CB_RESOURCE_FAILURE;}copy_bytes(r->input,input,bytes);
    if(state.request_tail)state.request_tail->next=r;else state.requests=r;state.request_tail=r;
    (void)error_reply(r,status,CB_NOT_DISPATCHED);r->completed=1;r->done=1;if(r->failure){cb_status_t failed=r->failure;drop_request(r);return failed;}
    *out=allocate(r->reply_bytes);if(!*out){drop_request(r);return CB_RESOURCE_FAILURE;}copy_bytes(*out,r->reply,r->reply_bytes);*out_bytes=r->reply_bytes;return status;
}
static cb_status_t invoke_mode(struct policy *owner,unsigned char *input,size_t bytes,unsigned char **out,size_t *out_bytes,uint64_t caller_deadline,struct cb_request_id *submitted){
    if(bytes<sizeof(struct cb_call_header))return CB_BAD_REQUEST;
    struct cb_call_header call;copy_bytes(&call,input,sizeof(call));
    if(call.version.bytes!=sizeof(call) || call.version.major!=1 || call.version.minor || call.flags || call.payload_bytes!=bytes-sizeof(call))return CB_BAD_REQUEST;
    uint64_t flags=lock();if(owner->revoked && !call.request.serial){if(submitted){unlock(flags);return CB_REVOKED;}cb_status_t rejected=reject_call(owner->pid,input,bytes,CB_REVOKED,out,out_bytes);unlock(flags);return rejected;}struct capability *cap=capability(owner->pid,call.handle);if(!cap){if(submitted){unlock(flags);return CB_BAD_HANDLE;}cb_status_t rejected=reject_call(owner->pid,input,bytes,CB_BAD_HANDLE,out,out_bytes);unlock(flags);return rejected;}
    struct publication *p=cap->publication;struct cb_description d=description(p);struct cb_interface_id base=CB_HANDLE_INTERFACE_ID;int reflection=interface_equal(&call.interface,&base) && call.operation==CB_HANDLE_DESCRIBE;
    if(!reflection && !interface_equal(&call.interface,&d.interface)){unlock(flags);return CB_VERSION_MISMATCH;}
    if(!reflection){struct cb_operation_description op;const unsigned char *name;if(!resource_operation(p,call.operation,&op,&name)){unlock(flags);return CB_UNAVAILABLE;}if(!permitted(cap->operations,name,op.name_bytes)){if(submitted){unlock(flags);return CB_DENIED;}cb_status_t rejected=reject_call(owner->pid,input,bytes,CB_DENIED,out,out_bytes);unlock(flags);return rejected;}}
    /* Deferred submission is only for provider calls. Kernel operations remain
       synchronous; no implicit workers, timeout mutations or reflection queues. */
    if(submitted && (p->kernel || reflection || call.deadline_ns || call.request.serial || call.request.namespace_id || call.attempt)){unlock(flags);return CB_BAD_REQUEST;}
    unsigned char *reserved_output=NULL;struct request *r=NULL;
    if(call.request.namespace_id || call.request.serial){
        for(r=state.requests;r;r=r->next)if(!r->abandoned && r->pid==owner->pid && r->id.namespace_id==call.request.namespace_id && r->id.serial==call.request.serial && r->cap==cap->id)break;
        if(!r || r->bytes!=bytes){unlock(flags);return CB_BAD_REQUEST;}struct cb_call_header original;copy_bytes(&original,r->input,sizeof(original));
        if(original.operation!=call.operation || !interface_equal(&original.interface,&call.interface) || original.attempt==UINT64_MAX || call.attempt!=original.attempt+1 || !equal_bytes(r->input+sizeof(call),input+sizeof(call),call.payload_bytes)){unlock(flags);return CB_BAD_REQUEST;}
        original.attempt=call.attempt;original.deadline_ns=call.deadline_ns;copy_bytes(r->input,&original,sizeof(original));
    }else{
        if(call.attempt){unlock(flags);return CB_BAD_REQUEST;}
        /* The captured frame has exactly the request's lifetime. One allocation
           owns both; no pointer into temporary gate scratch is retained. */
        if(bytes>SIZE_MAX-sizeof(*r)){unlock(flags);return CB_RESOURCE_FAILURE;}
        r=allocate(sizeof(*r)+bytes);if(!r){unlock(flags);return CB_RESOURCE_FAILURE;}r->input=(unsigned char *)(r+1);
        r->id=(struct cb_request_id){owner->pid,serial()};if(!r->id.serial){release(r);unlock(flags);return CB_RESOURCE_FAILURE;}
        call.request=r->id;copy_bytes(r->input,input,bytes);copy_bytes(r->input,&call,sizeof(call));r->pid=owner->pid;r->cap=cap->id;r->publication=p;r->bytes=bytes;++p->refs;if(state.request_tail)state.request_tail->next=r;else state.requests=r;state.request_tail=r;
        if(reflection){
            if(call.payload_bytes!=sizeof(struct cb_describe_request)){(void)error_reply(r,CB_BAD_REQUEST,CB_NOT_DISPATCHED);}
            else {struct cb_describe_request ask;copy_bytes(&ask,input+sizeof(call),sizeof(ask));if(ask.version.major!=1 || ask.version.bytes!=sizeof(ask) || !interface_equal(&ask.interface,&d.interface))(void)error_reply(r,CB_VERSION_MISMATCH,CB_NOT_DISPATCHED);
                else {cb_status_t status=reflect(cap,&r->reply,&r->reply_bytes);if(status)(void)error_reply(r,status,CB_NOT_DISPATCHED);else{struct cb_reply_header h;copy_bytes(&h,r->reply,sizeof(h));h.request=r->id;copy_bytes(r->reply,&h,sizeof(h));r->done=1;}}}
            r->completed=1;
        }else if(p->dead){(void)error_reply(r,p->dead==2?CB_REVOKED:CB_PROVIDER_FAILED,CB_NOT_DISPATCHED);r->completed=1;}
        else if(p->kernel){
            /* Reserve the operation-sized caller result before a resource
               mutation can take effect, including header-only GPU replies. */
            reserved_output=allocate(sizeof(struct cb_reply_header)+kernel_reply_capacity(p,call.operation,input+sizeof(call),call.payload_bytes));
            if(!reserved_output){(void)error_reply(r,CB_RESOURCE_FAILURE,CB_NOT_DISPATCHED);r->completed=1;}
            else {
                r->dispatched=1;unlock(flags);HP_KERNEL_BEGIN(owner);unsigned char *reply=NULL;size_t reply_bytes=0;cb_status_t status=kernel_call(owner->pid,p,&call,input+sizeof(call),&reply,&reply_bytes);HP_KERNEL_END(owner);flags=lock();
                if(status){release(reserved_output);reserved_output=NULL;(void)error_reply(r,status,status==CB_RESOURCE_FAILURE && ((p->kernel==3 && call.operation==CB_STREAM_WRITE) || (p->kernel==2 && (call.operation==CB_IO_FILESYSTEM || call.operation==CB_IO_GPU)))?CB_OUTCOME_UNKNOWN:CB_NOT_COMMITTED);}else {r->reply=reply;r->reply_bytes=reply_bytes;r->done=1;}r->completed=1;++p->completed;p->last_operation=call.operation;
            }
        }else{HP_HANDOFF(r,0);struct policy *provider=policy(p->pid);if(provider)wake(provider->receiver);}
    }
    if(submitted){*submitted=r->id;unlock(flags);return CB_OK;}
    uint64_t deadline=call.deadline_ns;if(caller_deadline && (!deadline || caller_deadline<deadline))deadline=caller_deadline;
    r->waiter=CALL(state.control,THREAD_CURRENT,0,0).value;
    while(!r->done && !r->failure){unlock(flags);HP_WAIT_BEGIN(owner);cb_status_t waiting=sleep_until(deadline);HP_WAIT_END(owner);flags=lock();if(waiting && !r->done){cb_status_t made=error_reply(r,waiting,r->dispatched?CB_OUTCOME_UNKNOWN:CB_NOT_DISPATCHED);if(!r->dispatched)r->completed=1;if(!r->done){unlock(flags);return made;}}}
    HP_HANDOFF(r,3);r->waiter=0;if(r->failure){unlock(flags);return r->failure;}*out=reserved_output?reserved_output:allocate(r->reply_bytes);if(!*out){unlock(flags);return CB_RESOURCE_FAILURE;}copy_bytes(*out,r->reply,r->reply_bytes);*out_bytes=r->reply_bytes;struct cb_reply_header reply;copy_bytes(&reply,r->reply,sizeof(reply));unlock(flags);return reply.status;
}
static cb_status_t invoke(struct policy *owner,unsigned char *input,size_t bytes,unsigned char **out,size_t *out_bytes,uint64_t deadline){return invoke_mode(owner,input,bytes,out,out_bytes,deadline,NULL);}
#include "handles-wait.inc"
static cb_status_t receive(struct policy *owner,uint64_t deadline,unsigned char **out,size_t *bytes,size_t capacity){
    uint64_t flags=lock();
    for(;;){struct request *r=state.requests;while(r && (r->done || r->dispatched || r->publication->dead || r->publication->pid!=owner->pid))r=r->next;
        struct channel *event=NULL;for(struct channel *ch=state.channels;ch;ch=ch->next)if(ch->pid==owner->pid && ch->ready && (!event || ch->sequence<event->sequence))event=ch;
        if(event && (!r || event->sequence<r->id.serial)){
            *bytes=event->bytes;if(capacity<*bytes){unlock(flags);return CB_BUFFER_TOO_SMALL;}
            *out=allocate(*bytes);if(!*out){unlock(flags);return CB_RESOURCE_FAILURE;}copy_bytes(*out,event->frame,*bytes);event->ready=0;owner->receiver=0;unlock(flags);return CB_OK;
        }
        if(owner->revoked){owner->receiver=0;unlock(flags);return CB_REVOKED;}
        if(r){
            struct cb_call_header original;copy_bytes(&original,r->input,sizeof(original));
            /* Admission context is kernel-owned, never taken from the caller. */
            char context[192];static const char prefix[]="{\"input_stream\":\"boot:1\",\"grant_ns\":";size_t context_bytes=sizeof(prefix)-1;copy_bytes(context,prefix,context_bytes);
            struct capability *caller_cap=capability(r->pid,r->cap);if(!caller_cap){unlock(flags);return CB_BAD_HANDLE;}char digits[24];unsigned count=0;uint64_t grant_time=caller_cap->grant_ns;do{digits[count++]='0'+grant_time%10;grant_time/=10;}while(grant_time);while(count)context[context_bytes++]=digits[--count];
            static const char process_prefix[]=",\"process\":";copy_bytes(context+context_bytes,process_prefix,sizeof(process_prefix)-1);context_bytes+=sizeof(process_prefix)-1;
            uint64_t caller_pid=r->pid;do{digits[count++]='0'+caller_pid%10;caller_pid/=10;}while(caller_pid);while(count)context[context_bytes++]=digits[--count];
            static const char identity_prefix[]=",\"installation\":\"";copy_bytes(context+context_bytes,identity_prefix,sizeof(identity_prefix)-1);context_bytes+=sizeof(identity_prefix)-1;
            struct policy *caller_policy=policy(r->pid);if(!caller_policy){unlock(flags);return CB_DENIED;}static const char hex[]="0123456789abcdef";
            for(unsigned i=0;i<16;++i){unsigned n=caller_policy->identity.installation_id[i];context[context_bytes++]=hex[n>>4];context[context_bytes++]=hex[n&15];}context[context_bytes++]='"';context[context_bytes++]='}';
            *bytes=sizeof(struct cb_delivery_header)+context_bytes+original.payload_bytes;
            if(capacity<*bytes){unlock(flags);return CB_BUFFER_TOO_SMALL;}
            unsigned char *reply=allocate(*bytes);if(!reply){unlock(flags);return CB_RESOURCE_FAILURE;}
            struct cb_delivery_header delivery={.call=original,.provider_epoch=r->publication->epoch,.context_bytes=context_bytes};delivery.call.version.bytes=sizeof(delivery);delivery.call.handle=r->publication->token;delivery.call.flags=CB_CALL_PROVIDER_DELIVERY;
            copy_bytes(reply,&delivery,sizeof(delivery));copy_bytes(reply+sizeof(delivery),context,context_bytes);copy_bytes(reply+sizeof(delivery)+context_bytes,r->input+original.version.bytes,original.payload_bytes);
            HP_HANDOFF(r,1);r->dispatched=1;r->provider=owner->pid;owner->receiver=0;*out=reply;unlock(flags);return CB_OK;
        }
        owner->receiver=CALL(state.control,THREAD_CURRENT,0,0).value;unlock(flags);HP_WAIT_BEGIN(owner);cb_status_t waiting=sleep_until(deadline);HP_WAIT_END(owner);flags=lock();if(waiting){owner->receiver=0;unlock(flags);return waiting;}
    }
}
static cb_status_t respond(struct policy *owner,const unsigned char *input,size_t bytes){
    if(bytes<sizeof(struct cb_reply_header))return CB_BAD_REQUEST;
    struct cb_reply_header h;copy_bytes(&h,input,sizeof(h));
    if(h.version.bytes!=sizeof(h) || h.version.major!=1 || h.version.minor || h.outcome>CB_OUTCOME_UNKNOWN || h.status>=CB_STATUS_COUNT || !cb_frame_fits(h.version.bytes,h.result_bytes,h.error_bytes,bytes) || h.version.bytes+h.result_bytes+h.error_bytes!=bytes || (!h.status && h.error_bytes))return CB_BAD_REQUEST;
    uint64_t flags=lock();struct request *r;for(r=state.requests;r;r=r->next)if(r->provider==owner->pid && r->id.namespace_id==h.request.namespace_id && r->id.serial==h.request.serial)break;
    if(!r || !r->dispatched || r->completed || r->publication->epoch!=h.provider_epoch){unlock(flags);return CB_BAD_REQUEST;}
    if(r->abandoned){
        struct cb_call_header original;copy_bytes(&original,r->input,sizeof(original));++r->publication->completed;r->publication->last_operation=original.operation;drop_request(r);unlock(flags);return CB_OK;
    }
    if(h.status){
        struct cb_call_header original;copy_bytes(&original,r->input,sizeof(original));
        size_t overhead=sizeof(struct cb_reply_header)+sizeof(struct cb_error_header)+sizeof(struct cb_trace_frame)+sizeof(error_location)-1;
        if(!cb_frame_fits(overhead,h.result_bytes,original.payload_bytes,CB_FRAME_LIMIT)){unlock(flags);return CB_BAD_REQUEST;}
        cb_status_t made=error_result(r,h.status,h.outcome,input+h.version.bytes,h.result_bytes);
        /* A valid provider BAD_REQUEST is a completed result, not a framing
           failure. Sizing is checked separately before replacing any reply. */
        if(made!=h.status || r->failure || !r->done){unlock(flags);return made;}
    }
    else {unsigned char *reply=allocate(bytes);if(!reply){unlock(flags);return CB_RESOURCE_FAILURE;}copy_bytes(reply,input,bytes);release(r->reply);r->reply=reply;r->reply_bytes=bytes;r->done=1;r->failure=0;}
    HP_HANDOFF(r,2);r->completed=1;++r->publication->completed;struct cb_call_header finished;copy_bytes(&finished,r->input,sizeof(finished));r->publication->last_operation=finished.operation;wake(r->waiter);wait_request_changed(r);unlock(flags);return CB_OK;
}
struct kernel_surface_result handles_surface_gate(MODULE_ARGS){
    MODULE_UNUSED();HP_STAMP(hp_entry);uint64_t pid=a;struct cb_system_gate *g=(void *)b;if(!g || g->bytes!=sizeof(*g) || g->reserved || g->input_bytes>CB_FRAME_LIMIT-512 || g->output_capacity>CB_FRAME_LIMIT || !range(pid,g->input,g->input_bytes,0) || !range(pid,g->output,g->output_capacity,1))return module_result(CB_BAD_REQUEST,1);
    uint64_t owner_flags=lock();struct policy *owner=policy(pid);unlock(owner_flags);if(!owner)return module_result(CB_DENIED,1);HP_BEGIN_CALL(owner,g->operation,hp_entry);
    /* Per-call scratch survives blocking on this thread's kernel stack. Retained
       requests/replies always own separate copies; larger frames use the heap. */
    unsigned char scratch[256];
    unsigned char *owned_input=g->input_bytes>sizeof(scratch)?allocate(g->input_bytes):NULL;
    unsigned char *input=g->input_bytes?(g->input_bytes<=sizeof(scratch)?scratch:owned_input):NULL,*output=NULL;size_t bytes=0;
    if(g->input_bytes && (!input || !transfer(pid,g->input,input,g->input_bytes,0))){release(owned_input);HP_CAPTURE_DONE();HP_ACTIVE_STAMP(hp_dispatch);HP_ACTIVE_STAMP(hp_output);HP_FINISH(owner);return module_result(CB_RESOURCE_FAILURE,1);}
    HP_CAPTURE_DONE();HP_ACTIVE_STAMP(hp_dispatch);cb_status_t status=CB_BAD_REQUEST;uint64_t flags;
    if(owner->revoked && (g->operation==CB_SYSTEM_ACQUIRE || g->operation==CB_SYSTEM_PUBLISH || g->operation==CB_SYSTEM_STANDARD)){status=CB_REVOKED;}
    else if(g->operation==CB_SYSTEM_STANDARD && !g->input_bytes && g->value<3){g->value=owner->standard[g->value];status=CB_OK;}
    else if(g->operation==CB_SYSTEM_ACQUIRE){flags=lock();status=acquire(owner,input,g->input_bytes,&g->value);unlock(flags);}
    else if(g->operation==CB_SYSTEM_PUBLISH){flags=lock();status=publish(owner,input,g->input_bytes,&g->value);unlock(flags);}
    else if(g->operation==CB_SYSTEM_INVOKE){if(g->output_capacity<sizeof(struct cb_reply_header)){status=CB_BUFFER_TOO_SMALL;bytes=sizeof(struct cb_reply_header);}else status=invoke(owner,input,g->input_bytes,&output,&bytes,g->deadline_ns);}
    else if(g->operation==CB_SYSTEM_SUBMIT){
        if(g->output_capacity<sizeof(struct cb_request_id)){status=CB_BUFFER_TOO_SMALL;bytes=sizeof(struct cb_request_id);}
        else {output=allocate(sizeof(struct cb_request_id));if(!output)status=CB_RESOURCE_FAILURE;else {struct cb_request_id id={0};unsigned char *rejected=NULL;size_t rejected_bytes=0;status=invoke_mode(owner,input,g->input_bytes,&rejected,&rejected_bytes,0,&id);release(rejected);if(!status){copy_bytes(output,&id,sizeof(id));bytes=sizeof(id);}}}
    }
    else if(g->operation==CB_SYSTEM_COLLECT && g->input_bytes==sizeof(struct cb_request_id)){struct cb_request_id id;copy_bytes(&id,input,sizeof(id));status=collect(owner,id,&output,&bytes,g->output_capacity);}
    else if(g->operation==CB_SYSTEM_ABANDON && g->input_bytes==sizeof(struct cb_request_id)){
        bytes=sizeof(struct cb_abandon_result);if(g->output_capacity<bytes)status=CB_BUFFER_TOO_SMALL;
        else {output=allocate(bytes);if(!output){bytes=0;status=CB_RESOURCE_FAILURE;}else {struct cb_request_id id;copy_bytes(&id,input,sizeof(id));status=abandon(owner,id,(void *)output);if(status)bytes=0;}}
    }
    else if(g->operation==CB_SYSTEM_WAIT)status=wait_any(owner,input,g->input_bytes,g->deadline_ns,&g->value);
    else if(g->operation==CB_SYSTEM_RECEIVE){if(!g->input_bytes)status=receive(owner,g->deadline_ns,&output,&bytes,g->output_capacity);}
    else if(g->operation==CB_SYSTEM_REPLY)status=respond(owner,input,g->input_bytes);
    else if(g->operation==CB_SYSTEM_RELEASE && g->input_bytes==8){uint64_t id;copy_bytes(&id,input,8);flags=lock();struct capability *cap=capability(pid,id);status=cap?CB_OK:CB_BAD_HANDLE;if(cap){if(cap->publisher)retire_publication(cap->publication,0);struct request *r=state.requests;while(r){struct request *next=r->next;if(r->pid==pid && r->cap==id)drop_request(r);r=next;}drop_capability(cap);}unlock(flags);}
    else if(g->operation==CB_SYSTEM_FORGET && g->input_bytes==sizeof(struct cb_request_id)){struct cb_request_id id;copy_bytes(&id,input,sizeof(id));flags=lock();struct request *r=state.requests;while(r && (r->pid!=pid || r->id.namespace_id!=id.namespace_id || r->id.serial!=id.serial))r=r->next;status=r?(r->done && (!r->dispatched || r->completed)?CB_OK:CB_BUSY):CB_BAD_REQUEST;if(r && status==CB_OK)drop_request(r);unlock(flags);}
    HP_ACTIVE_STAMP(hp_output);g->result_bytes=bytes;if(output){if(g->output_capacity<bytes){status=CB_BUFFER_TOO_SMALL;if(g->operation==CB_SYSTEM_INVOKE && g->output_capacity>=sizeof(struct cb_reply_header) && !transfer(pid,g->output,output,sizeof(struct cb_reply_header),1))status=CB_BAD_REQUEST;}else if(!transfer(pid,g->output,output,bytes,1))status=CB_BAD_REQUEST;}
    release(owned_input);release(output);HP_FINISH(owner);return module_result(status,1);
}
struct kernel_surface_result handles_surface_post(MODULE_ARGS);
struct kernel_surface_result handles_surface_exit(MODULE_ARGS){
    MODULE_UNUSED();uint64_t flags=lock();struct policy *owner=policy(a);if(!owner){unlock(flags);return module_result(1,1);}
    const struct cb_interface_id io_interface=CB_IO_INTERFACE_ID;for(struct channel *ch=state.channels;ch;ch=ch->next)if(ch->owner_watch==a){ch->owner_watch=0;(void)handles_surface_post(ch->pid,ch->token,(uintptr_t)&a,8,(uintptr_t)&io_interface,CB_IO_OWNER_EXIT);}
    for(struct publication *p=state.publications,*next;p;p=next){next=p->next;if(p->pid==a)retire_publication(p,1);}
    struct channel **ch=&state.channels;while(*ch){struct channel *event=*ch;if(event->pid==a){*ch=event->next;release(event->frame);release(event);}else ch=&event->next;}
    struct request *r=state.requests;while(r){struct request *next=r->next;if(r->pid==a)drop_request(r);r=next;}
    struct capability *cap=state.caps;while(cap){struct capability *next=cap->next;if(cap->pid==a)drop_capability(cap);cap=next;}
    struct policy **at=&state.policies;while(*at!=owner)at=&(*at)->next;*at=owner->next;unlock(flags);free_policy(owner);return module_result(1,1);
}

struct kernel_surface_result handles_surface_available(MODULE_ARGS){
    MODULE_UNUSED();if(!a)return module_result(0,0);uint64_t flags=lock(),pid=0;for(struct publication *p=state.publications;p;p=p->next)if(!p->dead && mf_equal(p->name,(const void *)a)){pid=p->pid?p->pid:UINT64_MAX;break;}unlock(flags);return module_result(pid,1);
}

struct kernel_surface_result handles_surface_channel(MODULE_ARGS){
    MODULE_UNUSED();if(!b || c>CB_FRAME_LIMIT-sizeof(struct cb_delivery_header) || !c)return module_result(0,0);
    struct channel *channel=allocate(sizeof(*channel));if(!channel)return module_result(0,0);channel->capacity=sizeof(struct cb_delivery_header)+c;channel->frame=allocate(channel->capacity);if(!channel->frame){release(channel);return module_result(0,0);}channel->pid=a;channel->token=b;
    uint64_t flags=lock();int ok=policy(a)!=NULL;for(struct channel *ch=state.channels;ch;ch=ch->next)if(ch->pid==a && ch->token==b)ok=0;
    if(ok){channel->next=state.channels;state.channels=channel;}unlock(flags);if(!ok){release(channel->frame);release(channel);}return module_result(ok,ok);
}
struct kernel_surface_result handles_surface_post(MODULE_ARGS){
    MODULE_UNUSED();uint64_t flags=lock();struct policy *owner=policy(a);struct channel *ch;for(ch=state.channels;ch;ch=ch->next)if(ch->pid==a && ch->token==b)break;
    if(!owner || !ch || ch->ready || !e || (d && !c) || d>ch->capacity-sizeof(struct cb_delivery_header)){unlock(flags);return module_result(0,0);}
    uint64_t ticket=serial();if(!ticket){unlock(flags);return module_result(0,0);}
    struct cb_delivery_header delivery={.call={.version={sizeof(delivery),1,0},.handle=b,.interface=*(const struct cb_interface_id *)e,.operation=f,.flags=CB_CALL_PROVIDER_DELIVERY|CB_CALL_SYSTEM_EVENT,.payload_bytes=d},.provider_epoch=1};
    copy_bytes(ch->frame,&delivery,sizeof(delivery));copy_bytes(ch->frame+sizeof(delivery),(const void *)c,d);ch->bytes=sizeof(delivery)+d;ch->sequence=ticket;ch->ready=1;wake(owner->receiver);unlock(flags);return module_result(1,1);
}
struct kernel_surface_result handles_surface_unsubscribe(MODULE_ARGS){
    MODULE_UNUSED();uint64_t flags=lock();struct channel **at=&state.channels;while(*at && ((*at)->pid!=a || (*at)->token!=b))at=&(*at)->next;struct channel *ch=*at;if(ch)*at=ch->next;unlock(flags);if(ch){release(ch->frame);release(ch);}return module_result(1,1);
}

struct kernel_surface_result handles_surface_inspect(MODULE_ARGS){
    MODULE_UNUSED();if(!a || !b)return module_result(0,0);uint64_t flags=lock();struct publication *p;for(p=state.publications;p;p=p->next)if(!p->dead && mf_equal(p->name,(const void *)a))break;
    if(!p){unlock(flags);return module_result(0,0);}struct handle_publication_info info={.process=p->pid,.epoch=p->epoch,.completed=p->completed,.last_operation=p->last_operation};
    for(struct capability *cap=state.caps;cap;cap=cap->next)if(cap->publication==p && !cap->publisher)++info.clients;
    struct cb_description desc=description(p);for(struct request *r=state.requests;r;r=r->next)if(r->publication==p && !r->done){struct cb_call_header call;copy_bytes(&call,r->input,sizeof(call));if((!c || call.operation==c) && interface_equal(&call.interface,&desc.interface)){++info.pending;if(r->dispatched)++info.dispatched;}}
    *(struct handle_publication_info *)b=info;unlock(flags);return module_result(1,1);
}
struct kernel_surface_result handles_surface_decommission(MODULE_ARGS){
    MODULE_UNUSED();uint64_t flags=lock();struct policy *owner=policy(a);struct channel *control;for(control=state.channels;control;control=control->next)if(control->pid==a && !control->token)break;
    if(!owner || !control){unlock(flags);return module_result(0,0);}for(struct publication *p=state.publications,*next;p;p=next){next=p->next;if(p->pid==a && !p->dead)retire_publication(p,0);}
    struct cb_delivery_header delivery={.call={.version={sizeof(delivery),1,0},.interface=CB_HANDLE_INTERFACE_ID,.operation=CB_HANDLE_RELEASE,.flags=CB_CALL_PROVIDER_DELIVERY|CB_CALL_SYSTEM_EVENT},.provider_epoch=1};
    copy_bytes(control->frame,&delivery,sizeof(delivery));control->bytes=sizeof(delivery);control->sequence=serial();control->ready=1;wake(owner->receiver);unlock(flags);return module_result(1,1);
}

struct kernel_surface_result handles_surface_owner_watch(MODULE_ARGS){
    MODULE_UNUSED();if(!a || !b || !c || d>1)return module_result(0,0);if(!d)return handles_surface_unsubscribe(a,c,0,0,0,0);uint64_t flags=lock();if(!policy(b)){unlock(flags);return module_result(0,0);}struct kernel_surface_result result=handles_surface_channel(a,c,8,0,0,0);if(!result.status)for(struct channel *ch=state.channels;ch;ch=ch->next)if(ch->pid==a && ch->token==c){ch->owner_watch=b;break;}unlock(flags);return result;
}

/* Trusted copied installation identity; never a borrowed policy pointer. */
struct kernel_surface_result handles_surface_identity(MODULE_ARGS){MODULE_UNUSED();if(!b || (c!=16 && c!=24))return module_result(0,0);uint64_t flags=lock();struct policy *p=policy(a);if(p){copy_bytes((void *)b,p->identity.installation_id,16);if(c==24)copy_bytes((unsigned char *)b+16,&p->installation_owner,8);}unlock(flags);return module_result(p!=NULL,p!=NULL);}

/* Revoke authority without reclaiming executable code, caps or in-flight payloads.
 * Dispatch and revocation share the broker lock; dispatched work can reply. */
struct kernel_surface_result handles_surface_revoke(MODULE_ARGS){MODULE_UNUSED();uint64_t flags=lock();struct policy *owner=policy(a);if(!owner){unlock(flags);return module_result(1,1);}owner->revoked=1;
    for(struct request *r=state.requests;r;r=r->next)if(r->pid==a && !r->dispatched && !r->done){(void)error_reply(r,CB_REVOKED,CB_NOT_DISPATCHED);r->completed=1;r->done=1;}
    for(struct publication *p=state.publications,*next;p;p=next){next=p->next;if(p->pid==a && !p->dead)retire_publication(p,0);}
    for(struct capability *cap=state.caps;cap;cap=cap->next)if(cap->pid==a && cap->stream)stream_wake(cap->stream);
    for(struct handle_waiter *w=handle_waiters;w;w=w->next)if(w->pid==a)wake(w->thread);
    wake(owner->receiver);unlock(flags);return module_result(1,1);
}

/* Forced user termination wakes copied provider calls without unwinding kernel
 * continuations. Dispatched work may commit; preserve UNKNOWN until completion. */
struct kernel_surface_result handles_surface_terminate(MODULE_ARGS){
    MODULE_UNUSED();struct kernel_surface_result revoked=handles_surface_revoke(a,0,0,0,0,0);if(revoked.status)return revoked;
    uint64_t flags=lock();for(struct request *r=state.requests;r;r=r->next)if(r->pid==a && !r->done){
        (void)error_reply(r,CB_REVOKED,r->dispatched?CB_OUTCOME_UNKNOWN:CB_NOT_DISPATCHED);
        if(!r->dispatched){r->completed=1;r->done=1;}}
    unlock(flags);return module_result(1,1);
}
