#include "../kmodules/shared/bootstrap.h"
#include "../kmodules/shared/threads.h"
#include <stdbool.h>
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void print(const char *p){while(*p)__asm__ volatile("outb %0,$0xe9"::"a"(*p++));}
static bool failed(unsigned line){print("FAIL bootstrap line ");char digits[16];unsigned n=0;do{digits[n++]='0'+line%10;line/=10;}while(line);while(n)__asm__ volatile("outb %0,$0xe9"::"a"(digits[--n]));print("\n");return false;}
bool kernel_bootstrap_test(const struct kernel_surfaces *hub){
    kernel_surface_call info=kernel_surface_find(hub,"bootstrap.info"),step=kernel_surface_find(hub,"event.step"),clock=kernel_surface_find(hub,"clock.now_ns"),read=kernel_surface_find(hub,"ext4_fs.read");if(!info || !step || !clock || !read)return failed(__LINE__);
    __asm__ volatile("sti":::"memory");uint64_t deadline=CALL(clock,0,0,0).value+UINT64_C(30000000000);struct bootstrap_info status;
    for(;;){if(CALL(info,&status,0,0).status)return failed(__LINE__);if(status.state==BOOTSTRAP_READY || status.state==BOOTSTRAP_FAILED)break;if(CALL(step,1,0,0).status || CALL(clock,0,0,0).value>deadline){print("FAIL bootstrap timeout\n");return failed(__LINE__);}}
    if(status.state==BOOTSTRAP_FAILED){if(status.error==BOOTSTRAP_DUPLICATE && status.matches==2){print("PASS bootstrap duplicate GUID rejected without mounting\n");return true;}print("FAIL bootstrap error ");char digit='0'+status.error;__asm__ volatile("outb %0,$0xe9"::"a"(digit));print("\n");return failed(__LINE__);}
    if(status.error || status.matches!=1 || !status.filesystem.cookie || !status.filesystem.readonly || !block_valid(&status.partition))return failed(__LINE__);
    uint8_t bytes[4096];kernel_surface_call create=kernel_surface_find(hub,"threads.create"),control=kernel_surface_find(hub,"threads.control");
    struct thread_request request={.abi=1,.cpu=0,.stack_bytes=32768,.entry=(uintptr_t)read,.major=1};
    request.payload.arguments[0]=status.filesystem.cookie;request.payload.arguments[1]=(uintptr_t)"/boot/system/drivers/probe.bin";request.payload.arguments[2]=123;request.payload.arguments[3]=(uintptr_t)bytes;request.payload.arguments[4]=sizeof(bytes);
    struct kernel_surface_result created=CALL(create,&request,0,0);if(created.status)return failed(__LINE__);struct thread_info thread;
    do{if(CALL(control,THREAD_INFO,created.value,&thread).status)return failed(__LINE__);if(thread.state==THREAD_EXITED)break;if(CALL(step,1,0,0).status || CALL(clock,0,0,0).value>deadline)return failed(__LINE__);}while(1);
    if(thread.result.status || thread.result.value!=sizeof(bytes) || CALL(control,THREAD_REAP,created.value,0).status)return failed(__LINE__);
    for(unsigned i=0;i<sizeof(bytes);++i)if(bytes[i]!=(uint8_t)((i+123)*37+11))return failed(__LINE__);
    print("PASS bootstrap unique GUID, retained transport, read-only ext4 mount and fixture read\n");return true;
}
