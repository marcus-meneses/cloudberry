#include "bindings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
static JSClassID stream_class;
struct stream {struct js_host *host;cb_handle_t cap;unsigned busy;};
enum {JOB_SLEEP,JOB_READ,JOB_WRITE,JOB_CHILD,JOB_PROVIDER};
struct js_job {struct js_job *next;unsigned kind;JSValue resolve,reject,resource;struct stream *stream;unsigned char *data;size_t bytes,offset;uint64_t deadline,pid;struct cb_client *client;cb_js_decode_fn decode;cb_js_done_fn done;};
static struct js_host *host(JSContext *ctx){return JS_GetContextOpaque(ctx);}
static void clear_failure(struct js_host *h){h->os->failure=CB_OK;h->os->failure_bytes=0;}
#define bytes_value cb_js_bytes

static JSValue os_error(JSContext *ctx,cb_status_t status,int frame){
    struct js_host *h=host(ctx);unsigned char saved[CB_FRAME_LIMIT];size_t n=frame?h->os->failure_bytes:0;if(n>sizeof(saved))n=0;if(n)memcpy(saved,h->os->failure_frame,n);unsigned char *p=saved;
    JSValue e=JS_NewError(ctx);if(JS_IsException(e))return e;
    JS_SetPropertyStr(ctx,e,"name",JS_NewString(ctx,"HandleError"));JS_SetPropertyStr(ctx,e,"message",JS_NewString(ctx,"Cloudberry handle operation failed"));JS_SetPropertyStr(ctx,e,"status",JS_NewUint32(ctx,status));
    JS_SetPropertyStr(ctx,e,"frame",bytes_value(ctx,p,n));
    if(n>=sizeof(struct cb_reply_header)){
        struct cb_reply_header reply;memcpy(&reply,p,sizeof(reply));
        if(reply.version.bytes==sizeof(reply) && cb_frame_fits(sizeof(reply),reply.result_bytes,reply.error_bytes,n)){
            JS_SetPropertyStr(ctx,e,"requestNamespace",JS_NewBigUint64(ctx,reply.request.namespace_id));JS_SetPropertyStr(ctx,e,"requestId",JS_NewBigUint64(ctx,reply.request.serial));JS_SetPropertyStr(ctx,e,"outcome",JS_NewUint32(ctx,reply.outcome));JS_SetPropertyStr(ctx,e,"providerEpoch",JS_NewBigUint64(ctx,reply.provider_epoch));
            if(reply.error_bytes>=sizeof(struct cb_error_header)){
                struct cb_error_header error;size_t start=sizeof(reply)+reply.result_bytes;memcpy(&error,p+start,sizeof(error));
                if(error.version.bytes==sizeof(error) && cb_frame_fits(sizeof(error),error.original_payload_bytes,error.trace_bytes,reply.error_bytes)){
                    JS_SetPropertyStr(ctx,e,"payload",bytes_value(ctx,p+start+sizeof(error),error.original_payload_bytes));JS_SetPropertyStr(ctx,e,"trace",bytes_value(ctx,p+start+sizeof(error)+error.original_payload_bytes,error.trace_bytes));
                }
            }
        }
    }
    return e;
}
JSValue cb_js_error(JSContext *ctx,cb_status_t status){return os_error(ctx,status,1);}
static JSValue throw_status(JSContext *ctx,cb_status_t status,int frame){JSValue error=os_error(ctx,status,frame);return JS_IsException(error)?error:JS_Throw(ctx,error);}
static const char *path_string(JSContext *ctx,JSValueConst value){size_t n;const char *s=JS_ToCStringLen(ctx,&n,value);if(s && strlen(s)!=n){JS_FreeCString(ctx,s);JS_ThrowTypeError(ctx,"path/name contains NUL");return NULL;}return s;}
static void stream_finalizer(JSRuntime *rt,JSValue value){(void)rt;struct stream *s=JS_GetOpaque(value,stream_class);if(s){if(s->cap)cb_runtime_close(s->host->os,s->cap);free(s);}}
static struct stream *get_stream(JSContext *ctx,JSValueConst value){struct stream *s=JS_GetOpaque2(ctx,value,stream_class);if(s && !s->cap){JS_ThrowTypeError(ctx,"stream is closed");return NULL;}return s;}
static JSValue new_stream(JSContext *ctx,cb_handle_t cap){
    JSValue value=JS_NewObjectClass(ctx,stream_class);if(JS_IsException(value)){cb_runtime_close(host(ctx)->os,cap);return value;}
    struct stream *s=calloc(1,sizeof(*s));if(!s){JS_FreeValue(ctx,value);cb_runtime_close(host(ctx)->os,cap);return JS_ThrowOutOfMemory(ctx);}s->host=host(ctx);s->cap=cap;JS_SetOpaque(value,s);return value;
}
static JSValue stream_close(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)argc;(void)argv;struct stream *s=JS_GetOpaque2(ctx,self,stream_class);if(!s)return JS_EXCEPTION;if(!s->cap)return JS_UNDEFINED;if(s->busy)return throw_status(ctx,CB_BUSY,0);clear_failure(host(ctx));cb_status_t status=cb_runtime_close(host(ctx)->os,s->cap);if(status)return throw_status(ctx,status,1);s->cap=0;return JS_UNDEFINED;
}
static struct js_job *new_job(JSContext *ctx,JSValue *promise,unsigned kind){
    size_t waiting=0;for(struct js_job *p=host(ctx)->jobs;p;p=p->next)if(p->kind!=JOB_SLEEP)++waiting;
    if(kind!=JOB_SLEEP && waiting>=(CB_FRAME_LIMIT-512)/sizeof(struct cb_wait_item)){*promise=throw_status(ctx,CB_BUSY,0);return NULL;}
    struct js_job *j=calloc(1,sizeof(*j));if(!j){*promise=JS_ThrowOutOfMemory(ctx);return NULL;}
    JSValue funcs[2];*promise=JS_NewPromiseCapability(ctx,funcs);if(JS_IsException(*promise)){free(j);return NULL;}j->kind=kind;j->resolve=funcs[0];j->reject=funcs[1];j->resource=JS_UNDEFINED;j->next=host(ctx)->jobs;host(ctx)->jobs=j;return j;
}
static void finish_job(struct js_host *h,struct js_job *j,JSValue result,int failed){
    if(JS_IsException(result)){result=JS_GetException(h->ctx);failed=1;}
    if(j->client && j->client->pending.serial){struct cb_abandon_result result;cb_abandon(j->client,&result);}
    struct js_job **at=&h->jobs;while(*at!=j)at=&(*at)->next;*at=j->next;
    if(j->stream)--j->stream->busy;
    if(j->done)j->done(h->ctx,j->resource);
    JSValue ret=JS_Call(h->ctx,failed?j->reject:j->resolve,JS_UNDEFINED,1,&result);if(JS_IsException(ret)){JSValue error=JS_GetException(h->ctx);cb_js_dump(h->ctx,error);JS_FreeValue(h->ctx,error);++h->failures;}JS_FreeValue(h->ctx,ret);JS_FreeValue(h->ctx,result);JS_FreeValue(h->ctx,j->resolve);JS_FreeValue(h->ctx,j->reject);JS_FreeValue(h->ctx,j->resource);free(j->client);free(j->data);free(j);
}
JSValue cb_js_request(JSContext *ctx,JSValueConst owner,cb_handle_t cap,struct cb_interface_id id,unsigned operation,const void *payload,size_t bytes,cb_js_decode_fn decode,cb_js_done_fn done){
    JSValue promise;struct js_job *j=new_job(ctx,&promise,JOB_PROVIDER);if(!j)return promise;
    j->resource=JS_DupValue(ctx,owner);j->decode=decode;j->done=done;j->client=calloc(1,sizeof(*j->client));
    if(!j->client){finish_job(host(ctx),j,os_error(ctx,CB_RESOURCE_FAILURE,0),1);return promise;}
    clear_failure(host(ctx));cb_status_t status=cb_runtime_submit(host(ctx)->os,j->client,cap,id,operation,payload,bytes);
    if(status)finish_job(host(ctx),j,os_error(ctx,status,1),1);
    return promise;
}
void cb_js_cancel(JSContext *ctx,JSValueConst owner){
    struct js_host *h=host(ctx);for(struct js_job *j=h->jobs,*next;j;j=next){next=j->next;if(j->kind==JOB_PROVIDER && JS_VALUE_GET_PTR(j->resource)==JS_VALUE_GET_PTR(owner))finish_job(h,j,os_error(ctx,CB_REVOKED,0),1);}
}
static int copy_bytes(JSContext *ctx,JSValueConst value,unsigned char **out,size_t *length){
    const unsigned char *data;const char *string=NULL;JSValue buffer=JS_UNDEFINED;size_t offset=0,n=0,bpe=0,capacity;
    if(JS_IsString(value)){string=JS_ToCStringLen(ctx,&n,value);if(!string)return -1;data=(const unsigned char *)string;}
    else {buffer=JS_GetTypedArrayBuffer(ctx,value,&offset,&n,&bpe);if(JS_IsException(buffer))return -1;data=JS_GetArrayBuffer(ctx,&capacity,buffer);if(!data || bpe!=1 || offset>capacity || n>capacity-offset){JS_FreeValue(ctx,buffer);JS_ThrowTypeError(ctx,"expected string or byte typed array");return -1;}data+=offset;}
    unsigned char *p=malloc(n?n:1);if(p && n)memcpy(p,data,n);JS_FreeCString(ctx,string);JS_FreeValue(ctx,buffer);if(!p){JS_ThrowOutOfMemory(ctx);return -1;}*out=p;*length=n;return 0;
}
static JSValue stream_read(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    struct stream *s=get_stream(ctx,self);if(!s)return JS_EXCEPTION;if(s->busy)return throw_status(ctx,CB_BUSY,0);
    uint64_t n=CB_STREAM_CHUNK;if(argc && JS_ToIndex(ctx,&n,argv[0]))return JS_EXCEPTION;if(n>CB_STREAM_CHUNK)n=CB_STREAM_CHUNK;
    unsigned char *data=malloc(n?n:1);if(!data)return JS_ThrowOutOfMemory(ctx);JSValue promise;struct js_job *j=new_job(ctx,&promise,JOB_READ);if(!j){free(data);return promise;}j->stream=s;++s->busy;j->resource=JS_DupValue(ctx,self);j->data=data;j->bytes=n;return promise;
}
static JSValue stream_write(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    struct stream *s=get_stream(ctx,self);if(!s)return JS_EXCEPTION;if(s->busy)return throw_status(ctx,CB_BUSY,0);unsigned char *data;size_t n;if(!argc)return JS_ThrowTypeError(ctx,"write requires bytes/string");if(copy_bytes(ctx,argv[0],&data,&n))return JS_EXCEPTION;JSValue promise;struct js_job *j=new_job(ctx,&promise,JOB_WRITE);if(!j){free(data);return promise;}j->stream=s;++s->busy;j->resource=JS_DupValue(ctx,self);j->data=data;j->bytes=n;return promise;
}
static JSValue open_file(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;if(!argc)return JS_ThrowTypeError(ctx,"open(path, mode='r')");const char *path=path_string(ctx,argv[0]);if(!path)return JS_EXCEPTION;const char *mode=argc>1?path_string(ctx,argv[1]):NULL;if(argc>1 && !mode){JS_FreeCString(ctx,path);return JS_EXCEPTION;}
    unsigned flags=CB_FILE_READ;if(mode){if(!strcmp(mode,"w"))flags=CB_FILE_RECREATE;else if(!strcmp(mode,"a"))flags=CB_FILE_APPEND;else if(strcmp(mode,"r")){JS_FreeCString(ctx,path);JS_FreeCString(ctx,mode);return JS_ThrowTypeError(ctx,"mode must be r, w or a");}}
    struct js_host *h=host(ctx);clear_failure(h);cb_handle_t cap;cb_status_t status=cb_runtime_open(h->os,cb_libc_cwd(),path,flags,&cap);JS_FreeCString(ctx,path);JS_FreeCString(ctx,mode);return status?throw_status(ctx,status,1):new_stream(ctx,cap);
}
static cb_status_t fs_request(JSContext *ctx,unsigned op,const char *path,uint64_t cursor){
    struct js_host *h=host(ctx);struct cb_runtime *r=h->os;clear_failure(h);
    if(!r->filesystem){struct cb_interface_id id=CB_FILESYSTEM_INTERFACE_ID;cb_status_t status=cb_system_get_handle(CB_FILESYSTEM_NAME,sizeof(CB_FILESYSTEM_NAME)-1,&id,1,0,&r->filesystem);if(status)return status;}
    char canonical[4096];if(!cb_path(canonical,sizeof(canonical),cb_libc_cwd(),path))return CB_BAD_REQUEST;r->client.bytes=0;cb_status_t status=cb_fs_call(&r->client,r->filesystem,op,canonical,0,cursor);if(status && r->client.bytes<=sizeof(r->failure_frame)){r->failure=status;r->failure_bytes=r->client.bytes;memcpy(r->failure_frame,r->client.result,r->client.bytes);}return status;
}
static JSValue list_directory(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;JSValue default_path=argc?JS_UNDEFINED:JS_NewString(ctx,cb_libc_cwd());const char *path=path_string(ctx,argc?argv[0]:default_path);JS_FreeValue(ctx,default_path);if(!path)return JS_EXCEPTION;JSValue array=JS_NewArray(ctx);if(JS_IsException(array)){JS_FreeCString(ctx,path);return array;}uint64_t cursor=0;uint32_t count=0;
    for(;;){cb_status_t status=fs_request(ctx,CB_FILESYSTEM_LIST,path,cursor);if(status){JS_FreeValue(ctx,array);JS_FreeCString(ctx,path);return throw_status(ctx,status,1);}struct cb_reply_header reply;struct cb_client *c=&host(ctx)->os->client;memcpy(&reply,c->result,sizeof(reply));if(!reply.result_bytes)break;struct cb_filesystem_entry e;if(reply.result_bytes!=sizeof(e)){JS_FreeValue(ctx,array);JS_FreeCString(ctx,path);return throw_status(ctx,CB_BAD_REQUEST,0);}memcpy(&e,cb_result(c),sizeof(e));if(e.cursor<=cursor || e.name_bytes>sizeof(e.name) || !e.name_bytes || count==UINT32_MAX){JS_FreeValue(ctx,array);JS_FreeCString(ctx,path);return throw_status(ctx,CB_BAD_REQUEST,0);}cursor=e.cursor;
        JSValue entry=JS_NewObject(ctx);if(JS_IsException(entry)){JS_FreeValue(ctx,array);JS_FreeCString(ctx,path);return entry;}JS_SetPropertyStr(ctx,entry,"name",JS_NewStringLen(ctx,e.name,e.name_bytes));JS_SetPropertyStr(ctx,entry,"directory",JS_NewBool(ctx,e.directory!=0));if(JS_SetPropertyUint32(ctx,array,count++,entry)<0){JS_FreeValue(ctx,array);JS_FreeCString(ctx,path);return JS_EXCEPTION;}}
    JS_FreeCString(ctx,path);return array;
}
static JSValue stat_path(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;if(!argc)return JS_ThrowTypeError(ctx,"stat(path)");const char *path=path_string(ctx,argv[0]);if(!path)return JS_EXCEPTION;cb_status_t status=fs_request(ctx,CB_FILESYSTEM_STAT,path,0);JS_FreeCString(ctx,path);if(status)return throw_status(ctx,status,1);struct cb_reply_header reply;struct cb_filesystem_stat stat;struct cb_client *c=&host(ctx)->os->client;memcpy(&reply,c->result,sizeof(reply));if(reply.result_bytes!=sizeof(stat))return throw_status(ctx,CB_BAD_REQUEST,0);memcpy(&stat,cb_result(c),sizeof(stat));JSValue value=JS_NewObject(ctx);if(JS_IsException(value))return value;JS_SetPropertyStr(ctx,value,"bytes",JS_NewBigUint64(ctx,stat.bytes));JS_SetPropertyStr(ctx,value,"directory",JS_NewBool(ctx,stat.directory!=0));return value;
}
static JSValue make_directory(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){(void)self;if(!argc)return JS_ThrowTypeError(ctx,"mkdir(path)");const char *path=path_string(ctx,argv[0]);if(!path)return JS_EXCEPTION;cb_status_t status=fs_request(ctx,CB_FILESYSTEM_MKDIR,path,0);JS_FreeCString(ctx,path);return status?throw_status(ctx,status,1):JS_UNDEFINED;}
static JSValue read_file(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;if(!argc)return JS_ThrowTypeError(ctx,"readFile(path)");const char *path=path_string(ctx,argv[0]);if(!path)return JS_EXCEPTION;struct js_host *h=host(ctx);clear_failure(h);unsigned char *data;size_t n;cb_status_t status=cb_runtime_load(h->os,cb_libc_cwd(),path,&data,&n);JS_FreeCString(ctx,path);if(status)return throw_status(ctx,status,1);JSValue v=bytes_value(ctx,data,n);cb_heap_free(&h->os->heap,data);return v;
}
static JSValue write_file(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;if(argc<2)return JS_ThrowTypeError(ctx,"writeFile(path,data,append=false)");const char *path=path_string(ctx,argv[0]);if(!path)return JS_EXCEPTION;unsigned char *data;size_t n;if(copy_bytes(ctx,argv[1],&data,&n)){JS_FreeCString(ctx,path);return JS_EXCEPTION;}int append=argc>2?JS_ToBool(ctx,argv[2]):0;if(append<0){free(data);JS_FreeCString(ctx,path);return JS_EXCEPTION;}
    struct js_host *h=host(ctx);clear_failure(h);cb_handle_t cap;cb_status_t status=cb_runtime_open(h->os,cb_libc_cwd(),path,append?CB_FILE_APPEND:CB_FILE_RECREATE,&cap);JS_FreeCString(ctx,path);if(!status){status=cb_runtime_write_all(h->os,cap,data,n);if(status){JSValue error=os_error(ctx,status,1);cb_runtime_close(h->os,cap);free(data);return JS_Throw(ctx,error);}status=cb_runtime_close(h->os,cap);}free(data);return status?throw_status(ctx,status,1):JS_UNDEFINED;
}
static JSValue clock_now(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){(void)self;(void)argc;(void)argv;uint64_t ns;clear_failure(host(ctx));cb_status_t s=cb_runtime_now(host(ctx)->os,&ns);return s?throw_status(ctx,s,1):JS_NewBigUint64(ctx,ns);}
static JSValue random_bytes(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;uint64_t n;if(!argc || JS_ToIndex(ctx,&n,argv[0]))return JS_ThrowTypeError(ctx,"randomBytes(length)");if(n>SIZE_MAX)return JS_ThrowRangeError(ctx,"length overflow");unsigned char *p=malloc(n?n:1);if(!p)return JS_ThrowOutOfMemory(ctx);clear_failure(host(ctx));cb_status_t s=cb_runtime_random(host(ctx)->os,p,n);JSValue result=s?os_error(ctx,s,1):bytes_value(ctx,p,n);memset(p,0,n);free(p);return s?JS_Throw(ctx,result):result;
}
static JSValue sleep_ms(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;double ms;if(!argc)return JS_ThrowTypeError(ctx,"sleep(milliseconds)");if(JS_ToFloat64(ctx,&ms,argv[0]))return JS_EXCEPTION;if(!isfinite(ms) || ms<0 || ms>9007199254.0)return JS_ThrowRangeError(ctx,"invalid delay");uint64_t now;clear_failure(host(ctx));cb_status_t s=cb_runtime_now(host(ctx)->os,&now);if(s)return throw_status(ctx,s,1);uint64_t duration=(uint64_t)(ms*1000000.0);if(duration>UINT64_MAX-now)return JS_ThrowRangeError(ctx,"deadline overflow");JSValue promise;struct js_job *j=new_job(ctx,&promise,JOB_SLEEP);if(j)j->deadline=now+duration;return promise;
}
static JSValue pipe_pair(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;(void)argc;(void)argv;struct js_host *h=host(ctx);struct cb_interface_id id=CB_STREAM_INTERFACE_ID;cb_handle_t cap;clear_failure(h);cb_status_t s=cb_system_get_handle(CB_STREAM_NAME,sizeof(CB_STREAM_NAME)-1,&id,1,0,&cap);if(s)return throw_status(ctx,s,0);
    struct cb_client *c=calloc(1,sizeof(*c));if(!c){cb_system_release(cap);return JS_ThrowOutOfMemory(ctx);}s=cb_invoke(c,cap,id,CB_STREAM_PIPE,NULL,0,0);if(s && c->bytes<=sizeof(h->os->failure_frame)){h->os->failure_bytes=c->bytes;memcpy(h->os->failure_frame,c->result,c->bytes);}struct cb_stream_pair pair={0};if(!s){struct cb_reply_header r;memcpy(&r,c->result,sizeof(r));if(r.result_bytes!=sizeof(pair))s=CB_BAD_REQUEST;else memcpy(&pair,cb_result(c),sizeof(pair));}cb_system_release(cap);free(c);if(s)return throw_status(ctx,s,1);JSValue v=JS_NewObject(ctx);if(JS_IsException(v)){cb_system_release(pair.reader);cb_system_release(pair.writer);return v;}JS_SetPropertyStr(ctx,v,"reader",new_stream(ctx,pair.reader));JS_SetPropertyStr(ctx,v,"writer",new_stream(ctx,pair.writer));return v;
}
/* Launch is synchronous admission; completion is a submitted provider request.
 * All child arguments and inherited endpoints are marshalled by the SDK. */
static JSValue exec_app(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;if(!argc)return JS_ThrowTypeError(ctx,"exec(name,args=[],options={})");if(argc>1 && !JS_IsArray(ctx,argv[1]))return JS_ThrowTypeError(ctx,"args must be an array");const char *name=path_string(ctx,argv[0]);if(!name)return JS_EXCEPTION;
    JSValue length=argc>1?JS_GetPropertyStr(ctx,argv[1],"length"):JS_NewInt32(ctx,0);uint64_t n=0;if(JS_ToIndex(ctx,&n,length) || n>INT_MAX-1){JS_FreeValue(ctx,length);JS_FreeCString(ctx,name);return JS_ThrowRangeError(ctx,"argument count overflow");}JS_FreeValue(ctx,length);
    const char **args=calloc((size_t)n+1,sizeof(*args));if(!args){JS_FreeCString(ctx,name);return JS_ThrowOutOfMemory(ctx);}args[0]=name;int failed=0;for(uint64_t i=0;i<n;++i){JSValue value=JS_GetPropertyUint32(ctx,argv[1],i);args[i+1]=path_string(ctx,value);JS_FreeValue(ctx,value);if(!args[i+1]){failed=1;break;}}
    JSValue options=argc>2?argv[2]:JS_UNDEFINED;const char *cwd=NULL;JSValue cv=JS_IsUndefined(options)?JS_UNDEFINED:JS_GetPropertyStr(ctx,options,"cwd");if(!JS_IsUndefined(cv))cwd=path_string(ctx,cv);if(JS_IsException(cv) || (!JS_IsUndefined(cv) && !cwd))failed=1;JS_FreeValue(ctx,cv);
    cb_handle_t caps[3]={0};const char *keys[]={"stdin","stdout","stderr"};
    for(unsigned i=0;!failed && i<3;++i){JSValue v=JS_IsUndefined(options)?JS_UNDEFINED:JS_GetPropertyStr(ctx,options,keys[i]);if(JS_IsUndefined(v)){struct stream *s=JS_GetOpaque(host(ctx)->standard[i],stream_class);if(s)caps[i]=s->cap;}else if(!JS_IsNull(v)){struct stream *s=get_stream(ctx,v);if(!s)failed=1;else caps[i]=s->cap;}JS_FreeValue(ctx,v);}
    struct js_host *h=host(ctx);JSValue promise=JS_EXCEPTION;
    if(!failed){
        struct js_job *j=new_job(ctx,&promise,JOB_CHILD);
        if(j){
            j->client=calloc(1,sizeof(*j->client));
            if(!j->client)finish_job(h,j,os_error(ctx,CB_RESOURCE_FAILURE,0),1);
            else {
                clear_failure(h);
                cb_status_t status=cb_runtime_launch(h->os,name,n+1,args,cwd?cwd:cb_libc_cwd(),caps,&j->pid);
                if(!status){
                    JS_SetPropertyStr(ctx,promise,"process",JS_NewBigUint64(ctx,j->pid));
                    struct cb_application_ref ref={j->pid};
                    status=cb_runtime_submit(h->os,j->client,h->os->applications,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_WAIT,&ref,sizeof(ref));
                }
                if(status){JSValue error=os_error(ctx,status,1);if(j->pid)JS_SetPropertyStr(ctx,error,"process",JS_NewBigUint64(ctx,j->pid));finish_job(h,j,error,1);}
            }
        }
    }
    for(uint64_t i=1;i<=n;++i)JS_FreeCString(ctx,args[i]);
    free(args);JS_FreeCString(ctx,name);JS_FreeCString(ctx,cwd);return promise;
}
static int process_id(JSContext *ctx,JSValueConst value,uint64_t *id){
    if(!JS_IsBigInt(ctx,value)){JS_ThrowTypeError(ctx,"process ID must be BigInt");return -1;}
    const char *text=JS_ToCString(ctx,value);if(!text)return -1;uint64_t result=0;int valid=*text!=0;
    for(const char *at=text;*at;++at){if(*at<'0' || *at>'9' || result>(UINT64_MAX-(unsigned)(*at-'0'))/10){valid=0;break;}result=result*10+(unsigned)(*at-'0');}
    JS_FreeCString(ctx,text);if(!valid || !result){JS_ThrowRangeError(ctx,"invalid process ID");return -1;}*id=result;return 0;
}
static JSValue wait_child(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;uint64_t pid;if(!argc)return JS_ThrowTypeError(ctx,"wait(process)");if(process_id(ctx,argv[0],&pid))return JS_EXCEPTION;
    JSValue promise;struct js_job *j=new_job(ctx,&promise,JOB_CHILD);if(!j)return promise;j->pid=pid;j->client=calloc(1,sizeof(*j->client));if(!j->client){finish_job(host(ctx),j,os_error(ctx,CB_RESOURCE_FAILURE,0),1);return promise;}
    /* Acquire/authorize the provider without creating another process. */
    struct cb_runtime *r=host(ctx)->os;clear_failure(host(ctx));struct cb_application_status info;cb_status_t status=cb_runtime_child(r,CB_APPLICATION_INFO,pid,0,&info);
    if(!status){struct cb_application_ref ref={pid};status=cb_runtime_submit(r,j->client,r->applications,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_WAIT,&ref,sizeof(ref));}
    JS_SetPropertyStr(ctx,promise,"process",JS_NewBigUint64(ctx,pid));if(status){JSValue error=os_error(ctx,status,1);JS_SetPropertyStr(ctx,error,"process",JS_NewBigUint64(ctx,pid));finish_job(host(ctx),j,error,1);}return promise;
}
static JSValue release_child(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;uint64_t pid;if(!argc)return JS_ThrowTypeError(ctx,"release(process)");if(process_id(ctx,argv[0],&pid))return JS_EXCEPTION;clear_failure(host(ctx));cb_status_t status=cb_runtime_child(host(ctx)->os,CB_APPLICATION_RELEASE,pid,0,NULL);return status?throw_status(ctx,status,1):JS_UNDEFINED;
}
static JSValue print_values(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic){
    (void)self;FILE *out=magic?stderr:stdout;for(int i=0;i<argc;++i){size_t n;const char *s=JS_ToCStringLen(ctx,&n,argv[i]);if(!s)return JS_EXCEPTION;int ok=(!i || fputc(' ',out)!=EOF) && fwrite(s,1,n,out)==n;JS_FreeCString(ctx,s);if(!ok)return throw_status(ctx,CB_RESOURCE_FAILURE,0);}if(fputc('\n',out)==EOF || fflush(out))return throw_status(ctx,CB_RESOURCE_FAILURE,0);return JS_UNDEFINED;
}
void cb_js_dump(JSContext *ctx,JSValueConst error){const char *s=JS_ToCString(ctx,error);fprintf(stderr,"%s\n",s?s:"<exception unavailable>");JS_FreeCString(ctx,s);JSValue stack=JS_GetPropertyStr(ctx,error,"stack");if(!JS_IsException(stack) && !JS_IsUndefined(stack)){s=JS_ToCString(ctx,stack);if(s){fprintf(stderr,"%s\n",s);JS_FreeCString(ctx,s);}}JS_FreeValue(ctx,stack);fflush(stderr);}
static const JSCFunctionListEntry stream_methods[]={JS_CFUNC_DEF("read",1,stream_read),JS_CFUNC_DEF("write",1,stream_write),JS_CFUNC_DEF("close",0,stream_close)};
static const JSCFunctionListEntry system_methods[]={JS_CFUNC_DEF("open",2,open_file),JS_CFUNC_DEF("list",1,list_directory),JS_CFUNC_DEF("stat",1,stat_path),JS_CFUNC_DEF("mkdir",1,make_directory),JS_CFUNC_DEF("readFile",1,read_file),JS_CFUNC_DEF("writeFile",3,write_file),JS_CFUNC_DEF("nowNs",0,clock_now),JS_CFUNC_DEF("randomBytes",1,random_bytes),JS_CFUNC_DEF("sleep",1,sleep_ms),JS_CFUNC_DEF("pipe",0,pipe_pair),JS_CFUNC_DEF("exec",3,exec_app),JS_CFUNC_DEF("wait",1,wait_child),JS_CFUNC_DEF("release",1,release_child)};
int cb_js_bind(struct js_host *h,int argc,char **argv){
    h->os=cb_libc_runtime();JS_SetContextOpaque(h->ctx,h);if(!stream_class)JS_NewClassID(&stream_class);JSClassDef def={.class_name="HandleStream",.finalizer=stream_finalizer};if(JS_NewClass(h->rt,stream_class,&def)<0)return -1;
    JSValue proto=JS_NewObject(h->ctx);JS_SetPropertyFunctionList(h->ctx,proto,stream_methods,sizeof(stream_methods)/sizeof(*stream_methods));JS_SetClassProto(h->ctx,stream_class,proto);
    JSValue global=JS_GetGlobalObject(h->ctx),system=JS_NewObject(h->ctx),console=JS_NewObject(h->ctx);JS_SetPropertyFunctionList(h->ctx,system,system_methods,sizeof(system_methods)/sizeof(*system_methods));JS_DefinePropertyValueStr(h->ctx,system,"cwd",JS_NewString(h->ctx,cb_libc_cwd()),JS_PROP_ENUMERABLE);JSValue args=JS_NewArray(h->ctx);for(int i=0;i<argc;++i)JS_SetPropertyUint32(h->ctx,args,i,JS_NewString(h->ctx,argv[i]));JS_SetPropertyStr(h->ctx,system,"args",args);
    const char *names[]={"stdin","stdout","stderr"};for(unsigned i=0;i<3;++i){cb_handle_t cap=cb_standard(i);h->standard[i]=cap?new_stream(h->ctx,cap):JS_NULL;JS_SetPropertyStr(h->ctx,system,names[i],JS_DupValue(h->ctx,h->standard[i]));}
    JS_SetPropertyStr(h->ctx,console,"log",JS_NewCFunctionMagic(h->ctx,print_values,"log",1,JS_CFUNC_generic_magic,0));JS_SetPropertyStr(h->ctx,console,"error",JS_NewCFunctionMagic(h->ctx,print_values,"error",1,JS_CFUNC_generic_magic,1));JS_SetPropertyStr(h->ctx,global,"console",console);int devices=cb_js_devices(h->ctx,system);JS_SetPropertyStr(h->ctx,global,"system",system);JS_FreeValue(h->ctx,global);return devices;
}
int cb_js_step(struct js_host *h,int block){
    unsigned budget=64;JSContext *ctx=h->ctx;while(budget-- && JS_IsJobPending(h->rt)){JSContext *job;int n=JS_ExecutePendingJob(h->rt,&job);if(n<0){JSValue error=JS_GetException(job);cb_js_dump(job,error);JS_FreeValue(job,error);++h->failures;}}
    uint64_t now=0;clear_failure(h);cb_status_t clock_status=h->jobs?cb_runtime_now(h->os,&now):CB_OK;
    for(struct js_job *j=h->jobs,*next;j;j=next){next=j->next;cb_status_t status=CB_OK;JSValue value=JS_UNDEFINED;int complete=0;clear_failure(h);
        if(j->kind==JOB_SLEEP){if(clock_status)status=clock_status;else if(now>=j->deadline)complete=1;}
        else if(j->kind==JOB_READ){size_t n=0;status=cb_runtime_read(h->os,j->stream->cap,j->data,j->bytes,&n,CB_STREAM_NONBLOCK);if(!status){value=bytes_value(ctx,j->data,n);complete=1;}}
        else if(j->kind==JOB_WRITE){size_t n=0;if(j->offset<j->bytes)status=cb_runtime_write(h->os,j->stream->cap,j->data+j->offset,j->bytes-j->offset,&n,CB_STREAM_NONBLOCK);if(!status){j->offset+=n;if(j->offset==j->bytes){value=JS_NewBigUint64(ctx,j->offset);complete=1;}else if(!n)status=CB_RESOURCE_FAILURE;}}
        else if(j->kind==JOB_PROVIDER){
            status=cb_runtime_collect(h->os,j->client);
            if(status==CB_WOULD_BLOCK && !j->client->collected)continue;
            if(!status || (status==CB_INPUT_RESYNC && j->client->collected)){value=j->decode(ctx,j->resource,j->client,status);status=CB_OK;complete=1;}
        }
        else if(j->kind==JOB_CHILD){
            status=cb_runtime_collect(h->os,j->client);
            if(status==CB_WOULD_BLOCK && !j->client->collected)continue;
            if(!status){
                struct cb_reply_header reply;memcpy(&reply,j->client->result,sizeof(reply));struct cb_application_status child;
                if(reply.result_bytes!=sizeof(child))status=CB_BAD_REQUEST;
                else {memcpy(&child,cb_result(j->client),sizeof(child));value=JS_NewObject(ctx);JS_SetPropertyStr(ctx,value,"process",JS_NewBigUint64(ctx,child.process));JS_SetPropertyStr(ctx,value,"exitCode",JS_NewBigUint64(ctx,child.exit_code));JS_SetPropertyStr(ctx,value,"state",JS_NewUint32(ctx,child.state));JS_SetPropertyStr(ctx,value,"faultVector",JS_NewBigUint64(ctx,child.fault_vector));complete=1;}
            }
        }
        if(status==CB_WOULD_BLOCK && (j->kind==JOB_READ || j->kind==JOB_WRITE))continue;
        if(status){JS_FreeValue(ctx,value);value=os_error(ctx,status,1);complete=1;}
        if(complete){
            if(JS_IsException(value)){value=JS_GetException(ctx);status=CB_RESOURCE_FAILURE;}
            if(j->kind==JOB_CHILD){
                if(!status){cb_status_t released=cb_runtime_child(h->os,CB_APPLICATION_RELEASE,j->pid,0,NULL);if(released){JS_FreeValue(ctx,value);value=os_error(ctx,released,1);status=released;}}
                if(status)JS_SetPropertyStr(ctx,value,"process",JS_NewBigUint64(ctx,j->pid));
            }
            finish_job(h,j,value,status!=CB_OK);
        }
    }
    if(!block || JS_IsJobPending(h->rt) || !h->jobs)return 0;
    size_t count=0;uint64_t deadline=0;for(struct js_job *j=h->jobs;j;j=j->next){if(j->kind==JOB_SLEEP){if(!deadline || j->deadline<deadline)deadline=j->deadline;}else ++count;}
    struct cb_wait_item *items=calloc(count?count:1,sizeof(*items));if(!items){++h->failures;return -1;}size_t at=0;
    for(struct js_job *j=h->jobs;j;j=j->next){if(j->kind==JOB_SLEEP)continue;if(j->kind==JOB_CHILD || j->kind==JOB_PROVIDER)items[at++]=(struct cb_wait_item){.kind=CB_WAIT_REQUEST,.request=j->client->pending};else items[at++]=(struct cb_wait_item){.kind=j->kind==JOB_READ?CB_WAIT_READ:CB_WAIT_WRITE,.handle=j->stream->cap};}
    uint64_t ready;clear_failure(h);cb_status_t status=cb_runtime_poll(h->os,items,count,deadline,64,&ready);free(items);if(status && status!=CB_TIMEOUT){/* An invalid wait set cannot be retried forever. Reject pending work. */while(h->jobs)finish_job(h,h->jobs,os_error(ctx,status,1),1);++h->failures;return -1;}return 0;
}
int cb_js_drain(struct js_host *h){while(h->jobs || JS_IsJobPending(h->rt))if(cb_js_step(h,1)<0)return -1;return h->failures?-1:0;}
void cb_js_unbind(struct js_host *h){while(h->jobs){struct js_job *j=h->jobs;if(j->client && j->client->pending.serial){struct cb_abandon_result result;cb_abandon(j->client,&result);}finish_job(h,j,JS_UNDEFINED,1);}for(unsigned i=0;i<3;++i)JS_FreeValue(h->ctx,h->standard[i]);}
