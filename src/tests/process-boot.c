#include "../kernel/modules.h"
#include "../kmodules/shared/threads.h"
#include "probe.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void print(const char *s){while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));}
bool kernel_process_test(const struct kernel_surfaces *hub){
    const struct kernel_hub_table *cpus=kernel_hub_find_table(hub,"smp.cpus");
    kernel_surface_call create=kernel_surface_find(hub,"threads.create"),control=kernel_surface_find(hub,"threads.control"),step=kernel_surface_find(hub,"event.step");
    struct probe_process probe={.cpus=cpus->count};
    struct thread_request r={.abi=1,.stack_bytes=65536,.entry=(uintptr_t)kernel_surface_find(hub,"probe.process"),.major=1,.payload={.arguments={(uintptr_t)&probe}}};
    struct kernel_surface_result t=CALL(create,&r,0,0);if(t.status)return false;
    for(;;){struct thread_info info;if(CALL(control,THREAD_INFO,t.value,&info).status)return false;if(info.state==THREAD_EXITED){if(info.result.status || probe.error || probe.completed!=38){print("FAIL process probe line ");char digits[24];unsigned n=0;uint64_t error=probe.error;do{digits[n++]='0'+error%10;error/=10;}while(error);while(n)__asm__ volatile("outb %0,$0xe9"::"a"(digits[--n]));print(" app ");n=0;error=probe.app_code;do{digits[n++]='0'+error%10;error/=10;}while(error);while(n)__asm__ volatile("outb %0,$0xe9"::"a"(digits[--n]));print("\n");return false;}break;}if(CALL(step,1,0,0).status)return false;}
    if(CALL(control,THREAD_REAP,t.value,0).status)return false;
    print("PASS handles: manifest admission, isolated request/reply, filtered reflection, denied operations, invalid buffers, process-local tokens, retained outcomes and provider failure\n");
    print("PASS protected processes: disk ELF, same-VA isolation, preemption, kernel/RO/NX/unmapped/privileged-instruction containment, GS switching and cleanup\n");return true;
}
