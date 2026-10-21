#include "../../sdk/service.h"
#include "../../interfaces/io.h"
#include "../../interfaces/sessions.h"
struct session {struct session *next;struct cb_session info;unsigned closing,owner_gone;cb_status_t close_error;};
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
    cb_status_t status=CB_OK;for(struct session *s=sessions;s;s=s->next)if(below(s,id)){cb_status_t t=query(s->info.id,2,exclude,NULL);s->close_error=t;if(t && !status)status=t;}return status;
}
static cb_status_t snapshot(struct session *s,struct cb_session_status *out){
    if(!s)return CB_NOT_FOUND;
    struct cb_io_session r={s->info.id,3,0};cb_status_t status=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_SESSION,&r,sizeof(r),0);if(status)return status;cb_copy(out,cb_result(&client),sizeof(*out));out->session=s->info;out->closing=s->closing;out->close_error=s->close_error;
    /* Include descendant sessions in an owner's drain report. */
    for(struct session *child=sessions;child;child=child->next)if(child!=s && below(child,s->info.id)){r.process=child->info.id;status=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_SESSION,&r,sizeof(r),0);if(status)return status;struct cb_session_status part;cb_copy(&part,cb_result(&client),sizeof(part));out->members+=part.members;out->running+=part.running+(!child->owner_gone);out->stop_failures+=part.stop_failures;if(!out->close_error)out->close_error=child->close_error;}
    return CB_OK;
}
/* Policy stays in userland; kernel mechanics select actual UID snapshots. */
static cb_status_t revoke_uid(uint64_t uid,struct cb_session_status *out){
    if(!uid)return CB_DENIED;
    cb_status_t error=CB_OK;
    for(struct session *s=sessions;s;s=s->next)if(s->info.uid==uid){cb_status_t t=close_session(s->info.id,0);if(t && !error)error=t;}
    cb_status_t t=query(uid,4,0,NULL);if(t && !error)error=t;
    struct cb_io_session r={uid,5,0};t=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_SESSION,&r,sizeof(r),0);
    if(t)return t;
    cb_copy(out,cb_result(&client),sizeof(*out));out->close_error=error;return error;
}
static cb_status_t management(uint64_t pid){
    cb_status_t status=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_CREDENTIALS,&pid,8,0);if(status)return status;
    struct cb_reply_header reply;cb_copy(&reply,client.result,sizeof(reply));if(reply.result_bytes<sizeof(struct cb_credentials))return CB_BAD_REQUEST;
    struct cb_credentials c;cb_copy(&c,cb_result(&client),sizeof(c));if(c.groups>(reply.result_bytes-sizeof(c))/8)return CB_BAD_REQUEST;if(!c.uid || c.gid==1)return CB_OK;
    for(uint64_t i=0;i<c.groups;++i){uint64_t gid;cb_copy(&gid,(const unsigned char *)cb_result(&client)+sizeof(c)+i*8,8);if(gid==1)return CB_OK;}return CB_DENIED;
}
static cb_status_t end_session(uint64_t caller,const struct cb_session *info,const void *payload,size_t bytes,struct cb_session_status *out,size_t *result){
    *result=0;uint64_t id=info->id;cb_status_t status;
    if(bytes){if(bytes!=8)return CB_BAD_REQUEST;cb_copy(&id,payload,8);status=id?management(caller):CB_DENIED;}
    else status=info->id==caller?CB_OK:CB_DENIED;
    if(status)return status;
    cb_status_t closed=close_session(id,bytes?0:caller),sampled=snapshot(find(id),out);if(!sampled)*result=sizeof(*out);
    return closed?closed:sampled;
}
static void dispose(struct session *s){for(struct session *q=sessions;q;q=q->next)if(q->info.parent==s->info.id)q->info.parent=s->info.parent;struct session **at=&sessions;while(*at!=s)at=&(*at)->next;*at=s->next;struct cb_memory_request r={.address=(uintptr_t)s};CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,&r,sizeof(r),0)==CB_OK);}
static void collect_sessions(void){struct session *s=sessions;while(s){struct session *next=s->next;if(s->owner_gone){struct cb_session_status state;if(snapshot(s,&state)==CB_OK && !state.running)dispose(s);}s=next;}}
static void forget(uint64_t owner){struct session *s=find(owner);if(!s)return;(void)close_session(owner,0);(void)watch(owner,0);s->owner_gone=1;collect_sessions();}
static cb_status_t respond(const struct cb_delivery_header *d,cb_status_t status,const void *payload,size_t bytes){
    /* Close errors retain their copied drain report, unlike ordinary SDK errors. */
    struct cb_reply_header h={.version={sizeof(h),1,0},.request=d->call.request,.provider_epoch=d->provider_epoch,.status=status,.outcome=!status?CB_COMMITTED:bytes || status==CB_RESOURCE_FAILURE || status==CB_PROVIDER_FAILED || status==CB_TIMEOUT?CB_OUTCOME_UNKNOWN:CB_NOT_COMMITTED,.result_bytes=bytes};
    cb_copy(responder.frame,&h,sizeof(h));cb_copy(responder.frame+sizeof(h),payload,bytes);return cb_system_reply(responder.frame,sizeof(h)+bytes);
}
__attribute__((noreturn)) void _start(void){io=cb_acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);memory=cb_acquire(CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID);static const char *names[]={"self","begin","end","status","revoke_uid"};publication=cb_publish(&client,CB_SESSIONS_NAME,(struct cb_interface_id)CB_SESSIONS_INTERFACE_ID,names,5);
    for(;;){uint64_t bytes;CB_REQUIRE(cb_system_receive(input,sizeof(input),&bytes,0)==CB_OK && bytes>=sizeof(struct cb_delivery_header));struct cb_delivery_header d;cb_copy(&d,input,sizeof(d));CB_REQUIRE(d.context_bytes<=bytes-sizeof(d) && d.call.payload_bytes==bytes-sizeof(d)-d.context_bytes);const unsigned char *payload=input+sizeof(d)+d.context_bytes;
        if(d.call.flags==(CB_CALL_PROVIDER_DELIVERY|CB_CALL_SYSTEM_EVENT)){if(d.call.operation==CB_IO_OWNER_EXIT && d.call.payload_bytes==8){uint64_t owner;cb_copy(&owner,payload,8);if(owner==d.call.handle)forget(owner);}else if(d.call.operation==CB_HANDLE_RELEASE && !d.call.handle){while(sessions){(void)close_session(sessions->info.id,0);dispose(sessions);}cb_system_release(publication);cb_exit(0);}continue;}
        struct cb_admission caller;cb_status_t status=CB_DENIED;size_t result=0;struct cb_session info={0};struct cb_session_status state={0};const void *output=&info;
        if(d.call.flags!=CB_CALL_PROVIDER_DELIVERY || d.call.handle!=publication || (d.call.payload_bytes && !((d.call.operation==CB_SESSIONS_STATUS || d.call.operation==CB_SESSIONS_REVOKE_UID || d.call.operation==CB_SESSIONS_END) && d.call.payload_bytes==8)) || !cb_admission_parse(input+sizeof(d),d.context_bytes,&caller))goto reply;
        status=query(caller.process,0,0,&info);if(status)goto reply;
        if(d.call.operation==CB_SESSIONS_SELF)result=sizeof(info);
        else if(d.call.operation==CB_SESSIONS_BEGIN){struct session *parent=find(info.id);if(find(caller.process) || (parent && parent->closing)){status=CB_BUSY;goto reply;}struct cb_memory_request r={.bytes=sizeof(struct session)};status=cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&r,sizeof(r),0);if(status)goto reply;struct cb_memory_result allocated;cb_copy(&allocated,cb_result(&client),sizeof(allocated));struct session *s=(void *)(uintptr_t)allocated.address;status=watch(caller.process,1);if(!status)status=query(caller.process,1,0,NULL);if(status){(void)watch(caller.process,0);r=(struct cb_memory_request){.address=(uintptr_t)s};CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,&r,sizeof(r),0)==CB_OK);goto reply;}s->info=(struct cb_session){caller.process,info.id,caller.process,info.uid};s->closing=s->owner_gone=0;s->close_error=CB_OK;s->next=sessions;sessions=s;info=s->info;result=sizeof(info);}
        else if(d.call.operation==CB_SESSIONS_END){status=end_session(caller.process,&info,payload,d.call.payload_bytes,&state,&result);if(result)output=&state;}
        else if(d.call.operation==CB_SESSIONS_REVOKE_UID && d.call.payload_bytes==8){status=management(caller.process);if(!status){uint64_t uid;cb_copy(&uid,payload,8);status=revoke_uid(uid,&state);if(state.closing){output=&state;result=sizeof(state);}}}
        else if(d.call.operation==CB_SESSIONS_STATUS){uint64_t id=info.id;if(d.call.payload_bytes)cb_copy(&id,payload,8);struct session *s=find(id);if(!s)status=CB_NOT_FOUND;else if(info.uid && info.uid!=s->info.uid)status=CB_DENIED;else {status=snapshot(s,&state);if(!status){output=&state;result=sizeof(state);}}}
        else status=CB_BAD_REQUEST;
reply:{cb_status_t sent=respond(&d,status,output,result);CB_REQUIRE(sent==CB_OK || sent==CB_BAD_REQUEST);collect_sessions();}
    }
}
