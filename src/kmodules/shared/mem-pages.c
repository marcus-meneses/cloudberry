#include "mem-pages.h"
#include "../../kernel/cpu.h"
#include "../../boot/memory.h"
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
struct frame {uint64_t physical;int mapped;};
struct allocation {
    struct allocation *next;
    uint64_t id,count,physical;
    uintptr_t owner,address,span;
    int stack,disposing;
    struct frame frames[];
};
static struct {
    const struct kernel_surfaces *hub;void *heap;
    kernel_surface_call allocate,free,reserve,release,map,unmap,batch,map_batch,cpu;
    struct allocation *all;uint64_t next_id;
    struct mem_pages_info info;
} state;
static void forget(struct allocation *record) {
    struct allocation **at=&state.all;while (*at!=record) at=&(*at)->next;*at=record->next;
    --state.info.allocations;
    if (record->stack) --state.info.stacks;
    CALL(state.free,state.heap,record,0);
}
static int dispose(struct allocation *r) {
    if (!r->stack) {
        if (state.release(r->physical,r->count,BOOT_RES_PAGES,r->owner,0,0).status) return 0;
        uint64_t flags=state.cpu?CALL(state.cpu,CPU_ENTER,0,0).value:0;state.info.physical_pages-=r->count;
        if(state.cpu)CALL(state.cpu,CPU_LEAVE,flags,0);
    } else {
        if(state.batch)for(size_t base=0;base<r->count;base+=BOOT_UNMAP_BATCH) {
            struct boot_owned_mapping items[BOOT_UNMAP_BATCH];size_t n=0;
            size_t end=base+BOOT_UNMAP_BATCH;if(end>r->count)end=r->count;
            for(size_t i=base;i<end;++i)if(r->frames[i].mapped)
                items[n++]=(struct boot_owned_mapping){r->address+(i+1)*4096,r->frames[i].physical,r->owner};
            if(n && CALL(state.batch,items,n,0).status)return 0;
            for(size_t i=base;i<end;++i)r->frames[i].mapped=0;
        }
        for (size_t i=0;i<r->count;++i) {
        struct frame *f=&r->frames[i];if (!f->physical) continue;
        if (f->mapped) {
            if (CALL(state.unmap,r->address+(i+1)*4096,f->physical,r->owner).status) return 0;
            f->mapped=0;
        }
        if (state.release(f->physical,1,BOOT_RES_STACK,r->owner,0,0).status) return 0;
        f->physical=0;uint64_t flags=state.cpu?CALL(state.cpu,CPU_ENTER,0,0).value:0;
        --state.info.physical_pages;--state.info.stack_pages;
        if(state.cpu)CALL(state.cpu,CPU_LEAVE,flags,0);
    }
    }
    uint64_t flags=state.cpu?CALL(state.cpu,CPU_ENTER,0,0).value:0;
    forget(r);if(state.cpu)CALL(state.cpu,CPU_LEAVE,flags,0);return 1;
}
static struct allocation *find(uint64_t id,uintptr_t owner,int stack) {
    for (struct allocation *r=state.all;r;r=r->next) if (r->id==id && r->owner==owner && r->stack==stack) return r;
    return NULL;
}
static uintptr_t stack_gap(size_t span) {
    uintptr_t at=MEM_STACK_BASE;
    while (span<=MEM_STACK_LIMIT-at) {
        uintptr_t next=at;
        for (struct allocation *r=state.all;r;r=r->next)
            if (r->stack && at<r->address+r->span && r->address<at+span && r->address+r->span>next) next=r->address+r->span;
        if (next==at) return at;
        at=next;
    }
    return 0;
}
struct kernel_surface_result mem_surface_pages_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)a;
    if (state.hub) return module_result(1,state.hub==hub && state.heap==(void *)b);
    if (!hub || hub->magic!=KERNEL_SURFACES_MAGIC || hub->major!=2 || !b) return module_result(0,0);
    kernel_surface_call allocate=kernel_surface_find_version(hub,"mem.calloc",1,0),free=kernel_surface_find_version(hub,"mem.free",1,0),
        reserve=kernel_surface_find_version(hub,"memory.reserve",1,1),release=kernel_surface_find_version(hub,"memory.release",1,0),
        map=kernel_surface_find_version(hub,"memory.map_owned",1,0),unmap=kernel_surface_find_version(hub,"memory.unmap_owned",1,0);
    if (!allocate || !free || !reserve || !release || !map || !unmap) return module_result(0,0);
    state.hub=hub;state.heap=(void *)b;state.allocate=allocate;state.free=free;state.reserve=reserve;state.release=release;state.map=map;state.unmap=unmap;
    state.cpu=kernel_surface_find(hub,"cpu.control");state.batch=kernel_surface_find(hub,"memory.unmap_batch");state.map_batch=kernel_surface_find(hub,"memory.map_batch");
    state.next_id=1;return module_result(1,1);
}
struct kernel_surface_result mem_surface_pages_alloc(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.hub || !a || a>SIZE_MAX/4096 || !c) return module_result(0,0);
    struct kernel_surface_result metadata=CALL(state.allocate,state.heap,1,sizeof(struct allocation));
    if (metadata.status || !metadata.value) return module_result(0,0);
    struct allocation *r=(void *)metadata.value;
    struct kernel_surface_result memory=state.reserve(a*4096,BOOT_RES_PAGES,b,(uintptr_t)&r->physical,0,0);
    if (memory.status || !memory.value) {CALL(state.free,state.heap,r,0);return module_result(0,0);}
    uint64_t flags=state.cpu?CALL(state.cpu,CPU_ENTER,0,0).value:0;
    if(!state.next_id) {if(state.cpu)CALL(state.cpu,CPU_LEAVE,flags,0);state.release(r->physical,a,BOOT_RES_PAGES,b,0,0);CALL(state.free,state.heap,r,0);return module_result(0,0);}
    r->id=state.next_id++;r->count=a;r->owner=b;r->address=memory.value;r->next=state.all;state.all=r;
    ++state.info.allocations;state.info.physical_pages+=a;
    *(struct mem_pages *)c=(struct mem_pages){r->id,r->physical,r->count,r->address,r->owner};uint64_t id=r->id;if(state.cpu)CALL(state.cpu,CPU_LEAVE,flags,0);return module_result(id,1);
}
struct kernel_surface_result mem_surface_pages_free(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.hub) return module_result(0,0);
    uint64_t flags=state.cpu?CALL(state.cpu,CPU_ENTER,0,0).value:0;struct allocation *r=find(a,b,0);
    int safe=r && !r->disposing;if(safe)r->disposing=1;if(state.cpu)CALL(state.cpu,CPU_LEAVE,flags,0);
    if(!safe)return module_result(0,0);
    int ok=dispose(r);if(!ok) {flags=state.cpu?CALL(state.cpu,CPU_ENTER,0,0).value:0;r->disposing=0;if(state.cpu)CALL(state.cpu,CPU_LEAVE,flags,0);}
    return module_result(ok,ok);
}
struct kernel_surface_result mem_surface_stack_alloc(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.hub || !a || a>SIZE_MAX-4095 || !c) return module_result(0,0);
    size_t count=(a+4095)/4096;
    if (count>(SIZE_MAX-sizeof(struct allocation))/sizeof(struct frame) || count>(MEM_STACK_LIMIT-MEM_STACK_BASE)/4096-2) return module_result(0,0);
    size_t span=(count+2)*4096;
    struct kernel_surface_result metadata=CALL(state.allocate,state.heap,1,sizeof(struct allocation)+count*sizeof(struct frame));
    if (metadata.status || !metadata.value) return module_result(0,0);
    struct allocation *r=(void *)metadata.value;
    uint64_t flags=state.cpu?CALL(state.cpu,CPU_ENTER,0,0).value:0;
    uintptr_t address=stack_gap(span);
    if(!address || !state.next_id) {
        if(state.cpu)CALL(state.cpu,CPU_LEAVE,flags,0);
        CALL(state.free,state.heap,r,0);return module_result(0,0);
    }
    r->id=state.next_id++;r->count=count;r->owner=b;r->address=address;r->span=span;r->stack=1;r->disposing=1;r->next=state.all;state.all=r;
    ++state.info.allocations;++state.info.stacks;
    if(state.cpu)CALL(state.cpu,CPU_LEAVE,flags,0);
    /* The published construction record reserves the virtual gap. Each physical
     * operation acquires its own short authority lock, allowing preemption and
     * unrelated allocation between batches. No caller may free this record yet. */
    for (size_t i=0;i<count;++i) {
        struct kernel_surface_result memory=state.reserve(4096,BOOT_RES_STACK,b,(uintptr_t)&r->frames[i].physical,0,0);
        if (memory.status || !memory.value) goto fail;
        flags=state.cpu?CALL(state.cpu,CPU_ENTER,0,0).value:0;
        ++state.info.physical_pages;++state.info.stack_pages;
        if(state.cpu)CALL(state.cpu,CPU_LEAVE,flags,0);
    }
    for(size_t base=0;base<count;base+=BOOT_UNMAP_BATCH) {
        size_t n=count-base;if(n>BOOT_UNMAP_BATCH)n=BOOT_UNMAP_BATCH;
        if(state.map_batch) {
            struct boot_owned_mapping items[BOOT_UNMAP_BATCH];
            for(size_t i=0;i<n;++i)items[i]=(struct boot_owned_mapping){address+(base+i+1)*4096,r->frames[base+i].physical,b};
            struct kernel_surface_result mapped=CALL(state.map_batch,items,n,0);
            if(mapped.value>n)goto fail;
            for(size_t i=0;i<mapped.value;++i)r->frames[base+i].mapped=1;
            if(mapped.status || mapped.value!=n)goto fail;
        } else for(size_t i=base;i<base+n;++i) {
            if(CALL(state.map,address+(i+1)*4096,r->frames[i].physical,b).status)goto fail;
            r->frames[i].mapped=1;
        }
    }
    flags=state.cpu?CALL(state.cpu,CPU_ENTER,0,0).value:0;r->disposing=0;
    *(struct mem_stack *)c=(struct mem_stack){r->id,address+4096,address+span-4096,b,count*4096};uint64_t id=r->id;
    if(state.cpu)CALL(state.cpu,CPU_LEAVE,flags,0);
    return module_result(id,1);
fail:
    if (!dispose(r)) {
        flags=state.cpu?CALL(state.cpu,CPU_ENTER,0,0).value:0;r->disposing=0;uint64_t id=r->id;
        if(state.cpu)CALL(state.cpu,CPU_LEAVE,flags,0);
        return module_result(id,0);
    }
    return module_result(0,0);
}
struct kernel_surface_result mem_surface_stack_free(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.hub) return module_result(0,0);
    uint64_t flags=state.cpu?CALL(state.cpu,CPU_ENTER,0,0).value:0;
    struct allocation *r=find(a,b,1);uintptr_t rsp;__asm__ volatile("mov %%rsp,%0" : "=r"(rsp));
    int safe=r && !r->disposing && !(rsp>=r->address && rsp<r->address+r->span);
    if(safe && state.cpu && CALL(state.cpu,CPU_STACK_BUSY,r->address,r->address+r->span).value)safe=0;
    if(safe)r->disposing=1;
    if(state.cpu)CALL(state.cpu,CPU_LEAVE,flags,0);
    if(!safe)return module_result(0,0);
    int ok=dispose(r);
    if(!ok) {flags=state.cpu?CALL(state.cpu,CPU_ENTER,0,0).value:0;r->disposing=0;if(state.cpu)CALL(state.cpu,CPU_LEAVE,flags,0);}
    return module_result(ok,ok);
}
struct kernel_surface_result mem_surface_pages_info(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.hub || !a) return module_result(0,0);
    *(struct mem_pages_info *)a=state.info;return module_result(1,1);
}
