#include "../kmodules/shared/io-process.h"
static int application_runtime(const struct kernel_surfaces *hub,uint64_t filesystem,struct probe_process *probe){
    kernel_surface_call launch=kernel_surface_find(hub,"bootstrap.launch"),info=kernel_surface_find(hub,"io.process_info"),clock=kernel_surface_find(hub,"clock.now_ns"),control=kernel_surface_find(hub,"threads.control"),stop=kernel_surface_find(hub,"process.stop"),pages=kernel_surface_find(hub,"mem.pages_info");
    kernel_surface_call profiling=kernel_surface_find(hub,"handles.profile");uint64_t heap_before=0,heap_after=0;
    if(profiling && CALL(profiling,HP_LIVE,&heap_before,0).status)return 0;
    struct mem_pages_info before,after;struct io_process_info baseline,status;uint64_t deadline=CALL(clock,0,0,0).value+UINT64_C(30000000000);
    if(CALL(pages,&before,0,0).status || CALL(info,&baseline,0,0).status || baseline.manager || baseline.children)return 0;
    struct kernel_surface_result manager=CALL(launch,filesystem,"applications",0);if(manager.status)return 0;
    for(;;){if(CALL(info,&status,0,0).status)return 0;if(status.launched>baseline.launched && !status.children)break;uint64_t time=CALL(clock,0,0,0).value;if(time>=deadline){text("FAIL application runtime timeout launched ");number(status.launched);text(" reaped ");number(status.reaped);text(" exit ");number(status.last_exit);text(" children ");number(status.children);text("\n");return 0;}if(CALL(control,THREAD_BLOCK_UNTIL,time+1000000,0).status)return 0;}
    probe->app_code=status.last_exit;if(status.last_exit || status.launched<baseline.launched+11 || status.launched!=status.reaped || status.faulted!=baseline.faulted+1){text("FAIL application runtime result exit ");number(status.last_exit);text(" launched ");number(status.launched);text(" faulted ");number(status.faulted);text("\n");return 0;}
    if(CALL(stop,manager.value,0,0).status || !finished(hub,manager.value,deadline,probe) || CALL(kernel_surface_find(hub,"bootstrap.release"),manager.value,0,0).status)return 0;
    if(CALL(info,&status,0,0).status || status.manager || status.children || CALL(pages,&after,0,0).status || before.stacks!=after.stacks || before.stack_pages!=after.stack_pages)return 0;
    if(profiling && (CALL(profiling,HP_LIVE,&heap_after,0).status || heap_after!=heap_before))return 0;
    text("PASS application runtime: installed userland launcher, arguments/cwd, manifest isolation, concurrent children, deferred wait, stop, fault notification, owner abandonment and cleanup\n");return 1;
}
