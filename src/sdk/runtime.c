#include "runtime.h"
struct cb_runtime_timer {struct cb_runtime_timer *next;uint64_t id,deadline;void (*callback)(void *);void *payload;};
static cb_status_t failure(struct cb_runtime *r,cb_status_t status,struct cb_client *c){
    if(status){r->failure=status;r->failure_bytes=0;if(c && c->bytes<=sizeof(r->failure_frame)){r->failure_bytes=c->bytes;cb_copy(r->failure_frame,c->result,c->bytes);}}return status;
}
static cb_status_t acquire(struct cb_runtime *r,cb_handle_t *out,const char *name,struct cb_interface_id id){if(*out)return CB_OK;return failure(r,cb_system_get_handle(name,cb_length(name),&id,1,0,out),NULL);}
static cb_status_t invoke(struct cb_runtime *r,cb_handle_t cap,struct cb_interface_id id,unsigned op,const void *payload,size_t bytes,uint64_t deadline){r->client.bytes=0;return failure(r,cb_invoke(&r->client,cap,id,op,payload,bytes,deadline),&r->client);}
static cb_status_t result(struct cb_runtime *r,void *out,size_t bytes){struct cb_reply_header h;cb_copy(&h,r->client.result,sizeof(h));if(r->client.bytes<sizeof(h) || h.version.bytes!=sizeof(h) || h.version.major!=1 || h.result_bytes!=bytes || !cb_frame_fits(sizeof(h),h.result_bytes,h.error_bytes,r->client.bytes))return failure(r,CB_BAD_REQUEST,NULL);cb_copy(out,cb_result(&r->client),bytes);return CB_OK;}
static void *pages(void *context,size_t n){struct cb_runtime *r=context;if(acquire(r,&r->memory,CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID))return NULL;struct cb_memory_request q={.bytes=n};struct cb_memory_result value;if(invoke(r,r->memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&q,sizeof(q),0) || result(r,&value,sizeof(value)))return NULL;return (void *)(uintptr_t)value.address;}
static int unpages(void *context,void *p,size_t n){(void)n;struct cb_runtime *r=context;struct cb_memory_request q={.address=(uintptr_t)p};return !invoke(r,r->memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,&q,sizeof(q),0);}
void cb_runtime_init(struct cb_runtime *r){*r=(struct cb_runtime){0};cb_heap_init(&r->heap,r,pages,unpages);}
cb_status_t cb_runtime_close(struct cb_runtime *r,cb_handle_t h){return failure(r,cb_system_release(h),NULL);}
static cb_status_t io(struct cb_runtime *r,unsigned op,const void *p,size_t n,void *out,size_t bytes){cb_status_t s=acquire(r,&r->io,CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);if(!s)s=invoke(r,r->io,(struct cb_interface_id)CB_IO_INTERFACE_ID,op,p,n,0);return s?s:result(r,out,bytes);}
cb_status_t cb_runtime_now(struct cb_runtime *r,uint64_t *out){return out?io(r,CB_IO_NOW_NS,NULL,0,out,8):CB_BAD_REQUEST;}
cb_status_t cb_runtime_utc(struct cb_runtime *r,struct cb_io_utc *out){return out?io(r,CB_IO_UTC,NULL,0,out,sizeof(*out)):CB_BAD_REQUEST;}
cb_status_t cb_runtime_delay(struct cb_runtime *r,uint64_t ns){uint64_t now,ready;cb_status_t s=cb_runtime_now(r,&now);if(s || !ns)return s;if(ns>UINT64_MAX-now)return failure(r,CB_BAD_REQUEST,NULL);s=cb_system_wait(NULL,0,now+ns,&ready);return s==CB_TIMEOUT?CB_OK:failure(r,s,NULL);}
cb_status_t cb_runtime_random(struct cb_runtime *r,void *out,size_t bytes){if(bytes && !out)return CB_BAD_REQUEST;for(size_t at=0;at<bytes;){uint64_t n=bytes-at>64?64:bytes-at;cb_status_t s=io(r,CB_IO_RANDOM,&n,8,(unsigned char *)out+at,n);if(s){for(size_t i=0;i<bytes;++i)((volatile unsigned char *)out)[i]=0;return s;}at+=n;}return CB_OK;}
cb_status_t cb_runtime_open(struct cb_runtime *r,const char *cwd,const char *path,unsigned mode,cb_handle_t *out){if(!cwd || !path || !out || cwd[0]!='/')return CB_BAD_REQUEST;char canonical[4096];if(!cb_path(canonical,sizeof(canonical),cwd,path))return CB_BAD_REQUEST;cb_status_t s=acquire(r,&r->filesystem,CB_FILESYSTEM_NAME,(struct cb_interface_id)CB_FILESYSTEM_INTERFACE_ID);if(s)return s;r->client.bytes=0;s=failure(r,cb_fs_call(&r->client,r->filesystem,CB_FILESYSTEM_OPEN,canonical,mode,0),&r->client);return s?s:result(r,out,sizeof(*out));}
cb_status_t cb_runtime_read(struct cb_runtime *r,cb_handle_t cap,void *out,size_t bytes,size_t *actual,unsigned flags){if(!actual || (bytes && !out))return CB_BAD_REQUEST;*actual=0;struct cb_stream_read q={bytes>CB_STREAM_CHUNK?CB_STREAM_CHUNK:bytes,flags};if(!bytes)return CB_OK;cb_status_t s=invoke(r,cap,(struct cb_interface_id)CB_STREAM_INTERFACE_ID,CB_STREAM_READ,&q,sizeof(q),0);if(s)return s;struct cb_reply_header h;cb_copy(&h,r->client.result,sizeof(h));if(h.result_bytes>q.bytes || h.result_bytes>SIZE_MAX)return failure(r,CB_BAD_REQUEST,NULL);s=result(r,out,h.result_bytes);if(!s)*actual=h.result_bytes;return s;}
cb_status_t cb_runtime_write(struct cb_runtime *r,cb_handle_t cap,const void *data,size_t bytes,size_t *actual,unsigned flags){if(!actual || (bytes && !data))return CB_BAD_REQUEST;*actual=0;if(!bytes)return CB_OK;unsigned char p[sizeof(struct cb_stream_write)+CB_STREAM_CHUNK];struct cb_stream_write q={flags};size_t n=bytes>CB_STREAM_CHUNK?CB_STREAM_CHUNK:bytes;cb_copy(p,&q,sizeof(q));cb_copy(p+sizeof(q),data,n);cb_status_t s=invoke(r,cap,(struct cb_interface_id)CB_STREAM_INTERFACE_ID,CB_STREAM_WRITE,p,sizeof(q)+n,0);uint64_t accepted;if(!s)s=result(r,&accepted,8);if(!s && accepted>n)s=failure(r,CB_BAD_REQUEST,NULL);if(!s)*actual=accepted;return s;}
cb_status_t cb_runtime_write_all(struct cb_runtime *r,cb_handle_t cap,const void *data,size_t bytes){for(size_t at=0;at<bytes;){size_t n;cb_status_t s=cb_runtime_write(r,cap,(const unsigned char *)data+at,bytes-at,&n,0);if(s)return s;if(!n)return failure(r,CB_RESOURCE_FAILURE,NULL);at+=n;}return CB_OK;}
cb_status_t cb_runtime_load(struct cb_runtime *r,const char *cwd,const char *path,unsigned char **out,size_t *bytes){
    if(!out || !bytes)return CB_BAD_REQUEST;
    *out=NULL;*bytes=0;cb_handle_t file;cb_status_t s=cb_runtime_open(r,cwd,path,CB_FILE_READ,&file);if(s)return s;
    size_t capacity=4096,used=0;unsigned char *p=cb_heap_alloc(&r->heap,capacity);if(!p)s=CB_RESOURCE_FAILURE;
    while(!s){if(used==capacity-1){if(capacity>SIZE_MAX/2){s=CB_RESOURCE_FAILURE;break;}unsigned char *next=cb_heap_realloc(&r->heap,p,capacity*2);if(!next){s=CB_RESOURCE_FAILURE;break;}p=next;capacity*=2;}
        size_t n;s=cb_runtime_read(r,file,p+used,capacity-used-1,&n,0);if(s || !n)break;used+=n;}
    cb_status_t closed=cb_runtime_close(r,file);if(!s)s=closed;if(s){cb_heap_free(&r->heap,p);return s;}p[used]=0;*out=p;*bytes=used;return CB_OK;
}
cb_status_t cb_runtime_launch(struct cb_runtime *r,const char *name,size_t argc,const char *const *argv,const char *cwd,const cb_handle_t streams[3],uint64_t *pid){
    if(!name || !cwd || !pid || (argc && !argv))return CB_BAD_REQUEST;
    unsigned char payload[CB_FRAME_LIMIT-1024];struct cb_application_launch_streams q={.base={.version={sizeof(q),1,1},.name_bytes=cb_length(name),.argc=argc,.cwd_bytes=cb_length(cwd)}};
    if(streams)for(unsigned i=0;i<3;++i)q.streams[i]=streams[i];
    size_t used=sizeof(q);if(q.base.name_bytes>sizeof(payload)-used)return CB_BAD_REQUEST;cb_copy(payload+used,name,q.base.name_bytes);used+=q.base.name_bytes;
    for(size_t i=0;i<argc;++i){if(!argv[i])return CB_BAD_REQUEST;size_t n=cb_length(argv[i]);if(n>=sizeof(payload)-used)return CB_BAD_REQUEST;++n;cb_copy(payload+used,argv[i],n);used+=n;q.base.arguments_bytes+=n;}
    if(q.base.cwd_bytes>sizeof(payload)-used)return CB_BAD_REQUEST;
    cb_copy(payload+used,cwd,q.base.cwd_bytes);used+=q.base.cwd_bytes;cb_copy(payload,&q,sizeof(q));
    if(!cb_application_launch_valid(payload,used))return CB_BAD_REQUEST;
    cb_status_t s=acquire(r,&r->applications,CB_APPLICATIONS_NAME,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID);if(!s)s=invoke(r,r->applications,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_LAUNCH,payload,used,0);return s?s:result(r,pid,8);
}
cb_status_t cb_runtime_child(struct cb_runtime *r,unsigned operation,uint64_t pid,uint64_t deadline,struct cb_application_status *out){if(operation!=CB_APPLICATION_WAIT && operation!=CB_APPLICATION_INFO && operation!=CB_APPLICATION_RELEASE && operation!=CB_APPLICATION_STOP && operation!=CB_APPLICATION_TERMINATE)return CB_BAD_REQUEST;struct cb_application_ref q={pid};cb_status_t s=acquire(r,&r->applications,CB_APPLICATIONS_NAME,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID);if(!s)s=invoke(r,r->applications,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,operation,&q,sizeof(q),deadline);if(!s && (operation==CB_APPLICATION_WAIT || operation==CB_APPLICATION_INFO)){if(!out)return CB_BAD_REQUEST;s=result(r,out,sizeof(*out));}return s;}
cb_status_t cb_runtime_submit(struct cb_runtime *r,struct cb_client *c,cb_handle_t cap,struct cb_interface_id id,unsigned op,const void *p,size_t n){return failure(r,cb_submit(c,cap,id,op,p,n),NULL);}
cb_status_t cb_runtime_collect(struct cb_runtime *r,struct cb_client *c){return failure(r,cb_collect(c),c);}
cb_status_t cb_runtime_timer(struct cb_runtime *r,uint64_t deadline,void (*callback)(void *),void *payload,uint64_t *id){if(!deadline || !callback || !id || r->timer_serial==UINT64_MAX)return CB_BAD_REQUEST;struct cb_runtime_timer *t=cb_heap_alloc(&r->heap,sizeof(*t));if(!t)return CB_RESOURCE_FAILURE;*t=(struct cb_runtime_timer){.id=++r->timer_serial,.deadline=deadline,.callback=callback,.payload=payload};struct cb_runtime_timer **at=&r->timers;while(*at && (*at)->deadline<=deadline)at=&(*at)->next;t->next=*at;*at=t;*id=t->id;return CB_OK;}
cb_status_t cb_runtime_cancel_timer(struct cb_runtime *r,uint64_t id){struct cb_runtime_timer **at=&r->timers;while(*at && (*at)->id!=id)at=&(*at)->next;if(!*at)return CB_BAD_REQUEST;struct cb_runtime_timer *t=*at;*at=t->next;cb_heap_free(&r->heap,t);return CB_OK;}
cb_status_t cb_runtime_poll(struct cb_runtime *r,const struct cb_wait_item *items,size_t count,uint64_t deadline,size_t budget,uint64_t *ready){
    if(!budget || !ready || (count && !items) || (!count && !deadline && !r->timers))return CB_BAD_REQUEST;
    *ready=UINT64_MAX;uint64_t now,limit=deadline;if(r->timers && (!limit || r->timers->deadline<limit))limit=r->timers->deadline;
    /* Even overdue timers validate/observe the wait set. Ready I/O does not
     * starve due callbacks, and callbacks do not hide revoked/invalid items. */
    cb_status_t s=cb_system_wait(items,count,limit,ready);if(s!=CB_OK && s!=CB_TIMEOUT)return failure(r,s,NULL);
    int io_ready=s==CB_OK;
    size_t dispatched=0;while(dispatched<budget && r->timers){s=cb_runtime_now(r,&now);if(s)return s;struct cb_runtime_timer *t=r->timers;if(t->deadline>now)break;r->timers=t->next;void (*callback)(void *)=t->callback;void *payload=t->payload;cb_heap_free(&r->heap,t);++dispatched;callback(payload);}
    return dispatched || io_ready?CB_OK:CB_TIMEOUT;
}
cb_status_t cb_runtime_destroy(struct cb_runtime *r){if(r->client.pending.serial)return CB_BUSY;while(r->timers)cb_runtime_cancel_timer(r,r->timers->id);if(!cb_heap_destroy(&r->heap))return CB_BUSY;cb_handle_t *caps[]={&r->filesystem,&r->applications,&r->io,&r->memory};for(unsigned i=0;i<4;++i)if(*caps[i]){cb_status_t s=cb_runtime_close(r,*caps[i]);if(s)return s;*caps[i]=0;}return CB_OK;}
