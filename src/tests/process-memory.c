/* Deterministic exhaustion during the actual process allocation/rollback code. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../kmodules/shared/process.c"
static int page_budget=-1,metadata_budget=-1,map_budget=-1;
static unsigned metadata_live,pages_live,mappings;
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result control_stub(MODULE_ARGS){MODULE_UNUSED();assert(a==THREAD_CURRENT);return module_result(7,1);}
static struct kernel_surface_result alloc_stub(MODULE_ARGS){MODULE_UNUSED();if(!metadata_budget)return module_result(0,0);if(metadata_budget>0)--metadata_budget;void *p=calloc(b,c);if(p)++metadata_live;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result free_stub(MODULE_ARGS){MODULE_UNUSED();assert(b && metadata_live);--metadata_live;free((void *)b);return module_result(1,1);}
static struct kernel_surface_result pages_stub(MODULE_ARGS){MODULE_UNUSED();if(!page_budget)return module_result(0,0);if(page_budget>0)--page_budget;void *p=calloc(1,4096);assert(p);++pages_live;*(struct mem_pages *)c=(struct mem_pages){.id=(uintptr_t)p,.address=(uintptr_t)p,.physical=(uintptr_t)p,.owner=b,.count=1};return module_result(1,1);}
static struct kernel_surface_result pages_free_stub(MODULE_ARGS){MODULE_UNUSED();assert(a && pages_live);--pages_live;free((void *)a);return module_result(1,1);}
static struct kernel_surface_result map_stub(MODULE_ARGS){MODULE_UNUSED();if(!map_budget)return module_result(0,0);if(map_budget>0)--map_budget;++mappings;return module_result(1,1);}
static struct kernel_surface_result unmap_stub(MODULE_ARGS){MODULE_UNUSED();assert(mappings);--mappings;return module_result(1,1);}
int main(void){
    struct {struct kernel_surfaces hub;struct kernel_surface surface;} fixture={0};fixture.hub.count=1;fixture.hub.methods_address=(uintptr_t)&fixture.surface;const char name[]="mem.space_unmap";for(size_t i=0;i<sizeof(name);++i)fixture.surface.name[i]=name[i];fixture.surface.call=unmap_stub;
    state.hub=&fixture.hub;state.cpu=cpu_stub;state.control=control_stub;state.alloc=alloc_stub;state.free=free_stub;state.pages_alloc=pages_stub;state.pages_free=pages_free_stub;state.space_map=map_stub;
    struct process p={.info={.id=1,.thread=7,.state=PROCESS_RUNNING,.address_space=1},.stack_bottom=MEM_USER_LIMIT-1048576};state.head=&p;
    struct kernel_surface_result old=process_surface_memory_alloc(1,8192,0,0,0,0);assert(!old.status && p.user_pages==2 && pages_live==2 && mappings==2);unsigned baseline=metadata_live;uint64_t next=p.memory_next;
    for(unsigned kind=0;kind<3;++kind)for(int budget=0;budget<4;++budget){
        if(kind==0)page_budget=budget;else if(kind==1)metadata_budget=budget;else map_budget=budget;
        assert(process_surface_memory_alloc(1,16384,0,0,0,0).status);page_budget=metadata_budget=map_budget=-1;
        assert(p.user_pages==2 && pages_live==2 && mappings==2 && metadata_live==baseline && p.memory_next==next && p.allocations && !p.allocations->next && p.allocations->address==old.value);
        assert(!process_surface_check(1,old.value,8192,1,0,0).status);
    }
    assert(!process_surface_memory_free(1,old.value,0,0,0,0).status && !p.user_pages && !pages_live && !metadata_live && !mappings && !p.pages && !p.allocations);
    puts("PASS process exhaustion: physical/metadata/map failures at partial allocation boundaries roll back; old memory remains valid and owned");
}
