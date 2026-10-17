#include "../kmodules/shared/usb.h"
#include "../kmodules/shared/threads.h"
#include "probe.h"
#include <stdbool.h>
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
bool kernel_usb_test(const struct kernel_surfaces *hub) {
#ifdef STORAGE_TEST
    const char *probe_name="probe.storage";
#elif defined(USB_HUB_TEST)
    const char *probe_name="probe.hubs";
#else
    const char *probe_name="probe.usb";
#endif
    kernel_surface_call create=kernel_surface_find(hub,"threads.create"),thread=kernel_surface_find(hub,"threads.control"),step=kernel_surface_find(hub,"event.step"),clock=kernel_surface_find(hub,"clock.now_ns"),entry=kernel_surface_find(hub,probe_name);
    if(!create || !thread || !step || !clock || !entry)return false;
    struct probe_usb job={.hub=hub};
#ifdef STORAGE_WRITE_TEST
    job.write=1;
#endif
#ifdef USB_HOTPLUG_TEST
    job.hotplug=USB_HOTPLUG_TEST;
#endif
    struct thread_request request={.abi=1,.cpu=0,.stack_bytes=32768,.entry=(uintptr_t)entry,.major=1};request.payload.arguments[0]=(uintptr_t)&job;
    struct kernel_surface_result created=CALL(create,&request,0,0);if(created.status)return false;
    uint64_t deadline=CALL(clock,0,0,0).value+(job.write?60000000000:20000000000);struct thread_info info;
    do {if(CALL(thread,THREAD_INFO,created.value,&info).status)return false;if(info.state==THREAD_EXITED)break;
        if(CALL(step,1,0,0).status || CALL(clock,0,0,0).value>deadline)return false;
    }while(1);
    if(job.errors || !job.done || info.result.status) {
        const char *message="FAIL USB core probe line ";while(*message)__asm__ volatile("outb %0,$0xe9"::"a"(*message++));
        char digits[16];unsigned n=0;uint64_t line=job.errors;do{digits[n++]='0'+line%10;line/=10;}while(line);
        while(n)__asm__ volatile("outb %0,$0xe9"::"a"(digits[--n]));
        __asm__ volatile("outb %0,$0xe9"::"a"((unsigned char)'\n'));return false;
    }
    return !CALL(thread,THREAD_REAP,created.value,0).status;
}
