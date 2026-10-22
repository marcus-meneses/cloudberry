/* Exercise the real control/publication code without privileged context switches. */
#include <assert.h>
#include <stdio.h>
#include "../kmodules/shared/threads.c"
void threads_switch(struct context *from,const struct context *to){(void)from;(void)to;assert(0);}
static unsigned wakes;
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();if(a==CPU_WAKE)++wakes;return module_result(0,1);}
static struct kernel_surface_result clock_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(123,1);}
int main(void){
    struct kernel_surfaces hub={0};struct local local={0};struct thread t={.info={.id=1,.cpu=0}};
    state.hub=&hub;state.cpu=cpu_stub;state.now=clock_stub;state.cpus=&local;state.head=state.tail=&t;
    /* A remote query sees BLOCKING until dispatcher completion. Waking during
     * that window must not publish a context whose save is still in progress. */
    set_status(&t,THREAD_BLOCKING);local.current=&t;t.wake_ns=999;
    struct thread_info info;assert(!call(threads_surface_control,THREAD_INFO,1,(uintptr_t)&info).status && info.state==THREAD_BLOCKING);
    for(unsigned i=0;i<2;++i)assert(!call(threads_surface_control,THREAD_WAKE,1,0).status);
    assert(status(&t)==THREAD_BLOCKING && t.wake_pending && !wakes);
    local.current=NULL;finish_suspend(&t,123,1);
    assert(status(&t)==THREAD_RUNNABLE && !t.wake_pending && !t.wake_ns);
    /* No wake: publish quiescent BLOCKED, preserving its sleep deadline. */
    set_status(&t,THREAD_BLOCKING);t.wake_ns=999;finish_suspend(&t,123,1);
    assert(status(&t)==THREAD_BLOCKED && t.wake_ns==999);
    assert(!call(threads_surface_control,THREAD_INFO,1,(uintptr_t)&info).status && info.state==THREAD_BLOCKED);
    assert(!call(threads_surface_control,THREAD_WAKE,1,0).status);
    assert(status(&t)==THREAD_RUNNABLE && !t.wake_ns && !t.wake_pending && wakes==1 && t.ready_ns==123);
    /* Existing before-block credits and exit/reap lifecycle remain intact. */
    for(unsigned mode=THREAD_RUNNABLE;mode<=THREAD_RUNNING;++mode){set_status(&t,mode);assert(!call(threads_surface_control,THREAD_WAKE,1,0).status && t.wake_pending);t.wake_pending=0;}
    set_status(&t,THREAD_EXITING);finish_suspend(&t,123,1);assert(status(&t)==THREAD_EXITED);
    /* Blocked sleepers do not outweigh real runnable work in placement. */
    struct local cpus[2]={0};state.cpus=cpus;state.count=2;
    struct thread sleepers[3]={0};for(unsigned i=0;i<3;++i){sleepers[i].info=(struct thread_info){.id=i+2,.cpu=0,.state=THREAD_BLOCKED,.process_id=i+2};sleepers[i].next=i<2?&sleepers[i+1]:&t;}
    t.info.cpu=1;set_status(&t,THREAD_RUNNABLE);t.next=NULL;state.head=sleepers;state.tail=&t;
    assert(call(threads_surface_control,THREAD_SELECT_CPU,0,0).value==0);
    struct thread_load measured;assert(!call(threads_surface_control,THREAD_LOAD,0,(uintptr_t)&measured).status && measured.blocked==3 && !measured.runnable);
    /* Only a fully detached, eligible user-return context may move. */
    struct thread peer={.info={.id=9,.cpu=1,.state=THREAD_RUNNING}};t.next=&peer;state.head=&t;state.tail=&peer;t.migratable=t.boundary=1;
    struct thread *previous;cpus[1].current=&t;assert(!select_thread(0,&previous));cpus[1].current=NULL;
    t.boundary=0;assert(!select_thread(0,&previous));t.boundary=1;t.migratable=0;assert(!select_thread(0,&previous));t.migratable=1;
    set_status(&t,THREAD_BLOCKING);assert(!select_thread(0,&previous));set_status(&t,THREAD_RUNNABLE);
    assert(select_thread(0,&previous)==&t && !previous && t.info.cpu==0 && cpus[0].migrations_in==1 && cpus[1].migrations_out==1);
    assert(call(threads_surface_control,THREAD_LOAD,2,(uintptr_t)&measured).status);
    puts("PASS thread state: runnable-pressure placement, detached boundary migration, pinned exclusions, migration metrics; blocking publication, coalesced transition wakes, post-block IPI and exit publication");
    return 0;
}
