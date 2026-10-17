#include "probe.h"
#include "../kmodules/shared/threads.h"
struct kernel_surface_result probe_space_call(MODULE_ARGS){
    MODULE_UNUSED();struct probe_space *p=(void *)a;const struct kernel_surfaces *hub=(const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    kernel_surface_call control=kernel_surface_find(hub,"threads.control"),clock=kernel_surface_find(hub,"clock.now_ns");
    uintptr_t cr3;__asm__ volatile("mov %%cr3,%0":"=r"(cr3));if(cr3!=p->root)++p->errors;
    volatile uint64_t *word=(void *)p->address;if(*word!=p->expected)++p->errors;
    __atomic_store_n(&p->phase,1,__ATOMIC_RELEASE);
    /* Busy execution forces timer preemption on CPU0 before the test dispatcher
     * can run again. CPU1 retains an active translation for targeted shootdown. */
    uint64_t deadline=clock(0,0,0,0,0,0).value+UINT64_C(10000000000);
    while(!__atomic_load_n(&p->command,__ATOMIC_ACQUIRE)){
        if(*word!=p->expected)++p->errors;
        if(clock(0,0,0,0,0,0).value>deadline)return module_result(1,0);
    }
    __atomic_store_n(&p->phase,2,__ATOMIC_RELEASE);
    if(control(THREAD_BLOCK,0,0,0,0,0).status)++p->errors;
    if(*word!=p->next)++p->errors;
    for(unsigned i=0;i<20;++i){if(control(THREAD_YIELD,0,0,0,0,0).status)++p->errors;__asm__ volatile("mov %%cr3,%0":"=r"(cr3));if(cr3!=p->root || *word!=p->next)++p->errors;}
    __atomic_store_n(&p->phase,3,__ATOMIC_RELEASE);return module_result(p->errors,p->errors==0);
}
