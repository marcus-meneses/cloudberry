#include "partition.h"
#include "block-util.h"
#include "threads.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
struct slice {struct slice *next;struct block_device parent,block;uint64_t start,refs;unsigned busy,disconnected;uint64_t cached;uint8_t scratch[4096],entries[4096];};
static struct {const struct kernel_surfaces *hub;uintptr_t heap;uint64_t serial;struct slice *first;kernel_surface_call cpu,thread,irq,alloc,free;} state;
static int foreground(void){uint64_t flags;__asm__ volatile("pushfq;pop %0":"=r"(flags));return (flags&512) && state.hub && !CALL(state.cpu,CPU_CURRENT,0,0).value && !CALL(state.cpu,CPU_DEPTH,0,0).value && !CALL(state.irq,0,0,0).value && CALL(state.thread,THREAD_CURRENT,0,0).value;
}
static uint64_t lock(void){return CALL(state.cpu,CPU_ENTER,0,0).value;
}
static void unlock(uint64_t f){CALL(state.cpu,CPU_LEAVE,f,0);}
static struct slice *find(uint64_t id){for(struct slice *s=state.first;s;s=s->next)if(s->block.cookie==id)return s;
    return NULL;
    }
struct gpt {uint64_t alternate,first,last,array;uint32_t count,stride,crc;uint8_t guid[16];};
static int header(struct slice *s,uint64_t lba,struct gpt *g){
    unsigned sector=s->parent.block_bytes;if(lba>=s->parent.blocks || !block_bytes_read(&s->parent,lba*sector,s->scratch,sector,s->scratch))return 0;
    uint8_t *p=s->scratch;unsigned bytes=disk_u32(p+12);uint32_t expected=disk_u32(p+16);
    if(!disk_equal(p,"EFI PART",8) || disk_u32(p+8)!=0x10000 || bytes<92 || bytes>sector || disk_u32(p+20) || disk_u64(p+24)!=lba)return 0;
    for(unsigned i=16;i<20;++i)p[i]=0;
    if(~disk_crc(UINT32_MAX,p,bytes,0xedb88320)!=expected)return 0;
    *g=(struct gpt){.alternate=disk_u64(p+32),.first=disk_u64(p+40),.last=disk_u64(p+48),.array=disk_u64(p+72),.count=disk_u32(p+80),.stride=disk_u32(p+84),.crc=disk_u32(p+88)};disk_copy(g->guid,p+56,16);
    uint64_t array_bytes=(uint64_t)g->count*g->stride;
    if(g->alternate>=s->parent.blocks || g->first<2 || g->first>g->last || g->last>=s->parent.blocks-1 || !g->count || g->stride<128 || (g->stride&(g->stride-1)) || g->array>=s->parent.blocks || array_bytes>s->parent.blocks*sector-g->array*sector)return 0;
    uint64_t sectors=(array_bytes+sector-1)/sector;
    return (lba==1?g->array>=2 && g->array+sectors<=g->first:g->array>g->last && g->array+sectors<=lba);
}
static int array_crc(struct slice *s,const struct gpt *g){
    uint64_t offset=g->array*s->parent.block_bytes,left=(uint64_t)g->count*g->stride;uint32_t crc=UINT32_MAX;
    while(left){size_t n=left>4096?4096:left;if(!block_bytes_read(&s->parent,offset,s->scratch,n,s->scratch))return 0;
    crc=disk_crc(crc,s->scratch,n,0xedb88320);left-=n;offset+=n;}return ~crc==g->crc;
}
static int entry_read(struct slice *s,uint64_t offset,void *out){
    uint64_t start=offset&~UINT64_C(4095),total=s->parent.blocks*s->parent.block_bytes;
    if(s->cached!=start){size_t n=total-start;if(n>4096)n=4096;if(!block_bytes_read(&s->parent,start,s->entries,n,s->scratch))return 0;s->cached=start;}
    disk_copy(out,s->entries+(offset-start),128);return 1;
}
static int select_partition(struct slice *s,const void *guid){
    s->cached=UINT64_MAX;
    struct gpt primary,backup;if(!header(s,1,&primary) || primary.alternate!=s->parent.blocks-1 || !header(s,primary.alternate,&backup) || backup.alternate!=1 || primary.first!=backup.first || primary.last!=backup.last || primary.count!=backup.count || primary.stride!=backup.stride || primary.crc!=backup.crc || !disk_equal(primary.guid,backup.guid,16) || !array_crc(s,&primary) || !array_crc(s,&backup))return -1;
    uint8_t entry[128],zero[16]={0};unsigned matched=0;uint64_t start=0,end=0;
    for(uint32_t i=0;i<primary.count;++i){uint64_t offset=primary.array*s->parent.block_bytes+(uint64_t)i*primary.stride;
        if(!entry_read(s,offset,entry))return -1;
        if(disk_equal(entry,zero,16))continue;
        uint64_t a=disk_u64(entry+32),b=disk_u64(entry+40);if(a<primary.first || a>b || b>primary.last)return -1;
        if(disk_equal(entry+16,guid,16)){if(++matched!=1)return -1;
    start=a;end=b;}
    }
    if(!matched)return 0;
    for(uint32_t i=0;i<primary.count;++i){if(!entry_read(s,primary.array*s->parent.block_bytes+(uint64_t)i*primary.stride,entry))return -1;
        if(disk_equal(entry,zero,16) || disk_equal(entry+16,guid,16))continue;
        if(disk_u64(entry+32)<=end && disk_u64(entry+40)>=start)return -1;
    }
    s->start=start;s->block.blocks=end-start+1;return 1;
}
struct kernel_surface_result partition_surface_init(MODULE_ARGS){
    MODULE_UNUSED();const struct kernel_surfaces *hub=(void *)a;if(state.hub)return module_result(1,state.hub==hub && state.heap==b);
    if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC || !b)return module_result(0,0);
    state.cpu=kernel_surface_find_version(hub,"cpu.control",1,0);state.thread=kernel_surface_find_version(hub,"threads.control",1,0);state.irq=kernel_surface_find_version(hub,"irq.context",1,0);state.alloc=kernel_surface_find_version(hub,"mem.calloc",1,0);state.free=kernel_surface_find_version(hub,"mem.free",1,0);
    if(!state.cpu || !state.thread || !state.irq || !state.alloc || !state.free)return module_result(0,0);
    state.hub=hub;state.heap=b;return module_result(1,1);
}
struct kernel_surface_result partition_surface_find(MODULE_ARGS){
    MODULE_UNUSED();const struct block_device *parent=(void *)a;if(!foreground() || !block_valid(parent) || !b || !c || !block_capacity(d) || state.serial==UINT64_MAX)return module_result(0,0);
    uint8_t zero[16]={0};if(disk_equal((void *)b,zero,16) || parent->control(parent->cookie,BLOCK_RETAIN,0,0,0,0).status)return module_result(0,0);
    struct kernel_surface_result r=CALL(state.alloc,state.heap,1,sizeof(struct slice));if(r.status){parent->control(parent->cookie,BLOCK_RELEASE,0,0,0,0);return module_result(0,0);
    }struct slice *s=(void *)r.value;block_copy(&s->parent,parent);block_copy(&s->block,parent);
    int selected=select_partition(s,(void *)b);if(selected!=1){parent->control(parent->cookie,BLOCK_RELEASE,0,0,0,0);CALL(state.free,state.heap,s,0);return module_result(selected==0?PARTITION_NOT_FOUND:PARTITION_CORRUPT,0);
    }
    uint64_t flags=lock();if(state.serial==UINT64_MAX){unlock(flags);parent->control(parent->cookie,BLOCK_RELEASE,0,0,0,0);CALL(state.free,state.heap,s,0);return module_result(0,0);}
    s->block.minor=1;s->block.cookie=++state.serial;s->block.first_block+=s->start;s->block.read=kernel_surface_find_version(state.hub,"partition.read",1,0);s->block.control=kernel_surface_find_version(state.hub,"partition.control",1,0);if(block_writable(parent)){s->block.write=kernel_surface_find_version(state.hub,"partition.write",1,0);s->block.flush=kernel_surface_find_version(state.hub,"partition.flush",1,0);}
    s->next=state.first;state.first=s;unlock(flags);block_export((void *)c,&s->block,d);return module_result(s->block.cookie,1);
}
struct kernel_surface_result partition_surface_read(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground())return module_result(0,0);
    uint64_t flags=lock();struct slice *s=find(a);if(!s || s->disconnected || s->busy || b>s->block.blocks || c>s->block.blocks-b || c>s->block.max_transfer_bytes/s->block.block_bytes || e<c*s->block.block_bytes || (c && !d)){unlock(flags);return module_result(0,0);
    }s->busy=1;unlock(flags);
    struct kernel_surface_result r=s->parent.read(s->parent.cookie,s->start+b,c,d,e,0);flags=lock();s->busy=0;unlock(flags);return r;
}
struct kernel_surface_result partition_surface_write(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground())return module_result(0,0);
    uint64_t flags=lock();struct slice *s=find(a);if(!s || s->disconnected || s->busy || !block_writable(&s->block) || b>s->block.blocks || c>s->block.blocks-b || c>s->block.max_transfer_bytes/s->block.block_bytes || e<c*s->block.block_bytes || (c && !d)){unlock(flags);return module_result(0,0);}
    s->busy=1;unlock(flags);struct kernel_surface_result r=s->parent.write(s->parent.cookie,s->start+b,c,d,e,0);flags=lock();s->busy=0;unlock(flags);return r;
}
struct kernel_surface_result partition_surface_flush(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground())return module_result(0,0);uint64_t flags=lock();struct slice *s=find(a);if(!s || s->disconnected || s->busy || !block_writable(&s->block)){unlock(flags);return module_result(0,0);}
    s->busy=1;unlock(flags);struct kernel_surface_result r=s->parent.flush(s->parent.cookie,0,0,0,0,0);flags=lock();s->busy=0;unlock(flags);return r;
}
struct kernel_surface_result partition_surface_control(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground())return module_result(0,0);
    uint64_t flags=lock();struct slice *s=find(a);int ok=s && (b==BLOCK_RETAIN?(!s->disconnected && !s->busy && s->refs<UINT64_MAX):b==BLOCK_RELEASE && s->refs);if(ok){if(b==BLOCK_RETAIN)++s->refs;else --s->refs;}int reap=ok && b==BLOCK_RELEASE && !s->refs && !s->busy && s->disconnected;uint64_t cookie=s?s->block.cookie:0;unlock(flags);
    if(reap)(void)partition_surface_close(cookie,0,0,0,0,0);
    return module_result(1,ok);
}
struct kernel_surface_result partition_surface_close(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground())return module_result(0,0);
    uint64_t flags=lock();struct slice *s=find(a);if(!s || s->refs || s->busy){unlock(flags);return module_result(0,0);
    }s->busy=1;unlock(flags);
    if(s->parent.control(s->parent.cookie,BLOCK_RELEASE,0,0,0,0).status){flags=lock();s->busy=0;unlock(flags);return module_result(0,0);
    }flags=lock();struct slice **p=&state.first;while(*p!=s)p=&(*p)->next;*p=s->next;unlock(flags);CALL(state.free,state.heap,s,0);return module_result(1,1);
}

struct kernel_surface_result partition_surface_disconnect(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground() || !a || !b)return module_result(0,0);uint64_t removed=0;int ok=1;
    uint64_t flags=lock();for(struct slice *s=state.first;s;s=s->next)if((uintptr_t)s->block.root_read==a && s->block.root_cookie==b)s->disconnected=1;unlock(flags);
    for(;;){flags=lock();struct slice *s=state.first;while(s && (!s->disconnected || (uintptr_t)s->block.root_read!=a || s->block.root_cookie!=b || s->refs || s->busy))s=s->next;
        if(!s){for(s=state.first;s;s=s->next)if(s->disconnected && (uintptr_t)s->block.root_read==a && s->block.root_cookie==b && s->busy)ok=0;unlock(flags);break;}uint64_t cookie=s->block.cookie;unlock(flags);
        if(partition_surface_close(cookie,0,0,0,0,0).status){ok=0;break;}++removed;
    }return module_result(removed,ok);
}

struct kernel_surface_result partition_surface_identity(MODULE_ARGS){
    MODULE_UNUSED();const struct block_device *device=(void *)a;if(!foreground() || !block_valid(device) || !b || CALL(device->control,device->cookie,BLOCK_RETAIN,0).status)return module_result(0,0);
    struct kernel_surface_result memory=CALL(state.alloc,state.heap,1,sizeof(struct slice));if(memory.status){CALL(device->control,device->cookie,BLOCK_RELEASE,0);return module_result(0,0);}struct slice *s=(void *)memory.value;block_copy(&s->parent,device);
    struct gpt primary,backup;int ok=header(s,1,&primary) && primary.alternate==device->blocks-1 && header(s,primary.alternate,&backup) && backup.alternate==1 && primary.first==backup.first && primary.last==backup.last && primary.count==backup.count && primary.stride==backup.stride && primary.crc==backup.crc && disk_equal(primary.guid,backup.guid,16) && array_crc(s,&primary) && array_crc(s,&backup);
    if(ok)disk_copy((void *)b,primary.guid,16);
    CALL(state.free,state.heap,s,0);if(CALL(device->control,device->cookie,BLOCK_RELEASE,0).status)ok=0;return module_result(1,ok);
}
