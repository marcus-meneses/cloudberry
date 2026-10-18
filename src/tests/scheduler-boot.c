#include "../kmodules/shared/scheduler.h"
#include "../kmodules/shared/hwenum.h"
#include "probe.h"
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
bool kernel_scheduler_test(const struct kernel_surfaces *hub) {
    kernel_surface_call init=kernel_surface_find_version(hub,"scheduler.init",1,0),
        submit=kernel_surface_find_version(hub,"scheduler.submit",3,0),
        run=kernel_surface_find_version(hub,"scheduler.run",1,0),
        cancel=kernel_surface_find_version(hub,"scheduler.cancel",1,0),
        result=kernel_surface_find_version(hub,"scheduler.result",2,0),
        remove_process=kernel_surface_find_version(hub,"scheduler.remove_process",1,0),
        stats_call=kernel_surface_find_version(hub,"scheduler.stats",2,0),
        bind=kernel_surface_find_version(hub,"inthandle.bind",1,0),
        check=kernel_surface_find_version(hub,"inthandle.check",1,0),
        arguments=kernel_surface_find_version(hub,"probe.arguments",2,3),
        payload_entry=kernel_surface_find_version(hub,"probe.payload",1,0);
    if (!init || !submit || !run || !cancel || !result || !remove_process || !stats_call || !bind || !check || !arguments || !payload_entry)
        return false;
    const struct kernel_hub_table *hardware=kernel_hub_find_table(hub,HWENUM_TABLE_NAME);
    if (!hardware) return false;
    const struct hwenum_table *table=(const void *)hardware->address;
    if (CALL(init,hub,table->heap,0).status) return false;
    struct scheduler_request request={.abi=SCHEDULER_REQUEST_ABI,.process_id=1,.kind=SCHEDULER_METHOD,.entry_vector=(uintptr_t)arguments,
        .entry_major=2,.entry_minor=3,.payload={.arguments={1,2,3,4,5,6}}};
    struct kernel_surface_result r=CALL(submit,&request,0,0);if (r.status || !r.value) return false;
    uint64_t first=r.value;
    request.process_id=0;
    r=CALL(submit,&request,0,0);if (r.status || !r.value) return false;
    struct scheduler_completion kernel_completion;
    if (CALL(result,r.value,&kernel_completion,0).status || kernel_completion.process_id ||
        CALL(remove_process,0,0,0).value!=1) return false;
    struct probe_payload payload={17,18};
    request=(struct scheduler_request){.abi=SCHEDULER_REQUEST_ABI,.process_id=1,.kind=SCHEDULER_METHOD,.entry_vector=(uintptr_t)payload_entry,
        .entry_major=1,.payload={.arguments={0,sizeof(payload),7},.data=&payload,.bytes=sizeof(payload)}};
    r=CALL(submit,&request,0,0);if (r.status || !r.value) return false;
    uint64_t second=r.value;
    payload.left=1000;request.payload.arguments[2]=1000;
    r=CALL(submit,&request,0,0);if (r.status || !r.value) return false;
    uint64_t cancelled=r.value;
    if (CALL(cancel,cancelled,0,0).status) return false;
    struct scheduler_completion completion;
    if (CALL(result,first,&completion,1).status!=KERNEL_SURFACE_FAILED || CALL(run,1,0,0).value!=1 ||
        CALL(result,first,&completion,1).status || completion.state!=SCHEDULER_COMPLETE ||
        completion.result.status || completion.result.value!=183) return false;
    if (CALL(result,second,&completion,0).status || completion.state!=SCHEDULER_QUEUED ||
        CALL(run,1,0,0).value!=1 || CALL(result,second,&completion,1).status ||
        completion.result.status || completion.result.value!=42) return false;
    if (CALL(result,cancelled,&completion,1).status || completion.state!=SCHEDULER_CANCELLED) return false;
    uint64_t before=CALL(check,129,0,0).value;
    if (CALL(bind,129,"probe.deferred",42).status) return false;
    struct inthandle_frame frame={.vector=129,.cs=8};
    request=(struct scheduler_request){.abi=SCHEDULER_REQUEST_ABI,.process_id=1,.kind=SCHEDULER_INTERRUPT,.entry_vector=129,.entry_major=1,
        .payload={.data=&frame,.bytes=sizeof(frame)}};
    r=CALL(submit,&request,0,0);if (r.status || !r.value) return false;
    uint64_t irq=r.value;frame.vector=129;
    if (CALL(run,1,0,0).value!=1 || CALL(result,irq,&completion,1).status || completion.result.status ||
        CALL(check,129,0,0).value!=before+1) return false;
    frame.vector=129;r=CALL(submit,&request,0,0);if (r.status || !r.value) return false;
    irq=r.value;
    if (CALL(bind,129,0,0).status || CALL(run,1,0,0).value!=1 ||
        CALL(result,irq,&completion,1).status || completion.result.status!=KERNEL_SURFACE_FAILED) return false;
    if (CALL(bind,129,"probe.failed",0).status) return false;
    r=CALL(submit,&request,0,0);if (r.status || !r.value) return false;
    irq=r.value;
    if (CALL(run,1,0,0).value!=1 || CALL(result,irq,&completion,1).status ||
        completion.result.status!=KERNEL_SURFACE_FAILED || CALL(bind,129,0,0).status) return false;
    request=(struct scheduler_request){.abi=SCHEDULER_REQUEST_ABI,.process_id=42,.kind=SCHEDULER_METHOD,
        .entry_vector=(uintptr_t)payload_entry,.entry_major=1,
        .payload={.arguments={0,sizeof(payload),7},.data=&payload,.bytes=sizeof(payload)}};
    r=CALL(submit,&request,0,0);if (r.status || !r.value) return false;
    uint64_t removed=r.value;
    if (CALL(run,1,0,0).value!=1) return false;
    r=CALL(submit,&request,0,0);if (r.status || !r.value) return false;
    uint64_t queued=r.value;
    request.process_id=43;
    r=CALL(submit,&request,0,0);if (r.status || !r.value) return false;
    uint64_t survivor=r.value;
    r=CALL(remove_process,42,0,0);
    if (r.status || r.value!=2 || CALL(result,removed,&completion,0).status!=KERNEL_SURFACE_FAILED ||
        CALL(result,queued,&completion,0).status!=KERNEL_SURFACE_FAILED ||
        CALL(result,survivor,&completion,0).status || completion.process_id!=43 ||
        CALL(run,1,0,0).value!=1 || CALL(result,survivor,&completion,1).status) return false;
    r=CALL(remove_process,42,0,0);if (r.status || r.value) return false;
    struct scheduler_stats stats;
    if (CALL(stats_call,&stats,0,0).status || stats.queued || stats.running || stats.complete ||
        stats.cancelled || stats.retained) return false;
    return true;
}
