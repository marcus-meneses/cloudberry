#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/process.c"
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result control_stub(MODULE_ARGS){
    MODULE_UNUSED();
    if(a==THREAD_ACCOUNTING){*(struct thread_account_info *)c=(struct thread_account_info){.runtime_ns=b*100,.timestamp_ns=5000,.cpus=4,.cpu=2,.state=THREAD_BLOCKED,.flags=1};return module_result(1,1);}
    assert(a==THREAD_INFO);*(struct thread_info *)c=(struct thread_info){.switches=42};return module_result(1,1);
}
int main(void){
    struct kernel_surfaces hub={0};state.hub=&hub;state.cpu=cpu_stub;state.control=control_stub;state.serial=4;
    struct process a={.info={.id=3,.thread=30,.state=PROCESS_RUNNING},.name="keyboard",.name_bytes=8,.user_pages=10};
    struct process b={.next=&a,.info={.id=1,.thread=10,.state=PROCESS_RUNNING,.stop_requested=1},.name="terminal",.name_bytes=8,.user_pages=20};
    struct process c={.next=&b,.info={.id=2,.state=PROCESS_REAPING}};
    state.head=&c;struct cb_process_query query={0};struct cb_process_sample sample;
    struct kernel_surface_result result=process_surface_sample((uintptr_t)&query,(uintptr_t)&sample,0,0,0,0);
    assert(!result.status && result.value==1 && sample.process==1 && sample.ceiling==4);
    assert(sample.user_bytes==20*4096 && sample.cpu_ns==1000 && sample.timestamp_ns==5000 && sample.cpus==4 && sample.switches==42);
    assert(sample.flags==(CB_PROCESS_CPU_VALID|CB_PROCESS_STOP_REQUESTED) && sample.name_bytes==8 && !memcmp(sample.name,"terminal",8));
    query=(struct cb_process_query){sample.process,sample.ceiling};
    struct process newer={.next=state.head,.info={.id=5,.state=PROCESS_READY}};state.head=&newer;state.serial=5;
    result=process_surface_sample((uintptr_t)&query,(uintptr_t)&sample,0,0,0,0);assert(!result.status && result.value==1 && sample.process==3);
    query.after=3;result=process_surface_sample((uintptr_t)&query,(uintptr_t)&sample,0,0,0,0);assert(!result.status && !result.value); /* ceiling excludes new launch */
    query=(struct cb_process_query){0};state.head=&a; /* previous process exited */
    result=process_surface_sample((uintptr_t)&query,(uintptr_t)&sample,0,0,0,0);assert(!result.status && result.value==1 && sample.process==3 && sample.ceiling==5);
    assert(process_surface_sample(0,(uintptr_t)&sample,0,0,0,0).status);
    puts("PASS process samples: ordered copied records, memory/runtime fields, reaping omission, bounded sweep and disappearing processes");
}
