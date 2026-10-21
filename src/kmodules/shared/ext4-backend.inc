/* Format adapter: the filesystem manager and stream broker see no ext4 types. */
#include "filesystem.h"
static cb_status_t backend_error(uint64_t cookie){
    struct ext4_info info={0};if(ext4_fs_surface_info(cookie,(uintptr_t)&info,0,0,0,0).status)return CB_RESOURCE_FAILURE;
    return info.error==EXT4_NOT_FOUND?CB_NOT_FOUND:info.error==EXT4_READ_ONLY?CB_READ_ONLY:info.error==EXT4_BUSY?CB_BUSY:info.error==EXT4_ARGUMENT || info.error==EXT4_TYPE?CB_BAD_REQUEST:CB_RESOURCE_FAILURE;
}
struct kernel_surface_result ext4_fs_surface_backend_invoke(MODULE_ARGS){
    MODULE_UNUSED();struct filesystem_request *r=(void *)b;if(!r)return module_result(0,0);r->status=CB_BAD_REQUEST;r->actual=0;
    struct kernel_surface_result result;
    if(r->operation==FS_PROBE){
        if(!block_valid(r->device) || r->device->block_bytes>4096)return module_result(1,1);
        uint8_t block[4096];uint64_t lba=1080/r->device->block_bytes;size_t offset=1080%r->device->block_bytes;
        result=r->device->read(r->device->cookie,lba,1,(uintptr_t)block,sizeof(block),0);
        r->status=result.status || result.value!=r->device->block_bytes?CB_RESOURCE_FAILURE:block[offset]==0x53 && block[offset+1]==0xef?CB_OK:CB_NOT_FOUND;return module_result(1,1);
    }
    if(r->operation==FS_MOUNT){struct ext4_info info={0};result=ext4_fs_surface_mount((uintptr_t)r->device,(uintptr_t)&info,r->flags&FS_MOUNT_WRITE?EXT4_MOUNT_WRITE:0,0,0,0);r->cookie=result.status?0:result.value;r->status=!result.status?CB_OK:result.value==EXT4_READ_ONLY?CB_READ_ONLY:CB_RESOURCE_FAILURE;return module_result(1,1);}
    if(r->operation==FS_UNMOUNT){result=ext4_fs_surface_unmount(a,0,0,0,0,0);r->status=result.status?CB_RESOURCE_FAILURE:CB_OK;return module_result(1,1);}
    if(r->operation==FS_SYNC){result=ext4_fs_surface_sync(a,0,0,0,0,0);r->status=result.status?backend_error(a):CB_OK;return module_result(1,1);}
    if(!r->path)return module_result(1,1);
    if(r->operation==FS_STAT){struct ext4_stat st;result=ext4_fs_surface_stat(a,(uintptr_t)r->path,(uintptr_t)&st,sizeof(st),0,0);if(!result.status)r->stat=(struct filesystem_stat){.bytes=st.bytes,.type=(st.mode&0170000)==0040000?FS_DIRECTORY:(st.mode&0170000)==0100000?FS_REGULAR:0,.uid=st.uid,.gid=st.gid,.permissions=st.mode&07777,.valid=1};}
    else if(r->operation==FS_LIST){struct ext4_dirent entry;result=ext4_fs_surface_list(a,(uintptr_t)r->path,(uintptr_t)&r->cursor,(uintptr_t)&entry,0,0);if(!result.status && result.value){r->actual=1;r->entry=(struct filesystem_dirent){.type=entry.type==2?FS_DIRECTORY:entry.type==1?FS_REGULAR:0,.name_bytes=entry.name_bytes};for(unsigned i=0;i<entry.name_bytes && i<sizeof(r->entry.name);++i)r->entry.name[i]=entry.name[i];}}
    else if(r->operation==FS_READ || r->operation==FS_WRITE){if(r->operation==FS_READ)result=ext4_fs_surface_read(a,(uintptr_t)r->path,r->offset,(uintptr_t)r->buffer,r->bytes,0);else result=ext4_fs_surface_write(a,(uintptr_t)r->path,r->offset,(uintptr_t)r->buffer,r->bytes,r->mode==CB_FILE_APPEND?EXT4_WRITE_APPEND:0);r->actual=result.value;}
    else if(r->operation==FS_CHMOD || r->operation==FS_CHOWN){result=ext4_fs_surface_mutate(a,r->operation==FS_CHMOD?EXT4_MUTATE_MODE:EXT4_MUTATE_OWNER,(uintptr_t)r->path,r->operation==FS_CHMOD?r->mode:r->uid|(r->gid<<32),0,0);}
    else if(r->operation==FS_REMOVE || r->operation==FS_MKDIR){result=ext4_fs_surface_mutate(a,r->operation==FS_REMOVE?EXT4_MUTATE_REMOVE:EXT4_MUTATE_MKDIR,(uintptr_t)r->path,0,0,0);if(!result.status && r->operation==FS_MKDIR && r->process){result=ext4_fs_surface_mutate(a,EXT4_MUTATE_MODE,(uintptr_t)r->path,0700,0,0);if(!result.status)result=ext4_fs_surface_mutate(a,EXT4_MUTATE_OWNER,(uintptr_t)r->path,r->uid|(r->gid<<32),0,0);}}
    else if(r->operation==FS_PREPARE){
        if(r->mode<CB_FILE_READ || r->mode>CB_FILE_APPEND)return module_result(1,1);
        struct ext4_info info;if(ext4_fs_surface_info(a,(uintptr_t)&info,0,0,0,0).status){r->status=CB_RESOURCE_FAILURE;return module_result(1,1);}if(r->mode!=CB_FILE_READ && info.readonly){r->status=CB_READ_ONLY;return module_result(1,1);}
        struct ext4_stat st;result=ext4_fs_surface_stat(a,(uintptr_t)r->path,(uintptr_t)&st,sizeof(st),0,0);
        if(!result.status && (st.mode&0170000)!=0100000)return module_result(1,1);
        if(result.status){r->status=backend_error(a);if(r->mode==CB_FILE_READ || r->status!=CB_NOT_FOUND)return module_result(1,1);result=ext4_fs_surface_mutate(a,EXT4_MUTATE_CREATE,(uintptr_t)r->path,0,0,0);if(!result.status && r->process){result=ext4_fs_surface_mutate(a,EXT4_MUTATE_MODE,(uintptr_t)r->path,0600,0,0);if(!result.status)result=ext4_fs_surface_mutate(a,EXT4_MUTATE_OWNER,(uintptr_t)r->path,r->uid|(r->gid<<32),0,0);}}
        if(!result.status && r->mode==CB_FILE_RECREATE)result=ext4_fs_surface_mutate(a,EXT4_MUTATE_TRUNCATE,(uintptr_t)r->path,0,0,0);
    }else return module_result(1,1);
    r->status=result.status?backend_error(a):CB_OK;return module_result(1,1);
}
struct kernel_surface_result ext4_fs_surface_backend(MODULE_ARGS){
    MODULE_UNUSED();if(!a || b<sizeof(struct filesystem_backend) || !state.hub)return module_result(0,0);
    kernel_surface_call entry=kernel_surface_find(state.hub,"ext4_fs.backend_invoke");if(!entry)return module_result(0,0);
    *(struct filesystem_backend *)a=(struct filesystem_backend){sizeof(struct filesystem_backend),2,0,entry};return module_result(1,1);
}
