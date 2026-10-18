#include "probe.h"
#include "../kmodules/shared/handles.h"
#include "../kmodules/shared/io.h"
#include "../kmodules/shared/process.h"
#include "../kmodules/shared/threads.h"
#include "../kmodules/shared/bootstrap.h"
#include "../kmodules/shared/bootstrap-apps.h"
#include "../kmodules/shared/clock.h"
#include "../kernel/cpu.h"
#include "handle-bench.h"
#include "../kmodules/shared/mem-pages.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
#define CHECK(x) do{if(!(x)){probe->error=__LINE__;return module_result(__LINE__,0);}}while(0)
static void text(const char *s){while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));}
static int admit_file(const struct kernel_surfaces *hub,uint64_t volume,uint64_t pid,const char *path){
    kernel_surface_call alloc=kernel_surface_find(hub,"mem.pages_alloc"),free=kernel_surface_find(hub,"mem.pages_free"),read=kernel_surface_find(hub,"ext4_fs.read"),admit=kernel_surface_find(hub,"handles.admit");
    struct mem_pages data;if(CALL(alloc,1,0,&data).status)return 0;struct kernel_surface_result got=read(volume,(uintptr_t)path,0,data.address,4096,0);
    struct handle_policy policy={.process=pid,.manifest=(const void *)data.address,.bytes=got.value};policy.identity.installation_id[0]=pid;
    int ok=!got.status && got.value && got.value<4096 && !CALL(admit,&policy,0,0).status;int freed=!CALL(free,data.id,0,0).status;return ok && freed;
}
static int finished(const struct kernel_surfaces *hub,uint64_t id,uint64_t deadline,struct probe_process *probe){
    kernel_surface_call info=kernel_surface_find(hub,"process.info"),reap=kernel_surface_find(hub,"process.reap"),clock=kernel_surface_find(hub,"clock.now_ns"),control=kernel_surface_find(hub,"threads.control");struct process_info result;
    for(;;){if(CALL(info,id,&result,0).status)return 0;if(result.state==PROCESS_EXITED || result.state==PROCESS_FAULTED)break;uint64_t time=CALL(clock,0,0,0).value;if(time>=deadline || CALL(control,THREAD_BLOCK_UNTIL,time+1000000,0).status)return 0;}
    probe->app_code=result.exit_code;if(result.state!=PROCESS_EXITED || result.exit_code)return 0;
    while(CALL(reap,id,0,0).status){uint64_t time=CALL(clock,0,0,0).value;if(time>=deadline || CALL(control,THREAD_BLOCK_UNTIL,time+1000000,0).status)return 0;}++probe->completed;return 1;
}
static void number(uint64_t value){char digits[24];unsigned n=0;do{digits[n++]='0'+value%10;value/=10;}while(value);while(n)__asm__ volatile("outb %0,$0xe9"::"a"(digits[--n]));}
static int benchmark(const struct kernel_surfaces *hub,uint64_t filesystem,struct probe_process *probe){
    kernel_surface_call load=kernel_surface_find(hub,"process.load"),start=kernel_surface_find(hub,"process.start"),info=kernel_surface_find(hub,"process.info"),reap=kernel_surface_find(hub,"process.reap"),control=kernel_surface_find(hub,"threads.control"),clock=kernel_surface_find(hub,"clock.now_ns"),cpu=kernel_surface_find(hub,"cpu.control"),clock_info=kernel_surface_find(hub,"clock.info");
    uint64_t deadline=CALL(clock,0,0,0).value+UINT64_C(30000000000),ids[2];struct process_request r={.abi=1,.filesystem=filesystem,.path="/boot/system/drivers/handle-bench.elf",.stack_bytes=131072};
    for(unsigned i=0;i<2;++i){r.cpu=i && probe->cpus>1?1:0;r.argument=i?0:1;struct kernel_surface_result p=CALL(load,&r,0,0);if(p.status || !admit_file(hub,filesystem,p.value,i?"/boot/system/drivers/bench-client.json":"/boot/system/drivers/bench-server.json"))return 0;ids[i]=p.value;}
    if(CALL(start,ids[0],0,0).status)return 0;
    while(!CALL(kernel_surface_find(hub,"handles.available"),"@bench",0,0).value){uint64_t time=CALL(clock,0,0,0).value;if(time>=deadline || CALL(control,THREAD_BLOCK_UNTIL,time+1000000,0).status)return 0;}
    /* Reset each participating CPU's attribution on that CPU, before launch. */
    for(unsigned i=0;i<(probe->cpus>1?2:1);++i){struct thread_request reset={.abi=1,.cpu=i,.stack_bytes=16384,.entry=(uintptr_t)cpu,.major=1,.payload={.arguments={CPU_METRICS_RESET}}};struct kernel_surface_result t=CALL(kernel_surface_find(hub,"threads.create"),&reset,0,0);if(t.status)return 0;for(;;){struct thread_info ti;if(CALL(control,THREAD_INFO,t.value,&ti).status)return 0;if(ti.state==THREAD_EXITED){if(ti.result.status || CALL(control,THREAD_REAP,t.value,0).status)return 0;break;}uint64_t time=CALL(clock,0,0,0).value;if(time>=deadline || CALL(control,THREAD_BLOCK_UNTIL,time+1000000,0).status)return 0;}}
    struct clock_info before,after;if(CALL(clock_info,&before,0,0).status || CALL(start,ids[1],0,0).status)return 0;
    struct process_info client;for(;;){if(CALL(info,ids[1],&client,0).status)return 0;if(client.state==PROCESS_EXITED || client.state==PROCESS_FAULTED)break;uint64_t time=CALL(clock,0,0,0).value;if(time>=deadline || CALL(control,THREAD_BLOCK_UNTIL,time+1000000,0).status)return 0;}
    probe->app_code=client.exit_code;struct handle_bench_report report;if(client.state!=PROCESS_EXITED || kernel_surface_find(hub,"process.copy")(ids[1],client.exit_code,(uintptr_t)&report,sizeof(report),0,0).status || report.magic!=HB_MAGIC || report.cases!=HB_CASES || report.samples!=HB_SAMPLES || CALL(clock_info,&after,0,0).status)return 0;
    if(before.source!=after.source || before.tsc_hz!=after.tsc_hz || before.calibrations!=after.calibrations)return 0;
    text("MEASURE handles clock ");number(before.source);text(" ");number(before.tsc_hz);text(" ");number(before.calibrations);text("\n");
    for(unsigned kind=0;kind<HB_CASES;++kind){text("MEASURE handles samples ");number(kind);for(unsigned trial=0;trial<HB_SAMPLES;++trial){text(" ");number(report.ns[kind][trial]);}text("\n");}
    for(unsigned i=0;i<(probe->cpus>1?2:1);++i){struct cpu_metrics m;int got=0;for(unsigned retry=0;retry<1024;++retry)if(!CALL(cpu,CPU_METRICS,&m,i).status){got=1;break;}if(!got)return 0;text("MEASURE handles lock ");uint64_t values[]={i,m.acquisitions,m.contended,m.max_wait_ns,m.max_hold_ns,m.max_irq_off_ns,m.tsc_ns_samples,m.hpet_ns_samples,m.unavailable_ns_samples};for(unsigned j=0;j<9;++j){if(j)text(" ");number(values[j]);}text("\n");
        const struct cpu_profile_peak *peaks[]={&m.wait_peak,&m.hold_peak,&m.irq_peak};for(unsigned j=0;j<3;++j){const struct cpu_profile_peak *p=peaks[j];const char *method=p->method && p->method<=kernel_surface_count(hub)?kernel_surface_entries(hub)[p->method-1].name:"kernel";text("ATTR handles ");number(i);text(" ");number(j);const uint64_t *fields=(const void *)p;for(unsigned k=0;k<sizeof(*p)/8;++k){text(" ");number(fields[k]);}text(" ");text(method);text("\n");}}
    while(CALL(reap,ids[1],0,0).status){uint64_t time=CALL(clock,0,0,0).value;if(time>=deadline || CALL(control,THREAD_BLOCK_UNTIL,time+1000000,0).status)return 0;}++probe->completed;
    if(!finished(hub,ids[0],deadline,probe))return 0;
    text("PASS handle benchmark: acquired/reflected/kernel and cross-process calls, owned allocation and raw clock/lock attribution\n");return 1;
}
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
    uint64_t stack_baseline=after.stacks;
    kernel_surface_call launch_keyboard=kernel_surface_find(hub,"bootstrap.keyboard"),inspect=kernel_surface_find(hub,"handles.inspect"),io_info=kernel_surface_find(hub,"io.info"),decommission=kernel_surface_find(hub,"process.stop");
    struct kernel_surface_result keyboard=CALL(launch_keyboard,volume.filesystem.cookie,0,0);CHECK(!keyboard.status && keyboard.value);struct process_info provider;CHECK(!CALL(info,keyboard.value,&provider,0).status);uint64_t root=provider.address_space;
    struct handle_publication_info publication;
    for(;;){if(!CALL(inspect,"/@keyboard",&publication,1).status)break;CHECK(!CALL(info,keyboard.value,&provider,0).status);probe->app_code=provider.exit_code;CHECK(provider.state!=PROCESS_EXITED && provider.state!=PROCESS_FAULTED && CALL(clock,0,0,0).value<deadline);CHECK(!CALL(control,THREAD_BLOCK_UNTIL,CALL(clock,0,0,0).value+1000000,0).status);}
    uint64_t clients[3];const char *policies[]={"/boot/system/drivers/keyboard-state.json","/boot/system/drivers/keyboard-read.json","/boot/system/drivers/keyboard-denied.json"};
    request=(struct process_request){.abi=1,.filesystem=volume.filesystem.cookie,.path="/boot/system/drivers/keyboard-client.elf",.stack_bytes=131072};
    for(unsigned i=0;i<3;++i){request.cpu=probe->cpus>1?1:0;request.argument=i==2?3:i;struct kernel_surface_result client=CALL(load,&request,0,0);CHECK(!client.status);clients[i]=client.value;CHECK(admit_file(hub,volume.filesystem.cookie,client.value,policies[i]) && !CALL(start,client.value,0,0).status);}
    for(;;){CHECK(!CALL(inspect,"/@keyboard",&publication,1).status);if(publication.pending>=2)break;CHECK(CALL(clock,0,0,0).value<deadline && !CALL(control,THREAD_BLOCK_UNTIL,CALL(clock,0,0,0).value+1000000,0).status);}
    text("READY keyboard input\n");for(unsigned i=0;i<3;++i)CHECK(finished(hub,clients[i],deadline,probe));
    struct io_provider_info original;CHECK(!CALL(io_info,keyboard.value,&original,0).status && original.device);
    request.argument=2;struct kernel_surface_result hot=CALL(load,&request,0,0);CHECK(!hot.status && admit_file(hub,volume.filesystem.cookie,hot.value,policies[0]) && !CALL(start,hot.value,0,0).status);
    for(;;){CHECK(!CALL(inspect,"/@keyboard",&publication,1).status);if(publication.pending)break;CHECK(CALL(clock,0,0,0).value<deadline && !CALL(control,THREAD_BLOCK_UNTIL,CALL(clock,0,0,0).value+1000000,0).status);}
    text("READY keyboard unplug\n");
    for(;;){struct io_provider_info current;CHECK(!CALL(io_info,keyboard.value,&current,0).status && !CALL(inspect,"/@keyboard",&publication,1).status);if(current.device!=original.device && current.active && publication.pending && publication.last_operation==2)break;CHECK(CALL(clock,0,0,0).value<deadline && !CALL(control,THREAD_BLOCK_UNTIL,CALL(clock,0,0,0).value+1000000,0).status);}
    text("READY keyboard replug input\n");CHECK(finished(hub,hot.value,deadline,probe));
    CHECK(!CALL(decommission,keyboard.value,0,0).status && !CALL(decommission,keyboard.value,0,0).status);
    struct bootstrap_apps_info app_status;CHECK(!CALL(kernel_surface_find(hub,"bootstrap.apps_info"),&app_status,0,0).status && app_status.state==4);
    CHECK(finished(hub,keyboard.value,deadline,probe));
    CHECK(CALL(inspect,"/@keyboard",&publication,0).status && CALL(io_info,keyboard.value,&original,0).status);
    CHECK(CALL(kernel_surface_find(hub,"mem.space_info"),root,keyboard.value,&provider).status);
    CHECK(!CALL(pages,&after,0,0).status && after.stacks==stack_baseline);
    for(unsigned cycle=0;cycle<2;++cycle){
        deadline=CALL(clock,0,0,0).value+UINT64_C(30000000000);
        request=(struct process_request){.abi=1,.filesystem=volume.filesystem.cookie,.path="/boot/system/drivers/keyboard-fault.elf",.stack_bytes=262144};
        struct kernel_surface_result crashed=CALL(load,&request,0,0);CHECK(!crashed.status && admit_file(hub,volume.filesystem.cookie,crashed.value,"/boot/system/drivers/keyboard/manifest.json") && !CALL(start,crashed.value,0,0).status);
        for(;;){struct io_provider_info worker;if(!CALL(inspect,"/@keyboard",&publication,0).status && !CALL(io_info,crashed.value,&worker,0).status && worker.active)break;CHECK(CALL(clock,0,0,0).value<deadline && !CALL(control,THREAD_BLOCK_UNTIL,CALL(clock,0,0,0).value+1000000,0).status);}
        request.path="/boot/system/drivers/keyboard-client.elf";request.cpu=probe->cpus>1?1:0;request.argument=4;
        struct kernel_surface_result waiting=CALL(load,&request,0,0);CHECK(!waiting.status && admit_file(hub,volume.filesystem.cookie,waiting.value,policies[0]) && !CALL(start,waiting.value,0,0).status);
        CHECK(finished(hub,waiting.value,deadline,probe));CHECK(!CALL(info,crashed.value,&provider,0).status && provider.state==PROCESS_FAULTED && provider.fault_vector==6);
        while(CALL(reap,crashed.value,0,0).status)CHECK(CALL(clock,0,0,0).value<deadline && !CALL(control,THREAD_BLOCK_UNTIL,CALL(clock,0,0,0).value+1000000,0).status);
        ++probe->completed;CHECK(CALL(io_info,crashed.value,&original,0).status && CALL(inspect,"/@keyboard",&publication,0).status);
        keyboard=CALL(launch_keyboard,volume.filesystem.cookie,0,0);CHECK(!keyboard.status && keyboard.value!=crashed.value);
        for(;;){if(!CALL(inspect,"/@keyboard",&publication,0).status && !CALL(io_info,keyboard.value,&original,0).status && original.active)break;CHECK(CALL(clock,0,0,0).value<deadline && !CALL(control,THREAD_BLOCK_UNTIL,CALL(clock,0,0,0).value+1000000,0).status);}
        CHECK(!CALL(decommission,keyboard.value,0,0).status && finished(hub,keyboard.value,deadline,probe));
        CHECK(!CALL(pages,&after,0,0).status && after.stacks==stack_baseline);
    }
    CHECK(benchmark(hub,volume.filesystem.cookie,probe));
    text("PASS provider lifecycle: active USB/read fault, original UNKNOWN outcome, explicit stop/reap and two fresh restart cycles\n");
    text("PASS keyboard handler: disk-loaded isolated /@keyboard, authorized independent clients, Unicode input, structured arrows, USB hotplug/reset and quiescent decommission\n");
    return module_result(1,1);
}
