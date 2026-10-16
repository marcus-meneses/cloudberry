#include "../kernel/modules.h"
#include "../kernel/cpu.h"
#include "../kmodules/shared/threads.h"
#include "probe.h"
/* Deliberate owner failure, linked only into the isolated watchdog kernel. */
bool kernel_mem_test(const struct kernel_surfaces *hub,struct boot_pages *pages) {
    (void)pages;kernel_surface_call create=kernel_surface_find(hub,"threads.create"),cpu=kernel_surface_find(hub,"cpu.control");
    static struct probe_thread holder={.mode=10};
    struct thread_request request={.abi=1,.cpu=1,.stack_bytes=16384,.entry=(uintptr_t)kernel_surface_find(hub,"probe.thread"),.major=1};request.payload.arguments[0]=(uintptr_t)&holder;
    __asm__ volatile("sti":::"memory");
    if(create((uintptr_t)&request,0,0,0,0,0).status)return false;
    while(!__atomic_load_n(&holder.phase,__ATOMIC_ACQUIRE))__asm__ volatile("pause");
    cpu(CPU_ENTER,0,0,0,0,0);return false; /* Must fault through the watchdog. */
}
