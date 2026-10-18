#include "../../sdk/system.h"
#include "../../interfaces/io.h"
#include "startup.h"
#include "hid.h"
static cb_handle_t memory_handle,io_handle,publication;
static uint64_t subscription,epoch,sequence,device_id,emitted_ns,repeat_at,evicted_sequence,evicted_ns;
static struct keyboard_layout layout;
static struct keyboard_hid keyboard;
static struct cb_keyboard_event *history;static size_t capacity,count,head;
struct pending {struct pending *next;struct cb_delivery_header delivery;struct cb_keyboard_read read;uint64_t grant_ns;};
static struct pending *pending;
static unsigned char incoming[CB_FRAME_LIMIT],outgoing[CB_FRAME_LIMIT];
static void copy(void *dst,const void *src,size_t bytes){unsigned char *d=dst;const unsigned char *s=src;for(size_t i=0;i<bytes;++i)d[i]=s[i];}
static int same_interface(const struct cb_interface_id *a,const struct cb_interface_id *b){for(unsigned i=0;i<16;++i)if(a->bytes[i]!=b->bytes[i])return 0;return 1;}
static cb_status_t invoke(cb_handle_t cap,struct cb_interface_id interface,uint32_t operation,const void *payload,size_t bytes,void *result,size_t capacity_result,size_t *actual){
    unsigned char input[sizeof(struct cb_call_header)+bytes],output[512];struct cb_call_header call=cb_call(cap,interface,operation,bytes);copy(input,&call,sizeof(call));copy(input+sizeof(call),payload,bytes);uint64_t received=0;
    cb_status_t status=cb_system_invoke(input,sizeof(input),output,sizeof(output),&received);if(received>=sizeof(struct cb_reply_header) && status!=CB_BUFFER_TOO_SMALL){struct cb_reply_header h;copy(&h,output,sizeof(h));if(actual)*actual=h.result_bytes;if(!status){if(h.result_bytes>capacity_result)return CB_BAD_REQUEST;copy(result,output+h.version.bytes,h.result_bytes);}if(h.request.serial)(void)cb_system_request_release(h.request);}return status;
}
static void *allocate(size_t bytes){struct cb_memory_request r={.bytes=bytes};struct cb_memory_result result;if(invoke(memory_handle,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&r,sizeof(r),&result,sizeof(result),NULL))return NULL;return (void *)(uintptr_t)result.address;}
static void release(void *address){if(!address)return;struct cb_memory_request r={.address=(uintptr_t)address};struct cb_memory_result result;if(invoke(memory_handle,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,&r,sizeof(r),&result,sizeof(result),NULL))cb_exit(CB_RESOURCE_FAILURE);}
static uint64_t clock_ns(void){uint64_t time=0;if(invoke(io_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_NOW_NS,NULL,0,&time,sizeof(time),NULL))cb_exit(CB_UNAVAILABLE);return time;}
static void acknowledge(void){struct cb_io_subscription request={subscription};if(invoke(io_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_KEYBOARD_ACK,&request,sizeof(request),NULL,0,NULL))cb_exit(CB_UNAVAILABLE);}
static struct cb_keyboard_event *at(size_t n){return &history[(head+n)%capacity];}
static void prune(void){while(count && emitted_ns>=at(0)->timestamp_ns && emitted_ns-at(0)->timestamp_ns>UINT64_C(5000000000)){evicted_sequence=at(0)->sequence;evicted_ns=at(0)->timestamp_ns;head=(head+1)%capacity;--count;}}
static uint64_t emit(void *unused,uint32_t kind,uint16_t usage,uint32_t codepoint,uint32_t modifiers,uint64_t origin,uint32_t flags){
    (void)unused;emitted_ns=clock_ns();prune();if(count==capacity){
        size_t next=capacity?capacity*2:64;struct cb_keyboard_event *larger=next>capacity && next<=SIZE_MAX/sizeof(*history)?allocate(next*sizeof(*history)):NULL;
        if(larger){for(size_t i=0;i<count;++i)larger[i]=*at(i);release(history);history=larger;capacity=next;head=0;}
        else if(capacity){evicted_sequence=sequence;evicted_ns=emitted_ns;count=0;head=0;if(sequence==UINT64_MAX)cb_exit(CB_RESOURCE_FAILURE);history[count++]=(struct cb_keyboard_event){.version={sizeof(*history),1,0},.sequence=++sequence,.timestamp_ns=emitted_ns,.kind=CB_KEY_RESET};}
        else cb_exit(CB_RESOURCE_FAILURE);
    }
    if(sequence==UINT64_MAX)cb_exit(CB_RESOURCE_FAILURE);
    uint64_t serial=++sequence;
    *at(count++)=(struct cb_keyboard_event){.version={sizeof(*history),1,0},.sequence=serial,.device_id=device_id,.timestamp_ns=emitted_ns,.origin_sequence=origin,.kind=kind,.flags=flags,.usage_page=usage?7:0,.usage=usage,.codepoint=codepoint,.modifiers=modifiers};return serial;
}
static void reply(const struct cb_delivery_header *delivery,cb_status_t status,uint32_t outcome,size_t result_bytes){
    struct cb_reply_header h={.version={sizeof(h),1,0},.request=delivery->call.request,.status=status,.outcome=outcome,.provider_epoch=delivery->provider_epoch,.result_bytes=result_bytes};copy(outgoing,&h,sizeof(h));cb_status_t sent=cb_system_reply(outgoing,sizeof(h)+result_bytes);if(sent!=CB_OK && sent!=CB_BAD_REQUEST)cb_exit(sent);
}
static int read_request(struct pending *request,int may_wait){
    struct cb_keyboard_read *r=&request->read;struct cb_keyboard_batch batch={.version={sizeof(batch),1,0},.provider_epoch=epoch,.next_sequence=sequence};
    int initial=!r->provider_epoch && !r->after_sequence;
    if((!initial && r->provider_epoch!=epoch) || r->after_sequence>sequence || (initial?request->grant_ns<=evicted_ns && evicted_sequence:r->after_sequence<evicted_sequence)){
        copy(outgoing+sizeof(struct cb_reply_header),&batch,sizeof(batch));reply(&request->delivery,CB_INPUT_RESYNC,CB_NOT_COMMITTED,sizeof(batch));return 1;
    }
    size_t slots=(CB_FRAME_LIMIT-512-sizeof(struct cb_reply_header)-sizeof(batch))/sizeof(struct cb_keyboard_event);if(slots>r->max_events)slots=r->max_events;
    size_t result=sizeof(struct cb_reply_header)+sizeof(batch);
    for(size_t i=0;i<count && batch.count<slots;++i){struct cb_keyboard_event *event=at(i);if(event->sequence<=r->after_sequence || event->timestamp_ns<request->grant_ns)continue;copy(outgoing+result,event,sizeof(*event));result+=sizeof(*event);batch.next_sequence=event->sequence;++batch.count;}
    if(!batch.count){if(request->delivery.call.deadline_ns && emitted_ns>=request->delivery.call.deadline_ns){reply(&request->delivery,CB_TIMEOUT,CB_NOT_COMMITTED,0);return 1;}return !may_wait;}
    copy(outgoing+sizeof(struct cb_reply_header),&batch,sizeof(batch));reply(&request->delivery,CB_OK,CB_COMMITTED,result-sizeof(struct cb_reply_header));return 1;
}
static void serve_pending(void){struct pending **at=&pending;while(*at){struct pending *request=*at;if(read_request(request,1)){*at=request->next;release(request);}else at=&request->next;}}
static int admission(const unsigned char *data,size_t bytes,uint64_t *grant){
    static const char prefix[]="{\"input_stream\":\"boot:1\",\"grant_ns\":";if(bytes<sizeof(prefix) || bytes>128)return 0;for(size_t i=0;i<sizeof(prefix)-1;++i)if(data[i]!=(unsigned char)prefix[i])return 0;
    size_t at=sizeof(prefix)-1;uint64_t n=0;unsigned digits=0;while(at<bytes && data[at]>='0'&&data[at]<='9'){unsigned digit=data[at++]-'0';if(n>(UINT64_MAX-digit)/10)return 0;n=n*10+digit;++digits;}if(!digits || at+1!=bytes || data[at]!='}')return 0;*grant=n;return 1;
}
static void publish(void){
    struct cb_publication p={.version={sizeof(p),1,0},.interface=CB_KEYBOARD_INTERFACE_ID,.interface_major=1,.name_bytes=sizeof(CB_KEYBOARD_NAME)-1};struct cb_description d={.version={sizeof(d),1,0},.interface=CB_KEYBOARD_INTERFACE_ID,.interface_major=1,.operation_count=2,.facts_bytes=2};
    size_t offset=sizeof(p)+p.name_bytes+sizeof(d);const char *names[]={"read","state"};
    for(unsigned i=0;i<2;++i){size_t n=i?5:4;static const char schema[]="{\"encoding\":\"C-ABI-LE\",\"version\":1}";struct cb_operation_description op={.version={sizeof(op),1,0},.operation=i+1,.name_bytes=n,.request_schema_bytes=sizeof(schema)-1,.reply_schema_bytes=sizeof(schema)-1};copy(outgoing+offset,&op,sizeof(op));offset+=sizeof(op);copy(outgoing+offset,names[i],n);offset+=n;copy(outgoing+offset,schema,sizeof(schema)-1);offset+=sizeof(schema)-1;copy(outgoing+offset,schema,sizeof(schema)-1);offset+=sizeof(schema)-1;}
    d.operations_bytes=offset-sizeof(p)-p.name_bytes-sizeof(d);copy(outgoing+offset,"{}",2);offset+=2;p.description_bytes=offset-sizeof(p)-p.name_bytes;copy(outgoing,&p,sizeof(p));copy(outgoing+sizeof(p),CB_KEYBOARD_NAME,p.name_bytes);copy(outgoing+sizeof(p)+p.name_bytes,&d,sizeof(d));if(cb_system_publish(outgoing,offset,&publication))cb_exit(CB_DENIED);
    struct {struct cb_call_header call;struct cb_describe_request describe;} reflection={.call=cb_call(publication,(struct cb_interface_id)CB_HANDLE_INTERFACE_ID,CB_HANDLE_DESCRIBE,sizeof(struct cb_describe_request)),.describe={.version={sizeof(struct cb_describe_request),1,0},.interface=CB_KEYBOARD_INTERFACE_ID}};uint64_t bytes;
    if(cb_system_invoke(&reflection,sizeof(reflection),outgoing,sizeof(outgoing),&bytes))cb_exit(CB_PROVIDER_FAILED);
    struct cb_reply_header h;copy(&h,outgoing,sizeof(h));copy(&d,outgoing+sizeof(h),sizeof(d));epoch=d.provider_epoch;if(!epoch)cb_exit(CB_PROVIDER_FAILED);(void)cb_system_request_release(h.request);
}
__attribute__((noreturn)) void _start(uint64_t argument){
    const struct keyboard_startup *startup=(const void *)(uintptr_t)argument;
    if(!startup || startup->version.bytes!=sizeof(*startup) || startup->version.major!=1 || !keyboard_layout_parse(&layout,(const unsigned char *)startup+sizeof(*startup),startup->layout_bytes))cb_exit(CB_BAD_REQUEST);
    const struct cb_interface_id memory=CB_MEMORY_INTERFACE_ID,io=CB_IO_INTERFACE_ID;
    if(cb_system_get_handle(CB_MEMORY_NAME,sizeof(CB_MEMORY_NAME)-1,&memory,1,0,&memory_handle) || cb_system_get_handle(CB_IO_NAME,sizeof(CB_IO_NAME)-1,&io,1,0,&io_handle))cb_exit(CB_DENIED);
    struct cb_io_subscription subscribed;if(invoke(io_handle,io,CB_IO_KEYBOARD_SUBSCRIBE,NULL,0,&subscribed,sizeof(subscribed),NULL))cb_exit(CB_UNAVAILABLE);subscription=subscribed.id;publish();
    for(;;){emitted_ns=clock_ns();prune();if(repeat_at && emitted_ns>=repeat_at){keyboard_repeat(&keyboard,&layout,emit,NULL);repeat_at=keyboard.repeat_key?emitted_ns+33000000:0;}serve_pending();
        uint64_t deadline=repeat_at;for(struct pending *p=pending;p;p=p->next)if(p->delivery.call.deadline_ns && (!deadline || p->delivery.call.deadline_ns<deadline))deadline=p->delivery.call.deadline_ns;
        uint64_t bytes=0;cb_status_t status=cb_system_receive(incoming,sizeof(incoming),&bytes,deadline);emitted_ns=clock_ns();prune();
        if(status==CB_TIMEOUT){if(repeat_at && emitted_ns>=repeat_at){keyboard_repeat(&keyboard,&layout,emit,NULL);repeat_at=keyboard.repeat_key?emitted_ns+33000000:0;}serve_pending();continue;}
        if(status || bytes<sizeof(struct cb_delivery_header))cb_exit(status?status:CB_BAD_REQUEST);
    struct cb_delivery_header delivery;copy(&delivery,incoming,sizeof(delivery));
        if(delivery.call.version.bytes!=sizeof(delivery) || delivery.context_bytes>bytes-sizeof(delivery) || delivery.call.payload_bytes!=bytes-sizeof(delivery)-delivery.context_bytes)cb_exit(CB_BAD_REQUEST);
        const unsigned char *payload=incoming+sizeof(delivery)+delivery.context_bytes;
        if(delivery.call.flags==(CB_CALL_PROVIDER_DELIVERY|CB_CALL_SYSTEM_EVENT)){
            const struct cb_interface_id base=CB_HANDLE_INTERFACE_ID;
            if(!delivery.call.handle && same_interface(&delivery.call.interface,&base) && delivery.call.operation==CB_HANDLE_RELEASE){
                while(pending){struct pending *p=pending;pending=p->next;reply(&p->delivery,CB_REVOKED,CB_NOT_COMMITTED,0);release(p);}
                struct cb_io_subscription close={subscription};if(invoke(io_handle,io,CB_IO_KEYBOARD_CLOSE,&close,sizeof(close),NULL,0,NULL))cb_exit(CB_RESOURCE_FAILURE);
                release(history);(void)cb_system_release(publication);(void)cb_system_release(io_handle);(void)cb_system_release(memory_handle);cb_exit(0);
            }
            if(!same_interface(&delivery.call.interface,&io) || delivery.call.operation!=CB_IO_KEYBOARD_EVENT || delivery.call.payload_bytes!=sizeof(struct cb_io_keyboard_event))cb_exit(CB_BAD_REQUEST);
    struct cb_io_keyboard_event event;copy(&event,payload,sizeof(event));if(event.subscription!=subscription)cb_exit(CB_BAD_HANDLE);
            if(event.kind==CB_IO_DEVICE_ADDED){device_id=event.device_id;keyboard_reset(&keyboard,emit,NULL);emit(NULL,CB_KEY_DEVICE_ADDED,0,0,0,0,0);struct cb_io_protocol protocol={.id=subscription};if(invoke(io_handle,io,CB_IO_KEYBOARD_PROTOCOL,&protocol,sizeof(protocol),NULL,0,NULL))cb_exit(CB_UNAVAILABLE);}
            else if(event.kind==CB_IO_REPORT){unsigned previous=keyboard.repeat_key;keyboard_report(&keyboard,&layout,event.report,emit,NULL);if(previous!=keyboard.repeat_key)repeat_at=keyboard.repeat_key?emitted_ns+400000000:0;}
            else {keyboard_reset(&keyboard,emit,NULL);repeat_at=0;emit(NULL,CB_KEY_DEVICE_REMOVED,0,0,0,0,0);device_id=0;}
            acknowledge();serve_pending();continue;
        }
        uint64_t grant_ns;if(delivery.call.flags!=CB_CALL_PROVIDER_DELIVERY || delivery.call.handle!=publication || !admission(incoming+sizeof(delivery),delivery.context_bytes,&grant_ns)){reply(&delivery,CB_DENIED,CB_NOT_COMMITTED,0);continue;}
        if(delivery.call.operation==CB_KEYBOARD_STATE && !delivery.call.payload_bytes){struct cb_keyboard_state state={.version={sizeof(state),1,0},.provider_epoch=epoch,.last_sequence=sequence};size_t size=sizeof(struct cb_reply_header)+sizeof(state);for(unsigned i=0;i<256;++i)if(keyboard.pressed[i]){struct cb_keyboard_pressed key={device_id,7,i,0};copy(outgoing+size,&key,sizeof(key));size+=sizeof(key);++state.count;}copy(outgoing+sizeof(struct cb_reply_header),&state,sizeof(state));reply(&delivery,CB_OK,CB_COMMITTED,size-sizeof(struct cb_reply_header));}
        else if(delivery.call.operation==CB_KEYBOARD_READ && delivery.call.payload_bytes==sizeof(struct cb_keyboard_read)){struct pending request={.delivery=delivery,.grant_ns=grant_ns};copy(&request.read,payload,sizeof(request.read));if(request.read.version.bytes!=sizeof(request.read) || request.read.version.major!=1 || request.read.reserved || !request.read.max_events){reply(&delivery,CB_BAD_REQUEST,CB_NOT_COMMITTED,0);continue;}
            if(!read_request(&request,1)){struct pending *copy_request=allocate(sizeof(*copy_request));if(!copy_request){reply(&delivery,CB_RESOURCE_FAILURE,CB_NOT_COMMITTED,0);continue;}*copy_request=request;copy_request->next=pending;pending=copy_request;}
        }else reply(&delivery,CB_BAD_REQUEST,CB_NOT_COMMITTED,0);
    }
}
