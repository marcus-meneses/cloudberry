#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kmodules/shared/bootstrap-apps.c"
static struct process_info observed={.id=11,.state=PROCESS_RUNNING};
static int present=1,published=1;
static uint64_t test_time,publish_at;static unsigned waits;
static struct kernel_surface_result clock_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(test_time,1);}
static uint64_t drivers_finish_at;static unsigned drivers_error,drivers_snapshot_error;
static struct kernel_surface_result drivers_stub(MODULE_ARGS){MODULE_UNUSED();*(struct driver_table *)a=(struct driver_table){.started=1,.finished=test_time>=drivers_finish_at,.error=drivers_error};return module_result(1,!drivers_snapshot_error);}
static struct kernel_surface_result thread_stub(MODULE_ARGS){MODULE_UNUSED();assert(a==THREAD_BLOCK_UNTIL);assert(b>test_time);++waits;test_time+=UINT64_C(10000000000);if(publish_at && test_time>=publish_at)published=1;return module_result(1,1);}
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result process_stub(MODULE_ARGS){MODULE_UNUSED();if(!present)return module_result(0,0);*(struct process_info *)b=observed;return module_result(1,1);}
static struct kernel_surface_result inspect_stub(MODULE_ARGS){MODULE_UNUSED();if(!published)return module_result(0,0);*(struct handle_publication_info *)b=(struct handle_publication_info){.process=11};return module_result(1,1);}
static long blocks,budget=-1;
static struct kernel_surface_result allocate_stub(MODULE_ARGS){MODULE_UNUSED();if(!budget)return module_result(0,0);if(budget>0)--budget;void *p=calloc(b,c);if(p)++blocks;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result free_stub(MODULE_ARGS){MODULE_UNUSED();assert(blocks>0);--blocks;free((void *)b);return module_result(1,1);}
static struct kernel_surface_result stat_stub(MODULE_ARGS){MODULE_UNUSED();*(struct ext4_stat *)c=(struct ext4_stat){.mode=0100644,.bytes=3};return module_result(1,1);}
static struct kernel_surface_result read_stub(MODULE_ARGS){MODULE_UNUSED();assert(e==3);memcpy((void *)d,"abc",3);return module_result(3,1);}
struct kernel_surface_result bootstrap_surface_info(MODULE_ARGS){MODULE_UNUSED();return module_result(0,0);}
static unsigned reap_fail,reap_calls;
static struct kernel_surface_result reap_stub(MODULE_ARGS){MODULE_UNUSED();++reap_calls;return module_result(1,!reap_fail);}
int main(void){
    struct kernel_surfaces hub={0};apps.hub=&hub;apps.cpu=cpu_stub;apps.process_info=process_stub;apps.inspect=inspect_stub;struct application record={.process=11,.state=2,.bundle="keyboard",.ready="/@keyboard"};apps.first=&record;apps.info.count=1;apps.info.state=2;
    struct bootstrap_apps_info out;assert(!bootstrap_surface_apps_info((uintptr_t)&out,0,0,0,0,0).status && out.state==3);
    observed.state=PROCESS_FAULTED;assert(!bootstrap_surface_apps_info((uintptr_t)&out,0,0,0,0,0).status && out.state==4 && out.error==2);
    observed.state=PROCESS_RUNNING;observed.stop_requested=1;assert(!bootstrap_surface_apps_info((uintptr_t)&out,0,0,0,0,0).status && out.state==4);
    observed.stop_requested=0;published=0;record.state=3;assert(!bootstrap_surface_apps_info((uintptr_t)&out,0,0,0,0,0).status && out.state==4 && out.error==3);
    published=1;assert(!bootstrap_surface_apps_info((uintptr_t)&out,0,0,0,0,0).status && out.state==3);
    present=0;assert(!bootstrap_surface_apps_info((uintptr_t)&out,0,0,0,0,0).status && out.state==4);
    present=1;published=1;struct bootstrap_application_info snapshot;assert(!bootstrap_surface_application_next(0,(uintptr_t)&snapshot,0,0,0,0).status && snapshot.process==11 && snapshot.state==3);assert(bootstrap_surface_application_next(11,(uintptr_t)&snapshot,0,0,0,0).status);
    apps.clock=clock_stub;apps.thread=thread_stub;
    /* Storage/ELF loading has already taken longer than the old whole-boot
     * budget. Each live provider still gets its complete readiness window. */
    const char *phase="publication readiness";test_time=UINT64_C(40000000000);publish_at=test_time+UINT64_C(20000000000);published=0;record.state=2;
    assert(wait_provider(&phase) && waits==2 && test_time==UINT64_C(60000000000));
    publish_at=test_time+UINT64_C(20000000000);published=0;record.state=2;
    assert(wait_provider(&phase) && waits==4 && test_time==UINT64_C(80000000000));
    publish_at=0;published=0;record.state=2;assert(!wait_provider(&phase) && !strcmp(phase,"publication timeout") && waits==7);
    test_time=UINT64_MAX;assert(!wait_provider(&phase) && !strcmp(phase,"readiness clock"));published=1;
    /* A ready volume does not mean selected disk modules are initialized.
     * Wait without holding apps.operation; failure/timeout never admits apps. */
    test_time=0;waits=0;drivers_finish_at=UINT64_C(20000000000);
    assert(wait_drivers(drivers_stub,&phase) && waits==2 && !apps.operation);
    drivers_error=DRIVER_LOAD;assert(!wait_drivers(drivers_stub,&phase) && !strcmp(phase,"selected driver failure"));drivers_error=0;
    drivers_snapshot_error=1;assert(!wait_drivers(drivers_stub,&phase));drivers_snapshot_error=0;
    test_time=0;drivers_finish_at=UINT64_MAX;waits=0;assert(!wait_drivers(drivers_stub,&phase) && waits==3);
    assert(!wait_drivers(NULL,&phase));test_time=UINT64_MAX;assert(!wait_drivers(drivers_stub,&phase));
    apps.alloc=allocate_stub;apps.free=free_stub;apps.stat=stat_stub;apps.read=read_stub;apps.heap=1;
    for(long fail=0;fail<7;++fail){budget=fail;char files[]="one.txt,two.txt";size_t bytes=0;assert(!startup(1,"bundle",files,&bytes) && !blocks);}
    budget=-1;char files[]="one.txt,two.txt";size_t bytes=0;void *packet=startup(1,"bundle",files,&bytes);const void *data;size_t n;assert(packet && cb_startup_find(packet,bytes,"two.txt",&data,&n) && n==3 && !memcmp(data,"abc",3));release(packet);assert(!blocks);
    char duplicate[]="one.txt,one.txt";assert(!startup(1,"bundle",duplicate,&bytes) && !blocks);char escape[]="../other";assert(!startup(1,"bundle",escape,&bytes) && !blocks);
    /* Failed accelerator processes must be fully reaped before fallback can
     * reuse names/roles. Uncertain cleanup retains the old record and fails. */
    apps.reap=reap_stub;apps.info.count=2;apps.first=allocate(sizeof(*apps.first));
    apps.first->process=12;apps.first->bundle=string("graphics-opengl");apps.first->ready=string("/@graphics");apps.first->next=&record;
    observed.state=PROCESS_EXITED;published=0;test_time=0;reap_fail=1;
    assert(!render_discard(&record)&&apps.first!=&record&&apps.info.count==2);
    reap_fail=0;assert(render_discard(&record)&&apps.first==&record&&apps.info.count==1&&apps.info.state==2&&!blocks&&reap_calls>=2);
    puts("PASS bootstrap application status: selected-driver ordering, publication, fault, stop, withdrawal and reaped owner");return 0;
}
