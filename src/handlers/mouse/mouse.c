#include "../../sdk/service.h"
#include "../../interfaces/io.h"
#include "../../interfaces/mouse.h"
#include "hid.h"
static struct cb_client client,reply;
static cb_handle_t memory,io,publication;
static uint64_t subscription,epoch,sequence,evicted_sequence,evicted_ns;
static unsigned char incoming[CB_FRAME_LIMIT],outgoing[CB_FRAME_LIMIT];
struct mouse {struct mouse *next;struct cb_io_hid_device usb;uint64_t id,seen;struct mouse_hid hid;unsigned rejected;};
struct pending {struct pending *next;struct cb_delivery_header delivery;struct cb_mouse_read read;uint64_t grant_ns;};
static uint64_t scan_serial;
static struct mouse *devices;static struct pending *pending,*spare;
static struct cb_mouse_event *history;static size_t capacity,count,head;
static void *allocate(size_t bytes){struct cb_memory_request r={.bytes=bytes};if(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&r,sizeof(r),0))return NULL;struct cb_memory_result value;cb_copy(&value,cb_result(&client),sizeof(value));return (void *)(uintptr_t)value.address;}
static void release(void *p){if(!p)return;struct cb_memory_request r={.address=(uintptr_t)p};CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,&r,sizeof(r),0)==CB_OK);}
static void *parser_alloc(void *ctx,size_t bytes){(void)ctx;return allocate(bytes);}
static void parser_free(void *ctx,void *p){(void)ctx;release(p);}
static int same_id(const void *a,const void *b,size_t n){for(size_t i=0;i<n;++i)if(((const unsigned char *)a)[i]!=((const unsigned char *)b)[i])return 0;return 1;}
static cb_status_t invoke(unsigned op,const void *p,size_t bytes){return cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,op,p,bytes,0);}
static cb_status_t connection(unsigned op,uint64_t id){struct cb_io_subscription r={id};return invoke(op,&r,sizeof(r));}
static uint64_t now(void){CB_REQUIRE(invoke(CB_IO_NOW_NS,NULL,0)==CB_OK);uint64_t value;cb_copy(&value,cb_result(&client),8);return value;}
static struct cb_mouse_event *at(size_t index){return history+(head+index)%capacity;}
static void prune(uint64_t time){while(count && time>=at(0)->timestamp_ns && time-at(0)->timestamp_ns>UINT64_C(5000000000)){evicted_sequence=at(0)->sequence;evicted_ns=at(0)->timestamp_ns;head=(head+1)%capacity;--count;}}
static void emit(struct mouse *m,unsigned kind,unsigned status,const struct mouse_hid_input *value,uint64_t time){
    prune(time);if(count==capacity){size_t n=capacity?capacity*2:64;struct cb_mouse_event *larger=n>capacity && n<=SIZE_MAX/sizeof(*history)?allocate(n*sizeof(*history)):NULL;
        if(larger){for(size_t i=0;i<count;++i)larger[i]=*at(i);release(history);history=larger;capacity=n;head=0;}
        else {CB_REQUIRE(capacity);evicted_sequence=sequence;evicted_ns=time;count=head=0;CB_REQUIRE(sequence!=UINT64_MAX);history[count++]=(struct cb_mouse_event){.version={sizeof(*history),1,0},.sequence=++sequence,.timestamp_ns=time,.kind=CB_MOUSE_RESET};}
    }
    /* A reset may have filled a one-entry buffer; normal buffers start at 64. */
    CB_REQUIRE(count<capacity && sequence!=UINT64_MAX);struct cb_mouse_event e={.version={sizeof(e),1,0},.sequence=++sequence,.device_id=m?m->id:0,.timestamp_ns=time,.kind=kind,.status=status,.resolution_x=1,.resolution_y=1};
    if(value){e.flags=value->flags;e.dx=value->dx;e.dy=value->dy;e.x=value->x;e.y=value->y;e.wheel_x=value->wheel_x;e.wheel_y=value->wheel_y;e.buttons=value->buttons;e.changed=value->changed;e.resolution_x=value->resolution_x;e.resolution_y=value->resolution_y;}else if(m){e.flags=m->hid.flags;e.x=m->hid.x;e.y=m->hid.y;e.buttons=m->hid.buttons;}
    *at(count++)=e;
}
static void respond(const struct cb_delivery_header *d,cb_status_t status,const void *p,size_t bytes){
    cb_status_t sent;
    if(status && bytes){struct cb_reply_header h={.version={sizeof(h),1,0},.request=d->call.request,.provider_epoch=d->provider_epoch,.status=status,.outcome=CB_NOT_COMMITTED,.result_bytes=bytes};cb_copy(reply.frame,&h,sizeof(h));cb_copy(reply.frame+sizeof(h),p,bytes);sent=cb_system_reply(reply.frame,sizeof(h)+bytes);}
    else sent=cb_respond(&reply,d,status,p,bytes);
    CB_REQUIRE(sent==CB_OK || sent==CB_BAD_REQUEST);
}
static int read_request(struct pending *p,uint64_t time){
    struct cb_mouse_read *r=&p->read;struct cb_mouse_batch batch={.version={sizeof(batch),1,0},.provider_epoch=epoch,.next_sequence=sequence};int initial=!r->provider_epoch && !r->after_sequence;
    if((!initial && r->provider_epoch!=epoch) || r->after_sequence>sequence || (initial?p->grant_ns<=evicted_ns && evicted_sequence:r->after_sequence<evicted_sequence)){respond(&p->delivery,CB_INPUT_RESYNC,&batch,sizeof(batch));return 1;}
    size_t slots=(sizeof(outgoing)-sizeof(struct cb_reply_header)-sizeof(batch)-512)/sizeof(struct cb_mouse_event);if(slots>r->max_events)slots=r->max_events;size_t bytes=sizeof(batch);
    for(size_t i=0;i<count && batch.count<slots;++i){struct cb_mouse_event *e=at(i);if(e->sequence<=r->after_sequence || e->timestamp_ns<p->grant_ns)continue;cb_copy(outgoing+bytes,e,sizeof(*e));bytes+=sizeof(*e);batch.next_sequence=e->sequence;++batch.count;}
    if(!batch.count){if(p->delivery.call.deadline_ns && time>=p->delivery.call.deadline_ns){respond(&p->delivery,CB_TIMEOUT,NULL,0);return 1;}return 0;}cb_copy(outgoing,&batch,sizeof(batch));respond(&p->delivery,CB_OK,outgoing,bytes);return 1;
}
static void serve(uint64_t time){struct pending **at=&pending;while(*at){struct pending *p=*at;if(read_request(p,time)){*at=p->next;if(!spare)spare=p;else release(p);}else at=&p->next;}}
static void configure_features(struct mouse *m){
    unsigned char original[CB_HID_LIMIT],frame[sizeof(struct cb_io_hid_feature)+CB_HID_LIMIT];
    for(unsigned id=0;id<256;++id){size_t bytes=mouse_hid_feature_bytes(&m->hid,id);if(!bytes || bytes>CB_HID_LIMIT)continue;struct cb_io_hid_feature request={m->id,id,bytes};
        if(invoke(CB_IO_HID_GET_FEATURE,&request,sizeof(request)))continue;
        struct cb_reply_header h;cb_copy(&h,client.result,sizeof(h));if(h.result_bytes!=bytes)continue;cb_copy(original,cb_result(&client),bytes);mouse_hid_feature_apply(&m->hid,id,original,bytes);cb_copy(frame,&request,sizeof(request));cb_copy(frame+sizeof(request),original,bytes);
        if(mouse_hid_feature_prepare(&m->hid,id,frame+sizeof(request),bytes) && invoke(CB_IO_HID_SET_FEATURE,frame,sizeof(request)+bytes)==CB_OK)mouse_hid_feature_apply(&m->hid,id,frame+sizeof(request),bytes);
    }
}
static int same_mouse(const struct mouse *m,const struct cb_io_hid_device *d){return m->usb.device==d->device && m->usb.generation==d->generation && m->usb.interface==d->interface;}
static void discover(void){
    uint64_t scan=++scan_serial;struct cb_io_hid_list request={.subscription=subscription};
    for(;;){cb_status_t status=invoke(CB_IO_HID_LIST,&request,sizeof(request));if(status)return;struct cb_reply_header h;cb_copy(&h,client.result,sizeof(h));if(!h.result_bytes)break;CB_REQUIRE(h.result_bytes==sizeof(struct cb_io_hid_device));struct cb_io_hid_device item;cb_copy(&item,cb_result(&client),sizeof(item));CB_REQUIRE(item.device>request.device || (item.device==request.device && item.ordinal>request.ordinal));request.device=item.device;request.ordinal=item.ordinal;
        int known=0;for(struct mouse *m=devices;m;m=m->next)if(same_mouse(m,&item)){m->seen=scan;known=1;break;}if(known)continue;
        struct cb_io_hid_open open={subscription,item.device,item.generation,item.interface,0};if(invoke(CB_IO_HID_OPEN,&open,sizeof(open)))continue;struct cb_io_hid_opened opened;cb_copy(&opened,cb_result(&client),sizeof(opened));
        struct mouse *m=allocate(sizeof(*m));if(!m){(void)connection(CB_IO_HID_CLOSE,opened.connection);continue;}m->usb=item;m->id=opened.connection;m->seen=scan;
        unsigned char *descriptor=opened.descriptor_bytes<=CB_HID_LIMIT?allocate(opened.descriptor_bytes):NULL;int valid=0;
        if(descriptor){struct cb_io_hid_data data={m->id,0,opened.descriptor_bytes};if(!invoke(CB_IO_HID_DESCRIPTOR,&data,sizeof(data))){struct cb_reply_header result;cb_copy(&result,client.result,sizeof(result));if(result.result_bytes==opened.descriptor_bytes){cb_copy(descriptor,cb_result(&client),opened.descriptor_bytes);valid=mouse_hid_parse(&m->hid,descriptor,opened.descriptor_bytes,parser_alloc,parser_free,NULL);}}release(descriptor);}
        if(valid){configure_features(m);uint32_t maximum=0;for(unsigned id=0;id<256;++id)if(m->hid.bits[0][id]){uint32_t bytes=(m->hid.bits[0][id]+7)/8+(m->hid.numbered?1:0);if(bytes>maximum)maximum=bytes;}struct cb_io_hid_capture capture={m->id,maximum,0};valid=invoke(CB_IO_HID_START,&capture,sizeof(capture))==CB_OK;}
        if(!valid){mouse_hid_free(&m->hid);CB_REQUIRE(connection(CB_IO_HID_CLOSE,m->id)==CB_OK);m->id=0;m->rejected=1;}
        m->next=devices;devices=m;if(valid)emit(m,CB_MOUSE_ADDED,CB_OK,NULL,now());
    }
    struct mouse **at=&devices;while(*at){struct mouse *m=*at;if(m->rejected && m->seen!=scan){*at=m->next;release(m);}else at=&m->next;}
}
static void remove_mouse(struct mouse *m,unsigned status,uint64_t time){
    struct mouse_hid_input released={.x=m->hid.x,.y=m->hid.y,.flags=m->hid.flags,.changed=m->hid.buttons,.resolution_x=1,.resolution_y=1};m->hid.buttons=0;emit(m,CB_MOUSE_REMOVED,status,&released,time);CB_REQUIRE(connection(CB_IO_HID_CLOSE,m->id)==CB_OK);
    struct mouse **at=&devices;while(*at!=m)at=&(*at)->next;*at=m->next;mouse_hid_free(&m->hid);release(m);
}
__attribute__((noreturn)) void _start(void){
    memory=cb_acquire(CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID);io=cb_acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);CB_REQUIRE(invoke(CB_IO_HID_SUBSCRIBE,NULL,0)==CB_OK);cb_copy(&subscription,cb_result(&client),8);
    static const char *names[]={"read","state"};publication=cb_publish(&client,CB_MOUSE_NAME,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID,names,2);
    struct cb_describe_request describe={.version={sizeof(describe),1,0},.interface=CB_MOUSE_INTERFACE_ID};CB_REQUIRE(cb_invoke(&client,publication,(struct cb_interface_id)CB_HANDLE_INTERFACE_ID,CB_HANDLE_DESCRIBE,&describe,sizeof(describe),0)==CB_OK);struct cb_description reflected;cb_copy(&reflected,cb_result(&client),sizeof(reflected));epoch=reflected.provider_epoch;CB_REQUIRE(epoch);discover();
    for(;;){uint64_t time=now();prune(time);serve(time);uint64_t deadline=0;for(struct pending *p=pending;p;p=p->next)if(p->delivery.call.deadline_ns && (!deadline || p->delivery.call.deadline_ns<deadline))deadline=p->delivery.call.deadline_ns;
        uint64_t bytes;cb_status_t status=cb_system_receive(incoming,sizeof(incoming),&bytes,deadline);if(status==CB_TIMEOUT)continue;CB_REQUIRE(status==CB_OK && bytes>=sizeof(struct cb_delivery_header));struct cb_delivery_header d;cb_copy(&d,incoming,sizeof(d));CB_REQUIRE(d.call.version.bytes==sizeof(d) && d.context_bytes<=bytes-sizeof(d) && d.call.payload_bytes==bytes-sizeof(d)-d.context_bytes);const unsigned char *payload=incoming+sizeof(d)+d.context_bytes;
        if(d.call.flags==(CB_CALL_PROVIDER_DELIVERY|CB_CALL_SYSTEM_EVENT)){
            if(!d.call.handle && d.call.operation==CB_HANDLE_RELEASE){while(pending){struct pending *p=pending;pending=p->next;respond(&p->delivery,CB_REVOKED,NULL,0);release(p);}CB_REQUIRE(connection(CB_IO_HID_CLOSE,subscription)==CB_OK);while(devices){struct mouse *m=devices;devices=m->next;mouse_hid_free(&m->hid);release(m);}release(history);release(spare);cb_system_release(publication);cb_exit(0);}
            if(d.call.operation!=CB_IO_HID_EVENT || !same_id(&d.call.interface,&(struct cb_interface_id)CB_IO_INTERFACE_ID,sizeof(d.call.interface)) || d.call.payload_bytes<sizeof(struct cb_io_hid_event))continue;
            struct cb_io_hid_event event;cb_copy(&event,payload,sizeof(event));CB_REQUIRE(event.subscription==subscription && event.bytes==d.call.payload_bytes-sizeof(event) && event.bytes<=CB_HID_LIMIT);if(event.kind==CB_IO_ERROR && event.status==CB_PROVIDER_FAILED)cb_exit(CB_PROVIDER_FAILED);CB_REQUIRE(connection(CB_IO_HID_ACK,event.connection?event.connection:subscription)==CB_OK);
            if(event.kind==CB_IO_HID_CHANGED){discover();continue;}
            struct mouse *m=devices;while(m && m->id!=event.connection)m=m->next;if(!m)continue;
            if(event.kind==CB_IO_REPORT){struct mouse_hid_input value;int decoded=mouse_hid_decode(&m->hid,payload+sizeof(event),event.bytes,&value);if(decoded==1)emit(m,CB_MOUSE_INPUT,CB_OK,&value,event.timestamp_ns);else if(decoded<0)emit(m,CB_MOUSE_ERROR,CB_BAD_REQUEST,NULL,event.timestamp_ns);}
            else remove_mouse(m,event.status,event.timestamp_ns);
            continue;
        }
        struct cb_admission caller;if(d.call.flags!=CB_CALL_PROVIDER_DELIVERY || d.call.handle!=publication || !cb_admission_parse(incoming+sizeof(d),d.context_bytes,&caller)){respond(&d,CB_DENIED,NULL,0);continue;}
        if(d.call.operation==CB_MOUSE_STATE && d.call.payload_bytes==8){uint64_t cursor;cb_copy(&cursor,payload,8);struct cb_mouse_state state={.version={sizeof(state),1,0},.provider_epoch=epoch,.last_sequence=sequence};struct mouse *m=devices;while(m){if(!m->rejected){if(!cursor)break;--cursor;}m=m->next;}cb_copy(outgoing,&state,sizeof(state));size_t n=sizeof(state);if(m){state.count=1;struct cb_mouse_device item={m->id,m->usb.device,m->usb.generation,m->usb.interface,m->hid.flags,m->hid.x,m->hid.y,m->hid.buttons};cb_copy(outgoing,&state,sizeof(state));cb_copy(outgoing+n,&item,sizeof(item));n+=sizeof(item);}respond(&d,CB_OK,outgoing,n);}
        else if(d.call.operation==CB_MOUSE_READ && d.call.payload_bytes==sizeof(struct cb_mouse_read)){struct pending p={.delivery=d,.grant_ns=caller.grant_ns};cb_copy(&p.read,payload,sizeof(p.read));if(p.read.version.bytes!=sizeof(p.read) || p.read.version.major!=1 || p.read.version.minor || p.read.reserved || !p.read.max_events){respond(&d,CB_BAD_REQUEST,NULL,0);continue;}if(!read_request(&p,now())){struct pending *save=spare;if(save)spare=NULL;else save=allocate(sizeof(*save));if(!save){respond(&d,CB_RESOURCE_FAILURE,NULL,0);continue;}*save=p;save->next=pending;pending=save;}}
        else respond(&d,CB_BAD_REQUEST,NULL,0);
    }
}
