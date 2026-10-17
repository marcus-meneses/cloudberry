#include "spaces.h"
#include "cpu.h"
#include "../boot/spaces.h"
#include "../kmodules/shared/mem-spaces.h"
struct space {struct space *next;struct mem_space_info info;struct boot_space_reaper reaper;unsigned disposing;};
static struct boot_pages *pages;
static uintptr_t heap;
static kernel_surface_call allocate,release;
static struct space *head;
static uint64_t serial;
static struct space *find(uint64_t id,uint64_t owner){for(struct space *s=head;s;s=s->next)if(s->info.id==id && s->info.owner==owner)return s;return NULL;}
bool kernel_spaces_prepare(struct boot_pages *p){
    uint64_t cr3,cr4;uint32_t lo,hi;__asm__ volatile("mov %%cr3,%0;mov %%cr4,%1":"=r"(cr3),"=r"(cr4));
    __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0xc0000080u));(void)hi;
    if((cr4&((1u<<12)|(1u<<17))) || !(lo&(1u<<11)) || !boot_spaces_prepare(p,cr3&UINT64_C(0x000ffffffffff000)))return false;
    /* Supervisor writes must honor read-only mappings as well. */
    uintptr_t cr0;__asm__ volatile("mov %%cr0,%0":"=r"(cr0));cr0|=UINT64_C(1)<<16;__asm__ volatile("mov %0,%%cr0"::"r"(cr0):"memory");kernel_cpu_select_root(cr3);
    pages=p;return true;
}
bool kernel_spaces_init(const struct kernel_surfaces *hub,uintptr_t context){
    if(!pages || heap || !context)return false;
    allocate=kernel_surface_find(hub,"mem.calloc");release=kernel_surface_find(hub,"mem.free");
    if(!allocate || !release)return false;
    heap=context;return true;
}
struct kernel_surface_result kernel_spaces_create(MODULE_ARGS){
    MODULE_UNUSED();if(!heap || !a || !b)return module_result(0,0);
    struct kernel_surface_result mem=allocate(heap,1,sizeof(struct space),0,0,0);if(mem.status)return mem;
    struct space *s=(void *)mem.value;uint64_t flags=kernel_cpu_enter();
    uint64_t root=serial==UINT64_MAX?0:boot_space_create(pages,a);
    if(root){s->info=(struct mem_space_info){.id=++serial,.owner=a,.root=root,.state=MEM_SPACE_READY};s->next=head;head=s;*(struct mem_space_info *)b=s->info;}
    uint64_t id=root?s->info.id:0;kernel_cpu_leave(flags);if(!root)release(heap,(uintptr_t)s,0,0,0,0);return module_result(id,root!=0);
}
struct kernel_surface_result kernel_spaces_map(MODULE_ARGS){
    MODULE_UNUSED();uint64_t flags=kernel_cpu_enter();struct space *s=find(a,b);
    int ok=s && s->info.state==MEM_SPACE_READY && e<=UINT32_MAX && boot_space_map(pages,s->info.root,b,c,d,e);
    if(ok){kernel_cpu_flush_space(a);}kernel_cpu_leave(flags);return module_result(ok,ok);
}
struct kernel_surface_result kernel_spaces_unmap(MODULE_ARGS){
    MODULE_UNUSED();uint64_t flags=kernel_cpu_enter();struct space *s=find(a,b);
    int ok=s && s->info.state==MEM_SPACE_READY && boot_space_unmap(pages,s->info.root,b,c);
    /* Keep the ledger lock until acknowledgements: a last unmap must not let
     * another CPU release/reuse the data frame before its stale TLB is gone. */
    if(ok){kernel_cpu_flush_space(a);}kernel_cpu_leave(flags);return module_result(ok,ok);
}
struct kernel_surface_result kernel_spaces_protect(MODULE_ARGS){
    MODULE_UNUSED();uint64_t flags=kernel_cpu_enter();struct space *s=find(a,b);
    int ok=s && s->info.state==MEM_SPACE_READY && d<=UINT32_MAX && boot_space_protect(pages,s->info.root,c,d);
    if(ok){kernel_cpu_flush_space(a);}kernel_cpu_leave(flags);return module_result(ok,ok);
}
struct kernel_surface_result kernel_spaces_query(MODULE_ARGS){
    MODULE_UNUSED();if(!d)return module_result(0,0);uint64_t flags=kernel_cpu_enter();struct space *s=find(a,b);struct mem_space_mapping out={0};unsigned permissions;
    int ok=s && s->info.state==MEM_SPACE_READY && boot_space_query(pages,s->info.root,c,&out.physical,&permissions);
    if(ok){out.flags=permissions;*(struct mem_space_mapping *)d=out;}kernel_cpu_leave(flags);return module_result(ok,ok);
}
struct kernel_surface_result kernel_spaces_info(MODULE_ARGS){
    MODULE_UNUSED();if(!c)return module_result(0,0);uint64_t flags=kernel_cpu_enter();struct space *s=find(a,b);if(s)*(struct mem_space_info *)c=s->info;kernel_cpu_leave(flags);return module_result(s!=NULL,s!=NULL);
}
struct kernel_surface_result kernel_spaces_pin(MODULE_ARGS){
    MODULE_UNUSED();uint64_t flags=kernel_cpu_enter();struct space *s=find(a,b);int ok=0;
    if(s && c<=1 && s->info.state==MEM_SPACE_READY){if(c && s->info.leases<UINT64_MAX){++s->info.leases;ok=1;}if(!c && s->info.leases){--s->info.leases;ok=1;}}
    kernel_cpu_leave(flags);return module_result(ok,ok);
}
struct kernel_surface_result kernel_spaces_switch(MODULE_ARGS){
    MODULE_UNUSED();uint64_t irq;__asm__ volatile("pushfq;pop %0":"=r"(irq));if(irq&512)return module_result(0,0);
    uint64_t flags=kernel_cpu_enter(),old=kernel_cpu_space();struct space *s=a?find(a,b):NULL;int ok=pages && ((!a && !b) || (s && s->info.state==MEM_SPACE_READY && s->info.leases));
    if(ok && a!=old){struct space *previous=NULL;for(struct space *p=head;p;p=p->next)if(p->info.id==old){previous=p;break;}
        if((old && (!previous || !previous->info.active)) || (s && s->info.active==UINT64_MAX))ok=0;
        else{if(previous)--previous->info.active;if(s)++s->info.active;uint64_t root=s?s->info.root:pages->kernel_root;
            kernel_cpu_select_root(root);kernel_cpu_set_space(a);}
    }
    kernel_cpu_leave(flags);return module_result(ok,ok);
}
struct kernel_surface_result kernel_spaces_destroy(MODULE_ARGS){
    MODULE_UNUSED();uint64_t irq;__asm__ volatile("pushfq;pop %0":"=r"(irq));
    if(!(irq&512) || kernel_cpu_control(CPU_DEPTH,0,0,0,0,0).value)return module_result(0,0);
    uint64_t flags=kernel_cpu_enter();struct space *s=find(a,b);
    if(!s || s->info.active || s->info.leases || s->disposing){kernel_cpu_leave(flags);return module_result(0,0);}
    s->info.state=MEM_SPACE_DESTROYING;s->disposing=1;kernel_cpu_leave(flags);
    unsigned result;
    do{flags=kernel_cpu_enter();result=boot_space_reap(pages,s->info.root,b,&s->reaper);kernel_cpu_leave(flags);}while(result==1);
    flags=kernel_cpu_enter();if(result==2){struct space **at=&head;while(*at!=s)at=&(*at)->next;*at=s->next;}else s->disposing=0;
    kernel_cpu_leave(flags);if(result==2)release(heap,(uintptr_t)s,0,0,0,0);return module_result(result==2,result==2);
}
