#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/scheduler.h"
#include "../kmodules/shared/mem.h"
static _Alignas(16) unsigned char arena[1024*1024];
static struct mem_heap heap;
static struct { struct kernel_surfaces hub; struct kernel_surface methods[10]; } fixture;
static size_t live,invoked,irq_deliveries;
static bool fail_allocation,irq_bound=true;
static unsigned order[32],order_count;
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static uint64_t time_ns,simulated_duration;
static bool fail_clock,simulate_end_failure,simulate_regression;
static struct kernel_surface_result clock_now(MODULE_ARGS) {MODULE_UNUSED();return module_result(time_ns,!fail_clock);}
static struct kernel_surface_result allocate(MODULE_ARGS) {
    MODULE_UNUSED(); void *p=fail_allocation?NULL:mem_calloc((void *)a,b,c);
    if (p) ++live;
    return module_result((uintptr_t)p,p!=NULL);
}
static struct kernel_surface_result release(MODULE_ARGS) {
    MODULE_UNUSED(); mem_free((void *)a,(void *)b); --live; return module_result(0,1);
}
static struct kernel_surface_result bound(MODULE_ARGS) {
    MODULE_UNUSED(); return module_result(a==128 && irq_bound,1);
}
static struct kernel_surface_result dispatch(MODULE_ARGS) {
    MODULE_UNUSED(); struct inthandle_frame *frame=(void *)a;
    if (!irq_bound || frame->vector!=128) return module_result(0,0);
    ++irq_deliveries; return module_result(1,1);
}
static struct kernel_surface_result work(MODULE_ARGS) {
    if (order_count<32) order[order_count++]=(unsigned)a;
    time_ns+=simulated_duration;
    if (simulate_end_failure) fail_clock=true;
    if (simulate_regression) time_ns-=100;
    ++invoked; return module_result(a+3*b+5*c+7*d+11*e+13*f,1);
}
struct blob { uint64_t left,right; };
static struct kernel_surface_result copied(MODULE_ARGS) {
    MODULE_UNUSED(); const struct blob *value=(void *)a;
    assert(a%16==0 && b==sizeof(*value)); ++invoked;
    return module_result(value->left+value->right+c,1);
}
static struct kernel_surface_result failed(MODULE_ARGS) {
    MODULE_UNUSED(); return module_result(99,0);
}
static struct scheduler_request method(kernel_surface_call entry) {
    struct scheduler_request request={.abi=SCHEDULER_REQUEST_ABI,.process_id=1,.kind=SCHEDULER_METHOD,.entry_vector=(uintptr_t)entry,.entry_major=1};
    return request;
}
static struct kernel_surface_result nested(MODULE_ARGS) {
    MODULE_UNUSED(); struct scheduler_stats stats;
    assert(!CALL(scheduler_surface_stats,&stats,0,0).status && stats.running==1);
    assert(CALL(scheduler_surface_run,1,0,0).status==KERNEL_SURFACE_FAILED);
    assert(CALL(scheduler_surface_cancel,*(uint64_t *)a,0,0).status==KERNEL_SURFACE_FAILED);
    struct scheduler_completion completion;
    assert(CALL(scheduler_surface_result,*(uint64_t *)a,&completion,1).status==KERNEL_SURFACE_FAILED);
    struct scheduler_request child=method(work); child.payload.arguments[0]=9;
    assert(!CALL(scheduler_surface_submit,&child,0,0).status);
    return module_result(7,1);
}
static struct kernel_surface_result remove_self(MODULE_ARGS) {
    MODULE_UNUSED(); const struct blob *payload=(void *)a;
    assert(payload->left==17 && payload->right==18);
    struct kernel_surface_result removal=CALL(scheduler_surface_remove_process,42,0,0);
    assert(!removal.status && removal.value==2);
    /* The active callback's copied payload must remain alive until it returns. */
    assert(payload->left==17 && payload->right==18 && live==2);
    struct scheduler_request request=method(work);request.process_id=42;
    assert(CALL(scheduler_surface_submit,&request,0,0).status==KERNEL_SURFACE_FAILED);
    request.process_id=43;
    assert(!CALL(scheduler_surface_submit,&request,0,0).status);
    return module_result(7,1);
}
static struct scheduler_completion result(uint64_t id, bool reap) {
    struct scheduler_completion out;
    assert(!CALL(scheduler_surface_result,id,&out,reap).status);
    return out;
}
int main(void) {
    assert(CALL(scheduler_surface_run,1,0,0).status==KERNEL_SURFACE_FAILED);
    assert(mem_init(&heap,arena,sizeof(arena)));
    fixture.hub.magic=KERNEL_SURFACES_MAGIC; fixture.hub.major=2;
    fixture.hub.header_bytes=offsetof(__typeof__(fixture),methods); fixture.hub.count=10;
    const char *names[]={"mem.calloc","mem.free","inthandle.dispatch","inthandle.is_bound",
                         "test.work","test.copied","test.nested","test.failed","test.remove","clock.now_ns"};
    kernel_surface_call calls[]={allocate,release,dispatch,bound,work,copied,nested,failed,remove_self,clock_now};
    for (unsigned i=0;i<10;++i) {
        strcpy(fixture.methods[i].name,names[i]);fixture.methods[i].call=calls[i];fixture.methods[i].major=1;fixture.methods[i].minor=1;
    }
    assert(!CALL(scheduler_surface_init,&fixture.hub,&heap,0).status);
    assert(!CALL(scheduler_surface_init,&fixture.hub,&heap,0).status);
    assert(CALL(scheduler_surface_init,&fixture.hub,arena,0).status==KERNEL_SURFACE_FAILED);
    struct scheduler_request request=method(work);
    request.payload.arguments[0]=1;
    uint64_t first=CALL(scheduler_surface_submit,&request,0,0).value; assert(first);
    request.payload.arguments[0]=2;
    uint64_t second=CALL(scheduler_surface_submit,&request,0,0).value; assert(second>first);
    request.payload.arguments[0]=3;
    uint64_t third=CALL(scheduler_surface_submit,&request,0,0).value; assert(third>second);
    assert(result(first,false).state==SCHEDULER_QUEUED);
    struct scheduler_completion out;
    assert(CALL(scheduler_surface_result,first,&out,1).status==KERNEL_SURFACE_FAILED);
    assert(!CALL(scheduler_surface_cancel,second,0,0).status);
    assert(!CALL(scheduler_surface_cancel,second,0,0).status);
    assert(CALL(scheduler_surface_run,0,0,0).value==0);
    assert(CALL(scheduler_surface_run,1,0,0).value==1 && order_count==1 && order[0]==1);
    assert(result(first,true).result.value==1);
    assert(CALL(scheduler_surface_result,first,&out,0).status==KERNEL_SURFACE_FAILED);
    assert(result(second,true).state==SCHEDULER_CANCELLED);
    assert(CALL(scheduler_surface_run,10,0,0).value==1 && order[1]==3);
    assert(result(third,true).state==SCHEDULER_COMPLETE);
    struct blob data={17,18};request=method(copied);
    request.payload.data=&data;request.payload.bytes=sizeof(data);request.payload.arguments[1]=sizeof(data);
    request.payload.arguments[2]=7;
    uint64_t id=CALL(scheduler_surface_submit,&request,0,0).value;assert(id);
    data.left=1000;request.payload.arguments[2]=1000;
    assert(CALL(scheduler_surface_run,1,0,0).value==1 && result(id,true).result.value==42);
    request=method(failed);id=CALL(scheduler_surface_submit,&request,0,0).value;
    assert(id && CALL(scheduler_surface_run,1,0,0).value==1);
    out=result(id,true);assert(out.result.status==KERNEL_SURFACE_FAILED && out.result.value==99);
    request=method(nested);request.payload.arguments[0]=(uintptr_t)&id;
    id=CALL(scheduler_surface_submit,&request,0,0).value;
    assert(id && CALL(scheduler_surface_run,1,0,0).value==1 && result(id,true).result.value==7);
    struct scheduler_stats stats;CALL(scheduler_surface_stats,&stats,0,0);
    assert(stats.queued==1 && stats.retained==1);
    assert(CALL(scheduler_surface_run,1,0,0).value==1);
    result(id+1,true);
    struct inthandle_frame frame={.vector=128,.cs=8};
    request=(struct scheduler_request){.abi=SCHEDULER_REQUEST_ABI,.process_id=1,.kind=SCHEDULER_INTERRUPT,.entry_vector=128,.entry_major=1,
        .payload={.data=&frame,.bytes=sizeof(frame)}};
    id=CALL(scheduler_surface_submit,&request,0,0).value;assert(id);
    frame.vector=129;
    assert(CALL(scheduler_surface_run,1,0,0).value==1 && irq_deliveries==1 && result(id,true).result.value==1);
    frame.vector=128;id=CALL(scheduler_surface_submit,&request,0,0).value;assert(id);
    irq_bound=false;
    assert(CALL(scheduler_surface_run,1,0,0).value==1 && result(id,true).result.status==KERNEL_SURFACE_FAILED);
    assert(CALL(scheduler_surface_submit,&request,0,0).status==KERNEL_SURFACE_FAILED);
    irq_bound=true;
    request.entry_vector=14;frame.vector=14;
    assert(CALL(scheduler_surface_submit,&request,0,0).status==KERNEL_SURFACE_FAILED);
    request=method(work);request.entry_major=2;
    assert(CALL(scheduler_surface_submit,&request,0,0).status==KERNEL_SURFACE_FAILED);
    request=method((kernel_surface_call)(uintptr_t)1);
    assert(CALL(scheduler_surface_submit,&request,0,0).status==KERNEL_SURFACE_FAILED);
    request=method(work);request.payload.bytes=SIZE_MAX;
    assert(CALL(scheduler_surface_submit,&request,0,0).status==KERNEL_SURFACE_FAILED);
    request=method(work);request.payload.bytes=1;request.payload.data=&data;request.payload.data_register=6;
    assert(CALL(scheduler_surface_submit,&request,0,0).status==KERNEL_SURFACE_FAILED);
    request=method(work); fail_allocation=true;
    assert(CALL(scheduler_surface_submit,&request,0,0).status==KERNEL_SURFACE_FAILED);
    fail_allocation=false;
    request=method(work);request.process_id=0;
    id=CALL(scheduler_surface_submit,&request,0,0).value;assert(id);
    assert(result(id,false).process_id==0);
    assert(CALL(scheduler_surface_remove_process,0,0,0).value==1);
    assert(CALL(scheduler_surface_result,id,&out,0).status==KERNEL_SURFACE_FAILED);
    request.process_id=42;
    uint64_t completed=CALL(scheduler_surface_submit,&request,0,0).value;
    assert(completed && CALL(scheduler_surface_run,1,0,0).value==1);
    uint64_t cancelled=CALL(scheduler_surface_submit,&request,0,0).value;
    assert(cancelled && !CALL(scheduler_surface_cancel,cancelled,0,0).status);
    request=method(copied);request.process_id=42;request.payload.data=&data;request.payload.bytes=sizeof(data);
    uint64_t queued=CALL(scheduler_surface_submit,&request,0,0).value;assert(queued);
    frame.vector=128;
    request=(struct scheduler_request){.abi=SCHEDULER_REQUEST_ABI,.process_id=42,
        .kind=SCHEDULER_INTERRUPT,.entry_vector=128,.entry_major=1,
        .payload={.data=&frame,.bytes=sizeof(frame)}};
    uint64_t interrupt=CALL(scheduler_surface_submit,&request,0,0).value;assert(interrupt);
    request=method(work);request.process_id=43;
    uint64_t survivor=CALL(scheduler_surface_submit,&request,0,0).value;assert(survivor);
    assert(result(survivor,false).process_id==43);
    struct kernel_surface_result removal=CALL(scheduler_surface_remove_process,42,0,0);
    assert(!removal.status && removal.value==4);
    assert(CALL(scheduler_surface_result,completed,&out,0).status==KERNEL_SURFACE_FAILED);
    assert(CALL(scheduler_surface_result,cancelled,&out,0).status==KERNEL_SURFACE_FAILED);
    assert(CALL(scheduler_surface_result,queued,&out,0).status==KERNEL_SURFACE_FAILED);
    assert(CALL(scheduler_surface_result,interrupt,&out,0).status==KERNEL_SURFACE_FAILED);
    assert(!CALL(scheduler_surface_remove_process,42,0,0).status);
    CALL(scheduler_surface_stats,&stats,0,0);
    assert(stats.queued==1 && stats.retained==1 && !stats.complete && !stats.cancelled && live==1);
    data=(struct blob){17,18};request=method(remove_self);request.process_id=42;
    request.payload.data=&data;request.payload.bytes=sizeof(data);
    uint64_t active=CALL(scheduler_surface_submit,&request,0,0).value;assert(active);
    request=method(work);request.process_id=42;
    assert(CALL(scheduler_surface_submit,&request,0,0).value);
    /* Remove another process first, then run the self-removing callback. */
    assert(CALL(scheduler_surface_remove_process,43,0,0).value==1);
    /* Keep one unrelated record alive throughout deferred cleanup. */
    request.process_id=44;survivor=CALL(scheduler_surface_submit,&request,0,0).value;assert(survivor);
    assert(CALL(scheduler_surface_run,1,0,0).value==1);
    assert(CALL(scheduler_surface_result,active,&out,0).status==KERNEL_SURFACE_FAILED);
    CALL(scheduler_surface_stats,&stats,0,0);
    assert(!stats.running && !stats.complete && stats.queued==2 && stats.retained==2 && live==2);
    assert(CALL(scheduler_surface_remove_process,43,0,0).value==1);
    assert(CALL(scheduler_surface_run,1,0,0).value==1 && result(survivor,true).process_id==44);
    request=method(work);request.process_id=42;
    id=CALL(scheduler_surface_submit,&request,0,0).value;assert(id);
    assert(CALL(scheduler_surface_remove_process,42,0,0).value==1);
    uint64_t deadline=0;
    assert(CALL(scheduler_surface_next_deadline,&deadline,0,0).value==SCHEDULER_IDLE);
    request=method(work);request.process_id=99;request.deadline_ns=100;request.payload.arguments[0]=10;
    uint64_t later=CALL(scheduler_surface_submit,&request,0,0).value;assert(later);
    request.deadline_ns=50;request.payload.arguments[0]=11;
    uint64_t earlier=CALL(scheduler_surface_submit,&request,0,0).value;assert(earlier);
    request.payload.arguments[0]=12;uint64_t same=CALL(scheduler_surface_submit,&request,0,0).value;assert(same);
    assert(result(earlier,false).state==SCHEDULER_WAITING);
    CALL(scheduler_surface_stats,&stats,0,0);assert(stats.queued==3 && stats.waiting==3);
    assert(CALL(scheduler_surface_next_deadline,&deadline,0,0).value==SCHEDULER_DEADLINE && deadline==50);
    assert(CALL(scheduler_surface_run,100,0,0).value==0);
    time_ns=49;assert(CALL(scheduler_surface_run,100,0,0).value==0);
    time_ns=50;unsigned before_order=order_count;
    assert(CALL(scheduler_surface_run,1,0,0).value==1 && order[before_order]==11);
    assert(result(earlier,true).state==SCHEDULER_COMPLETE);
    assert(CALL(scheduler_surface_next_deadline,&deadline,0,0).value==SCHEDULER_READY);
    assert(CALL(scheduler_surface_run,1,0,0).value==1 && order[before_order+1]==12);result(same,true);
    assert(!CALL(scheduler_surface_cancel,later,0,0).status);result(later,true);
    request.deadline_ns=UINT64_MAX;assert(CALL(scheduler_surface_submit,&request,0,0).value);
    assert(CALL(scheduler_surface_next_deadline,&deadline,0,0).value==SCHEDULER_DEADLINE && deadline==UINT64_MAX);
    assert(CALL(scheduler_surface_remove_process,99,0,0).value==1);
    CALL(scheduler_surface_stats,&stats,0,0);assert(!stats.waiting && !stats.queued && !stats.retained);
    request.deadline_ns=51;fail_clock=true;
    assert(CALL(scheduler_surface_submit,&request,0,0).status==KERNEL_SURFACE_FAILED);
    assert(CALL(scheduler_surface_run,1,0,0).status==KERNEL_SURFACE_FAILED);
    assert(CALL(scheduler_surface_next_deadline,&deadline,0,0).status==KERNEL_SURFACE_FAILED);
    fail_clock=false;request.deadline_ns=0;
    struct scheduler_diagnostics diagnostics_before,diagnostics_after;
    assert(!CALL(scheduler_surface_info,&diagnostics_before,0,0).status);
    time_ns=100;request=method(work);request.process_id=123;request.deadline_ns=50;
    id=CALL(scheduler_surface_submit,&request,0,0).value;assert(id);simulated_duration=200;
    assert(CALL(scheduler_surface_run,1,0,0).value==1);simulated_duration=0;result(id,true);
    assert(!CALL(scheduler_surface_info,&diagnostics_after,0,0).status);
    assert(diagnostics_after.completed==diagnostics_before.completed+1 && diagnostics_after.last_task_id==id &&
        diagnostics_after.last_process_id==123 && diagnostics_after.total_runtime_ns==diagnostics_before.total_runtime_ns+200 &&
        diagnostics_after.max_runtime_ns>=200 && diagnostics_after.max_lateness_ns>=50);
    request.deadline_ns=0;
    time_ns=1000;simulate_end_failure=true;
    id=CALL(scheduler_surface_submit,&request,0,0).value;assert(id);
    assert(CALL(scheduler_surface_run,1,0,0).value==1);simulate_end_failure=false;fail_clock=false;
    assert(result(id,true).result.status==KERNEL_SURFACE_OK);
    CALL(scheduler_surface_info,&diagnostics_before,0,0);assert(diagnostics_before.timing_failures);
    simulate_regression=true;
    id=CALL(scheduler_surface_submit,&request,0,0).value;assert(id);
    assert(CALL(scheduler_surface_run,1,0,0).value==1);simulate_regression=false;
    assert(result(id,true).result.status==KERNEL_SURFACE_OK);
    CALL(scheduler_surface_info,&diagnostics_after,0,0);
    assert(diagnostics_after.timing_failures==diagnostics_before.timing_failures+1 && diagnostics_after.clock_regressions==diagnostics_before.clock_regressions+1);
    static uint64_t ids[8192];size_t count=0;
    while ((id=CALL(scheduler_surface_submit,&request,0,0).value)!=0) {assert(count<8192);ids[count++]=id;}
    assert(count>100);
    for (size_t i=0;i<count;++i) {assert(!CALL(scheduler_surface_cancel,ids[i],0,0).status);result(ids[i],true);}
    CALL(scheduler_surface_stats,&stats,0,0);
    assert(!stats.retained && !stats.queued && !stats.running && !stats.complete && !stats.cancelled && !live);
    void *whole=mem_malloc(&heap,sizeof(arena)-64);assert(whole);mem_free(&heap,whole);
    puts("PASS: scheduler FIFO/budgets, register and copied payloads, interrupt vectors, cancellation/reaping, reentry, process ownership/removal and exhaustion cleanup");
    return 0;
}
