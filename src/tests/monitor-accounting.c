#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/thread-account.h"
#include "../apps/monitor/model.h"
int main(void){
    struct thread_account a={0};
    thread_account_begin(&a,100,1);
    assert(thread_account_value(&a,140,1)==40);
    thread_account_end(&a,150,1);assert(a.total==50 && !a.active && !a.invalid);
    assert(thread_account_value(&a,10000,1)==50); /* sleeping is not CPU usage */
    thread_account_begin(&a,10000,1);thread_account_end(&a,10050,1);assert(a.total==100);
    thread_account_begin(&a,11000,1);thread_account_end(&a,10999,1);assert(a.invalid && a.total==100);
    a=(struct thread_account){.total=UINT64_MAX-5};thread_account_begin(&a,1,1);
    assert(thread_account_value(&a,9,1)==UINT64_MAX);thread_account_end(&a,9,1);assert(a.total==UINT64_MAX);
    a=(struct thread_account){0};thread_account_begin(&a,1,0);assert(a.invalid && !a.active);
    struct cb_process_sample old={.process=7,.timestamp_ns=100,.cpu_ns=10,.flags=CB_PROCESS_CPU_VALID},now=old;
    now.timestamp_ns+=1000;now.cpu_ns+=250;assert(monitor_cpu(&old,&now)==250);
    now.cpu_ns=old.cpu_ns;assert(monitor_cpu(&old,&now)==0);
    now.cpu_ns=old.cpu_ns+1000;assert(monitor_cpu(&old,&now)==1000);
    now.flags=0;assert(monitor_cpu(&old,&now)==-1);now.flags=CB_PROCESS_CPU_VALID;
    now.process=8;assert(monitor_cpu(&old,&now)==-1);now.process=7;
    now.timestamp_ns=old.timestamp_ns;assert(monitor_cpu(&old,&now)==-1);
    now.timestamp_ns=old.timestamp_ns+1;now.cpu_ns=old.cpu_ns-1;assert(monitor_cpu(&old,&now)==-1);
    old.timestamp_ns=old.cpu_ns=0;now.timestamp_ns=UINT64_MAX;now.cpu_ns=UINT64_MAX/2;
    assert(monitor_cpu(&old,&now)>=499 && monitor_cpu(&old,&now)<=500);
    now.state=CB_PROCESS_RUNNING;now.thread_state=3;assert(!strcmp(monitor_state(&now),"SLEEP"));
    now.flags|=CB_PROCESS_STOP_REQUESTED;assert(!strcmp(monitor_state(&now),"STOP"));
    assert(monitor_page(0,0,50,81)==0 && monitor_page(0,9,50,81)==0);
    assert(monitor_page(0,100,50,81)==1 && monitor_page(1,100,50,81)==2);
    assert(monitor_page(2,100,50,81)==2 && monitor_page(2,100,50,82)==1);
    assert(monitor_page(0,100,50,82)==0 && monitor_page(2,9,50,82)==0);
    assert(monitor_page(0,100,4,81)==0);
    puts("PASS process CPU accounting: live slices, blocked intervals, clock failures, saturation and CPU percentages");
}
