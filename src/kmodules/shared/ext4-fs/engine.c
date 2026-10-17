#include "engine.h"
#include "runtime.h"
#include "../block-util.h"
#include <ext4.h>
#include <ext4_blockdev.h>
#include <ext4_errno.h>
#include <ext4_inode.h>
#include <ext4_oflags.h>
#include <string.h>
struct allocation;
struct ext4_engine {
    struct allocation *allocations;
    struct block_device device;
    struct ext4_blockdev disk;
    struct ext4_blockdev_iface interface;
    char name[20],mount[24];
    unsigned error,poisoned,readonly,mounted,journal;
    uint8_t sector[4096];
};
static uintptr_t heap;
static kernel_surface_call allocate,resize,release;
static struct ext4_engine *active;
static unsigned busy;
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
int ext4_engine_setup(const struct kernel_surfaces *hub,uintptr_t h){
    allocate=kernel_surface_find_version(hub,"mem.calloc",1,0);resize=kernel_surface_find_version(hub,"mem.realloc",1,0);release=kernel_surface_find_version(hub,"mem.free",1,0);heap=h;return allocate && resize && release;
}
int ext4_engine_enter(struct ext4_engine *ctx){if(ctx && ctx->poisoned)return 0;if(__atomic_exchange_n(&busy,1,__ATOMIC_ACQUIRE))return 0;active=ctx;if(ctx)ctx->error=EXT4_OK;return 1;}
void ext4_engine_leave(void){active=NULL;__atomic_store_n(&busy,0,__ATOMIC_RELEASE);}
/* Every library allocation belongs to its mount. No pointer may escape the
 * synchronous engine call. Abort unlinks registry roots, then discards this
 * allocation set without traversing damaged cache/journal state or doing I/O. */
struct allocation {struct allocation *prev,*next;struct ext4_engine *owner;size_t bytes;};
_Static_assert(sizeof(struct allocation)%16==0,"allocator payload alignment");
void *ext4_user_calloc(size_t n,size_t size){
    if(n && size>SIZE_MAX/n)return NULL;
    size_t bytes=n*size;if(bytes>SIZE_MAX-sizeof(struct allocation))return NULL;
    struct kernel_surface_result r=CALL(allocate,heap,1,bytes+sizeof(struct allocation));if(r.status)return NULL;
    struct allocation *p=(void *)r.value;p->bytes=bytes;p->owner=active;if(active){p->next=active->allocations;if(p->next)p->next->prev=p;active->allocations=p;}return p+1;
}
void *ext4_user_malloc(size_t size){return ext4_user_calloc(1,size);}
void ext4_user_free(void *pointer){if(!pointer)return;struct allocation *p=(struct allocation *)pointer-1;if(p->owner){if(p->prev)p->prev->next=p->next;else p->owner->allocations=p->next;if(p->next)p->next->prev=p->prev;}CALL(release,heap,p,0);}
void *ext4_user_realloc(void *pointer,size_t size){
    if(!pointer)return ext4_user_malloc(size);
    if(!size){ext4_user_free(pointer);return NULL;}
    if(size>SIZE_MAX-sizeof(struct allocation))return NULL;
    struct allocation *p=(struct allocation *)pointer-1;
    struct kernel_surface_result r=CALL(resize,heap,p,size+sizeof(*p));if(r.status)return NULL;p=(void *)r.value;p->bytes=size;
    if(p->owner){if(p->prev)p->prev->next=p;else p->owner->allocations=p;if(p->next)p->next->prev=p;}return p+1;
}
int ext4_engine_abort_enter(struct ext4_engine *ctx){if(__atomic_exchange_n(&busy,1,__ATOMIC_ACQUIRE))return 0;active=ctx;return 1;}
void ext4_engine_disconnect(struct ext4_engine *ctx){ctx->poisoned=1;ctx->error=EXT4_IO;}
int ext4_engine_abort(struct ext4_engine *ctx){
    ctx->poisoned=1;
    if(ext4_device_forget(ctx->name)!=EOK)return 0;
    while(ctx->allocations)ext4_user_free(ctx->allocations+1);
    ext4_user_free(ctx);active=NULL;return 1;
}
static unsigned error_code(int r){switch(r){case EOK:return EXT4_OK;case ENOMEM:return EXT4_MEMORY;case ENOENT:return EXT4_NOT_FOUND;case EINVAL:case EFBIG:return EXT4_ARGUMENT;case ENOTSUP:return EXT4_UNSUPPORTED;case EROFS:return EXT4_READ_ONLY;case ENOSPC:return EXT4_NO_SPACE;case EEXIST:return EXT4_EXISTS;case ENOTEMPTY:return EXT4_NOT_EMPTY;case ENOTDIR:case EISDIR:return EXT4_TYPE;default:return EXT4_IO;}}
void cloudberry_ext4_failure(int error){if(active){active->poisoned=1;active->error=error_code(error);}}
unsigned ext4_engine_error(struct ext4_engine *ctx){return ctx->error;}
static int done(struct ext4_engine *ctx,int r){if(ctx->poisoned)return 0;ctx->error=error_code(r);return r==EOK;}
static int disk_open(struct ext4_blockdev *disk){(void)disk;return EOK;}
static int disk_close(struct ext4_blockdev *disk){(void)disk;return EOK;}
static int disk_io(struct ext4_blockdev *disk,void *buffer,uint64_t block,uint32_t count,int writing){
    struct ext4_engine *ctx=disk->bdif->p_user;const struct block_device *d=&ctx->device;
    if(ctx->poisoned || (writing && ctx->readonly) || block>d->blocks || count>d->blocks-block){cloudberry_ext4_failure(EIO);return EIO;}
    uint8_t *p=buffer;while(count){if(ctx->poisoned)return EIO;uint32_t n=d->max_transfer_bytes/d->block_bytes;if(n>count)n=count;size_t bytes=(size_t)n*d->block_bytes;
        kernel_surface_call call=writing?d->write:d->read;struct kernel_surface_result r=call(d->cookie,block,n,(uintptr_t)p,bytes,0);
        if(r.status || r.value!=bytes){cloudberry_ext4_failure(EIO);return EIO;}
        /* Conservative first implementation: every write is stable before the
         * next library write. This orders journal data, commit and checkpoint. */
        if(writing && d->flush(d->cookie,0,0,0,0,0).status){cloudberry_ext4_failure(EIO);return EIO;}
        p+=bytes;block+=n;count-=n;
    }return EOK;
}
static int disk_read(struct ext4_blockdev *disk,void *p,uint64_t block,uint32_t n){return disk_io(disk,p,block,n,0);}
static int disk_write(struct ext4_blockdev *disk,const void *p,uint64_t block,uint32_t n){return disk_io(disk,(void *)p,block,n,1);}
static void hex(char *out,uint64_t value){for(unsigned n=0;n<16;++n){unsigned digit=(value>>((15-n)*4))&15;out[n]=digit<10?'0'+digit:'a'+digit-10;}out[16]=0;}
static char *path_for(struct ext4_engine *ctx,const char *path){
    if(!path || *path!='/'){ctx->error=EXT4_ARGUMENT;return NULL;}
    size_t prefix=strlen(ctx->mount),length=strlen(path);if(length>SIZE_MAX-prefix-1){ctx->error=EXT4_ARGUMENT;return NULL;}
    char *out=ext4_user_malloc(prefix+length+1);if(!out){ctx->error=EXT4_MEMORY;return NULL;}memcpy(out,ctx->mount,prefix);size_t used=prefix;
    const char *p=path;while(*p){while(*p=='/')++p;if(!*p)break;const char *first=p;size_t n=0;while(*p && *p!='/'){++p;if(++n>255){ext4_user_free(out);ctx->error=EXT4_ARGUMENT;return NULL;}}
        if(n==1 && first[0]=='.')continue;
        if(n==2 && first[0]=='.' && first[1]=='.'){if(used>prefix){while(used>prefix && out[used-1]!='/')--used;if(used>prefix)--used;}continue;}
        if(used>prefix)out[used++]='/';
        memcpy(out+used,first,n);used+=n;
    }out[used]=0;return out;
}
struct ext4_engine *ext4_engine_mount(const struct block_device *device,int readonly,uint64_t id,unsigned *error){
    struct ext4_engine *ctx=ext4_user_calloc(1,sizeof(*ctx));if(!ctx){*error=EXT4_MEMORY;return NULL;}active=ctx;block_copy(&ctx->device,device);ctx->readonly=readonly;
    ctx->name[0]='v';hex(ctx->name+1,id);ctx->mount[0]='/';memcpy(ctx->mount+1,ctx->name,17);ctx->mount[18]='/';ctx->mount[19]=0;
    ctx->interface=(struct ext4_blockdev_iface){.open=disk_open,.close=disk_close,.bread=disk_read,.bwrite=disk_write,.ph_bsize=device->block_bytes,.ph_bcnt=device->blocks,.ph_bbuf=ctx->sector,.p_user=ctx};
    ctx->disk=(struct ext4_blockdev){.bdif=&ctx->interface,.part_size=device->blocks*device->block_bytes};
    int r=ext4_device_register(&ctx->disk,ctx->name);if(r)goto fail;
    r=ext4_mount(ctx->name,ctx->mount,readonly);if(r){ext4_device_unregister(ctx->name);goto fail;}ctx->mounted=1;
    if(!readonly){r=ext4_recover(ctx->mount);if(!r)r=ext4_journal_start(ctx->mount);if(!r)ctx->journal=1;if(r || ctx->poisoned)goto retained;
        r=ext4_cache_write_back(ctx->mount,0);if(r)goto retained;}
    *error=EXT4_OK;return ctx;
retained:
    /* Failed initialization with uncertain journal state remains reachable.
     * Suppress further writes; caller retains parent/context until reboot. */
    ctx->poisoned=1;ctx->error=error_code(r?r:EIO);*error=ctx->error;return ctx;
fail:
    *error=ctx->poisoned?ctx->error:error_code(r);ext4_user_free(ctx);active=NULL;return NULL;
}
int ext4_engine_sync(struct ext4_engine *ctx){
    if(ctx->poisoned)return 0;
    if(ctx->readonly)return done(ctx,EOK);
    int r=ext4_cache_flush(ctx->mount);if(!r && ctx->device.flush(ctx->device.cookie,0,0,0,0,0).status){cloudberry_ext4_failure(EIO);r=EIO;}return done(ctx,r);
}
int ext4_engine_unmount(struct ext4_engine *ctx){
    if(!ext4_engine_sync(ctx))return 0;
    int r=ctx->journal?ext4_journal_stop(ctx->mount):EOK;if(!r)ctx->journal=0;
    if(!r)r=ext4_umount(ctx->mount);
    if(!r)r=ext4_device_unregister(ctx->name);
    if(!done(ctx,r))return 0;
    ext4_user_free(ctx);active=NULL;return 1;
}
int ext4_engine_stat(struct ext4_engine *ctx,const char *path,struct ext4_stat *out){
    char *full=path_for(ctx,path);if(!full)return 0;struct ext4_inode inode;uint32_t number;int r=ext4_raw_inode_fill(full,&number,&inode);ext4_user_free(full);
    if(!done(ctx,r))return 0;
    const uint8_t *p=(void *)&inode;*out=(struct ext4_stat){.inode=number,.bytes=disk_u32(p+4)|(uint64_t)disk_u32(p+108)<<32,.mode=disk_u16(p),.flags=disk_u32(p+32)};return 1;
}
struct kernel_surface_result ext4_engine_read(struct ext4_engine *ctx,const char *path,uint64_t offset,void *buffer,size_t bytes){
    char *full=path_for(ctx,path);if(!full)return module_result(0,0);ext4_file file;size_t count=0;int r=ext4_fopen2(&file,full,O_RDONLY);ext4_user_free(full);
    if(!r){if(offset<file.fsize){file.fpos=offset;r=ext4_fread(&file,buffer,bytes,&count);}int closed=ext4_fclose(&file);if(!r)r=closed;}return module_result(count,done(ctx,r));
}
static int parent_exists(char *full){
    char *last=full;for(char *p=full;*p;++p)if(*p=='/')last=p;
    if(!last[1])return EINVAL;
    char saved=last[1];last[1]=0;int r=ext4_inode_exist(full,EXT4_DE_DIR);last[1]=saved;return r;
}
static int grow_to(ext4_file *file,uint64_t target){
    uint8_t zero[4096]={0};while(file->fsize<target){size_t n=target-file->fsize;if(n>sizeof(zero))n=sizeof(zero);file->fpos=file->fsize;size_t done=0;int r=ext4_fwrite(file,zero,n,&done);if(r)return r;if(done!=n)return ENOSPC;}return EOK;
}
struct kernel_surface_result ext4_engine_write(struct ext4_engine *ctx,const char *path,uint64_t offset,const void *buffer,size_t bytes,unsigned flags){
    if(ctx->readonly){ctx->error=EXT4_READ_ONLY;return module_result(0,0);}
    if(!(flags&EXT4_WRITE_APPEND) && (offset>UINT64_C(0x100000000)*ctx->disk.lg_bsize || bytes>UINT64_C(0x100000000)*ctx->disk.lg_bsize-offset)){ctx->error=EXT4_ARGUMENT;return module_result(0,0);}
    char *full=path_for(ctx,path);if(!full)return module_result(0,0);
    int options=O_WRONLY;if(flags&EXT4_WRITE_CREATE)options|=O_CREAT;if(flags&EXT4_WRITE_EXCLUSIVE)options|=O_EXCL;if(flags&EXT4_WRITE_APPEND)options|=O_APPEND;
    ext4_file file;size_t count=0;int r=EOK;
    if(flags&EXT4_WRITE_CREATE){r=parent_exists(full);if(!r && (flags&EXT4_WRITE_EXCLUSIVE) && !ext4_inode_exist(full,EXT4_DE_UNKNOWN))r=EEXIST;}
    if(!r)r=ext4_fopen2(&file,full,options);
    ext4_user_free(full);
    if(!r){if(!(flags&EXT4_WRITE_APPEND)){if(bytes && offset>file.fsize)r=grow_to(&file,offset);file.fpos=offset;}else {uint64_t limit=UINT64_C(0x100000000)*ctx->disk.lg_bsize;if(file.fsize>limit || bytes>limit-file.fsize)r=EFBIG;else file.fpos=file.fsize;}
        while(!r && count<bytes){size_t n=bytes-count;if(n>65536)n=65536;size_t wrote=0;r=ext4_fwrite(&file,(const uint8_t *)buffer+count,n,&wrote);count+=wrote;if(!r && wrote!=n)r=ENOSPC;}
        int closed=ext4_fclose(&file);if(!r)r=closed;}
    if(!r && count!=bytes)r=ENOSPC;
    int ok=done(ctx,r);if(ok)ok=ext4_engine_sync(ctx);return module_result(count,ok);
}
int ext4_engine_mutate(struct ext4_engine *ctx,unsigned operation,const char *path,uint64_t size,const char *destination){
    if(ctx->readonly){ctx->error=EXT4_READ_ONLY;return 0;}char *full=path_for(ctx,path);if(!full)return 0;int r=EINVAL;
    if(operation==EXT4_MUTATE_CREATE || operation==EXT4_MUTATE_MKDIR){r=parent_exists(full);if(!r && !ext4_inode_exist(full,EXT4_DE_UNKNOWN))r=EEXIST;if(r){ext4_user_free(full);return done(ctx,r);}}
    if(operation==EXT4_MUTATE_TRUNCATE && size>UINT64_C(0x100000000)*ctx->disk.lg_bsize){ext4_user_free(full);ctx->error=EXT4_ARGUMENT;return 0;}
    switch(operation){
    case EXT4_MUTATE_CREATE:{ext4_file file;r=ext4_fopen2(&file,full,O_WRONLY|O_CREAT|O_EXCL);if(!r)r=ext4_fclose(&file);break;}
    case EXT4_MUTATE_TRUNCATE:{ext4_file file;r=ext4_fopen2(&file,full,O_WRONLY);if(!r){r=size>file.fsize?grow_to(&file,size):ext4_ftruncate(&file,size);int closed=ext4_fclose(&file);if(!r)r=closed;}break;}
    case EXT4_MUTATE_REMOVE:r=ext4_inode_exist(full,EXT4_DE_REG_FILE);if(!r)r=ext4_fremove_atomic(full);break;
    case EXT4_MUTATE_MKDIR:r=ext4_dir_mk(full);break;
    case EXT4_MUTATE_RMDIR:{ext4_dir dir;r=ext4_dir_open(&dir,full);if(!r){const ext4_direntry *entry;while((entry=ext4_dir_entry_next(&dir))){if(!((entry->name_length==1 && entry->name[0]=='.') || (entry->name_length==2 && entry->name[0]=='.' && entry->name[1]=='.'))){r=ENOTEMPTY;break;}}int closed=ext4_dir_close(&dir);if(!r)r=closed;if(!r)r=ext4_dir_rm_empty(full);}break;}
    case EXT4_MUTATE_RENAME:{char *to=path_for(ctx,destination);if(to){r=parent_exists(to);if(!r && !strcmp(full,to))r=EOK;else if(!r && !ext4_inode_exist(to,EXT4_DE_UNKNOWN))r=EEXIST;else if(!r){size_t n=strlen(full);if(!strncmp(full,to,n) && to[n]=='/')r=EINVAL;else r=ext4_frename_atomic(full,to);}ext4_user_free(to);}break;}
    }
    ext4_user_free(full);int ok=done(ctx,r);if(ok)ok=ext4_engine_sync(ctx);return ok;
}
struct kernel_surface_result ext4_engine_list(struct ext4_engine *ctx,const char *path,uint64_t *cursor,struct ext4_dirent *out){
    char *full=path_for(ctx,path);if(!full)return module_result(0,0);ext4_dir dir;int r=ext4_dir_open(&dir,full);ext4_user_free(full);unsigned found=0;
    if(!r){dir.next_off=*cursor;const ext4_direntry *entry=ext4_dir_entry_next(&dir);if(entry){*out=(struct ext4_dirent){.inode=entry->inode,.type=entry->inode_type,.name_bytes=entry->name_length};memcpy(out->name,entry->name,entry->name_length);*cursor=dir.next_off;found=1;}else *out=(struct ext4_dirent){0};r=ext4_dir_close(&dir);}
    return module_result(found,done(ctx,r));
}
