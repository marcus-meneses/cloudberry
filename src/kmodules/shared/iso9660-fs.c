/* Read-only ECMA-119 / Joliet backend. All I/O uses the retained block contract. */
#include "filesystem.h"
#include "block-util.h"
#include "../../kernel/cpu.h"
struct iso_node {uint32_t extent,bytes;unsigned directory;};
struct iso_volume {struct iso_volume *next;struct block_device disk;struct iso_node root;uint64_t id,bytes;unsigned joliet,busy;unsigned char scratch[4096];};
static struct {const struct kernel_surfaces *hub;uintptr_t heap;uint64_t serial;struct iso_volume *first;kernel_surface_call cpu,alloc,free;} iso;
#define IC(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static uint64_t ilock(void){return IC(iso.cpu,CPU_ENTER,0,0).value;}
static void iunlock(uint64_t flags){IC(iso.cpu,CPU_LEAVE,flags,0);}
static int foreground(void){return iso.hub && !IC(iso.cpu,CPU_CURRENT,0,0).value && !IC(iso.cpu,CPU_DEPTH,0,0).value;}
static uint32_t be32(const unsigned char *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static unsigned be16(const unsigned char *p){return (unsigned)p[0]<<8|p[1];}
static int dual32(const unsigned char *p,uint32_t *out){*out=disk_u32(p);return *out==be32(p+4);}
static int dual16(const unsigned char *p,unsigned *out){*out=disk_u16(p);return *out==be16(p+2);}
static cb_status_t bytes(struct iso_volume *v,uint64_t at,void *out,size_t count){if(at>v->bytes || count>v->bytes-at)return CB_BAD_REQUEST;return block_bytes_read(&v->disk,at,out,count,v->scratch)?CB_OK:CB_RESOURCE_FAILURE;}
static cb_status_t node(struct iso_volume *v,const unsigned char *r,size_t available,struct iso_node *out){
    if(available<34 || r[0]<34 || r[0]>available || !r[32] || 33u+r[32]>r[0] || r[26] || r[27] || (r[25]&~3u))return CB_BAD_REQUEST;
    uint32_t extent,length;unsigned sequence;if(!dual32(r+2,&extent) || !dual32(r+10,&length) || !dual16(r+28,&sequence) || sequence!=1 || extent>UINT32_MAX-r[1])return CB_BAD_REQUEST;
    extent+=r[1];uint64_t offset=(uint64_t)extent*2048;if(offset>v->bytes || length>v->bytes-offset)return CB_BAD_REQUEST;
    *out=(struct iso_node){extent,length,!!(r[25]&2)};return CB_OK;
}
/* Bounded descriptor scan. A malformed identified ISO is an error, not NOT_FOUND. */
static cb_status_t inspect(struct iso_volume *v){
    unsigned char block[2048],selected[2048];unsigned have=0,primary=0,terminated=0;uint64_t total=v->disk.blocks*v->disk.block_bytes;v->bytes=total;
    if(total<17*2048)return CB_NOT_FOUND;
    for(unsigned index=16;index<80;++index){cb_status_t status=bytes(v,(uint64_t)index*2048,block,sizeof(block));if(status)return status;
        if(!disk_equal(block+1,"CD001",5) || block[6]!=1)return index==16?CB_NOT_FOUND:CB_BAD_REQUEST;
        if(block[0]==255){terminated=1;break;}
        if(block[0]==1){if(primary)return CB_BAD_REQUEST;primary=1;if(!have){disk_copy(selected,block,sizeof(block));have=1;}}
        else if(block[0]==2 && !block[7] && block[88]=='%' && block[89]=='/' && (block[90]=='@' || block[90]=='C' || block[90]=='E')){if(v->joliet)return CB_BAD_REQUEST;v->joliet=1;disk_copy(selected,block,sizeof(block));have=1;}
    }
    if(!have || !primary || !terminated)return CB_BAD_REQUEST;
    uint32_t sectors;unsigned blocksize,setsize,sequence;if(!dual32(selected+80,&sectors) || !dual16(selected+128,&blocksize) || blocksize!=2048 || !dual16(selected+120,&setsize) || setsize!=1 || !dual16(selected+124,&sequence) || sequence!=1 || sectors<17 || (uint64_t)sectors*2048>total || selected[881]!=1)return CB_BAD_REQUEST;
    v->bytes=(uint64_t)sectors*2048;cb_status_t status=node(v,selected+156,34,&v->root);return status?status:!v->root.directory || !v->root.bytes?CB_BAD_REQUEST:CB_OK;
}
static cb_status_t name(struct iso_volume *v,const unsigned char *p,size_t n,char out[256],unsigned *length){
    size_t used=0;if(v->joliet && (n&1))return CB_BAD_REQUEST;
    for(size_t i=0;i<n;){uint32_t c=p[i++];if(v->joliet){c=c<<8|p[i++];if(c>=0xd800 && c<=0xdbff){if(i+2>n)return CB_BAD_REQUEST;unsigned low=be16(p+i);i+=2;if(low<0xdc00 || low>0xdfff)return CB_BAD_REQUEST;c=0x10000+((c-0xd800)<<10)+(low-0xdc00);}else if(c>=0xdc00 && c<=0xdfff)return CB_BAD_REQUEST;}
        if(c==';')break;
        if(!c || c=='/' || c<32 || c==127)return CB_BAD_REQUEST;
        if(!v->joliet && c>='A' && c<='Z')c+='a'-'A';
        unsigned count=c<0x80?1:c<0x800?2:c<0x10000?3:4;if(count>255-used)return CB_BAD_REQUEST;
        if(count==1)out[used++]=c;else {out[used++]=(count==2?0xc0:count==3?0xe0:0xf0)|(c>>(6*(count-1)));while(--count)out[used++]=0x80|((c>>(6*(count-1)))&63);}
    }
    if(!v->joliet && used && out[used-1]=='.')--used;
    if(!used || (used==1 && out[0]=='.') || (used==2 && out[0]=='.' && out[1]=='.'))return CB_BAD_REQUEST;
    out[used]=0;*length=used;return CB_OK;
}
static cb_status_t next(struct iso_volume *v,const struct iso_node *directory,uint64_t *cursor,struct iso_node *found,struct filesystem_dirent *entry,int *exists){
    *exists=0;if(!directory->directory || *cursor>directory->bytes)return CB_BAD_REQUEST;
    unsigned char sector[2048];
    while(*cursor<directory->bytes){uint64_t block=*cursor&~UINT64_C(2047);size_t remaining=directory->bytes-block;if(remaining>2048)remaining=2048;cb_status_t status=bytes(v,(uint64_t)directory->extent*2048+block,sector,remaining);if(status)return status;
        size_t at=*cursor-block;unsigned size=sector[at];if(!size){for(size_t i=at;i<remaining;++i)if(sector[i])return CB_BAD_REQUEST;*cursor=block+remaining;continue;}
        if(size>remaining-at)return CB_BAD_REQUEST;
        const unsigned char *r=sector+at;status=node(v,r,size,found);if(status)return status;*cursor+=size;
        if(r[32]==1 && (r[33]==0 || r[33]==1))continue;
        status=name(v,r+33,r[32],entry->name,&entry->name_bytes);if(status)return status;
        entry->type=found->directory?FS_DIRECTORY:FS_REGULAR;*exists=1;return CB_OK;
    }return CB_OK;
}
static cb_status_t lookup(struct iso_volume *v,const char *path,struct iso_node *out){
    if(!path || *path!='/')return CB_BAD_REQUEST;
    *out=v->root;size_t at=1;
    while(path[at]){size_t begin=at;while(path[at] && path[at]!='/')++at;size_t n=at-begin;if(!n || n>255 || (n==1 && path[begin]=='.') || (n==2 && path[begin]=='.' && path[begin+1]=='.') || !out->directory)return CB_BAD_REQUEST;
        uint64_t cursor=0;int exists,matched=0;struct filesystem_dirent entry;struct iso_node child;
        do{cb_status_t status=next(v,out,&cursor,&child,&entry,&exists);if(status)return status;if(exists && entry.name_bytes==n && disk_equal(entry.name,path+begin,n)){*out=child;matched=1;break;}}while(exists);
        if(!matched)return CB_NOT_FOUND;
        if(path[at]=='/')++at;
    }return CB_OK;
}
struct kernel_surface_result iso9660_fs_surface_init(MODULE_ARGS){MODULE_UNUSED();if(iso.hub)return module_result(1,iso.hub==(const void *)a);if(!a || !b)return module_result(0,0);const struct kernel_surfaces *hub=(const void *)a;iso.cpu=kernel_surface_find(hub,"cpu.control");iso.alloc=kernel_surface_find(hub,"mem.calloc");iso.free=kernel_surface_find(hub,"mem.free");if(!iso.cpu || !iso.alloc || !iso.free)return module_result(0,0);iso.heap=b;iso.hub=hub;return module_result(1,1);}
struct kernel_surface_result iso9660_fs_surface_invoke(MODULE_ARGS){
    MODULE_UNUSED();struct filesystem_request *r=(void *)b;if(!foreground() || !r)return module_result(0,0);r->status=CB_BAD_REQUEST;r->actual=0;
    if(r->operation==FS_PROBE || r->operation==FS_MOUNT){
        if(!block_valid(r->device))return module_result(1,1);
        struct iso_volume probe={0};block_copy(&probe.disk,r->device);r->status=inspect(&probe);if(r->status || r->operation==FS_PROBE)return module_result(1,1);
        if(r->flags){r->status=CB_READ_ONLY;return module_result(1,1);}if(iso.serial==UINT64_MAX){r->status=CB_RESOURCE_FAILURE;return module_result(1,1);}
        struct kernel_surface_result memory=IC(iso.alloc,iso.heap,1,sizeof(probe));if(memory.status){r->status=CB_RESOURCE_FAILURE;return module_result(1,1);}struct iso_volume *v=(void *)memory.value;*v=probe;
        if(v->disk.control(v->disk.cookie,BLOCK_RETAIN,0,0,0,0).status){IC(iso.free,iso.heap,v,0);r->status=CB_RESOURCE_FAILURE;return module_result(1,1);}uint64_t flags=ilock();v->id=++iso.serial;v->next=iso.first;iso.first=v;iunlock(flags);r->cookie=v->id;r->status=CB_OK;return module_result(1,1);
    }
    uint64_t flags=ilock();struct iso_volume *v=iso.first;while(v && v->id!=a)v=v->next;if(!v){iunlock(flags);return module_result(1,1);}if(v->busy){iunlock(flags);r->status=CB_BUSY;return module_result(1,1);}v->busy=1;iunlock(flags);
    if(r->operation==FS_UNMOUNT){r->status=v->disk.control(v->disk.cookie,BLOCK_RELEASE,0,0,0,0).status?CB_RESOURCE_FAILURE:CB_OK;if(!r->status){flags=ilock();struct iso_volume **at=&iso.first;while(*at!=v)at=&(*at)->next;*at=v->next;iunlock(flags);IC(iso.free,iso.heap,v,0);return module_result(1,1);}}
    else if(r->operation==FS_CHMOD || r->operation==FS_CHOWN || r->operation==FS_WRITE || r->operation==FS_REMOVE || r->operation==FS_RMDIR || r->operation==FS_MKDIR || (r->operation==FS_PREPARE && r->mode!=CB_FILE_READ))r->status=CB_READ_ONLY;
    else if(r->operation==FS_SYNC)r->status=CB_OK;
    else {struct iso_node entry;r->status=lookup(v,r->path,&entry);if(!r->status){
        if(r->operation==FS_STAT)r->stat=(struct filesystem_stat){.bytes=entry.bytes,.type=entry.directory?FS_DIRECTORY:FS_REGULAR,.permissions=entry.directory?0555:0444,.valid=1};
        else if(r->operation==FS_LIST){int exists;struct iso_node child;r->status=next(v,&entry,&r->cursor,&child,&r->entry,&exists);if(!r->status)r->actual=exists;}
        else if(r->operation==FS_PREPARE){if(entry.directory)r->status=CB_BAD_REQUEST;}
        else if(r->operation==FS_READ){if(entry.directory || (r->bytes && !r->buffer))r->status=CB_BAD_REQUEST;else if(r->offset<entry.bytes){size_t count=r->bytes;if(count>entry.bytes-r->offset)count=entry.bytes-r->offset;r->status=bytes(v,(uint64_t)entry.extent*2048+r->offset,r->buffer,count);if(!r->status)r->actual=count;}}
        else r->status=CB_BAD_REQUEST;
    }}
    flags=ilock();v->busy=0;iunlock(flags);return module_result(1,1);
}
struct kernel_surface_result iso9660_fs_surface_backend(MODULE_ARGS){MODULE_UNUSED();if(!a || b<sizeof(struct filesystem_backend) || !iso.hub)return module_result(0,0);kernel_surface_call call=kernel_surface_find(iso.hub,"iso9660_fs.invoke");if(!call)return module_result(0,0);*(struct filesystem_backend *)a=(struct filesystem_backend){sizeof(struct filesystem_backend),2,1,call};return module_result(1,1);}
