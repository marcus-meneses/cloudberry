#include "../../sdk/service.h"
#include "../../interfaces/io.h"
#include "../../interfaces/sessions.h"
struct session {struct session *next;struct cb_session info;unsigned closing;};
static struct session *sessions;
static struct cb_client client,responder;
static cb_handle_t io,memory,publication;
static unsigned char input[CB_FRAME_LIMIT];
static cb_status_t query(uint64_t pid,unsigned op,uint64_t exclude,struct cb_session *out){struct cb_io_session r={pid,op,exclude};cb_status_t s=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_SESSION,&r,sizeof(r),0);if(!s && !op)cb_copy(out,cb_result(&client),sizeof(*out));return s;}
static cb_status_t watch(uint64_t pid,unsigned enable){struct cb_io_owner_watch r={pid,pid,enable};return cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_OWNER_WATCH,&r,sizeof(r),0);}
static struct session *find(uint64_t id){for(struct session *s=sessions;s;s=s->next)if(s->info.id==id)return s;return NULL;}
static int below(struct session *s,uint64_t ancestor){while(s){if(s->info.id==ancestor)return 1;s=find(s->info.parent);}return 0;}
static cb_status_t close_session(uint64_t id,uint64_t exclude){
    struct session *root=find(id);if(!root)return CB_BAD_HANDLE;
    /* Mark the complete subtree first. Retain records while owners live so
     * nested owner-exit notifications can still resolve their ancestry. */
    for(struct session *s=sessions;s;s=s->next)if(below(s,id))s->closing=1;
    cb_status_t status=CB_OK;for(struct session *s=sessions;s;s=s->next)if(below(s,id)){cb_status_t t=query(s->info.id,2,exclude,NULL);if(t)status=t;}return status;
}
static void forget(uint64_t owner){struct session *s=find(owner);if(!s)return;(void)close_session(owner,0);(void)watch(owner,0);
    /* Reparent closed descendants before deleting this record. Their kernel
     * session IDs stay invalidated, and process ancestry still gates focus. */
    for(struct session *q=sessions;q;q=q->next)if(q->info.parent==owner)q->info.parent=s->info.parent;
    struct session **at=&sessions;while(*at!=s)at=&(*at)->next;*at=s->next;struct cb_memory_request r={.address=(uintptr_t)s};CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,&r,sizeof(r),0)==CB_OK);
}
__attribute__((noreturn)) void _start(void){io=cb_acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);memory=cb_acquire(CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID);static const char *names[]={"self","begin","end"};publication=cb_publish(&client,CB_SESSIONS_NAME,(struct cb_interface_id)CB_SESSIONS_INTERFACE_ID,names,3);
    for(;;){uint64_t bytes;CB_REQUIRE(cb_system_receive(input,sizeof(input),&bytes,0)==CB_OK && bytes>=sizeof(struct cb_delivery_header));struct cb_delivery_header d;cb_copy(&d,input,sizeof(d));CB_REQUIRE(d.context_bytes<=bytes-sizeof(d) && d.call.payload_bytes==bytes-sizeof(d)-d.context_bytes);const unsigned char *payload=input+sizeof(d)+d.context_bytes;
        if(d.call.flags==(CB_CALL_PROVIDER_DELIVERY|CB_CALL_SYSTEM_EVENT)){if(d.call.operation==CB_IO_OWNER_EXIT && d.call.payload_bytes==8){uint64_t owner;cb_copy(&owner,payload,8);if(owner==d.call.handle)forget(owner);}else if(d.call.operation==CB_HANDLE_RELEASE && !d.call.handle){while(sessions)forget(sessions->info.id);cb_system_release(publication);cb_exit(0);}continue;}
        struct cb_admission caller;cb_status_t status=CB_DENIED;size_t result=0;struct cb_session info={0};
        if(d.call.flags!=CB_CALL_PROVIDER_DELIVERY || d.call.handle!=publication || d.call.payload_bytes || !cb_admission_parse(input+sizeof(d),d.context_bytes,&caller))goto reply;
        status=query(caller.process,0,0,&info);if(status)goto reply;
        if(d.call.operation==CB_SESSIONS_SELF)result=sizeof(info);
        else if(d.call.operation==CB_SESSIONS_BEGIN){struct session *parent=find(info.id);if(find(caller.process) || (parent && parent->closing)){status=CB_BUSY;goto reply;}struct cb_memory_request r={.bytes=sizeof(struct session)};status=cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&r,sizeof(r),0);if(status)goto reply;struct cb_memory_result allocated;cb_copy(&allocated,cb_result(&client),sizeof(allocated));struct session *s=(void *)(uintptr_t)allocated.address;status=watch(caller.process,1);if(!status)status=query(caller.process,1,0,NULL);if(status){(void)watch(caller.process,0);r=(struct cb_memory_request){.address=(uintptr_t)s};CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,&r,sizeof(r),0)==CB_OK);goto reply;}s->info=(struct cb_session){caller.process,info.id,caller.process,info.uid};s->closing=0;s->next=sessions;sessions=s;info=s->info;result=sizeof(info);}
        else if(d.call.operation==CB_SESSIONS_END){if(info.id!=caller.process)status=CB_DENIED;else status=close_session(info.id,caller.process);}
        else status=CB_BAD_REQUEST;
reply:{cb_status_t sent=cb_respond(&responder,&d,status,&info,result);CB_REQUIRE(sent==CB_OK || sent==CB_BAD_REQUEST);}
    }
}
