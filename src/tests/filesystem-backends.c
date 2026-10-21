/* Exercise the production mount manager with two independently queried formats. */
#include "../kmodules/shared/filesystem.c"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static long live,budget=-1;static unsigned mounted,unmounted,ambiguous,probe_error,fail_unmount,reenter;
static struct {struct kernel_surfaces hub;struct kernel_surface methods[8];} fixture;
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result alloc_stub(MODULE_ARGS){MODULE_UNUSED();if(!budget)return module_result(0,0);if(budget>0)--budget;void *p=calloc(b,c);if(p)++live;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result free_stub(MODULE_ARGS){MODULE_UNUSED();assert(live);--live;free((void *)b);return module_result(1,1);}
static struct kernel_surface_result data_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result format_call(unsigned type,uint64_t cookie,struct filesystem_request *r){
    r->status=CB_OK;r->actual=0;
    if(r->operation==FS_PROBE){if(reenter){struct kernel_surface_result nested=filesystem_surface_mount((uintptr_t)r->device,(uintptr_t)"/nested",0,0,0,0);assert(nested.status && nested.value==CB_BUSY);}r->status=probe_error?CB_RESOURCE_FAILURE:ambiguous || r->device->cookie==type?CB_OK:CB_NOT_FOUND;}
    else if(r->operation==FS_MOUNT){struct filesystem_request pending={.operation=FS_STAT,.path="/file"};assert(!filesystem_surface_file(fs.serial,(uintptr_t)&pending,0,0,0,0).status && pending.status==CB_RESOURCE_FAILURE);assert(r->device->cookie==type);++mounted;r->cookie=type*100+mounted;}
    else if(r->operation==FS_UNMOUNT){if(fail_unmount)r->status=CB_RESOURCE_FAILURE;else ++unmounted;}
    else if(r->operation==FS_READ){assert(cookie/100==type && !strcmp(r->path,"/file"));assert(r->bytes>=1);*(char *)r->buffer='A'+type-1;r->actual=1;}
    else if(r->operation==FS_STAT){assert(cookie/100==type);r->stat=(struct filesystem_stat){.bytes=type,.type=FS_REGULAR};}
    return module_result(1,1);
}
static struct kernel_surface_result format_a(MODULE_ARGS){MODULE_UNUSED();return format_call(1,a,(void *)b);}
static struct kernel_surface_result format_b(MODULE_ARGS){MODULE_UNUSED();return format_call(2,a,(void *)b);}
static struct kernel_surface_result query_a(MODULE_ARGS){MODULE_UNUSED();assert(b>=sizeof(struct filesystem_backend));*(struct filesystem_backend *)a=(struct filesystem_backend){sizeof(struct filesystem_backend),2,0,format_a};return module_result(1,1);}
static struct kernel_surface_result query_b(MODULE_ARGS){MODULE_UNUSED();assert(b>=sizeof(struct filesystem_backend));*(struct filesystem_backend *)a=(struct filesystem_backend){sizeof(struct filesystem_backend),2,0,format_b};return module_result(1,1);}
static uint64_t mount_at(unsigned type,const char *path){struct block_device device={.cookie=type};struct kernel_surface_result r=filesystem_surface_mount((uintptr_t)&device,(uintptr_t)path,FS_MOUNT_WRITE,0,0,0);assert(!r.status);return r.value;}
static void root_stat(const char *path,unsigned expected){unsigned char packet[32+sizeof(struct cb_filesystem_request)+128]={0};uint64_t op=CB_FILESYSTEM_STAT;memcpy(packet+8,&op,8);struct cb_filesystem_request req={.path_bytes=strlen(path)};memcpy(packet+32,&req,sizeof(req));memcpy(packet+32+sizeof(req),path,req.path_bytes);struct cb_filesystem_stat out;struct io_invocation r={.process=7,.operation=CB_IO_FILESYSTEM,.input=packet,.bytes=32+sizeof(req)+req.path_bytes,.output=&out,.capacity=sizeof(out)};assert(!filesystem_surface_invoke((uintptr_t)&r,0,0,0,0,0).status && r.status==(expected?CB_OK:CB_DENIED) && (!expected || out.bytes==expected));}
int main(void){
    fixture.hub=(struct kernel_surfaces){.header_bytes=offsetof(__typeof__(fixture),methods),.count=8};
    const char *names[]={"cpu.control","mem.calloc","mem.free","applications.data","alpha.fs_backend","alpha.invoke","beta.fs_backend","beta.invoke"};kernel_surface_call calls[]={cpu_stub,alloc_stub,free_stub,data_stub,query_a,format_a,query_b,format_b};for(unsigned i=0;i<8;++i){strcpy(fixture.methods[i].name,names[i]);fixture.methods[i].call=calls[i];}
    budget=1;assert(filesystem_surface_init((uintptr_t)&fixture.hub,1,0,0,0,0).status && !live && !fs.hub);budget=-1;
    assert(!filesystem_surface_init((uintptr_t)&fixture.hub,1,0,0,0,0).status && live==2);assert(!filesystem_surface_register((uintptr_t)query_a,0,0,0,0,0).status && live==2);
    struct kernel_surface_result root=filesystem_surface_adopt((uintptr_t)query_a,100,(uintptr_t)"/",0,0,0);assert(!root.status);assert(!filesystem_surface_bind((uintptr_t)"filesystem",7,root.value,0,0,0).status);
    uint64_t one=mount_at(1,"/one"),two=mount_at(2,"/two");assert(mounted==2);root_stat("/file",1);root_stat("/one/file",1);root_stat("/two/file",2);root_stat("/twofold",1);root_stat("/two",2);root_stat("/system/config/identity",0);root_stat("/system/config/identity/credentials.bin",0);root_stat("/system/config/identity-confusable",1);
    char data=0;struct filesystem_request read={.operation=FS_READ,.path="/file",.buffer=&data,.bytes=1};assert(!filesystem_surface_file(two,(uintptr_t)&read,0,0,0,0).status && !read.status && data=='B');
    assert(!filesystem_surface_retain(two,0,0,0,0,0).status);assert(filesystem_surface_unmount(two,0,0,0,0,0).value==CB_BUSY);assert(!filesystem_surface_release(two,0,0,0,0,0).status);
    find_mount(two)->active=1;assert(filesystem_surface_unmount(two,0,0,0,0,0).value==CB_BUSY);find_mount(two)->active=0;
    uint64_t nested=mount_at(1,"/two/nested");assert(filesystem_surface_unmount(two,0,0,0,0,0).value==CB_BUSY);assert(!filesystem_surface_unmount(nested,0,0,0,0,0).status);
    fail_unmount=1;assert(filesystem_surface_unmount(two,0,0,0,0,0).status && !find_mount(two)->closing);fail_unmount=0;assert(!filesystem_surface_unmount(two,0,0,0,0,0).status);assert(!filesystem_surface_file(two,(uintptr_t)&read,0,0,0,0).status && read.status==CB_RESOURCE_FAILURE);
    struct block_device device={.cookie=3};long before=live;assert(filesystem_surface_mount((uintptr_t)&device,(uintptr_t)"/unknown",0,0,0,0).value==CB_NOT_FOUND && live==before);device.cookie=1;ambiguous=1;assert(filesystem_surface_mount((uintptr_t)&device,(uintptr_t)"/ambiguous",0,0,0,0).status && live==before);ambiguous=0;probe_error=1;assert(filesystem_surface_mount((uintptr_t)&device,(uintptr_t)"/ioerror",0,0,0,0).value==CB_RESOURCE_FAILURE && live==before);probe_error=0;
    for(long n=0;n<2;++n){budget=n;unsigned old=mounted;assert(filesystem_surface_mount((uintptr_t)&device,(uintptr_t)"/oom",0,0,0,0).status && live==before && mounted==old);}budget=-1;
    reenter=1;uint64_t serial=mount_at(1,"/serialized");reenter=0;assert(!filesystem_surface_unmount(serial,0,0,0,0,0).status);assert(!filesystem_surface_unmount(one,0,0,0,0,0).status);assert(filesystem_surface_unmount(root.value,0,0,0,0,0).value==CB_BUSY);
    assert(mounted==unmounted);struct mount *m=fs.mounts;assert(m && !m->next && !m->refs);ffree(m->path);ffree(m);while(fs.backends){struct backend *next=fs.backends->next;ffree(fs.backends);fs.backends=next;}assert(!live);
    puts("PASS filesystem backends: two formats, probes, routing, refs, active/nested mounts, failed unmount, OOM rollback and serialized lifecycle");return 0;
}
