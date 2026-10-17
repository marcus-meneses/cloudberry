#include "probe.h"
#include "../kmodules/shared/handles.h"
#include "../kmodules/shared/process.h"
#include "../kmodules/shared/threads.h"
#include "../kmodules/shared/bootstrap.h"
#include "../kmodules/shared/mem-pages.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
#define CHECK(x) do{if(!(x)){probe->error=__LINE__;return module_result(__LINE__,0);}}while(0)
struct kernel_surface_result probe_process_call(MODULE_ARGS){
    MODULE_UNUSED();struct probe_process *probe=(void *)a;const struct kernel_surfaces *hub=(const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    kernel_surface_call boot_info=kernel_surface_find(hub,"bootstrap.info"),load=kernel_surface_find(hub,"process.load"),start=kernel_surface_find(hub,"process.start"),info=kernel_surface_find(hub,"process.info"),reap=kernel_surface_find(hub,"process.reap"),control=kernel_surface_find(hub,"threads.control"),clock=kernel_surface_find(hub,"clock.now_ns"),pages=kernel_surface_find(hub,"mem.pages_info");
    struct bootstrap_info volume;uint64_t boot_deadline=CALL(clock,0,0,0).value+UINT64_C(30000000000);
    for(;;){CHECK(!CALL(boot_info,&volume,0,0).status);if(volume.state==BOOTSTRAP_READY)break;CHECK(volume.state!=BOOTSTRAP_FAILED && volume.state!=BOOTSTRAP_DISCONNECTED && CALL(clock,0,0,0).value<boot_deadline);CHECK(!CALL(control,THREAD_BLOCK_UNTIL,CALL(clock,0,0,0).value+1000000,0).status);}
    struct mem_pages_info before,after;CHECK(!CALL(pages,&before,0,0).status);
    struct process_request request={.abi=1,.filesystem=volume.filesystem.cookie,.path="/boot/system/drivers/invalid.elf",.stack_bytes=65536};
    CHECK(CALL(load,&request,0,0).status);
    request.path="/boot/system/drivers/process-probe.elf";
    uint64_t ids[8];
    for(unsigned i=0;i<8;++i){request.cpu=i>1 && probe->cpus>1?1:0;request.argument=i<2?0x1111*(i+1):((uint64_t)(i-1)<<32)|0x3333;
        struct kernel_surface_result r=CALL(load,&request,0,0);CHECK(!r.status && r.value);ids[i]=r.value;}
    for(unsigned i=0;i<8;++i)CHECK(!CALL(start,ids[i],0,0).status);
    uint64_t deadline=CALL(clock,0,0,0).value+UINT64_C(30000000000);
    for(unsigned i=0;i<8;++i){struct process_info result;
        for(;;){CHECK(!CALL(info,ids[i],&result,0).status);if(result.state==PROCESS_EXITED || result.state==PROCESS_FAULTED)break;
            CHECK(CALL(clock,0,0,0).value<deadline && !CALL(control,THREAD_BLOCK_UNTIL,CALL(clock,0,0,0).value+1000000,0).status);}
        if(i<2 || i==7){CHECK(result.state==PROCESS_EXITED && result.exit_code==(i==7?0x3333:0x1111*(i+1)) && result.switches>1);}
        else{CHECK(result.state==PROCESS_FAULTED && result.fault_vector==(i==3?13:14));if(i==2)CHECK(result.fault_address==KERNEL_SURFACES_ADDRESS);if(i==6)CHECK(result.fault_address==0x300000);}
        while(CALL(reap,ids[i],0,0).status)CHECK(CALL(clock,0,0,0).value<deadline && !CALL(control,THREAD_BLOCK_UNTIL,CALL(clock,0,0,0).value+1000000,0).status);
        CHECK(CALL(info,ids[i],&result,0).status);++probe->completed;
    }
    kernel_surface_call admit=kernel_surface_find(hub,"handles.admit"),available=kernel_surface_find(hub,"handles.available"),read=kernel_surface_find(hub,"ext4_fs.read"),alloc=kernel_surface_find(hub,"mem.pages_alloc"),free=kernel_surface_find(hub,"mem.pages_free");
    request.path="/boot/system/drivers/handles-probe.elf";request.cpu=0;request.stack_bytes=131072;uint64_t actors[2];
    for(unsigned i=0;i<2;++i){request.cpu=i && probe->cpus>1?1:0;request.argument=i?0:1;struct kernel_surface_result created=CALL(load,&request,0,0);CHECK(!created.status);actors[i]=created.value;
        struct mem_pages manifest;CHECK(!CALL(alloc,1,0,&manifest).status);const char *path=i?"/boot/system/drivers/handles-client.json":"/boot/system/drivers/handles-server.json";
        struct kernel_surface_result got=read(volume.filesystem.cookie,(uintptr_t)path,0,manifest.address,4096,0);CHECK(!got.status && got.value && got.value<4096);
        struct handle_policy policy={.process=actors[i],.manifest=(const void *)manifest.address,.bytes=got.value};policy.identity.installation_id[0]=i+1;
        CHECK(!CALL(admit,&policy,0,0).status && CALL(admit,&policy,0,0).status && !CALL(free,manifest.id,0,0).status);
    }
    CHECK(!CALL(start,actors[0],0,0).status);
    while(!CALL(available,"@echo",0,0).value)CHECK(CALL(clock,0,0,0).value<deadline && !CALL(control,THREAD_BLOCK_UNTIL,CALL(clock,0,0,0).value+1000000,0).status);
    CHECK(!CALL(start,actors[1],0,0).status);
    for(unsigned i=0;i<2;++i){struct process_info result;
        for(;;){CHECK(!CALL(info,actors[i],&result,0).status);if(result.state==PROCESS_EXITED || result.state==PROCESS_FAULTED)break;CHECK(CALL(clock,0,0,0).value<deadline && !CALL(control,THREAD_BLOCK_UNTIL,CALL(clock,0,0,0).value+1000000,0).status);}
        probe->app_code=result.exit_code;CHECK(result.state==PROCESS_EXITED && !result.exit_code);
        while(CALL(reap,actors[i],0,0).status)CHECK(CALL(clock,0,0,0).value<deadline && !CALL(control,THREAD_BLOCK_UNTIL,CALL(clock,0,0,0).value+1000000,0).status);
        ++probe->completed;
    }
    CHECK(!CALL(pages,&after,0,0).status && before.allocations==after.allocations && before.physical_pages==after.physical_pages && before.stacks==after.stacks && before.stack_pages==after.stack_pages);
    return module_result(1,1);
}
