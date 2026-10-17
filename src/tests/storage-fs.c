#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "../kmodules/shared/partition.h"
#include "../kmodules/shared/ext4-fs.h"
#include "../kmodules/shared/threads.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct {struct kernel_surfaces hub;struct kernel_surface methods[12];} fixture;
static FILE *image;static uint64_t size,corrupt=UINT64_MAX;static unsigned refs,live,budget=UINT32_MAX;
static struct kernel_surface_result cpu(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result thread(MODULE_ARGS){MODULE_UNUSED();return module_result(a==THREAD_CURRENT?1:0,1);}
static struct kernel_surface_result allocate(MODULE_ARGS){MODULE_UNUSED();if(!budget)return module_result(0,0);if(budget!=UINT32_MAX)--budget;void *p=calloc(b,c);if(p)++live;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result resize_memory(MODULE_ARGS){MODULE_UNUSED();void *p=realloc((void *)b,c);if(p && !b)++live;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result release(MODULE_ARGS){MODULE_UNUSED();assert(live);free((void *)b);--live;return module_result(1,1);}
static struct kernel_surface_result control(MODULE_ARGS){MODULE_UNUSED();if(b==BLOCK_RETAIN)++refs;else {assert(refs);--refs;}return module_result(1,1);}
static struct kernel_surface_result read_block(MODULE_ARGS){MODULE_UNUSED();assert(a==1 && b<=size/512 && c<=8 && c<=size/512-b && e>=c*512);assert(!fseeko(image,b*512,SEEK_SET));assert(fread((void *)d,512,c,image)==c);if(corrupt>=b*512 && corrupt<b*512+c*512)((uint8_t *)d)[corrupt-b*512]^=1;return module_result(c*512,1);}
static void method(const char *name,kernel_surface_call call){struct kernel_surface *m=&fixture.methods[fixture.hub.count++];strcpy(m->name,name);m->call=call;m->major=1;}
int main(int argc,char **argv){
    assert(argc==4);image=fopen(argv[1],"rb");assert(image);assert(!fseeko(image,0,SEEK_END));size=ftello(image);
    fixture.hub=(struct kernel_surfaces){.magic=KERNEL_SURFACES_MAGIC,.header_bytes=sizeof(struct kernel_surfaces)};
    method("cpu.control",cpu);method("threads.control",thread);method("irq.context",cpu);method("mem.calloc",allocate);method("mem.free",release);method("mem.realloc",resize_memory);
    method("partition.read",partition_surface_read);method("partition.control",partition_surface_control);
    assert(!partition_surface_init((uintptr_t)&fixture.hub,1,0,0,0,0).status);assert(!ext4_fs_surface_init((uintptr_t)&fixture.hub,1,0,0,0,0).status);
    struct block_device raw={.major=1,.block_bytes=512,.max_transfer_bytes=4096,.blocks=size/512,.cookie=1,.read=read_block,.control=control},part;
    unsigned char guid[16];for(unsigned i=0;i<16;++i){unsigned n;assert(sscanf(argv[2]+2*i,"%2x",&n)==1);guid[i]=n;}
    unsigned base_live=live;budget=0;assert(partition_surface_find((uintptr_t)&raw,(uintptr_t)guid,(uintptr_t)&part,0,0,0).status && !refs);budget=UINT32_MAX;
    corrupt=512+16;assert(partition_surface_find((uintptr_t)&raw,(uintptr_t)guid,(uintptr_t)&part,0,0,0).status && !refs && live==base_live);
    corrupt=size-512+16;assert(partition_surface_find((uintptr_t)&raw,(uintptr_t)guid,(uintptr_t)&part,0,0,0).status && !refs);corrupt=UINT64_MAX;
    assert(!partition_surface_find((uintptr_t)&raw,(uintptr_t)guid,(uintptr_t)&part,0,0,0).status && refs==1);
    struct ext4_info info;budget=0;assert(ext4_fs_surface_mount((uintptr_t)&part,(uintptr_t)&info,0,0,0,0).status);budget=UINT32_MAX;
    corrupt=1024*1024+1024+1020;assert(ext4_fs_surface_mount((uintptr_t)&part,(uintptr_t)&info,0,0,0,0).status);corrupt=UINT64_MAX;
    assert(!ext4_fs_surface_mount((uintptr_t)&part,(uintptr_t)&info,0,0,0,0).status);uint64_t id=info.cookie;
    assert(partition_surface_close(part.cookie,0,0,0,0,0).status);
    struct ext4_stat stat;assert(!ext4_fs_surface_stat(id,(uintptr_t)argv[3],(uintptr_t)&stat,0,0,0).status && (stat.mode&0xf000)==0x4000);
    char path[512];snprintf(path,sizeof(path),"%s/probe.bin",argv[3]);assert(!ext4_fs_surface_stat(id,(uintptr_t)path,(uintptr_t)&stat,0,0,0).status && stat.bytes==16384);
    unsigned char out[8192];assert(!ext4_fs_surface_read(id,(uintptr_t)path,123,(uintptr_t)out,sizeof(out),0).status);for(unsigned n=0;n<sizeof(out);++n)assert(out[n]==(uint8_t)((n+123)*37+11));
    assert(ext4_fs_surface_read(id,(uintptr_t)path,16384,(uintptr_t)out,1,0).value==0);
    assert(ext4_fs_surface_stat(id,(uintptr_t)"/missing",(uintptr_t)&stat,0,0,0).status);
    uint64_t cursor=0;struct ext4_dirent entry;unsigned found=0;do{struct kernel_surface_result r=ext4_fs_surface_list(id,(uintptr_t)argv[3],(uintptr_t)&cursor,(uintptr_t)&entry,0,0);assert(!r.status);if(!r.value)break;if(!strcmp(entry.name,"probe.bin"))++found;}while(1);assert(found==1);
    snprintf(path,sizeof(path),"%s/fragment.bin",argv[3]);
    assert(!ext4_fs_surface_stat(id,(uintptr_t)path,(uintptr_t)&stat,0,0,0).status && stat.bytes==80*8192);
    for(unsigned index=0;index<80;++index){assert(!ext4_fs_surface_read(id,(uintptr_t)path,index*8192,(uintptr_t)out,8192,0).status);for(unsigned n=0;n<8192;++n)assert(out[n]==(n<4096?index+1:0));}
    /* Group-descriptor damage must be rejected even after a successful mount. */
    assert(!ext4_fs_surface_unmount(id,0,0,0,0,0).status);
    corrupt=1024*1024+(info.block_bytes==1024?2:1)*info.block_bytes+30;
    assert(ext4_fs_surface_mount((uintptr_t)&part,(uintptr_t)&info,0,0,0,0).status);corrupt=UINT64_MAX;assert(ext4_fs_surface_stat(id,(uintptr_t)"/",(uintptr_t)&stat,0,0,0).status);
    assert(!partition_surface_close(part.cookie,0,0,0,0,0).status && !refs && !live);
    assert(part.read(part.cookie,0,1,(uintptr_t)out,sizeof(out),0).status);
    fclose(image);puts("PASS partition/ext4: GPT CRCs, GUID selection, allocation rollback, checksummed mount/stat/list, cross-block reads, EOF and retained-parent teardown");return 0;
}
