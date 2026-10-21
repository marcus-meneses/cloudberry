#include "ext4-fs.h"
#include "block-util.h"
#include "ext4-fs/engine.h"
#include "threads.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
struct inode {uint64_t number,size;uint32_t flags,generation,uid,gid;uint16_t mode;uint8_t map[60];};
struct volume {struct volume *next;struct block_device device;struct ext4_info info;uint64_t first_data,groups;uint32_t blocks_group,inodes_group,descriptor_bytes,seed,metadata;unsigned busy,disconnected;struct ext4_engine *engine;uint8_t scratch[4096],node[4096],data[4096];};
struct fs_waiter {struct fs_waiter *next;uint64_t cookie,thread;};
static struct {struct fs_waiter *waiters;const struct kernel_surfaces *hub;uintptr_t heap;uint64_t serial;unsigned mounting;struct volume *first;kernel_surface_call cpu,thread,irq,alloc,free;} state;
static int foreground(void){uint64_t flags;__asm__ volatile("pushfq;pop %0":"=r"(flags));return (flags&512) && state.hub && !CALL(state.cpu,CPU_CURRENT,0,0).value && !CALL(state.cpu,CPU_DEPTH,0,0).value && !CALL(state.irq,0,0,0).value && CALL(state.thread,THREAD_CURRENT,0,0).value;
}
static uint64_t lock(void){return CALL(state.cpu,CPU_ENTER,0,0).value;
}
static void unlock(uint64_t f){CALL(state.cpu,CPU_LEAVE,f,0);}
static struct volume *find(uint64_t id){for(struct volume *v=state.first;v;v=v->next)if(v->info.cookie==id)return v;
    return NULL;
}
/* Wait by stable cookie, never retain a volume pointer across a block. This
 * serializes one mount's calls and survives disconnect/unmount while queued. */
static void wake_volume(uint64_t cookie){for(struct fs_waiter *w=state.waiters;w;w=w->next)if(w->cookie==cookie)(void)CALL(state.thread,THREAD_WAKE,w->thread,0);}
static struct volume *acquire(uint64_t id){
    if(!foreground())return NULL;
    for(;;){uint64_t f=lock();struct volume *v=find(id);if(!v || v->disconnected){unlock(f);return NULL;}if(!v->busy){v->busy=1;v->info.error=EXT4_OK;unlock(f);if(v->engine && !ext4_engine_enter(v->engine)){f=lock();v->busy=0;v->info.error=ext4_engine_error(v->engine);if(!v->info.error)v->info.error=EXT4_BUSY;wake_volume(id);unlock(f);return NULL;}return v;}
        struct fs_waiter waiter={.cookie=id,.thread=CALL(state.thread,THREAD_CURRENT,0,0).value,.next=state.waiters};state.waiters=&waiter;unlock(f);struct kernel_surface_result blocked=CALL(state.thread,THREAD_BLOCK,0,0);f=lock();struct fs_waiter **at=&state.waiters;while(*at!=&waiter)at=&(*at)->next;*at=waiter.next;unlock(f);if(blocked.status)return NULL;
    }
}
static void finish(struct volume *v){if(v->engine){if(!v->info.error)v->info.error=ext4_engine_error(v->engine);ext4_engine_leave();}uint64_t f=lock();v->busy=0;wake_volume(v->info.cookie);unlock(f);}
static int fail(struct volume *v,unsigned error){v->info.error=error;return 0;
}
static uint32_t crc(uint32_t seed,const void *p,size_t n){return disk_crc(seed,p,n,0x82f63b78);
}
static uint32_t inode_seed(struct volume *v,const struct inode *i){uint8_t words[8];for(unsigned n=0;n<4;++n){words[n]=i->number>>(8*n);words[n+4]=i->generation>>(8*n);}return crc(v->seed,words,8);
}
static int bytes(struct volume *v,uint64_t offset,void *out,size_t n){if(!block_bytes_read(&v->device,offset,out,n,v->scratch))return fail(v,EXT4_IO);
    return 1;
}
static int block(struct volume *v,uint64_t n,void *out){if(n>=v->info.blocks)return fail(v,EXT4_CORRUPT);
    return bytes(v,n*v->info.block_bytes,out,v->info.block_bytes);
}
static int super(struct volume *v,int writable){
    uint8_t *s=v->data;if(!bytes(v,1024,s,1024))return 0;
    if(disk_u16(s+56)!=0xef53)return fail(v,EXT4_CORRUPT);
    uint32_t incompat=disk_u32(s+96),ro=disk_u32(s+100),compat=disk_u32(s+92),log=disk_u32(s+24);
    if(log>2 || (incompat&~(writable?0x22c6u:0x22c2u)) || !(incompat&0x40) || (ro&~0x46bu) || (compat&~0x23cu) || disk_u32(s+76)!=1)return fail(v,EXT4_UNSUPPORTED);
    if((!writable && (!(disk_u16(s+58)&1) || (incompat&4))) || (disk_u16(s+58)&2) || disk_u32(s+232))return fail(v,EXT4_CORRUPT);
    if(writable && (!(compat&4) || (incompat&0x2000) || (1024u<<log)<v->device.block_bytes))return fail(v,EXT4_UNSUPPORTED);
    v->metadata=!!(ro&0x400);if((incompat&0x2000) && !v->metadata)return fail(v,EXT4_CORRUPT);
    if(v->metadata && (s[373]!=1 || crc(UINT32_MAX,s,1020)!=disk_u32(s+1020)))return fail(v,EXT4_CORRUPT);
    v->info.block_bytes=1024u<<log;v->info.blocks=disk_u32(s+4);if(incompat&0x80)v->info.blocks|=(uint64_t)disk_u32(s+336)<<32;
    v->info.inodes=disk_u32(s);v->info.inode_bytes=disk_u16(s+88);v->blocks_group=disk_u32(s+32);v->inodes_group=disk_u32(s+40);v->first_data=disk_u32(s+20);v->descriptor_bytes=(incompat&0x80)?disk_u16(s+254):32;
    if(!v->info.blocks || v->info.blocks>v->device.blocks*v->device.block_bytes/v->info.block_bytes || !v->info.inodes || v->first_data!=(log?0u:1u) || v->info.blocks<=v->first_data || !v->blocks_group || v->blocks_group>v->info.block_bytes*8 || !v->inodes_group || v->inodes_group>v->info.block_bytes*8 || v->info.inode_bytes<128 || v->info.inode_bytes>512 || (v->info.inode_bytes&(v->info.inode_bytes-1)) || v->descriptor_bytes<32 || v->descriptor_bytes>64 || ((incompat&0x80) && v->descriptor_bytes!=64))return fail(v,EXT4_UNSUPPORTED);
    v->groups=(v->info.blocks-v->first_data+v->blocks_group-1)/v->blocks_group;
    if((v->info.inodes+v->inodes_group-1)/v->inodes_group>v->groups || (v->first_data+1)*v->info.block_bytes+v->groups*v->descriptor_bytes>v->info.blocks*v->info.block_bytes)return fail(v,EXT4_CORRUPT);
    disk_copy(v->info.uuid,s+104,16);v->seed=(incompat&0x2000)?disk_u32(s+624):crc(UINT32_MAX,s+104,16);v->info.readonly=1;return 1;
}
static int inode_read(struct volume *v,uint64_t number,struct inode *out){
    if(!number || number>v->info.inodes)return fail(v,EXT4_CORRUPT);
    uint32_t group=(number-1)/v->inodes_group;uint64_t index=(number-1)%v->inodes_group;uint8_t desc[64],raw[512];
    if(!bytes(v,(v->first_data+1)*v->info.block_bytes+(uint64_t)group*v->descriptor_bytes,desc,v->descriptor_bytes))return 0;
    if(v->metadata){uint16_t expected=disk_u16(desc+30);desc[30]=desc[31]=0;uint8_t g[4];for(unsigned n=0;n<4;++n)g[n]=group>>(8*n);if((uint16_t)crc(crc(v->seed,g,4),desc,v->descriptor_bytes)!=expected)return fail(v,EXT4_CORRUPT);
    }
    uint64_t table=disk_u32(desc+8);if(v->descriptor_bytes==64)table|=(uint64_t)disk_u32(desc+40)<<32;
    uint64_t table_blocks=((uint64_t)v->inodes_group*v->info.inode_bytes+v->info.block_bytes-1)/v->info.block_bytes;
    if(!table || table>=v->info.blocks || table_blocks>v->info.blocks-table)return fail(v,EXT4_CORRUPT);
    if(!bytes(v,table*v->info.block_bytes+index*v->info.inode_bytes,raw,v->info.inode_bytes))return 0;
    *out=(struct inode){.number=number,.mode=disk_u16(raw),.uid=disk_u16(raw+2)|(uint32_t)disk_u16(raw+120)<<16,.gid=disk_u16(raw+24)|(uint32_t)disk_u16(raw+122)<<16,.size=disk_u32(raw+4)|(uint64_t)disk_u32(raw+108)<<32,.flags=disk_u32(raw+32),.generation=disk_u32(raw+100)};disk_copy(out->map,raw+40,60);
    if(v->info.inode_bytes>128 && disk_u16(raw+128)>v->info.inode_bytes-128)return fail(v,EXT4_CORRUPT);
    if((out->mode&0xf000)==0x8000 && out->size>UINT64_C(0x100000000)*v->info.block_bytes)return fail(v,EXT4_CORRUPT);
    if(v->metadata){uint32_t expected=disk_u16(raw+124);raw[124]=raw[125]=0;int high=v->info.inode_bytes>128 && disk_u16(raw+128)>=4;
        if(high){expected|=(uint32_t)disk_u16(raw+130)<<16;raw[130]=raw[131]=0;}
        uint32_t calculated=crc(inode_seed(v,out),raw,v->info.inode_bytes);if(!high)calculated&=65535;if(calculated!=expected)return fail(v,EXT4_CORRUPT);
    }
    if((out->flags&0x10000000) || (out->flags&0x800))return fail(v,EXT4_UNSUPPORTED);
    return 1;
}
/* Extent traversal follows strictly decreasing depths; validates sorted ranges
 * and physical bounds before reading. Holes and unwritten extents return zero. */
static int mapped(struct volume *v,const struct inode *i,uint32_t logical,uint64_t *physical){
    if(!(i->flags&0x80000))return fail(v,EXT4_UNSUPPORTED);
    uint8_t root[60];disk_copy(root,i->map,60);const uint8_t *node=root;unsigned capacity=60,expected=UINT32_MAX;uint64_t visited[6]={0};
    for(unsigned level=0;level<6;++level){unsigned count=disk_u16(node+2),maximum=disk_u16(node+4),depth=disk_u16(node+6);unsigned limit=(capacity-12)/12;
        if(disk_u16(node)!=0xf30a || !maximum || maximum>limit || count>maximum || depth>5 || (expected!=UINT32_MAX && depth!=expected))return fail(v,EXT4_CORRUPT);
        if(level && v->metadata){unsigned tail=12+maximum*12;if(tail+4>capacity || crc(inode_seed(v,i),node,tail)!=disk_u32(node+tail))return fail(v,EXT4_CORRUPT);
    }
        uint64_t next=0,end=0;int found=0;
        for(unsigned n=0;n<count;++n){const uint8_t *e=node+12+n*12;uint32_t first=disk_u32(e);if(n && first<end)return fail(v,EXT4_CORRUPT);
            if(depth){end=(uint64_t)first+1;uint64_t child=disk_u32(e+4)|(uint64_t)disk_u16(e+8)<<32;if(!child || child>=v->info.blocks)return fail(v,EXT4_CORRUPT);
    if(first<=logical)next=child;}
            else {unsigned length=disk_u16(e+4);int unwritten=length>32768;if(unwritten)length-=32768;uint64_t disk=disk_u32(e+8)|(uint64_t)disk_u16(e+6)<<32;end=(uint64_t)first+length;
                if(!length || end>UINT64_C(0x100000000) || !disk || disk>=v->info.blocks || length>v->info.blocks-disk)return fail(v,EXT4_CORRUPT);
                if(logical>=first && logical<end){*physical=unwritten?0:disk+logical-first;found=1;}}
        }
        if(!depth){if(!found)*physical=0;return 1;
    }if(!next){*physical=0;return 1;
    }
        for(unsigned n=0;n<level;++n)if(visited[n]==next)return fail(v,EXT4_CORRUPT);
    visited[level]=next;
        if(!block(v,next,v->node))return 0;
    node=v->node;capacity=v->info.block_bytes;expected=depth-1;
    }return fail(v,EXT4_CORRUPT);
}
static int file_block(struct volume *v,const struct inode *i,uint64_t logical){
    if(logical>UINT32_MAX)return fail(v,EXT4_UNSUPPORTED);
    uint64_t physical;if(!mapped(v,i,logical,&physical))return 0;
    if(physical)return block(v,physical,v->data);
    for(unsigned n=0;n<v->info.block_bytes;++n)v->data[n]=0;
    return 1;
}
static int directory_next(struct volume *v,const struct inode *i,uint64_t *cursor,struct ext4_dirent *out){
    if((i->mode&0xf000)!=0x4000)return fail(v,EXT4_TYPE);
    if(i->flags&0x1000)return fail(v,EXT4_UNSUPPORTED); /* HTree checksum format is a separate supported-feature milestone. */
    while(*cursor<i->size){uint64_t logical=*cursor/v->info.block_bytes;unsigned within=*cursor%v->info.block_bytes;
        if(!file_block(v,i,logical))return 0;
        unsigned usable=v->info.block_bytes;if(v->metadata){if(usable<12)return fail(v,EXT4_CORRUPT);
    uint8_t *tail=v->data+usable-12;
            if(disk_u32(tail) || disk_u16(tail+4)!=12 || tail[6] || tail[7]!=0xde || crc(inode_seed(v,i),v->data,usable-12)!=disk_u32(tail+8))return fail(v,EXT4_CORRUPT);
    usable-=12;}
        if(within>=usable){*cursor=(logical+1)*v->info.block_bytes;continue;}
        if(usable-within<8)return fail(v,EXT4_CORRUPT);
        const uint8_t *p=v->data+within;unsigned record=disk_u16(p+4),length=p[6];uint32_t number=disk_u32(p);
        if(usable-within<8 || record<8 || (record&3) || record>usable-within || record>i->size-*cursor || length>record-8 || p[7]>7 || number>v->info.inodes)return fail(v,EXT4_CORRUPT);
        *cursor+=record;if(!number)continue;if(!length)return fail(v,EXT4_CORRUPT);
        for(unsigned n=0;n<length;++n)if(!p[8+n] || p[8+n]=='/')return fail(v,EXT4_CORRUPT);
        *out=(struct ext4_dirent){.inode=number,.type=p[7],.name_bytes=length};disk_copy(out->name,p+8,length);return 1;
    }*out=(struct ext4_dirent){0};return 1;
}
static int lookup(struct volume *v,const char *path,struct inode *out){
    if(!path || *path!='/')return fail(v,EXT4_ARGUMENT);
    if(!inode_read(v,2,out))return 0;
    const char *p=path;while(*p){while(*p=='/')++p;if(!*p)break;const char *first=p;unsigned length=0;while(*p && *p!='/'){if(++length>255)return fail(v,EXT4_ARGUMENT);
    ++p;}
        uint64_t cursor=0;struct ext4_dirent entry;int found=0;
        do{if(!directory_next(v,out,&cursor,&entry))return 0;
    if(entry.inode && entry.name_bytes==length && disk_equal(entry.name,first,length)){found=1;break;}}while(entry.inode);
        if(!found)return fail(v,EXT4_NOT_FOUND);
    if(!inode_read(v,entry.inode,out))return 0;
        if((out->mode&0xf000)==0xa000)return fail(v,EXT4_UNSUPPORTED);
    }if(p>path && p[-1]=='/' && (out->mode&0xf000)!=0x4000)return fail(v,EXT4_TYPE);
    return 1;
}
struct kernel_surface_result ext4_fs_surface_init(MODULE_ARGS){
    MODULE_UNUSED();const struct kernel_surfaces *hub=(void *)a;if(state.hub)return module_result(1,state.hub==hub && state.heap==b);
    if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC || !b)return module_result(0,0);
    state.cpu=kernel_surface_find_version(hub,"cpu.control",1,0);state.thread=kernel_surface_find_version(hub,"threads.control",1,0);state.irq=kernel_surface_find_version(hub,"irq.context",1,0);state.alloc=kernel_surface_find_version(hub,"mem.calloc",1,0);state.free=kernel_surface_find_version(hub,"mem.free",1,0);
    if(!state.cpu || !state.thread || !state.irq || !state.alloc || !state.free)return module_result(0,0);
    if(!ext4_engine_setup(hub,b))return module_result(0,0);
    state.hub=hub;state.heap=b;return module_result(1,1);
}
static int overlaps(const struct block_device *a,const struct block_device *b){
    if(a->root_read!=b->root_read || a->root_cookie!=b->root_cookie)return 0;
    return a->first_block<=b->first_block?a->blocks>b->first_block-a->first_block:b->blocks>a->first_block-b->first_block;
}
struct kernel_surface_result ext4_fs_surface_mount(MODULE_ARGS){
    MODULE_UNUSED();const struct block_device *device=(void *)a;unsigned writable=c&EXT4_MOUNT_WRITE;
    if(!foreground() || !block_valid(device) || !b || (c&~EXT4_MOUNT_WRITE))return module_result(EXT4_ARGUMENT,0);
    if(writable && !block_writable(device))return module_result(EXT4_READ_ONLY,0);
    struct block_device copy;block_copy(&copy,device);uint64_t flags=lock();
    if(state.mounting || state.serial==UINT64_MAX){unlock(flags);return module_result(EXT4_BUSY,0);}
    for(struct volume *p=state.first;p;p=p->next)if((writable || !p->info.readonly) && overlaps(&copy,&p->device)){unlock(flags);return module_result(EXT4_BUSY,0);}
    state.mounting=1;uint64_t id=++state.serial;unlock(flags);unsigned error=EXT4_IO;struct volume *v=NULL;
    if(device->control(device->cookie,BLOCK_RETAIN,0,0,0,0).status)goto exit;
    struct kernel_surface_result r=CALL(state.alloc,state.heap,1,sizeof(*v));if(r.status){error=EXT4_MEMORY;goto release;}
    v=(void *)r.value;block_copy(&v->device,device);v->info.cookie=id;
    if(!super(v,writable)){error=v->info.error;goto cleanup;}
    /* Replay must precede reading inodes potentially covered by a committed log. */
    if(writable){
        if(!ext4_engine_enter(NULL)){error=EXT4_BUSY;goto cleanup;}
        v->engine=ext4_engine_mount(device,0,id,&error);ext4_engine_leave();
        if(!v->engine)goto cleanup;
        v->info.readonly=0;
    }else {
        struct inode root;if(!inode_read(v,2,&root) || (root.mode&0xf000)!=0x4000){error=v->info.error?v->info.error:EXT4_CORRUPT;goto cleanup;}error=EXT4_OK;
        /* Seeded checksums retain the qualified read-only parser; the private
         * journal engine currently refuses that feature for writable mounts. */
        if(!(disk_u32(v->data+96)&0x2000)){
            if(!ext4_engine_enter(NULL)){error=EXT4_BUSY;goto cleanup;}
            v->engine=ext4_engine_mount(device,1,id,&error);ext4_engine_leave();if(!v->engine)goto cleanup;
        }
    }
    flags=lock();v->next=state.first;state.first=v;state.mounting=0;unlock(flags);*(struct ext4_info *)b=v->info;
    if(error){v->info.error=error;*(struct ext4_info *)b=v->info;return module_result(error,0);}
    return module_result(id,1);
cleanup:CALL(state.free,state.heap,v,0);
release:device->control(device->cookie,BLOCK_RELEASE,0,0,0,0);
exit:flags=lock();state.mounting=0;unlock(flags);return module_result(error,0);
}
struct kernel_surface_result ext4_fs_surface_unmount(MODULE_ARGS){
    MODULE_UNUSED();struct volume *v=acquire(a);if(!v)return module_result(0,0);
    if(v->engine){if(!ext4_engine_unmount(v->engine)){finish(v);return module_result(0,0);}v->engine=NULL;ext4_engine_leave();}
    if(v->device.control(v->device.cookie,BLOCK_RELEASE,0,0,0,0).status){finish(v);return module_result(0,0);
    }
    uint64_t flags=lock();struct volume **p=&state.first;while(*p!=v)p=&(*p)->next;*p=v->next;wake_volume(v->info.cookie);unlock(flags);CALL(state.free,state.heap,v,0);return module_result(1,1);
}
struct kernel_surface_result ext4_fs_surface_stat(MODULE_ARGS){
    MODULE_UNUSED();struct volume *v=acquire(a);if(!v)return module_result(0,0);
    if(v->engine){struct ext4_stat st;int ok=c && ext4_engine_stat(v->engine,(void *)b,&st);if(ok)disk_copy((void *)c,&st,d>=sizeof(st)?sizeof(st):24);finish(v);return module_result(1,ok);}
    struct inode i;int ok=c && lookup(v,(void *)b,&i);if(ok){struct ext4_stat st={i.number,i.size,i.mode,i.flags,i.uid,i.gid};disk_copy((void *)c,&st,d>=sizeof(st)?sizeof(st):24);}finish(v);return module_result(1,ok);
}
struct kernel_surface_result ext4_fs_surface_read(MODULE_ARGS){
    MODULE_UNUSED();struct volume *v=acquire(a);if(!v)return module_result(0,0);
    if(v->engine){struct kernel_surface_result r=(!e || d)?ext4_engine_read(v->engine,(void *)b,c,(void *)d,e):module_result(0,0);finish(v);return r;}
    struct inode i;size_t done=0;int ok=(!e || d) && lookup(v,(void *)b,&i);
    if(ok && (i.mode&0xf000)!=0x8000)ok=fail(v,EXT4_TYPE);
    if(ok && c<i.size){uint64_t left=i.size-c;if(left>e)left=e;while(left){if(!file_block(v,&i,c/v->info.block_bytes)){ok=0;break;}unsigned within=c%v->info.block_bytes;size_t n=v->info.block_bytes-within;if(n>left)n=left;disk_copy((uint8_t *)d+done,v->data+within,n);done+=n;c+=n;left-=n;}}
    finish(v);return module_result(done,ok);
}
struct kernel_surface_result ext4_fs_surface_list(MODULE_ARGS){
    MODULE_UNUSED();struct volume *v=acquire(a);if(!v)return module_result(0,0);
    if(v->engine){struct kernel_surface_result r=(c && d)?ext4_engine_list(v->engine,(void *)b,(void *)c,(void *)d):module_result(0,0);finish(v);return r;}
    struct inode i;struct ext4_dirent entry={0};int ok=c && d && lookup(v,(void *)b,&i);uint64_t cursor=c?*(uint64_t *)c:0;
    if(ok && cursor>i.size)ok=fail(v,EXT4_ARGUMENT);
    if(ok)ok=directory_next(v,&i,&cursor,&entry);
    if(ok){*(uint64_t *)c=cursor;*(struct ext4_dirent *)d=entry;}finish(v);return module_result(entry.inode?1:0,ok);
}
struct kernel_surface_result ext4_fs_surface_info(MODULE_ARGS){
    MODULE_UNUSED();if(!state.hub || !b || CALL(state.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);
    uint64_t flags=lock();struct volume *v=find(a);if(v)*(struct ext4_info *)b=v->info;unlock(flags);return module_result(1,v!=NULL);
}

struct kernel_surface_result ext4_fs_surface_write(MODULE_ARGS){
    struct volume *v=acquire(a);if(!v)return module_result(0,0);
    if(v->info.readonly || !v->engine){v->info.error=EXT4_READ_ONLY;finish(v);return module_result(0,0);}
    if((e && !d) || (f&~7u) || ((f&EXT4_WRITE_EXCLUSIVE) && !(f&EXT4_WRITE_CREATE)) || (!(f&EXT4_WRITE_APPEND) && (c>INT64_MAX || e>INT64_MAX-c))){v->info.error=EXT4_ARGUMENT;finish(v);return module_result(0,0);}
    struct kernel_surface_result r=ext4_engine_write(v->engine,(void *)b,c,(void *)d,e,f);finish(v);return r;
}
struct kernel_surface_result ext4_fs_surface_mutate(MODULE_ARGS){
    MODULE_UNUSED();struct volume *v=acquire(a);if(!v)return module_result(0,0);
    if(v->info.readonly || !v->engine){v->info.error=EXT4_READ_ONLY;finish(v);return module_result(0,0);}
    int ok=d<=INT64_MAX && ext4_engine_mutate(v->engine,b,(void *)c,d,(void *)e);finish(v);return module_result(1,ok);
}
struct kernel_surface_result ext4_fs_surface_sync(MODULE_ARGS){
    MODULE_UNUSED();struct volume *v=acquire(a);if(!v)return module_result(0,0);int ok=!v->engine || ext4_engine_sync(v->engine);finish(v);return module_result(1,ok);
}

/* Root identity invalidation: stop new calls immediately. An active call drains
 * before abort/free; a retry never reconnects this mount to replacement media. */
struct kernel_surface_result ext4_fs_surface_disconnect(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground() || !a || !b)return module_result(0,0);int ok=1;uint64_t removed=0;
    uint64_t mark=lock();for(struct volume *v=state.first;v;v=v->next)if((uintptr_t)v->device.root_read==a && v->device.root_cookie==b){v->disconnected=1;v->info.error=EXT4_IO;if(v->engine)ext4_engine_disconnect(v->engine);}unlock(mark);
    for(;;){uint64_t flags=lock();struct volume *v=state.first;
        while(v && ((uintptr_t)v->device.root_read!=a || v->device.root_cookie!=b))v=v->next;
        if(!v){unlock(flags);break;}v->disconnected=1;v->info.error=EXT4_IO;if(v->engine)ext4_engine_disconnect(v->engine);
        if(v->busy){unlock(flags);ok=0;break;}v->busy=1;unlock(flags);
        if(v->engine){if(!ext4_engine_abort_enter(v->engine)){flags=lock();v->busy=0;unlock(flags);ok=0;break;}
            if(!ext4_engine_abort(v->engine)){ext4_engine_leave();flags=lock();v->busy=0;unlock(flags);ok=0;break;}v->engine=NULL;ext4_engine_leave();}
        if(v->device.control(v->device.cookie,BLOCK_RELEASE,0,0,0,0).status){flags=lock();v->busy=0;unlock(flags);ok=0;break;}
        flags=lock();struct volume **p=&state.first;while(*p!=v)p=&(*p)->next;*p=v->next;wake_volume(v->info.cookie);unlock(flags);CALL(state.free,state.heap,v,0);++removed;
    }return module_result(removed,ok);
}

#include "ext4-backend.inc"
