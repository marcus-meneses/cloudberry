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
    /* Force fast statistics only in this isolated failure probe. The blocked
     * acquisition must still time out using independent HPET, not this rate. */
    uint64_t timing_lock=kernel_cpu_enter_domain(1,0);
    if(kernel_cpu_control(CPU_PROFILE_CLOCK,1000000000,0,0,0,0).status)return false;
    kernel_cpu_leave_domain(1,timing_lock);
    if(create((uintptr_t)&request,0,0,0,0,0).status)return false;
    while(!__atomic_load_n(&holder.phase,__ATOMIC_ACQUIRE))__asm__ volatile("pause");
    cpu(CPU_ENTER,0,0,0,0,0);return false; /* Must fault through the watchdog. */
}
