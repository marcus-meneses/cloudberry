#include "../kmodules/shared/threads.h"
#include "probe.h"
#include <stdbool.h>
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
bool kernel_block_test(const struct kernel_surfaces *hub){
    kernel_surface_call create=kernel_surface_find(hub,"threads.create"),control=kernel_surface_find(hub,"threads.control"),step=kernel_surface_find(hub,"event.step"),clock=kernel_surface_find(hub,"clock.now_ns"),entry=kernel_surface_find(hub,"probe.block");
    if(!create || !control || !step || !clock || !entry)return false;
    struct probe_usb job={.hub=hub,.write=BLOCK_BACKEND_TEST};struct thread_request request={.abi=1,.cpu=0,.stack_bytes=32768,.entry=(uintptr_t)entry,.major=1};request.payload.arguments[0]=(uintptr_t)&job;
    __asm__ volatile("sti":::"memory");struct kernel_surface_result created=CALL(create,&request,0,0);if(created.status)return false;
    uint64_t deadline=CALL(clock,0,0,0).value+UINT64_C(60000000000);struct thread_info info;
    do{if(CALL(control,THREAD_INFO,created.value,&info).status)return false;if(info.state==THREAD_EXITED)break;if(CALL(step,1,0,0).status || CALL(clock,0,0,0).value>deadline)return false;}while(1);
    if(job.errors || !job.done || info.result.status){const char *p="FAIL block probe line ";while(*p)__asm__ volatile("outb %0,$0xe9"::"a"(*p++));char digits[24];unsigned n=0;uint64_t line=job.errors;do{digits[n++]='0'+line%10;line/=10;}while(line);while(n)__asm__ volatile("outb %0,$0xe9"::"a"(digits[--n]));__asm__ volatile("outb %0,$0xe9"::"a"((unsigned char)'\n'));return false;}
    return !CALL(control,THREAD_REAP,created.value,0).status;
}
