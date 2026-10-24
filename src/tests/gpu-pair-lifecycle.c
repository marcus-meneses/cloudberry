/* Regression for provider self-exit during dependency-ordered teardown. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/handles.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static struct {const struct kernel_surfaces *hub;} state;
static void log_text(const char *s){(void)s;}
static void log_number(uint64_t n){(void)n;}
static uint64_t ticks;
static unsigned snapshots,terminated,late_terminate,waits,never_reap,retain_publication,release_failure;
static struct {uint64_t pid;unsigned published,exited,reaped,released;} providers[4];
static uint64_t now(void){return ticks;}
static int wait_tick(void){++waits;ticks+=UINT64_C(1000000000);return 1;}
static unsigned index_of(uint64_t pid){for(unsigned i=0;i<4;++i)if(providers[i].pid==pid)return i;assert(0);return 0;}
static struct kernel_surface_result inspect_model(MODULE_ARGS){
    MODULE_UNUSED();const char *names[]={"/@text","/@mouse","/@graphics","/@display"};
    for(unsigned i=0;i<4;++i)if(!strcmp((const char *)a,names[i])){
        if(!terminated)++snapshots;
        if(!providers[i].published)return module_result(0,0);
        *(struct handle_publication_info *)b=(struct handle_publication_info){.process=providers[i].pid};return module_result(1,1);
    }assert(0);return module_result(0,0);
}
static struct kernel_surface_result terminate_model(MODULE_ARGS){
    MODULE_UNUSED();unsigned i=index_of(a);assert(snapshots==4);++terminated;
    if(providers[i].exited)++late_terminate;
    providers[i].exited=1;providers[i].published=retain_publication;
    /* Last graphics client disappears: display self-exits before its stop. */
    if(i==2){providers[3].exited=1;providers[3].published=0;}
    return module_result(1,1);
}
static struct kernel_surface_result reap_model(MODULE_ARGS){
    MODULE_UNUSED();unsigned i=index_of(a);assert(providers[i].exited);
    if(never_reap || !waits)return module_result(0,0);
    providers[i].reaped=1;return module_result(1,1);
}
static struct kernel_surface_result release_model(MODULE_ARGS){
    MODULE_UNUSED();unsigned i=index_of(a);assert(providers[i].reaped);
    if(release_failure)return module_result(0,0);
    providers[i].released=1;return module_result(1,1);
}
static struct kernel_surface methods[]={
    {.name="process.terminate",.call=terminate_model},
    {.name="process.reap",.call=reap_model},
    {.name="bootstrap.release",.call=release_model}
};
#include "gpu-pair-lifecycle.inc"
static void reset(void){
    ticks=0;snapshots=terminated=late_terminate=waits=never_reap=retain_publication=release_failure=0;
    for(unsigned i=0;i<4;++i)providers[i]=(__typeof__(providers[0])){.pid=5-i,.published=1};
}
int main(void){
    struct kernel_surfaces hub={.count=3,.methods_address=(uintptr_t)methods};state.hub=&hub;
    reset();assert(pair_stop_pipeline(inspect_model) && terminated==4 && late_terminate==1 && waits==1);
    for(unsigned i=0;i<4;++i)assert(providers[i].reaped && providers[i].released);
    reset();providers[3].published=0;assert(!pair_stop_pipeline(inspect_model) && !terminated);
    reset();never_reap=1;assert(!pair_stop_pipeline(inspect_model) && waits==10 && !providers[0].released);
    reset();retain_publication=1;assert(!pair_stop_pipeline(inspect_model) && !providers[0].released);
    reset();release_failure=1;assert(!pair_stop_pipeline(inspect_model) && !providers[0].released);
    puts("PASS paired lifecycle: PID snapshot, dependent self-exit/unpublication, reap wait/timeout and fail-closed cleanup");
    return 0;
}
