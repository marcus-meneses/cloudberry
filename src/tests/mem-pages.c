#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/mem-pages.h"
#include "../kmodules/shared/mem.h"
#include "../boot/memory.h"
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static _Alignas(4096) unsigned char physical[4*1024*1024],metadata[1024*1024];
static struct boot_pages pages;static struct mem_heap heap;
static struct {struct kernel_surfaces hub;struct kernel_surface methods[8];} fixture;
static struct {uintptr_t address,owner;uint64_t physical;} mappings[512];
static size_t map_batches,unmap_batches;
static size_t live;static int map_budget=-1,reserve_budget=-1;static bool fail_alloc;
static struct boot_reservation *record(uint64_t p) {
    for (struct boot_reservation_chunk *c=pages.reservations.first;c;c=c->next)
        for (size_t i=0;i<c->count;++i) if (c->entries[i].base==p) return &c->entries[i];
    return NULL;
}
static struct kernel_surface_result allocate(MODULE_ARGS) {MODULE_UNUSED();void *p=fail_alloc?NULL:mem_calloc((void *)a,b,c);if (p) ++live;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result free_meta(MODULE_ARGS) {MODULE_UNUSED();mem_free((void *)a,(void *)b);--live;return module_result(1,1);}
static struct kernel_surface_result reserve(MODULE_ARGS) {
    MODULE_UNUSED();if (!reserve_budget) return module_result(0,0);if (reserve_budget>0) --reserve_budget;
    uint64_t p=boot_pages_reserve(&pages,a/4096,b,c);if (p && d) *(uint64_t *)d=p;
    return module_result(p?(uintptr_t)boot_physical_pointer(&pages,p):0,p!=0);
}
static struct kernel_surface_result release(MODULE_ARGS) {MODULE_UNUSED();bool ok=boot_pages_release(&pages,a,b,c,d);return module_result(ok,ok);}
static struct kernel_surface_result map(MODULE_ARGS) {
    MODULE_UNUSED();if (!map_budget) return module_result(0,0);if (map_budget>0) --map_budget;
    struct boot_reservation *r=record(b);assert(r && r->owner==c && r->kind==BOOT_RES_STACK);
    for (unsigned i=0;i<512;++i) assert(mappings[i].address!=a);
    for (unsigned i=0;i<512;++i) if (!mappings[i].address) {mappings[i].address=a;mappings[i].physical=b;mappings[i].owner=c;++r->mappings;return module_result(1,1);}
    return module_result(0,0);
}
static struct kernel_surface_result unmap(MODULE_ARGS) {
    MODULE_UNUSED();for (unsigned i=0;i<512;++i) if (mappings[i].address==a && mappings[i].physical==b && mappings[i].owner==c) {
        struct boot_reservation *r=record(b);assert(r && r->mappings);--r->mappings;mappings[i].address=0;return module_result(1,1);
    }return module_result(0,0);
}
static struct kernel_surface_result map_batch(MODULE_ARGS) {
    MODULE_UNUSED();const struct boot_owned_mapping *items=(const void *)a;assert(b && b<=BOOT_UNMAP_BATCH);++map_batches;
    size_t n=0;for(;n<b;++n)if(CALL(map,items[n].address,items[n].physical,items[n].owner).status)break;
    return module_result(n,n==b);
}
static struct kernel_surface_result unmap_batch(MODULE_ARGS) {
    MODULE_UNUSED();const struct boot_owned_mapping *items=(const void *)a;assert(b && b<=BOOT_UNMAP_BATCH);++unmap_batches;
    for(size_t n=0;n<b;++n)assert(!CALL(unmap,items[n].address,items[n].physical,items[n].owner).status);
    return module_result(1,1);
}
int main(void) {
    struct boot_memory_entry region={0x1000,sizeof(physical),0};struct boot_memory_entry *entries[]={&region};struct boot_memory_map memory={0,1,entries};
    assert(boot_pages_init(&pages,&memory,(uintptr_t)physical-0x1000));assert(mem_init(&heap,metadata,sizeof(metadata)));
    fixture.hub=(struct kernel_surfaces){.magic=KERNEL_SURFACES_MAGIC,.major=2,.count=8,.header_bytes=offsetof(__typeof__(fixture),methods)};
    const char *names[]={"mem.calloc","mem.free","memory.reserve","memory.release","memory.map_owned","memory.unmap_owned","memory.map_batch","memory.unmap_batch"};kernel_surface_call calls[]={allocate,free_meta,reserve,release,map,unmap,map_batch,unmap_batch};
    for (unsigned i=0;i<8;++i) {strcpy(fixture.methods[i].name,names[i]);fixture.methods[i].call=calls[i];fixture.methods[i].major=1;fixture.methods[i].minor=1;}
    assert(!CALL(mem_surface_pages_init,&fixture.hub,&heap,0).status);
    assert(CALL(mem_surface_pages_init,&fixture.hub,physical,0).status==KERNEL_SURFACE_FAILED);
    struct mem_pages a,b,c,reused;
    assert(CALL(mem_surface_pages_alloc,1,1,&a).value==a.id && a.id);
    assert(CALL(mem_surface_pages_alloc,1,2,&b).value==b.id && b.id);
    assert(CALL(mem_surface_pages_alloc,1,3,&c).value==c.id && c.id);
    memset((void *)a.address,0xa5,4096);assert(CALL(mem_surface_pages_free,a.id,2,0).status==KERNEL_SURFACE_FAILED);
    record(a.physical)->mappings=1;assert(CALL(mem_surface_pages_free,a.id,1,0).status==KERNEL_SURFACE_FAILED);record(a.physical)->mappings=0;
    assert(!CALL(mem_surface_pages_free,a.id,1,0).status && !CALL(mem_surface_pages_free,c.id,3,0).status);
    assert(CALL(mem_surface_pages_free,a.id,1,0).status==KERNEL_SURFACE_FAILED);
    struct mem_stack stack={0};assert(!CALL(mem_surface_stack_alloc,5000,0,&stack).status && stack.bytes==8192 && stack.top-stack.bottom==8192);
    uint64_t frame0=0,frame1=0;
    for (unsigned i=0;i<512;++i) if (mappings[i].address) {
        assert(mappings[i].address>=stack.bottom && mappings[i].address<stack.top);
        if (mappings[i].address==stack.bottom) frame0=mappings[i].physical;else frame1=mappings[i].physical;
    }
    assert(frame0==a.physical && frame1==c.physical && frame1!=frame0+4096);
    for (unsigned i=0;i<4096;++i) assert(!((unsigned char *)a.address)[i]);
    assert(CALL(mem_surface_stack_free,stack.id,1,0).status==KERNEL_SURFACE_FAILED);
    assert(!CALL(mem_surface_stack_free,stack.id,0,0).status);
    for (unsigned i=0;i<512;++i) assert(!mappings[i].address);
    assert(CALL(mem_surface_pages_alloc,1,1,&reused).value==reused.id && reused.id!=a.id && reused.physical==a.physical);
    assert(CALL(mem_surface_pages_free,a.id,1,0).status==KERNEL_SURFACE_FAILED);
    assert(!CALL(mem_surface_pages_free,reused.id,1,0).status && !CALL(mem_surface_pages_free,b.id,2,0).status);
    struct mem_stack large;size_t maps=map_batches,unmaps=unmap_batches;
    assert(!CALL(mem_surface_stack_alloc,33*4096,8,&large).status);
    assert(map_batches-maps==3 && !CALL(mem_surface_stack_free,large.id,8,0).status && unmap_batches-unmaps==3);
    size_t baseline=pages.reservations.count;
    struct mem_stack unchanged={.id=UINT64_MAX};map_budget=1;
    assert(CALL(mem_surface_stack_alloc,3*4096,7,&unchanged).status==KERNEL_SURFACE_FAILED && unchanged.id==UINT64_MAX);
    assert(pages.reservations.count==baseline && !live);map_budget=-1;reserve_budget=1;
    assert(CALL(mem_surface_stack_alloc,3*4096,7,&unchanged).status==KERNEL_SURFACE_FAILED && unchanged.id==UINT64_MAX);
    assert(pages.reservations.count==baseline && !live);reserve_budget=-1;fail_alloc=true;
    assert(CALL(mem_surface_stack_alloc,4096,7,&unchanged).status==KERNEL_SURFACE_FAILED && !live);fail_alloc=false;
    assert(CALL(mem_surface_stack_alloc,SIZE_MAX,7,&unchanged).status==KERNEL_SURFACE_FAILED);
    struct mem_pages_info info;assert(!CALL(mem_surface_pages_info,&info,0,0).status && !info.allocations && !info.physical_pages && !info.stacks && !info.stack_pages);
    void *whole=mem_malloc(&heap,sizeof(metadata)-64);assert(whole);mem_free(&heap,whole);
    puts("PASS: owned page release/zero/reuse, stale IDs, fragmented guarded stacks, bounded mapping batches, bounds and failed-allocation rollback");return 0;
}
