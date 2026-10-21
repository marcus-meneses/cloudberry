#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "../kmodules/shared/iso9660-fs.c"
static unsigned char *image;static size_t image_bytes;static unsigned pins,allocations,fail_io,oom,fail_release;
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result alloc_stub(MODULE_ARGS){MODULE_UNUSED();void *p=oom?NULL:calloc(b,c);if(p)++allocations;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result free_stub(MODULE_ARGS){MODULE_UNUSED();assert(allocations);--allocations;free((void *)b);return module_result(1,1);}
static struct kernel_surface_result control_stub(MODULE_ARGS){MODULE_UNUSED();if(b==BLOCK_RETAIN)++pins;else {assert(b==BLOCK_RELEASE && pins);if(fail_release)return module_result(0,0);--pins;}return module_result(1,1);}
static unsigned sector_bytes;
static struct kernel_surface_result read_stub(MODULE_ARGS){MODULE_UNUSED();uint64_t at=b*sector_bytes,n=c*sector_bytes;uint64_t total=(image_bytes+sector_bytes-1)/sector_bytes*sector_bytes;if(fail_io || at>total || n>total-at || n>e)return module_result(0,0);size_t copy=at<image_bytes?image_bytes-at:0;if(copy>n)copy=n;if(copy)memcpy((void *)d,image+at,copy);if(n>copy)memset((unsigned char *)d+copy,0,n-copy);return module_result(n,1);}
static cb_status_t run(uint64_t cookie,struct filesystem_request *r){assert(!iso9660_fs_surface_invoke(cookie,(uintptr_t)r,0,0,0,0).status);return r->status;}
static void exercise(unsigned sector){
    sector_bytes=sector;struct block_device disk={.major=1,.block_bytes=sector,.max_transfer_bytes=4096,.blocks=(image_bytes+sector-1)/sector,.cookie=1,.read=read_stub,.control=control_stub};struct filesystem_request request={.operation=FS_PROBE,.device=&disk};assert(run(0,&request)==CB_OK && !pins);
    oom=1;request.operation=FS_MOUNT;assert(run(0,&request)==CB_RESOURCE_FAILURE && !pins);oom=0;request.flags=FS_MOUNT_WRITE;assert(run(0,&request)==CB_READ_ONLY && !pins);request.flags=0;assert(run(0,&request)==CB_OK && pins==1);uint64_t cookie=request.cookie;
    request=(struct filesystem_request){.operation=FS_STAT,.path="/cloudberry.cnf"};assert(run(cookie,&request)==CB_OK && request.stat.valid && request.stat.type==FS_REGULAR && request.stat.permissions==0444);uint64_t size=request.stat.bytes;assert(size>10);
    unsigned char data[4096];request=(struct filesystem_request){.operation=FS_READ,.path="/cloudberry.cnf",.buffer=data,.bytes=10};assert(run(cookie,&request)==CB_OK && request.actual==10 && data[0]=='#');request.offset=size;assert(run(cookie,&request)==CB_OK && !request.actual);
    request=(struct filesystem_request){.operation=FS_READ,.path="/kernel.elf",.buffer=data,.bytes=4};assert(run(cookie,&request)==CB_OK && request.actual==4 && !memcmp(data,"\177ELF",4));
    request=(struct filesystem_request){.operation=FS_STAT,.path="/kmodules/helpers/mem.kmod"};assert(run(cookie,&request)==CB_OK && request.stat.bytes);
    request=(struct filesystem_request){.operation=FS_LIST,.path="/"};unsigned names=0,found=0;do{assert(run(cookie,&request)==CB_OK);if(request.actual){++names;if(!strcmp(request.entry.name,"kernel.elf"))found=1;}}while(request.actual);assert(names>=4 && found);
    if(iso.first->joliet){struct iso_volume *v=iso.first;uint64_t at=(uint64_t)v->root.extent*2048,end=at+v->root.bytes;int changed=0;while(at<end){unsigned char *record=image+at;if(!record[0]){at=(at+2048)&~UINT64_C(2047);continue;}char decoded[256];unsigned n=0;if(record[32]>1 && name(v,record+33,record[32],decoded,&n)==CB_OK && !strcmp(decoded,"kernel.elf")){unsigned char saved=record[36];record[36]=0xe9;request=(struct filesystem_request){.operation=FS_READ,.path="/k\xc3\xa9rnel.elf",.buffer=data,.bytes=4};assert(run(cookie,&request)==CB_OK && request.actual==4 && !memcmp(data,"\177ELF",4));record[36]=saved;changed=1;break;}at+=record[0];}assert(changed);}
    request=(struct filesystem_request){.operation=FS_STAT,.path="/does-not-exist"};assert(run(cookie,&request)==CB_NOT_FOUND);
    for(unsigned op=FS_WRITE;op<=FS_MKDIR;++op){if(op==FS_PREPARE)continue;request=(struct filesystem_request){.operation=op,.path="/kernel.elf"};assert(run(cookie,&request)==CB_READ_ONLY);}
    fail_io=1;request=(struct filesystem_request){.operation=FS_READ,.path="/kernel.elf",.buffer=data,.bytes=4};assert(run(cookie,&request)==CB_RESOURCE_FAILURE);fail_io=0;
    request=(struct filesystem_request){.operation=FS_UNMOUNT};fail_release=1;assert(run(cookie,&request)==CB_RESOURCE_FAILURE && pins==1);fail_release=0;assert(run(cookie,&request)==CB_OK && !pins && !allocations);assert(run(cookie,&request)==CB_BAD_REQUEST);
    unsigned char *descriptor=image+16*2048;unsigned char saved=descriptor[1];descriptor[1]='X';request=(struct filesystem_request){.operation=FS_PROBE,.device=&disk};assert(run(0,&request)==CB_NOT_FOUND);descriptor[1]=saved;
    /* Corrupt both root records: out-of-bounds extent and multi-extent flags. */
    unsigned selected=16;for(unsigned index=16;index<80 && image[index*2048]!=255;++index)if(image[index*2048]==2)selected=index;{unsigned char *record=image+selected*2048+156;unsigned char flags=record[25];record[25]|=128;assert(run(0,&request)==CB_BAD_REQUEST);record[25]=flags;unsigned char extent[8];memcpy(extent,record+2,8);memset(record+2,255,8);assert(run(0,&request)==CB_BAD_REQUEST);memcpy(record+2,extent,8);}
    assert(!pins && !allocations);
}
int main(int argc,char **argv){assert(argc==2);int fd=open(argv[1],O_RDONLY);struct stat st;assert(fd>=0 && !fstat(fd,&st));image_bytes=st.st_size;image=mmap(NULL,image_bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);assert(image!=MAP_FAILED);close(fd);iso.hub=(const void *)1;iso.cpu=cpu_stub;iso.alloc=alloc_stub;iso.free=free_stub;exercise(512);exercise(4096);munmap(image,image_bytes);puts("PASS ISO9660: real image/Joliet lookup, listing, nested paths, EOF, 512/4096 sectors, read-only/OOM/I/O failures, retention and malformed descriptors");}
