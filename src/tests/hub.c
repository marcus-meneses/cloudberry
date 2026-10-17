#define _GNU_SOURCE
#define KERNEL_HUB_ARRAYS_ADDRESS UINT64_C(0x20000000000)
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../kernel/modules.c"
static int backing,fail_claim,fail_alias;static uint64_t cursor=4096;static unsigned claims;
static struct kernel_surfaces header;static struct boot_pages pages;
static _Atomic int entered,finish_call,stop_readers;
uint64_t kernel_cpu_enter(void){return 0;}
uint64_t kernel_cpu_enter_tag(uint64_t tag){(void)tag;return 0;}
uint64_t kernel_cpu_enter_domain(unsigned domain,uint64_t tag){(void)domain;(void)tag;return 0;}
void kernel_cpu_leave(uint64_t f){(void)f;}
void kernel_cpu_leave_domain(unsigned domain,uint64_t f){(void)domain;(void)f;}
void kernel_cpu_flush(void){}
uint64_t boot_pages_claim(struct boot_pages *p,size_t n,uint32_t kind,uintptr_t owner){(void)p;(void)kind;(void)owner;assert(n==1);if(fail_claim)return 0;uint64_t out=cursor;cursor+=4096;++claims;return out;}
void boot_pages_zero(struct boot_pages *p,uint64_t physical,size_t n){(void)p;void *at=mmap(NULL,n*4096,PROT_READ|PROT_WRITE,MAP_SHARED,backing,physical);assert(at!=MAP_FAILED);memset(at,0,n*4096);munmap(at,n*4096);}
bool boot_map_page(struct boot_pages *p,uintptr_t at,uint64_t physical,bool write,bool exec){(void)p;if(write && fail_alias){fail_alias=0;return false;}void *out=mmap((void *)at,4096,PROT_READ|(write?PROT_WRITE:0)|(exec?PROT_EXEC:0),MAP_SHARED|MAP_FIXED_NOREPLACE,backing,physical);return out!=(void *)-1;}
static struct kernel_surface_result add(MODULE_ARGS){MODULE_UNUSED();return module_result(a+5,1);}
static struct kernel_surface_result hold(MODULE_ARGS){MODULE_UNUSED();atomic_store(&entered,1);while(!atomic_load(&finish_call))sched_yield();return module_result(77,1);}
static kernel_surface_call held;
static void *caller(void *unused){(void)unused;struct kernel_surface_result r=held(0,0,0,0,0,0);assert(!r.status && r.value==77);return NULL;}
static void *reader(void *unused){(void)unused;while(!atomic_load(&stop_readers)){uint32_t count=kernel_surface_count(&header);const struct kernel_surface *entries=kernel_surface_entries(&header);for(uint32_t i=0;i<count;++i){assert(entries[i].call && entries[i].major && entries[i].name[0]);}uint32_t n=kernel_hub_table_count(&header);const struct kernel_hub_table *tables=kernel_hub_tables(&header);for(uint32_t i=0;i<n;++i)assert(tables[i].address && tables[i].bytes);}return NULL;}
int main(void){
    backing=memfd_create("hub-tests",0);assert(backing>=0 && !ftruncate(backing,64*1024*1024));
    hub_writable=&header;hub_pages=&pages;header=(struct kernel_surfaces){.magic=KERNEL_SURFACES_MAGIC,.major=KERNEL_HUB_ABI,.methods_address=KERNEL_METHODS_ADDRESS,.tables_address=KERNEL_TABLES_ADDRESS,.owners_address=KERNEL_OWNERS_ADDRESS};
    assert(grow(&owner_range,4*sizeof(struct kernel_hub_owner)));struct kernel_hub_owner *owners=writable(&owner_range);
    for(unsigned i=0;i<4;++i){owners[i]=(struct kernel_hub_owner){.id=i,.state=HUB_OWNER_LIVE,.flags=i?0:HUB_OWNER_BOOT_PINNED};}header.owner_count=4;
    pthread_t readers[3];for(unsigned i=0;i<3;++i)assert(!pthread_create(&readers[i],NULL,reader,NULL));
    for(unsigned round=0;round<6;++round){unsigned count=100*(round+1);assert(ensure_methods(count));for(unsigned i=header.count;i<count;++i){char name[32];snprintf(name,sizeof(name),"growth.method%u",i);write_surface(i,name,add,1,0,1,2);}__atomic_store_n(&header.count,count,__ATOMIC_RELEASE);}
    const struct kernel_surface *first=kernel_surface_entries(&header);kernel_surface_call stable=first[0].call;assert(!stable(37,0,0,0,0,0).status && stable(37,0,0,0,0,0).value==42);
    static uint64_t payload=123;struct kernel_hub_table batch[200];memset(batch,0,sizeof(batch));for(unsigned i=0;i<200;++i){snprintf(batch[i].name,sizeof(batch[i].name),"growth.table%u",i);batch[i].address=(uintptr_t)&payload;batch[i].bytes=8;batch[i].count=1;batch[i].entry_bytes=8;batch[i].owner=2;}
    fail_claim=1;uint32_t original=header.table_count;assert(publish_tables((uintptr_t)batch,200,0,0,0,0).status && header.table_count==original);fail_claim=0;
    fail_alias=1;assert(publish_tables((uintptr_t)batch,200,0,0,0,0).status && header.table_count==original);
    assert(!publish_tables((uintptr_t)batch,200,0,0,0,0).status);const struct kernel_hub_table *table=kernel_hub_find_table(&header,"growth.table0");assert(table && table->owner==2 && owners[2].tables==200);
    unsigned before=claims;batch[1]=batch[0];assert(publish_tables((uintptr_t)batch,2,0,0,0,0).status && claims==before && header.table_count==200);
    assert(first==kernel_surface_entries(&header) && stable==first[0].call && table==kernel_hub_find_table(&header,"growth.table0"));
    assert(owner_control(0,HUB_OWNER_DRAIN,0,0,0,0).status && owner_control(2,HUB_OWNER_DRAIN,0,0,0,0).status);
    assert(ensure_methods(602));write_surface(600,"drain.hold",hold,1,0,1,1);write_surface(601,"drain.add",add,1,0,1,1);__atomic_store_n(&header.count,602,__ATOMIC_RELEASE);held=kernel_surface_find(&header,"drain.hold");kernel_surface_call stale=kernel_surface_find(&header,"drain.add");assert(held && stale);
    pthread_t worker;assert(!pthread_create(&worker,NULL,caller,NULL));while(!atomic_load(&entered))sched_yield();assert(owners[1].active==1);
    assert(!owner_control(1,HUB_OWNER_DRAIN,0,0,0,0).status && !kernel_surface_find(&header,"drain.add") && owner_pin(1,1,0,0,0,0).status && owner_control(1,HUB_OWNER_RETIRE,0,0,0,0).status);
    assert(!stale(1,0,0,0,0,0).status);atomic_store(&finish_call,1);pthread_join(worker,NULL);assert(!owners[1].active && stale(1,0,0,0,0,0).status);
    assert(!owner_control(1,HUB_OWNER_RESUME,0,0,0,0).status && !owner_pin(1,1,0,0,0,0).status && !owner_control(1,HUB_OWNER_DRAIN,0,0,0,0).status);
    assert(!stale(1,0,0,0,0,0).status && owner_control(1,HUB_OWNER_RETIRE,0,0,0,0).status);
    assert(!owner_pin(1,0,0,0,0,0).status && !owner_control(1,HUB_OWNER_RETIRE,0,0,0,0).status && stale(1,0,0,0,0,0).status && owner_control(1,HUB_OWNER_RESUME,0,0,0,0).status);
    atomic_store(&stop_readers,1);for(unsigned i=0;i<3;++i)pthread_join(readers[i],NULL);
    printf("PASS dynamic hub: %u methods, %u tables, stable RO/RX aliases, OOM/map-failure retry, atomic publication, concurrent readers, active/leased drain, retirement and stale calls (%u physical pages)\n",header.count,header.table_count,claims);close(backing);return 0;
}
