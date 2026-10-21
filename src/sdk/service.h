#ifndef CLOUDBERRY_SDK_SERVICE_H
#define CLOUDBERRY_SDK_SERVICE_H
#include "system.h"
#include "../interfaces/admission.h"
#include "../interfaces/startup.h"
#define CB_REQUIRE(x) do {if(!(x))cb_exit(__LINE__);}while(0)
static inline size_t cb_length(const char *s){size_t n=0;while(s[n])++n;return n;}
static inline void cb_copy(void *d,const void *s,size_t n){cb_startup_copy(d,s,n);}
struct cb_client {unsigned char frame[CB_FRAME_LIMIT],result[CB_FRAME_LIMIT];uint64_t bytes,attempt;struct cb_request_id pending;cb_handle_t pending_handle;unsigned collected;};
static inline cb_status_t cb_invoke(struct cb_client *c,cb_handle_t handle,struct cb_interface_id interface,unsigned op,const void *p,size_t n,uint64_t deadline){
    if(n>CB_FRAME_LIMIT-sizeof(struct cb_call_header)-512)return CB_BAD_REQUEST;
    struct cb_call_header h=cb_call(handle,interface,op,n);h.deadline_ns=deadline;if(c->pending.serial){struct cb_call_header previous;cb_copy(&previous,c->frame,sizeof(previous));if(c->pending_handle!=handle || previous.operation!=op || previous.payload_bytes!=n)return CB_BUSY;for(size_t i=0;i<sizeof(interface);++i)if(previous.interface.bytes[i]!=interface.bytes[i])return CB_BUSY;for(size_t i=0;i<n;++i)if(c->frame[sizeof(h)+i]!=((const unsigned char *)p)[i])return CB_BUSY;h.request=c->pending;h.attempt=c->attempt+1;c->attempt=h.attempt;}cb_copy(c->frame,&h,sizeof(h));cb_copy(c->frame+sizeof(h),p,n);
    for(size_t i=0;i<sizeof(struct cb_reply_header);++i)c->result[i]=0;
    cb_status_t status=cb_system_invoke(c->frame,sizeof(h)+n,c->result,sizeof(c->result),&c->bytes);
    struct cb_reply_header reply;cb_copy(&reply,c->result,sizeof(reply));
    if(reply.request.serial){cb_status_t forgotten=cb_system_request_release(reply.request);if(forgotten==CB_BUSY){c->pending=reply.request;c->pending_handle=handle;c->attempt=h.attempt;}else {c->pending=(struct cb_request_id){0};if(forgotten!=CB_OK)return forgotten;}}
    return status;
}
/* Submission keeps one owned request per client. Collect is nonblocking and
 * never resubmits; wait-any can observe this request alongside pipe readiness. */
static inline cb_status_t cb_submit(struct cb_client *c,cb_handle_t handle,struct cb_interface_id interface,unsigned op,const void *p,size_t n){
    if(c->pending.serial)return CB_BUSY;
    if(n>CB_FRAME_LIMIT-sizeof(struct cb_call_header)-512)return CB_BAD_REQUEST;
    struct cb_call_header h=cb_call(handle,interface,op,n);cb_copy(c->frame,&h,sizeof(h));cb_copy(c->frame+sizeof(h),p,n);
    struct cb_request_id id={0};cb_status_t status=cb_system_submit(c->frame,sizeof(h)+n,&id);
    if(!status){c->pending=id;c->pending_handle=handle;c->attempt=0;c->bytes=0;c->collected=0;}return status;
}
static inline cb_status_t cb_collect(struct cb_client *c){
    c->collected=0;c->bytes=0;
    if(!c->pending.serial)return CB_BAD_REQUEST;
    cb_status_t status=cb_system_collect(c->pending,c->result,sizeof(c->result),&c->bytes);
    if(status!=CB_OK)return status;
    if(c->bytes<sizeof(struct cb_reply_header))return CB_BAD_REQUEST;
    struct cb_reply_header reply;cb_copy(&reply,c->result,sizeof(reply));
    if(c->bytes<sizeof(reply) || reply.version.bytes!=sizeof(reply) || reply.version.major!=1 || reply.request.namespace_id!=c->pending.namespace_id || reply.request.serial!=c->pending.serial || reply.status>=CB_STATUS_COUNT || !cb_frame_fits(sizeof(reply),reply.result_bytes,reply.error_bytes,c->bytes) || sizeof(reply)+reply.result_bytes+reply.error_bytes!=c->bytes)return CB_BAD_REQUEST;
    c->collected=1;status=reply.status;
    cb_status_t forgotten=cb_system_request_release(c->pending);
    if(forgotten==CB_OK)c->pending=(struct cb_request_id){0};
    return forgotten!=CB_OK?forgotten:status;
}
static inline cb_status_t cb_abandon(struct cb_client *c,struct cb_abandon_result *result){
    if(!c->pending.serial)return CB_BAD_REQUEST;
    cb_status_t status=cb_system_abandon(c->pending,result);
    if(!status){c->pending=(struct cb_request_id){0};c->pending_handle=0;c->bytes=0;c->collected=0;}return status;
}
static inline void *cb_result(struct cb_client *c){return c->result+sizeof(struct cb_reply_header);}
static inline cb_handle_t cb_acquire(const char *name,struct cb_interface_id id){cb_handle_t h=0;CB_REQUIRE(cb_system_get_handle(name,cb_length(name),&id,1,0,&h)==CB_OK);return h;}
static inline cb_status_t cb_respond(struct cb_client *c,const struct cb_delivery_header *d,cb_status_t status,const void *p,size_t n){
    struct cb_reply_header h={.version={sizeof(h),1,0},.request=d->call.request,.provider_epoch=d->provider_epoch,.status=status,.outcome=!status?CB_COMMITTED:(status==CB_RESOURCE_FAILURE || status==CB_PROVIDER_FAILED || status==CB_TIMEOUT?CB_OUTCOME_UNKNOWN:CB_NOT_COMMITTED),.result_bytes=status?0:n};
    cb_copy(c->frame,&h,sizeof(h));if(!status)cb_copy(c->frame+sizeof(h),p,n);return cb_system_reply(c->frame,sizeof(h)+h.result_bytes);
}
static inline cb_handle_t cb_publish(struct cb_client *c,const char *name,struct cb_interface_id id,const char *const *names,unsigned count){
    struct cb_publication p={.version={sizeof(p),1,0},.interface=id,.interface_major=1,.name_bytes=cb_length(name)};
    struct cb_description d={.version={sizeof(d),1,0},.interface=id,.interface_major=1,.operation_count=count,.facts_bytes=2};size_t at=sizeof(p)+p.name_bytes+sizeof(d);
    for(unsigned i=0;i<count;++i){size_t n=cb_length(names[i]);struct cb_operation_description o={.version={sizeof(o),1,0},.operation=i+1,.name_bytes=n,.request_schema_bytes=2,.reply_schema_bytes=2};CB_REQUIRE(at+sizeof(o)+n+4<sizeof(c->frame));cb_copy(c->frame+at,&o,sizeof(o));at+=sizeof(o);cb_copy(c->frame+at,names[i],n);at+=n;cb_copy(c->frame+at,"{}{}",4);at+=4;}
    d.operations_bytes=at-sizeof(p)-p.name_bytes-sizeof(d);cb_copy(c->frame+at,"{}",2);at+=2;p.description_bytes=sizeof(d)+d.operations_bytes+2;cb_copy(c->frame,&p,sizeof(p));cb_copy(c->frame+sizeof(p),name,p.name_bytes);cb_copy(c->frame+sizeof(p)+p.name_bytes,&d,sizeof(d));cb_handle_t h=0;CB_REQUIRE(cb_system_publish(c->frame,at,&h)==CB_OK);return h;
}
#endif
