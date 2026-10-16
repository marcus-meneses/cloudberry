#include "../kmodules/shared/mem-pages.h"
#include "../boot/memory.h"
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static uint64_t leaf(struct boot_pages *pages,uintptr_t address) {
    uint64_t cr3;__asm__ volatile("mov %%cr3,%0" : "=r"(cr3));uint64_t *table=boot_physical_pointer(pages,cr3&UINT64_C(0x000ffffffffff000));
    for (unsigned shift=39;shift>12;shift-=9) {uint64_t e=table[(address>>shift)&511];if (!(e&1) || (e&128)) return 0;table=boot_physical_pointer(pages,e&UINT64_C(0x000ffffffffff000));}
    return table[(address>>12)&511];
}
extern struct kernel_surface_result test_stack_call(uintptr_t,kernel_surface_call,uintptr_t,uintptr_t);
bool kernel_pages_test(const struct kernel_surfaces *hub,struct boot_pages *pages) {
    kernel_surface_call alloc=kernel_surface_find_version(hub,"mem.pages_alloc",1,0),free=kernel_surface_find_version(hub,"mem.pages_free",1,0),
        stacks=kernel_surface_find_version(hub,"mem.stack_alloc",1,0),stack_free=kernel_surface_find_version(hub,"mem.stack_free",1,0),
        info=kernel_surface_find_version(hub,"mem.pages_info",1,0),map=kernel_surface_find_version(hub,"memory.map_owned",1,0),
        unmap=kernel_surface_find_version(hub,"memory.unmap_owned",1,0);
    if (!alloc || !free || !stacks || !stack_free || !info || !map || !unmap) return false;
    struct mem_pages_info initial;if(CALL(info,&initial,0,0).status)return false;
    struct mem_pages first,second;struct mem_stack stack,reused;
    if (CALL(alloc,2,41,&first).status || first.count!=2 || !first.physical || first.physical%4096) return false;
    unsigned char *bytes=(void *)first.address;for (size_t i=0;i<8192;++i) {if (bytes[i]) return false;bytes[i]=0xab;}
    if (CALL(free,first.id,42,0).status!=KERNEL_SURFACE_FAILED || CALL(free,first.id,41,0).status) return false;
    for (size_t i=0;i<8192;++i) if (bytes[i]) return false;
    if (CALL(alloc,2,42,&second).status || second.physical!=first.physical || second.id==first.id ||
        CALL(free,first.id,41,0).status!=KERNEL_SURFACE_FAILED) return false;
    uintptr_t alias=MEM_STACK_BASE-4096;
    if (CALL(map,alias,second.physical,42).status || CALL(free,second.id,42,0).status!=KERNEL_SURFACE_FAILED ||
        CALL(unmap,alias,second.physical,41).status!=KERNEL_SURFACE_FAILED ||
        CALL(unmap,alias,second.physical,42).status || leaf(pages,alias) || CALL(free,second.id,42,0).status) return false;
    if (CALL(stacks,9000,0,&stack).status || stack.bytes!=12288 || stack.top-stack.bottom!=12288 ||
        leaf(pages,stack.bottom-4096) || leaf(pages,stack.top)) return false;
    for (uintptr_t p=stack.bottom;p<stack.top;p+=4096) {
        if ((leaf(pages,p)&UINT64_C(0x8000000000000007))!=UINT64_C(0x8000000000000003)) return false;
        volatile uint64_t *word=(void *)p;if (*word) return false;*word=0x12345678;
    }
    if (test_stack_call(stack.top,stack_free,stack.id,0).status!=KERNEL_SURFACE_FAILED) return false;
    if (CALL(stack_free,stack.id,1,0).status!=KERNEL_SURFACE_FAILED || CALL(stack_free,stack.id,0,0).status) return false;
    for (uintptr_t p=stack.bottom;p<stack.top;p+=4096) if (leaf(pages,p)) return false;
    if (CALL(stacks,9000,1,&reused).status || reused.bottom!=stack.bottom || reused.id==stack.id) return false;
    for (uintptr_t p=reused.bottom;p<reused.top;p+=4096) if (*(volatile uint64_t *)p) return false;
    if (CALL(stack_free,reused.id,1,0).status) return false;
    struct mem_pages_info status;if (CALL(info,&status,0,0).status || status.allocations!=initial.allocations || status.physical_pages!=initial.physical_pages || status.stacks!=initial.stacks || status.stack_pages!=initial.stack_pages) return false;
    const char *s="PASS managed pages: ownership, mapping references, guards, zeroing and release/reuse\n";
    while (*s) __asm__ volatile("outb %0,$0xe9" :: "a"(*s++));
    return true;
}
