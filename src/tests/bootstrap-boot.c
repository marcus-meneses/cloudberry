#include "../kmodules/shared/bootstrap.h"
#include "../kmodules/shared/bootstrap-load.h"
#include "../kmodules/shared/config.h"
#include "../kmodules/shared/threads.h"
#include <stdbool.h>
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void print(const char *p){while(*p)__asm__ volatile("outb %0,$0xe9"::"a"(*p++));}
static void number(uint64_t value){char s[24];unsigned n=0;do{s[n++]='0'+value%10;value/=10;}while(value);while(n)__asm__ volatile("outb %0,$0xe9"::"a"(s[--n]));}
static bool failed(unsigned line){print("FAIL bootstrap line ");char digits[16];unsigned n=0;do{digits[n++]='0'+line%10;line/=10;}while(line);while(n)__asm__ volatile("outb %0,$0xe9"::"a"(digits[--n]));print("\n");return false;}
bool kernel_bootstrap_test(const struct kernel_surfaces *hub){
    kernel_surface_call info=kernel_surface_find(hub,"bootstrap.info"),step=kernel_surface_find(hub,"event.step"),clock=kernel_surface_find(hub,"clock.now_ns"),read=kernel_surface_find(hub,"ext4_fs.read");if(!info || !step || !clock || !read)return failed(__LINE__);
    __asm__ volatile("sti":::"memory");uint64_t deadline=CALL(clock,0,0,0).value+UINT64_C(30000000000);struct bootstrap_info status;
    for(;;){if(CALL(info,&status,0,0).status)return failed(__LINE__);if(status.state==BOOTSTRAP_READY || status.state==BOOTSTRAP_FAILED)break;if(CALL(step,1,0,0).status || CALL(clock,0,0,0).value>deadline){print("FAIL bootstrap timeout\n");return failed(__LINE__);}}
    if(status.state==BOOTSTRAP_FAILED){if(status.error==BOOTSTRAP_DUPLICATE && status.matches==2){print("PASS bootstrap duplicate GUID rejected without mounting\n");return true;}print("FAIL bootstrap error ");char digit='0'+status.error;__asm__ volatile("outb %0,$0xe9"::"a"(digit));print("\n");return failed(__LINE__);}
    if(status.error || status.matches!=1 || !status.filesystem.cookie || !status.filesystem.readonly || !block_valid(&status.partition))return failed(__LINE__);
    struct driver_table loading;kernel_surface_call progress=kernel_surface_find(hub,"bootstrap.driver_info");
    do{if(CALL(progress,&loading,0,0).status)return failed(__LINE__);if(loading.finished)break;if(CALL(step,1,0,0).status || CALL(clock,0,0,0).value>deadline)return failed(__LINE__);}while(1);
    uint8_t bytes[4096];kernel_surface_call create=kernel_surface_find(hub,"threads.create"),control=kernel_surface_find(hub,"threads.control");
    struct thread_request request={.abi=1,.cpu=0,.stack_bytes=32768,.entry=(uintptr_t)read,.major=1};
    request.payload.arguments[0]=status.filesystem.cookie;request.payload.arguments[1]=(uintptr_t)"/boot/system/drivers/probe.bin";request.payload.arguments[2]=123;request.payload.arguments[3]=(uintptr_t)bytes;request.payload.arguments[4]=sizeof(bytes);
    struct kernel_surface_result created=CALL(create,&request,0,0);if(created.status)return failed(__LINE__);struct thread_info thread;
    do{if(CALL(control,THREAD_INFO,created.value,&thread).status)return failed(__LINE__);if(thread.state==THREAD_EXITED)break;if(CALL(step,1,0,0).status || CALL(clock,0,0,0).value>deadline)return failed(__LINE__);}while(1);
    if(thread.result.status || thread.result.value!=sizeof(bytes) || CALL(control,THREAD_REAP,created.value,0).status)return failed(__LINE__);
    for(unsigned i=0;i<sizeof(bytes);++i)if(bytes[i]!=(uint8_t)((i+123)*37+11))return failed(__LINE__);
    const struct kernel_hub_table *configured=kernel_hub_find_table(hub,"config.drivers.modules");const struct config_value *modules=configured?(const void *)configured->address:NULL;
    if(modules && modules->bytes>1){struct driver_table drivers;kernel_surface_call driver_info=kernel_surface_find(hub,"bootstrap.driver_info");
        do{if(CALL(driver_info,&drivers,0,0).status)return failed(__LINE__);if(drivers.finished)break;if(CALL(step,1,0,0).status || CALL(clock,0,0,0).value>deadline)return failed(__LINE__);}while(1);
        for(const struct driver_record *r=drivers.first;r;r=r->next){print("MEASURE driver ");print(r->name);print(" state/error/load/detail ");number(r->state);print("/");number(r->error);print("/");number(r->result.error);print("/");number(r->result.detail);print("\n");}
        int negative=drivers.count==2;
        if((negative && drivers.error!=DRIVER_LOAD) || (!negative && (drivers.count!=1 || drivers.error)))return failed(__LINE__);
        kernel_surface_call entry=kernel_surface_find(hub,"future.test"),value=kernel_surface_find_version(hub,"future.value",1,2),retire=kernel_surface_find(hub,"hub.owner_control");
        if(!entry || !value || value(0,0,0,0,0,0).status || kernel_surface_find(hub,"future_fail.value"))return failed(__LINE__);
        uint64_t owner=kernel_surface_owner(hub,"future.test");if(negative){request=(struct thread_request){.abi=1,.cpu=0,.stack_bytes=32768,.entry=(uintptr_t)entry,.major=1};created=CALL(create,&request,0,0);if(created.status)return failed(__LINE__);
        do{if(CALL(control,THREAD_INFO,created.value,&thread).status)return failed(__LINE__);if(thread.state==THREAD_EXITED)break;if(CALL(step,1,0,0).status || CALL(clock,0,0,0).value>deadline)return failed(__LINE__);}while(1);
        if(thread.result.status){print("FAIL future test ");return failed(thread.result.value);}
        if(CALL(control,THREAD_REAP,created.value,0).status)return failed(__LINE__);
        }else {const struct kernel_surface *entries=kernel_surface_entries(hub);for(uint32_t i=0;i<kernel_surface_count(hub);++i)if(kernel_hub_name_equal(entries[i].name,"future_fail.value"))return failed(__LINE__);}
        if(CALL(retire,owner,HUB_OWNER_DRAIN,0).status || CALL(retire,owner,HUB_OWNER_RETIRE,0).status || !value(0,0,0,0,0,0).status)return failed(__LINE__);
        print(negative?"PASS disk modules: ext4-only load, hidden init, PCI selection, versioned dependencies, init/start failures, lease release and stale-gate retirement\n":"PASS disk modules: explicit selection, hidden init, PCI match, activation, unselected image exclusion and retirement\n");
    }
    print("PASS bootstrap unique GUID, retained transport, read-only ext4 mount and fixture read\n");return true;
}
