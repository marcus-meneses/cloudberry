#include <assert.h>
#include <stdio.h>
#include "../kmodules/shared/bootstrap-apps.c"
static struct process_info observed={.id=11,.state=PROCESS_RUNNING};
static int present=1,published=1;
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result process_stub(MODULE_ARGS){MODULE_UNUSED();if(!present)return module_result(0,0);*(struct process_info *)b=observed;return module_result(1,1);}
static struct kernel_surface_result inspect_stub(MODULE_ARGS){MODULE_UNUSED();if(!published)return module_result(0,0);*(struct handle_publication_info *)b=(struct handle_publication_info){.process=11};return module_result(1,1);}
int main(void){
    struct kernel_surfaces hub={0};apps.hub=&hub;apps.cpu=cpu_stub;apps.process_info=process_stub;apps.inspect=inspect_stub;apps.info.keyboard=11;apps.info.state=2;
    struct bootstrap_apps_info out;assert(!bootstrap_surface_apps_info((uintptr_t)&out,0,0,0,0,0).status && out.state==3);
    observed.state=PROCESS_FAULTED;assert(!bootstrap_surface_apps_info((uintptr_t)&out,0,0,0,0,0).status && out.state==4 && out.error==2);
    observed.state=PROCESS_RUNNING;observed.stop_requested=1;assert(!bootstrap_surface_apps_info((uintptr_t)&out,0,0,0,0,0).status && out.state==4);
    observed.stop_requested=0;published=0;apps.info.state=3;assert(!bootstrap_surface_apps_info((uintptr_t)&out,0,0,0,0,0).status && out.state==4 && out.error==3);
    published=1;assert(!bootstrap_surface_apps_info((uintptr_t)&out,0,0,0,0,0).status && out.state==3);
    present=0;assert(!bootstrap_surface_apps_info((uintptr_t)&out,0,0,0,0,0).status && out.state==4);
    puts("PASS bootstrap application status: publication, fault, stop, withdrawal and reaped owner");return 0;
}
