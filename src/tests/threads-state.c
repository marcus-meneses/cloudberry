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
    local.current=NULL;finish_suspend(&t);
    assert(status(&t)==THREAD_RUNNABLE && !t.wake_pending && !t.wake_ns);
    /* No wake: publish quiescent BLOCKED, preserving its sleep deadline. */
    set_status(&t,THREAD_BLOCKING);t.wake_ns=999;finish_suspend(&t);
    assert(status(&t)==THREAD_BLOCKED && t.wake_ns==999);
    assert(!call(threads_surface_control,THREAD_INFO,1,(uintptr_t)&info).status && info.state==THREAD_BLOCKED);
    assert(!call(threads_surface_control,THREAD_WAKE,1,0).status);
    assert(status(&t)==THREAD_RUNNABLE && !t.wake_ns && !t.wake_pending && wakes==1 && t.ready_ns==123);
    /* Existing before-block credits and exit/reap lifecycle remain intact. */
    for(unsigned mode=THREAD_RUNNABLE;mode<=THREAD_RUNNING;++mode){set_status(&t,mode);assert(!call(threads_surface_control,THREAD_WAKE,1,0).status && t.wake_pending);t.wake_pending=0;}
    set_status(&t,THREAD_EXITING);finish_suspend(&t);assert(status(&t)==THREAD_EXITED);
    puts("PASS thread state: blocking publication, coalesced transition wakes, post-block IPI and exit publication");
    return 0;
}
