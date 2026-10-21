#include "../../sdk/stream.h"
#include "../../interfaces/identity.h"
#include "../../interfaces/io.h"
#include "store.h"
#include "../../interfaces/installations.h"
#include "crypto/password.h"
struct transaction {struct transaction *next;uint64_t id,owner,generation;struct id_blob files[3];size_t written[3];};
static struct cb_client client,responder;
static cb_handle_t memory,filesystem,io,publication;
static struct id_store database;
static struct transaction *transactions;
static uint64_t serial;
static const unsigned char *catalog;
static unsigned char incoming[CB_FRAME_LIMIT],output[CB_FRAME_LIMIT];
static void *allocate(void *unused,size_t bytes){(void)unused;if(bytes>SIZE_MAX-sizeof(size_t))return NULL;struct cb_memory_request request={.bytes=bytes+sizeof(size_t)};if(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&request,sizeof(request),0))return NULL;struct cb_memory_result result;cb_copy(&result,cb_result(&client),sizeof(result));size_t *p=(void *)(uintptr_t)result.address;*p=bytes;return p+1;}
static void release(void *unused,void *pointer){(void)unused;if(!pointer)return;size_t *p=(size_t *)pointer-1;volatile unsigned char *v=pointer;for(size_t i=0;i<*p;++i)v[i]=0;struct cb_memory_request request={.address=(uintptr_t)p};CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,&request,sizeof(request),0)==CB_OK);}
static cb_status_t read_file(void *unused,const char *path,struct id_blob *out){
    (void)unused;*out=(struct id_blob){0};cb_status_t status=cb_fs_call(&client,filesystem,CB_FILESYSTEM_STAT,path,0,0);if(status)return status;struct cb_filesystem_stat stat;cb_copy(&stat,cb_result(&client),sizeof(stat));if(stat.directory || stat.bytes>SIZE_MAX)return CB_BAD_REQUEST;
    status=cb_fs_call(&client,filesystem,CB_FILESYSTEM_OPEN,path,CB_FILE_READ,0);if(status)return status;cb_handle_t stream;cb_copy(&stream,cb_result(&client),sizeof(stream));unsigned char *data=stat.bytes?allocate(NULL,stat.bytes):NULL;if(stat.bytes && !data){cb_system_release(stream);return CB_RESOURCE_FAILURE;}
    size_t at=0;while(at<stat.bytes){size_t n=0;status=cb_stream_read(&client,stream,data+at,stat.bytes-at,&n,0);if(status || !n){if(!status)status=CB_RESOURCE_FAILURE;break;}at+=n;}cb_status_t closed=cb_system_release(stream);if(!status)status=closed;if(status)release(NULL,data);else *out=(struct id_blob){data,stat.bytes};return status;
}
static cb_status_t write_file(void *unused,const char *path,const unsigned char *data,size_t bytes){
    (void)unused;cb_status_t status=cb_fs_call(&client,filesystem,CB_FILESYSTEM_OPEN,path,CB_FILE_RECREATE,0);if(status)return status;cb_handle_t stream;cb_copy(&stream,cb_result(&client),sizeof(stream));status=cb_write_all(&client,stream,data,bytes);cb_status_t closed=cb_system_release(stream);return status?status:closed;
}
static cb_status_t watch(uint64_t owner,uint64_t token,unsigned enable){struct cb_io_owner_watch request={owner,token,enable};return cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_OWNER_WATCH,&request,sizeof(request),0);}
static struct transaction *find(uint64_t owner,uint64_t id){for(struct transaction *t=transactions;t;t=t->next)if(t->owner==owner && t->id==id)return t;return NULL;}
static void dispose(struct transaction *t){CB_REQUIRE(watch(t->owner,t->id,0)==CB_OK);struct transaction **at=&transactions;while(*at!=t)at=&(*at)->next;*at=t->next;for(unsigned k=0;k<3;++k)release(NULL,t->files[k].data);release(NULL,t);}
static cb_status_t begin(uint64_t owner,const struct cb_identity_begin *request,uint64_t *id){
    if(database.failed)return CB_RESOURCE_FAILURE;
    if(request->generation==UINT64_MAX)return CB_RESOURCE_FAILURE;
    if(request->bytes[2])return CB_BAD_REQUEST;
    if(request->generation!=id_u64(database.files[0].data+24))return CB_BUSY;
    for(struct transaction *t=transactions;t;t=t->next)if(t->owner==owner)return CB_BUSY;
    if(serial==UINT64_MAX)return CB_RESOURCE_FAILURE;
    struct transaction *t=allocate(NULL,sizeof(*t));if(!t)return CB_RESOURCE_FAILURE;for(size_t i=0;i<sizeof(*t);++i)((unsigned char *)t)[i]=0;t->owner=owner;t->id=++serial;t->generation=request->generation;
    for(unsigned k=0;k<3;++k){uint64_t bytes=request->bytes[k]?request->bytes[k]:database.files[k].bytes;if(bytes<ID_HEADER || bytes>SIZE_MAX)goto failure;t->files[k].bytes=bytes;t->files[k].data=allocate(NULL,bytes);if(!t->files[k].data)goto failure;if(!request->bytes[k]){cb_copy(t->files[k].data,database.files[k].data,bytes);id_put64(t->files[k].data+24,t->generation+1);id_put32(t->files[k].data+48,id_crc(t->files[k].data,bytes,48));t->written[k]=bytes;}}
    cb_status_t status=watch(owner,t->id,1);if(status){for(unsigned k=0;k<3;++k)release(NULL,t->files[k].data);release(NULL,t);return status;}t->next=transactions;transactions=t;*id=t->id;return CB_OK;
failure:for(unsigned k=0;k<3;++k)release(NULL,t->files[k].data);release(NULL,t);return CB_RESOURCE_FAILURE;
}
static cb_status_t credential(struct transaction *t,const struct cb_identity_credential *request){
    if(request->algorithm>1 || (request->algorithm==1 && request->rounds<ID_KDF_MIN_ROUNDS))return CB_BAD_REQUEST;
    if(!request->algorithm && !request->uid)return CB_DENIED;
    struct id_blob *b=&t->files[2];const unsigned char *old=id_find(b,request->uid);if(!old && !request->algorithm)return CB_NOT_FOUND;
    if(!old && b->bytes>SIZE_MAX-96)return CB_RESOURCE_FAILURE;
    size_t bytes=b->bytes+(old?0:96)-(request->algorithm?0:96);unsigned char *data=allocate(NULL,bytes);if(!data)return CB_RESOURCE_FAILURE;cb_copy(data,b->data,ID_HEADER);size_t at=ID_HEADER,out=ID_HEADER;int inserted=0;
    while(at<b->bytes){const unsigned char *r=b->data+at;uint64_t uid=id_u64(r+8);if(!inserted && request->algorithm && request->uid<=uid){unsigned char *entry=data+out;for(unsigned i=0;i<96;++i)entry[i]=0;id_put32(entry,96);id_put32(entry+4,request->algorithm);id_put64(entry+8,request->uid);id_put32(entry+16,request->rounds);id_put32(entry+20,32);id_put32(entry+24,32);cb_copy(entry+32,request->salt,32);cb_copy(entry+64,request->hash,32);out+=96;inserted=1;}if(uid!=request->uid){cb_copy(data+out,r,96);out+=96;}at+=96;}
    if(request->algorithm && !inserted){unsigned char *entry=data+out;for(unsigned i=0;i<96;++i)entry[i]=0;id_put32(entry,96);id_put32(entry+4,1);id_put64(entry+8,request->uid);id_put32(entry+16,request->rounds);id_put32(entry+20,32);id_put32(entry+24,32);cb_copy(entry+32,request->salt,32);cb_copy(entry+64,request->hash,32);out+=96;}
    if(out!=bytes){release(NULL,data);return CB_RESOURCE_FAILURE;}id_put64(data+16,bytes);id_put64(data+32,(bytes-ID_HEADER)/96);id_put32(data+48,id_crc(data,bytes,48));release(NULL,b->data);*b=(struct id_blob){data,bytes};t->written[2]=bytes;return CB_OK;
}
#include "auth.inc"
#include "home.inc"
__attribute__((noreturn)) void _start(const struct cb_startup *packet){
    const void *catalog_data;size_t catalog_bytes;CB_REQUIRE(packet && cb_startup_find(packet,packet->bytes,"catalog.bin",&catalog_data,&catalog_bytes) && cb_install_valid(catalog_data,catalog_bytes));catalog=catalog_data;memory=cb_acquire(CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID);filesystem=cb_acquire(CB_FILESYSTEM_NAME,(struct cb_interface_id)CB_FILESYSTEM_INTERFACE_ID);io=cb_acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);
    database=(struct id_store){.allocate=allocate,.release=release,.read=read_file,.write=write_file};CB_REQUIRE(id_store_load(&database)==CB_OK);
    static const char *names[]={"info","users","groups","begin","stage","commit","abort","credential","self","authenticate","password","home"};publication=cb_publish(&client,CB_IDENTITY_NAME,(struct cb_interface_id)CB_IDENTITY_INTERFACE_ID,names,12);
    for(;;){uint64_t bytes;CB_REQUIRE(cb_system_receive(incoming,sizeof(incoming),&bytes,0)==CB_OK && bytes>=sizeof(struct cb_delivery_header));struct cb_delivery_header d;cb_copy(&d,incoming,sizeof(d));CB_REQUIRE(d.context_bytes<=bytes-sizeof(d) && d.call.payload_bytes==bytes-sizeof(d)-d.context_bytes);const unsigned char *payload=incoming+sizeof(d)+d.context_bytes;
        if(d.call.flags==(CB_CALL_PROVIDER_DELIVERY|CB_CALL_SYSTEM_EVENT)){
            if(d.call.operation==CB_IO_OWNER_EXIT && d.call.payload_bytes==8 && id_equal(&d.call.interface,&(struct cb_interface_id)CB_IO_INTERFACE_ID,sizeof(d.call.interface))){uint64_t owner;cb_copy(&owner,payload,8);struct transaction *t=find(owner,d.call.handle);if(t)dispose(t);}
            else if(d.call.operation==CB_HANDLE_RELEASE && !d.call.handle){while(transactions)dispose(transactions);for(unsigned k=0;k<3;++k)release(NULL,database.files[k].data);cb_system_release(publication);cb_exit(0);}
            continue;
        }
        struct cb_admission caller;cb_status_t status=CB_BAD_REQUEST;size_t result=0;struct cb_credentials who;int admin=0;if(d.call.flags!=CB_CALL_PROVIDER_DELIVERY || d.call.handle!=publication || !cb_admission_parse(incoming+sizeof(d),d.context_bytes,&caller)){status=CB_DENIED;goto reply;}
        if(database.failed && d.call.operation!=CB_IDENTITY_ABORT){status=CB_RESOURCE_FAILURE;goto reply;}
        status=caller_credentials(caller.process,&who,&admin);if(status)goto reply;
        if(d.call.operation>=CB_IDENTITY_BEGIN && d.call.operation<=CB_IDENTITY_CREDENTIAL && d.call.operation!=CB_IDENTITY_ABORT && !admin){status=CB_DENIED;goto reply;}
        status=CB_BAD_REQUEST;
        if(d.call.operation==CB_IDENTITY_SELF && !d.call.payload_bytes){struct cb_reply_header h;cb_copy(&h,client.result,sizeof(h));cb_copy(output,cb_result(&client),h.result_bytes);result=h.result_bytes;status=CB_OK;}
        else if((d.call.operation==CB_IDENTITY_AUTHENTICATE || d.call.operation==CB_IDENTITY_PASSWORD) && d.call.payload_bytes>=sizeof(struct cb_identity_password)){struct cb_identity_password request;cb_copy(&request,payload,sizeof(request));status=CB_BAD_REQUEST;if(request.old_bytes<=256 && request.password_bytes<=256 && d.call.payload_bytes==sizeof(request)+(uint64_t)request.old_bytes+request.password_bytes)status=d.call.operation==CB_IDENTITY_AUTHENTICATE?authenticate(caller.process,&request,payload+sizeof(request)):change_password(caller.process,&who,admin,&request,payload+sizeof(request));}
        else if(d.call.operation==CB_IDENTITY_HOME && d.call.payload_bytes==8){uint64_t uid;cb_copy(&uid,payload,8);status=home(uid,&who,admin,&result);}
        else if(d.call.operation==CB_IDENTITY_INFO && !d.call.payload_bytes){struct cb_identity_info info={.generation=id_u64(database.files[0].data+24),.next_uid=id_u64(database.files[0].data+40),.next_gid=id_u64(database.files[1].data+40),.users=id_u64(database.files[0].data+32),.groups=id_u64(database.files[1].data+32),.healthy=1};cb_copy(output,&info,sizeof(info));result=sizeof(info);status=CB_OK;}
        else if((d.call.operation==CB_IDENTITY_USERS || d.call.operation==CB_IDENTITY_GROUPS) && d.call.payload_bytes==8){uint64_t cursor;cb_copy(&cursor,payload,8);struct id_blob *b=&database.files[d.call.operation==CB_IDENTITY_USERS?0:1];if(cursor>b->bytes)goto reply;size_t n=b->bytes-cursor;if(n>4096)n=4096;status=CB_OK;if(n){uint64_t next=cursor+n;cb_copy(output,&next,8);cb_copy(output+8,b->data+cursor,n);result=n+8;}}
        else if(d.call.operation==CB_IDENTITY_BEGIN && d.call.payload_bytes==sizeof(struct cb_identity_begin)){struct cb_identity_begin request;cb_copy(&request,payload,sizeof(request));uint64_t id;status=begin(caller.process,&request,&id);if(!status){cb_copy(output,&id,8);result=8;}}
        else if(d.call.operation==CB_IDENTITY_STAGE && d.call.payload_bytes>=sizeof(struct cb_identity_stage)){struct cb_identity_stage request;cb_copy(&request,payload,sizeof(request));struct transaction *t=find(caller.process,request.transaction);if(!t){status=CB_BAD_HANDLE;goto reply;}if(request.reserved || request.file>1 || request.offset!=t->written[request.file])goto reply;size_t n=d.call.payload_bytes-sizeof(request);if(n>t->files[request.file].bytes-t->written[request.file])goto reply;cb_copy(t->files[request.file].data+t->written[request.file],payload+sizeof(request),n);t->written[request.file]+=n;status=CB_OK;}
        else if(d.call.operation==CB_IDENTITY_CREDENTIAL && d.call.payload_bytes==sizeof(struct cb_identity_credential)){struct cb_identity_credential request;cb_copy(&request,payload,sizeof(request));struct transaction *t=find(caller.process,request.transaction);status=request.uid==0 && who.uid?CB_DENIED:t?credential(t,&request):CB_BAD_HANDLE;}
        else if((d.call.operation==CB_IDENTITY_COMMIT || d.call.operation==CB_IDENTITY_ABORT) && d.call.payload_bytes==8){uint64_t id;cb_copy(&id,payload,8);struct transaction *t=find(caller.process,id);if(!t){status=CB_BAD_HANDLE;goto reply;}if(d.call.operation==CB_IDENTITY_ABORT){dispose(t);status=CB_OK;}else {if(t->generation!=id_u64(database.files[0].data+24)){status=CB_BUSY;goto reply;}for(unsigned k=0;k<3;++k)if(t->written[k]!=t->files[k].bytes)goto reply;status=id_store_commit(&database,t->files);if(!status){uint64_t generation=id_u64(database.files[0].data+24);cb_copy(output,&generation,8);result=8;dispose(t);}}}
reply:{cb_status_t sent=cb_respond(&responder,&d,status,output,result);id_wipe(incoming,sizeof(incoming));CB_REQUIRE(sent==CB_OK || sent==CB_BAD_REQUEST);}
    }
}
