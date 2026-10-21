#include "store.h"
static const char *const paths[]={"/system/config/identity/users.bin","/system/config/identity/groups.bin","/system/config/identity/credentials.bin","/system/config/identity/transaction.bin"};
const char *id_path(unsigned k){return k<4?paths[k]:NULL;}
static void copy(void *d,const void *s,size_t n){unsigned char *a=d;const unsigned char *b=s;for(size_t i=0;i<n;++i)a[i]=b[i];}
static void clear(struct id_store *s,struct id_blob b[3]){for(unsigned k=0;k<3;++k){s->release(s->context,b[k].data);b[k]=(struct id_blob){0};}}
static int journal_valid(const struct id_blob *w,struct id_blob files[3]){
    if(w->bytes<72 || !id_equal(w->data,"CBIDTX01",8) || id_u32(w->data+8)!=1 || id_u32(w->data+12)!=64 || id_u64(w->data+16)!=w->bytes || id_u32(w->data+60) || id_u32(w->data+56)!=id_crc(w->data,w->bytes,56) || !id_equal(w->data+w->bytes-8,"COMMIT01",8))return 0;
    size_t at=64;for(unsigned k=0;k<3;++k){uint64_t bytes=id_u64(w->data+32+k*8);if(bytes>w->bytes-8-at)return 0;files[k]=(struct id_blob){w->data+at,(size_t)bytes};at+=bytes;}
    return at==w->bytes-8 && id_set_valid(files) && id_u64(w->data+24)==id_u64(files[0].data+24);
}
cb_status_t id_store_load(struct id_store *s){
    struct id_blob disk[3]={{0}},wal={0},replay[3];cb_status_t status=s->read(s->context,paths[3],&wal);if(status && status!=CB_NOT_FOUND)return status;
    int valid=!status && journal_valid(&wal,replay),complete=1;for(unsigned k=0;k<3;++k){status=s->read(s->context,paths[k],&disk[k]);if(status)complete=0;}
    complete=complete && id_set_valid(disk);
    if(valid){
        if(complete && id_u64(disk[0].data+24)>id_u64(replay[0].data+24)){status=CB_RESOURCE_FAILURE;goto done;}
        for(unsigned k=0;k<3;++k){status=s->write(s->context,paths[k],replay[k].data,replay[k].bytes);if(status)goto done;}
        status=s->write(s->context,paths[3],NULL,0);if(status)goto done;clear(s,disk);
        for(unsigned k=0;k<3;++k){status=s->read(s->context,paths[k],&disk[k]);if(status)goto done;}complete=id_set_valid(disk);
    }
    status=complete?CB_OK:CB_RESOURCE_FAILURE;if(!status){clear(s,s->files);for(unsigned k=0;k<3;++k){s->files[k]=disk[k];disk[k]=(struct id_blob){0};}s->failed=0;}
done:clear(s,disk);s->release(s->context,wal.data);return status;
}
cb_status_t id_store_commit(struct id_store *s,const struct id_blob files[3]){
    if(s->failed)return CB_RESOURCE_FAILURE;
    if(!id_transition_valid(s->files,files))return CB_BAD_REQUEST;
    size_t bytes=72;for(unsigned k=0;k<3;++k){if(files[k].bytes>SIZE_MAX-bytes)return CB_RESOURCE_FAILURE;bytes+=files[k].bytes;}
    unsigned char *wal=s->allocate(s->context,bytes);if(!wal)return CB_RESOURCE_FAILURE;for(size_t i=0;i<64;++i)wal[i]=0;copy(wal,"CBIDTX01",8);id_put32(wal+8,1);id_put32(wal+12,64);id_put64(wal+16,bytes);id_put64(wal+24,id_u64(files[0].data+24));size_t at=64;
    for(unsigned k=0;k<3;++k){id_put64(wal+32+k*8,files[k].bytes);copy(wal+at,files[k].data,files[k].bytes);at+=files[k].bytes;}copy(wal+at,"COMMIT01",8);id_put32(wal+56,id_crc(wal,bytes,56));
    /* Allocate the new in-memory snapshot before any durable side effect. */
    struct id_blob next[3]={{0}};for(unsigned k=0;k<3;++k){next[k].data=s->allocate(s->context,files[k].bytes);next[k].bytes=files[k].bytes;if(!next[k].data){clear(s,next);s->release(s->context,wal);return CB_RESOURCE_FAILURE;}copy(next[k].data,files[k].data,files[k].bytes);}
    cb_status_t status=s->write(s->context,paths[3],wal,bytes);if(!status)for(unsigned k=0;k<3;++k){status=s->write(s->context,paths[k],files[k].data,files[k].bytes);if(status)break;}if(!status)status=s->write(s->context,paths[3],NULL,0);
    if(status){if(status!=CB_READ_ONLY)s->failed=1;}else {clear(s,s->files);for(unsigned k=0;k<3;++k){s->files[k]=next[k];next[k]=(struct id_blob){0};}}
    clear(s,next);s->release(s->context,wal);return status;
}
