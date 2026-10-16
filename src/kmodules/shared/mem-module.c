#include "module.h"
#include "mem.h"
#include "../../kernel/cpu.h"
#include "../../boot/memory.h"
static const struct kernel_surfaces *hub;static kernel_surface_call cpu;
static uint64_t lock(void) {return cpu?cpu(CPU_ENTER,0,0,0,0,0).value:0;}
static void unlock(uint64_t flags) {if(cpu)cpu(CPU_LEAVE,flags,0,0,0,0);}
static void zero(void *p,size_t n) {unsigned char *out=p;for(size_t i=0;i<n;++i)out[i]=0;}

static int grow(struct mem_heap *heap, size_t bytes, size_t alignment) {
    if (!hub || !heap || !heap->begin || bytes > SIZE_MAX - 128 ||
        alignment > SIZE_MAX - bytes - 128) return 0;
    size_t required = bytes + alignment + 128;
    if (required > SIZE_MAX - 4095) return 0;
    required = (required + 4095) & ~(size_t)4095;
    kernel_surface_call reserve = kernel_surface_find_version(hub, "memory.reserve", 1, 0);
    if (!reserve) return 0;
    struct kernel_surface_result r = reserve(required, BOOT_RES_HEAP, (uintptr_t)heap, 0, 0, 0);
    if(r.status || !r.value)return 0;
    uint64_t flags=lock();int ok=mem_add_region(heap,(void *)r.value,required);unlock(flags);return ok;
}
struct kernel_surface_result mem_surface_configure(MODULE_ARGS) {
    MODULE_UNUSED(); const struct kernel_surfaces *candidate = (const void *)a;
    if (!candidate || candidate->magic != KERNEL_SURFACES_MAGIC || candidate->major != 2 ||
        !kernel_surface_find_version(candidate, "memory.reserve", 1, 0)) return module_result(0, 0);
    cpu=kernel_surface_find(candidate,"cpu.control");hub = candidate; return module_result(1, 1);
}
struct kernel_surface_result mem_surface_context(MODULE_ARGS) {
    MODULE_UNUSED();
    static const struct kmodule_context_info info={sizeof(struct mem_heap),_Alignof(struct mem_heap)};
    return module_result((uintptr_t)&info, 1);
}
struct kernel_surface_result mem_surface_init(MODULE_ARGS) {
    MODULE_UNUSED(); int ok = mem_init((void *)a, (void *)b, c); return module_result(ok, ok);
}
static void *allocate(struct mem_heap *heap,size_t bytes,size_t alignment) {
    uint64_t flags=lock();void *p=alignment?mem_aligned_alloc(heap,alignment,bytes):mem_malloc(heap,bytes);unlock(flags);
    if(!p && bytes && grow(heap,bytes,alignment?alignment:16)) {
        flags=lock();p=alignment?mem_aligned_alloc(heap,alignment,bytes):mem_malloc(heap,bytes);unlock(flags);
    }
    return p;
}
struct kernel_surface_result mem_surface_malloc(MODULE_ARGS) {
    MODULE_UNUSED();void *p=allocate((void *)a,b,0);return module_result((uintptr_t)p,p || !b);
}
struct kernel_surface_result mem_surface_free(MODULE_ARGS) {
    MODULE_UNUSED();uint64_t flags=lock();mem_free((void *)a,(void *)b);unlock(flags);return module_result(0,1);
}
struct kernel_surface_result mem_surface_calloc(MODULE_ARGS) {
    MODULE_UNUSED();if(c && b>SIZE_MAX/c)return module_result(0,0);
    size_t bytes=b*c;void *p=allocate((void *)a,bytes,0);if(p)zero(p,bytes);
    return module_result((uintptr_t)p,p || !bytes);
}
struct kernel_surface_result mem_surface_realloc(MODULE_ARGS) {
    MODULE_UNUSED();if(!b)return mem_surface_malloc(a,c,0,0,0,0);
    if(!c) {mem_surface_free(a,b,0,0,0,0);return module_result(0,1);}
    uint64_t flags=lock();size_t old=mem_usable_size((void *)b);void *p=mem_resize_in_place((void *)a,(void *)b,c);unlock(flags);
    if(p)return module_result((uintptr_t)p,1);
    p=allocate((void *)a,c,0);if(!p)return module_result(0,0);
    size_t bytes=old<c?old:c;unsigned char *out=p;const unsigned char *in=(const void *)b;
    for(size_t i=0;i<bytes;++i)out[i]=in[i];
    mem_surface_free(a,b,0,0,0,0);return module_result((uintptr_t)p,1);
}
struct kernel_surface_result mem_surface_aligned(MODULE_ARGS) {
    MODULE_UNUSED();if(!b || (b&(b-1)) || c%b)return module_result(0,0);
    void *p=allocate((void *)a,c,b);return module_result((uintptr_t)p,p || !c);
}
struct kernel_surface_result mem_surface_usable(MODULE_ARGS) {
    MODULE_UNUSED();uint64_t flags=lock();size_t bytes=mem_usable_size((void *)a);unlock(flags);return module_result(bytes,1);
}
