#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _start identity_entry
#include "../handlers/identity/identity.c"
#undef _start
static long live,budget=-1;static unsigned watched;static int reject_watch;
cb_status_t cb_system_invoke(const void *p,uint64_t bytes,void *out,uint64_t capacity,uint64_t *received){
    struct cb_call_header call;memcpy(&call,p,sizeof(call));assert(bytes==sizeof(call)+call.payload_bytes && capacity>=sizeof(struct cb_reply_header)+16);struct cb_reply_header h={.version={sizeof(h),1,0}};struct cb_memory_result result={0};
    if(call.handle==1){struct cb_memory_request r;memcpy(&r,(const unsigned char *)p+sizeof(call),sizeof(r));if(call.operation==CB_MEMORY_ALLOCATE){void *data=budget?calloc(1,r.bytes):NULL;if(budget>0)--budget;if(data){++live;result=(struct cb_memory_result){(uintptr_t)data,r.bytes};h.result_bytes=sizeof(result);}else h.status=CB_RESOURCE_FAILURE;}else {assert(call.operation==CB_MEMORY_FREE && live);--live;free((void *)(uintptr_t)r.address);}}
    else {assert(call.handle==2 && call.operation==CB_IO_OWNER_WATCH);struct cb_io_owner_watch w;memcpy(&w,(const unsigned char *)p+sizeof(call),sizeof(w));assert(w.process && w.token);if(reject_watch)h.status=CB_RESOURCE_FAILURE;else if(w.enable)++watched;else {assert(watched);--watched;}}
    memcpy(out,&h,sizeof(h));if(h.result_bytes)memcpy((unsigned char *)out+sizeof(h),&result,sizeof(result));*received=sizeof(h)+h.result_bytes;return h.status;
}
cb_status_t cb_system_request_release(struct cb_request_id id){(void)id;assert(0);return CB_BAD_REQUEST;}
__attribute__((noreturn)) void cb_exit(uint64_t code){fprintf(stderr,"unexpected provider exit: %llu\n",(unsigned long long)code);abort();}
int main(int argc,char **argv){assert(argc==2);memory=1;io=2;database.allocate=allocate;database.release=release;
    for(unsigned k=0;k<3;++k){char path[4096];snprintf(path,sizeof(path),"%s/%s",argv[1],k==0?"users.bin":k==1?"groups.bin":"credentials.bin");FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long bytes=ftell(f);rewind(f);database.files[k]=(struct id_blob){allocate(NULL,bytes),(size_t)bytes};assert(database.files[k].data && fread(database.files[k].data,1,bytes,f)==(size_t)bytes);fclose(f);}
    long baseline=live;struct cb_identity_begin request={.generation=1};uint64_t one,two;
    for(long limit=0;limit<4;++limit){budget=limit;assert(begin(7,&request,&one)==CB_RESOURCE_FAILURE && live==baseline && !transactions && !watched);}budget=-1;reject_watch=1;assert(begin(7,&request,&one)==CB_RESOURCE_FAILURE && live==baseline && !transactions);reject_watch=0;
    assert(begin(7,&request,&one)==CB_OK && begin(8,&request,&two)==CB_OK && watched==2 && one!=two);assert(!find(7,two) && !find(8,one));assert(begin(7,&request,&two)==CB_BUSY);
    struct cb_identity_credential update={.uid=0,.algorithm=0};assert(credential(find(7,one),&update)==CB_DENIED);update=(struct cb_identity_credential){.uid=1,.algorithm=1,.rounds=600000};assert(credential(find(7,one),&update)==CB_OK && id_find(&find(7,one)->files[2],0) && id_find(&find(7,one)->files[2],1));
    /* Owner-exit cleanup uses the same authenticated owner/token lookup. */
    dispose(find(7,one));assert(!find(7,one) && watched==1);dispose(transactions);assert(!transactions && !watched && live==baseline);
    for(unsigned k=0;k<3;++k)release(NULL,database.files[k].data);
    assert(!live);puts("PASS identity transactions: OOM/watch rollback, owner isolation, root credential protection, private hash updates and abandoned staging cleanup");
}
