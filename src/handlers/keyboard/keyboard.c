#include "../../interfaces/admission.h"
#include "../../sdk/system.h"
#include "../../interfaces/io.h"
#include "../../interfaces/startup.h"
#include "hid.h"
#include "report.h"
static cb_handle_t memory_handle,io_handle,publication;
static uint64_t subscription,connection_id,epoch,sequence,device_id,emitted_ns,repeat_at,evicted_sequence,evicted_ns;
static struct keyboard_layout layout;
static struct keyboard_hid keyboard;
static struct keyboard_descriptor descriptor;
static struct cb_keyboard_event *history;static size_t capacity,count,head;
struct pending {struct pending *next;struct cb_delivery_header delivery;struct cb_keyboard_read read;uint64_t grant_ns,process,focus_generation;};
static struct pending *pending,*spare;
static uint64_t focus_owner,focus_generation,focus_floor;
struct focus_saved {struct focus_saved *next;uint64_t process;};
static struct focus_saved *focus_stack;
#define FOCUS_WATCH UINT64_C(0x6b657962666f6375)
static unsigned char incoming[CB_FRAME_LIMIT],outgoing[CB_FRAME_LIMIT];
static void copy(void *dst,const void *src,size_t bytes){unsigned char *d=dst;const unsigned char *s=src;for(size_t i=0;i<bytes;++i)d[i]=s[i];}
static int same_interface(const struct cb_interface_id *a,const struct cb_interface_id *b){for(unsigned i=0;i<16;++i)if(a->bytes[i]!=b->bytes[i])return 0;return 1;}
static cb_status_t invoke(cb_handle_t cap,struct cb_interface_id interface,uint32_t operation,const void *payload,size_t bytes,void *result,size_t capacity_result,size_t *actual){
    unsigned char input[sizeof(struct cb_call_header)+bytes],output[CB_HID_LIMIT+512];struct cb_call_header call=cb_call(cap,interface,operation,bytes);copy(input,&call,sizeof(call));copy(input+sizeof(call),payload,bytes);uint64_t received=0;
    cb_status_t status=cb_system_invoke(input,sizeof(input),output,sizeof(output),&received);if(received>=sizeof(struct cb_reply_header) && status!=CB_BUFFER_TOO_SMALL){struct cb_reply_header h;copy(&h,output,sizeof(h));if(actual)*actual=h.result_bytes;if(!status){if(h.result_bytes>capacity_result)return CB_BAD_REQUEST;copy(result,output+h.version.bytes,h.result_bytes);}if(h.request.serial)(void)cb_system_request_release(h.request);}return status;
}
static void *allocate(size_t bytes){struct cb_memory_request r={.bytes=bytes};struct cb_memory_result result;if(invoke(memory_handle,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&r,sizeof(r),&result,sizeof(result),NULL))return NULL;return (void *)(uintptr_t)result.address;}
static void release(void *address){if(!address)return;struct cb_memory_request r={.address=(uintptr_t)address};struct cb_memory_result result;if(invoke(memory_handle,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,&r,sizeof(r),&result,sizeof(result),NULL))cb_exit(CB_RESOURCE_FAILURE);}
static uint64_t clock_ns(void){uint64_t time=0;if(invoke(io_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_NOW_NS,NULL,0,&time,sizeof(time),NULL))cb_exit(CB_UNAVAILABLE);return time;}
static cb_status_t hid_connection(unsigned op,uint64_t id){struct cb_io_subscription request={id};return invoke(io_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,op,&request,sizeof(request),NULL,0,NULL);}
static void *report_alloc(void *context,size_t bytes){(void)context;return allocate(bytes);}
static void report_free(void *context,void *p){(void)context;release(p);}
static void discover(void){
    if(connection_id)return;
    struct cb_io_hid_list list={.subscription=subscription};
    for(;;){struct cb_io_hid_device item;size_t bytes=0;if(invoke(io_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_HID_LIST,&list,sizeof(list),&item,sizeof(item),&bytes) || !bytes)return;if(bytes!=sizeof(item))cb_exit(CB_BAD_REQUEST);list.device=item.device;list.ordinal=item.ordinal;
        struct cb_io_hid_open request={subscription,item.device,item.generation,item.interface,0};struct cb_io_hid_opened opened;if(invoke(io_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_HID_OPEN,&request,sizeof(request),&opened,sizeof(opened),NULL))continue;
        unsigned char *raw=opened.descriptor_bytes<=CB_HID_LIMIT?allocate(opened.descriptor_bytes):NULL;int valid=0;
        if(raw){struct cb_io_hid_data read={opened.connection,0,opened.descriptor_bytes};bytes=0;if(!invoke(io_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_HID_DESCRIPTOR,&read,sizeof(read),raw,opened.descriptor_bytes,&bytes) && bytes==opened.descriptor_bytes)valid=keyboard_descriptor_parse(&descriptor,raw,bytes,report_alloc,report_free,NULL);release(raw);}
        if(valid){struct cb_io_hid_capture capture={opened.connection,descriptor.info.input_bytes,0};valid=invoke(io_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_HID_START,&capture,sizeof(capture),NULL,0,NULL)==CB_OK;}
        if(valid){connection_id=opened.connection;device_id=item.device;return;}
        keyboard_descriptor_free(&descriptor);if(hid_connection(CB_IO_HID_CLOSE,opened.connection))cb_exit(CB_RESOURCE_FAILURE);
    }
}
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
/* The reflexive ancestry query also verifies that this process belongs to an
 * open session. Closed-session pending reads cannot retain console ownership. */
static int input_live(uint64_t pid){uint64_t args[2]={pid,pid},live=0;return !invoke(io_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_INPUT_ANCESTOR,args,sizeof(args),&live,sizeof(live),NULL) && live;}
static cb_status_t change_focus(uint64_t process,int claim){
    if(!claim && focus_owner!=process)return CB_DENIED;
    if(claim && !input_live(process))return CB_REVOKED;
    if(claim && focus_owner==process)return CB_OK;
    if(focus_generation==UINT64_MAX)return CB_RESOURCE_FAILURE;
    struct focus_saved *save=NULL;
    if(claim && focus_owner){uint64_t args[2]={focus_owner,process},descendant=0;cb_status_t status=invoke(io_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_INPUT_ANCESTOR,args,sizeof(args),&descendant,sizeof(descendant),NULL);if(status)return status;if(!descendant)return CB_BUSY;save=allocate(sizeof(*save));if(!save)return CB_RESOURCE_FAILURE;save->process=focus_owner;save->next=focus_stack;}
    uint64_t next=claim?process:0;
    if(claim && focus_owner){struct cb_io_owner_watch old={focus_owner,FOCUS_WATCH,0};cb_status_t s=invoke(io_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_OWNER_WATCH,&old,sizeof(old),NULL,0,NULL);if(s){release(save);return s;}}
    struct cb_io_owner_watch watch={process,FOCUS_WATCH,claim!=0};
    cb_status_t status=invoke(io_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_OWNER_WATCH,&watch,sizeof(watch),NULL,0,NULL);if(status){if(claim && focus_owner){watch=(struct cb_io_owner_watch){focus_owner,FOCUS_WATCH,1};if(invoke(io_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_OWNER_WATCH,&watch,sizeof(watch),NULL,0,NULL))cb_exit(CB_RESOURCE_FAILURE);}release(save);return status;}
    if(claim){if(save)focus_stack=save;}
    else while(focus_stack){struct focus_saved *old=focus_stack;focus_stack=old->next;watch=(struct cb_io_owner_watch){old->process,FOCUS_WATCH,1};status=input_live(old->process)?invoke(io_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_OWNER_WATCH,&watch,sizeof(watch),NULL,0,NULL):CB_REVOKED;next=old->process;release(old);if(!status)break;next=0;}
    uint64_t focus[2]={next,focus_generation+1};status=invoke(io_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_INPUT_FOCUS_SET,focus,sizeof(focus),NULL,0,NULL);if(status)cb_exit(status);
    focus_owner=next;focus_floor=sequence;++focus_generation;return CB_OK;
}
static void batch_reply(struct pending *request,struct cb_keyboard_batch *batch,cb_status_t status){
    size_t bytes=batch->version.bytes;copy(outgoing+sizeof(struct cb_reply_header),batch,sizeof(*batch));
    if(bytes==sizeof(struct cb_keyboard_batch_focus))copy(outgoing+sizeof(struct cb_reply_header)+sizeof(*batch),&focus_generation,sizeof(focus_generation));
    reply(&request->delivery,status,status?CB_NOT_COMMITTED:CB_COMMITTED,bytes+(size_t)batch->count*sizeof(struct cb_keyboard_event));
}
static int read_request(struct pending *request,int may_wait){
    if(!input_live(request->process)){if(focus_owner==request->process && change_focus(focus_owner,0))cb_exit(CB_RESOURCE_FAILURE);reply(&request->delivery,CB_REVOKED,CB_NOT_COMMITTED,0);return 1;}
    struct cb_keyboard_read *r=&request->read;int aware=r->version.bytes==sizeof(struct cb_keyboard_read_focus);size_t prefix=aware?sizeof(struct cb_keyboard_batch_focus):sizeof(struct cb_keyboard_batch);
    struct cb_keyboard_batch batch={.version={prefix,1,aware?1:0},.provider_epoch=epoch,.next_sequence=sequence,.flags=focus_owner && focus_owner!=request->process?CB_KEYBOARD_FOCUS_SUSPENDED:0};
    if((aware && request->focus_generation!=focus_generation) || r->after_sequence<focus_floor){batch.next_sequence=focus_floor;batch_reply(request,&batch,CB_INPUT_RESYNC);return 1;}
    if(focus_owner && focus_owner!=request->process){if(request->delivery.call.deadline_ns && emitted_ns>=request->delivery.call.deadline_ns){reply(&request->delivery,CB_TIMEOUT,CB_NOT_COMMITTED,0);return 1;}return !may_wait;}
    int initial=!r->provider_epoch && !r->after_sequence;
    if((!initial && r->provider_epoch!=epoch) || r->after_sequence>sequence || (initial?request->grant_ns<=evicted_ns && evicted_sequence:r->after_sequence<evicted_sequence)){
        batch_reply(request,&batch,CB_INPUT_RESYNC);return 1;
    }
    size_t slots=(CB_FRAME_LIMIT-512-sizeof(struct cb_reply_header)-prefix)/sizeof(struct cb_keyboard_event);if(slots>r->max_events)slots=r->max_events;
    size_t result=sizeof(struct cb_reply_header)+prefix;
    for(size_t i=0;i<count && batch.count<slots;++i){struct cb_keyboard_event *event=at(i);if(event->sequence<=r->after_sequence || event->timestamp_ns<request->grant_ns)continue;copy(outgoing+result,event,sizeof(*event));result+=sizeof(*event);batch.next_sequence=event->sequence;++batch.count;}
    if(!batch.count){if(request->delivery.call.deadline_ns && emitted_ns>=request->delivery.call.deadline_ns){reply(&request->delivery,CB_TIMEOUT,CB_NOT_COMMITTED,0);return 1;}return !may_wait;}
    batch_reply(request,&batch,CB_OK);return 1;
}
/* Timed reads are frequent. Reuse one owned pending node rather than mapping
 * and unmapping a process page for every empty read and timeout. */
static struct pending *pending_allocate(void){struct pending *p=spare;if(p)spare=NULL;else p=allocate(sizeof(*p));return p;}
static void pending_release(struct pending *p){if(!spare){p->next=NULL;spare=p;}else release(p);}
static void serve_pending(void){struct pending **at=&pending;while(*at){struct pending *request=*at;if(read_request(request,1)){*at=request->next;pending_release(request);}else at=&request->next;}}
static int admission(const unsigned char *data,size_t bytes,uint64_t *grant){struct cb_admission a;if(!cb_admission_parse(data,bytes,&a))return 0;*grant=a.grant_ns;return 1;}

static void publish(void){
    struct cb_publication p={.version={sizeof(p),1,0},.interface=CB_KEYBOARD_INTERFACE_ID,.interface_major=1,.name_bytes=sizeof(CB_KEYBOARD_NAME)-1};struct cb_description d={.version={sizeof(d),1,0},.interface=CB_KEYBOARD_INTERFACE_ID,.interface_major=1,.operation_count=4,.facts_bytes=2};
    size_t offset=sizeof(p)+p.name_bytes+sizeof(d);const char *names[]={"read","state","focus_claim","focus_release"};
    for(unsigned i=0;i<4;++i){size_t n=0;while(names[i][n])++n;static const char schema[]="{\"encoding\":\"C-ABI-LE\",\"version\":1}";struct cb_operation_description op={.version={sizeof(op),1,0},.operation=i+1,.name_bytes=n,.request_schema_bytes=sizeof(schema)-1,.reply_schema_bytes=sizeof(schema)-1};copy(outgoing+offset,&op,sizeof(op));offset+=sizeof(op);copy(outgoing+offset,names[i],n);offset+=n;copy(outgoing+offset,schema,sizeof(schema)-1);offset+=sizeof(schema)-1;copy(outgoing+offset,schema,sizeof(schema)-1);offset+=sizeof(schema)-1;}
    d.operations_bytes=offset-sizeof(p)-p.name_bytes-sizeof(d);copy(outgoing+offset,"{}",2);offset+=2;p.description_bytes=offset-sizeof(p)-p.name_bytes;copy(outgoing,&p,sizeof(p));copy(outgoing+sizeof(p),CB_KEYBOARD_NAME,p.name_bytes);copy(outgoing+sizeof(p)+p.name_bytes,&d,sizeof(d));if(cb_system_publish(outgoing,offset,&publication))cb_exit(CB_DENIED);
    struct {struct cb_call_header call;struct cb_describe_request describe;} reflection={.call=cb_call(publication,(struct cb_interface_id)CB_HANDLE_INTERFACE_ID,CB_HANDLE_DESCRIBE,sizeof(struct cb_describe_request)),.describe={.version={sizeof(struct cb_describe_request),1,0},.interface=CB_KEYBOARD_INTERFACE_ID}};uint64_t bytes;
    if(cb_system_invoke(&reflection,sizeof(reflection),outgoing,sizeof(outgoing),&bytes))cb_exit(CB_PROVIDER_FAILED);
    struct cb_reply_header h;copy(&h,outgoing,sizeof(h));copy(&d,outgoing+sizeof(h),sizeof(d));epoch=d.provider_epoch;if(!epoch)cb_exit(CB_PROVIDER_FAILED);(void)cb_system_request_release(h.request);
}
__attribute__((noreturn)) void _start(uint64_t argument){
    const struct cb_startup *startup=(const void *)(uintptr_t)argument;
    const struct cb_interface_id memory=CB_MEMORY_INTERFACE_ID,io=CB_IO_INTERFACE_ID;
    if(!startup || cb_system_get_handle(CB_MEMORY_NAME,sizeof(CB_MEMORY_NAME)-1,&memory,1,0,&memory_handle) || cb_system_get_handle(CB_IO_NAME,sizeof(CB_IO_NAME)-1,&io,1,0,&io_handle))cb_exit(CB_DENIED);
    const void *configuration,*definition;size_t configuration_bytes,definition_bytes,name_bytes;const char *name;
    if(!cb_startup_find(startup,startup->bytes,"config.cnf",&configuration,&configuration_bytes) || !keyboard_config_layout(configuration,configuration_bytes,&name,&name_bytes) || name_bytes>SIZE_MAX-14)cb_exit(CB_BAD_REQUEST);
    char *path=allocate(name_bytes+14);if(!path)cb_exit(CB_RESOURCE_FAILURE);copy(path,"layouts/",8);copy(path+8,name,name_bytes);copy(path+8+name_bytes,".json",6);
    int valid=cb_startup_find(startup,startup->bytes,path,&definition,&definition_bytes) && keyboard_layout_parse(&layout,definition,definition_bytes);release(path);if(!valid)cb_exit(CB_BAD_REQUEST);
    struct cb_io_subscription subscribed;if(invoke(io_handle,io,CB_IO_HID_SUBSCRIBE,NULL,0,&subscribed,sizeof(subscribed),NULL))cb_exit(CB_UNAVAILABLE);subscription=subscribed.id;publish();discover();if(connection_id){keyboard_reset(&keyboard,emit,NULL);emit(NULL,CB_KEY_DEVICE_ADDED,0,0,0,0,0);}
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
                struct cb_io_subscription close={subscription};if(invoke(io_handle,io,CB_IO_HID_CLOSE,&close,sizeof(close),NULL,0,NULL))cb_exit(CB_RESOURCE_FAILURE);
                while(focus_stack){struct focus_saved *old=focus_stack;focus_stack=old->next;release(old);}
                keyboard_descriptor_free(&descriptor);release(spare);spare=NULL;release(history);(void)cb_system_release(publication);(void)cb_system_release(io_handle);(void)cb_system_release(memory_handle);cb_exit(0);
            }
            if(same_interface(&delivery.call.interface,&io) && delivery.call.operation==CB_IO_OWNER_EXIT && delivery.call.handle==FOCUS_WATCH && delivery.call.payload_bytes==8){uint64_t owner;copy(&owner,payload,8);if(owner==focus_owner){if(change_focus(owner,0))cb_exit(CB_RESOURCE_FAILURE);serve_pending();}continue;}
            if(!same_interface(&delivery.call.interface,&io) || delivery.call.operation!=CB_IO_HID_EVENT || delivery.call.payload_bytes<sizeof(struct cb_io_hid_event))cb_exit(CB_BAD_REQUEST);
            struct cb_io_hid_event event;copy(&event,payload,sizeof(event));if(event.subscription!=subscription || event.bytes!=delivery.call.payload_bytes-sizeof(event))cb_exit(CB_BAD_HANDLE);
            if(event.kind==CB_IO_ERROR && event.status==CB_PROVIDER_FAILED)cb_exit(CB_PROVIDER_FAILED);
            if(hid_connection(CB_IO_HID_ACK,event.connection?event.connection:subscription))cb_exit(CB_UNAVAILABLE);
            if(event.kind==CB_IO_HID_CHANGED){int was=connection_id!=0;discover();if(!was && connection_id){keyboard_reset(&keyboard,emit,NULL);emit(NULL,CB_KEY_DEVICE_ADDED,0,0,0,0,0);}}
            else if(event.connection==connection_id){
                if(event.kind==CB_IO_REPORT){uint8_t pressed[256];int decoded=keyboard_descriptor_decode(&descriptor,payload+sizeof(event),event.bytes,pressed);unsigned previous=keyboard.repeat_key;if(decoded>0)keyboard_pressed(&keyboard,&layout,pressed,emit,NULL);else if(decoded<0){keyboard_descriptor_reset(&descriptor);keyboard_reset(&keyboard,emit,NULL);}if(previous!=keyboard.repeat_key)repeat_at=keyboard.repeat_key?emitted_ns+400000000:0;}
                else {keyboard_reset(&keyboard,emit,NULL);repeat_at=0;emit(NULL,CB_KEY_DEVICE_REMOVED,0,0,0,0,0);if(hid_connection(CB_IO_HID_CLOSE,connection_id))cb_exit(CB_RESOURCE_FAILURE);keyboard_descriptor_free(&descriptor);connection_id=device_id=0;discover();if(connection_id)emit(NULL,CB_KEY_DEVICE_ADDED,0,0,0,0,0);}
            }
            serve_pending();continue;
        }
        struct cb_admission caller;uint64_t grant_ns;if(delivery.call.flags!=CB_CALL_PROVIDER_DELIVERY || delivery.call.handle!=publication || !cb_admission_parse(incoming+sizeof(delivery),delivery.context_bytes,&caller) || !admission(incoming+sizeof(delivery),delivery.context_bytes,&grant_ns)){reply(&delivery,CB_DENIED,CB_NOT_COMMITTED,0);continue;}
        if((delivery.call.operation==CB_KEYBOARD_FOCUS_CLAIM || delivery.call.operation==CB_KEYBOARD_FOCUS_RELEASE) && !delivery.call.payload_bytes){cb_status_t status=change_focus(caller.process,delivery.call.operation==CB_KEYBOARD_FOCUS_CLAIM);struct cb_keyboard_focus result={epoch,focus_floor,focus_generation,focus_owner};copy(outgoing+sizeof(struct cb_reply_header),&result,sizeof(result));reply(&delivery,status,status?CB_NOT_COMMITTED:CB_COMMITTED,status?0:sizeof(result));serve_pending();continue;}
        if(delivery.call.operation==CB_KEYBOARD_STATE && !delivery.call.payload_bytes){if(!input_live(caller.process)){reply(&delivery,CB_REVOKED,CB_NOT_COMMITTED,0);continue;}if(focus_owner && focus_owner!=caller.process){reply(&delivery,CB_DENIED,CB_NOT_COMMITTED,0);continue;}struct cb_keyboard_state state={.version={sizeof(state),1,0},.provider_epoch=epoch,.last_sequence=sequence};size_t size=sizeof(struct cb_reply_header)+sizeof(state);for(unsigned i=0;i<256;++i)if(keyboard.pressed[i]){struct cb_keyboard_pressed key={device_id,7,i,0};copy(outgoing+size,&key,sizeof(key));size+=sizeof(key);++state.count;}copy(outgoing+sizeof(struct cb_reply_header),&state,sizeof(state));reply(&delivery,CB_OK,CB_COMMITTED,size-sizeof(struct cb_reply_header));}
        else if(delivery.call.operation==CB_KEYBOARD_READ && (delivery.call.payload_bytes==sizeof(struct cb_keyboard_read) || delivery.call.payload_bytes==sizeof(struct cb_keyboard_read_focus))){struct pending request={.delivery=delivery,.grant_ns=grant_ns,.process=caller.process};copy(&request.read,payload,sizeof(request.read));if(request.read.version.bytes!=delivery.call.payload_bytes || request.read.version.major!=1 || request.read.version.minor!=(delivery.call.payload_bytes==sizeof(struct cb_keyboard_read_focus)?1:0) || request.read.reserved || !request.read.max_events){reply(&delivery,CB_BAD_REQUEST,CB_NOT_COMMITTED,0);continue;}
            if(delivery.call.payload_bytes==sizeof(struct cb_keyboard_read_focus))copy(&request.focus_generation,payload+sizeof(request.read),sizeof(request.focus_generation));
            if(!read_request(&request,1)){struct pending *copy_request=pending_allocate();if(!copy_request){reply(&delivery,CB_RESOURCE_FAILURE,CB_NOT_COMMITTED,0);continue;}*copy_request=request;copy_request->next=pending;pending=copy_request;}
        }else reply(&delivery,CB_BAD_REQUEST,CB_NOT_COMMITTED,0);
    }
}
