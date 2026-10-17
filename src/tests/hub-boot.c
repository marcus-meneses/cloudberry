#include "../kernel/modules.h"
#include "../kernel/cpu.h"
#include "../kmodules/shared/threads.h"
#include "../kmodules/shared/scheduler.h"
#include "../kmodules/shared/irq.h"
#include "../kmodules/shared/smp.h"
#include "probe.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void print(const char *p){while(*p)__asm__ volatile("outb %0,$0xe9"::"a"(*p++));}
static bool failure(unsigned line){print("FAIL hub-boot.c line ");char d[12];unsigned n=0;do{d[n++]='0'+line%10;line/=10;}while(line);while(n)__asm__ volatile("outb %0,$0xe9"::"a"(d[--n]));print("\n");return false;}
#define CHECK(x) do{if(!(x))return failure(__LINE__);}while(0)
static void name(char *out,const char *prefix,unsigned value){while(*prefix)*out++=*prefix++;char digits[12];unsigned n=0;do{digits[n++]='0'+value%10;value/=10;}while(value);while(n)*out++=digits[--n];*out=0;}
static struct kmodule_image *fixture(struct boot_pages *pages,const char *module,const char *prefix,unsigned exports,kernel_surface_call jump){
    size_t code=sizeof(struct kmodule_image)+exports*sizeof(struct kmodule_export),bytes=(code+32+4095)&~(size_t)4095;
    uint64_t flags=kernel_cpu_enter(),physical=boot_pages_claim(pages,bytes/4096,BOOT_RES_OTHER,0);kernel_cpu_leave(flags);if(!physical)return NULL;boot_pages_zero(pages,physical,bytes/4096);
    struct kmodule_image *h=boot_physical_pointer(pages,physical);*h=(struct kmodule_image){.magic=KMODULE_MAGIC,.format=KMODULE_FORMAT,.header_bytes=sizeof(*h),.image_bytes=bytes,.memory_bytes=bytes+4096,.writable_offset=bytes,.exports_offset=sizeof(*h),.exports_count=exports,.export_bytes=sizeof(struct kmodule_export)};
    for(unsigned i=0;module[i];++i)h->name[i]=module[i];
    struct kmodule_export *entries=(void *)(h+1);for(unsigned i=0;i<exports;++i){name(entries[i].name,prefix,i);entries[i].major=1;entries[i].offset=code+(i && jump?16:0);}
    unsigned char *p=(void *)((uintptr_t)h+code);const unsigned char add[]={0x48,0x89,0xf8,0x48,0x83,0xc0,5,0x31,0xd2,0xc3};
    if(jump){p[0]=0x48;p[1]=0xb8;for(unsigned i=0;i<8;++i)p[2+i]=(uintptr_t)jump>>(i*8);p[10]=0xff;p[11]=0xe0;p+=16;}
    for(unsigned i=0;i<sizeof(add);++i){p[i]=add[i];}return h;
}
static bool permissions(struct boot_pages *pages,uintptr_t address,uint64_t wanted){uint64_t cr3;__asm__ volatile("mov %%cr3,%0":"=r"(cr3));uint64_t *table=boot_physical_pointer(pages,cr3&UINT64_C(0x000ffffffffff000));for(unsigned shift=39;shift>12;shift-=9){uint64_t p=table[(address>>shift)&511];if(!(p&1) || (p&128))return false;table=boot_physical_pointer(pages,p&UINT64_C(0x000ffffffffff000));}return (table[(address>>12)&511]&UINT64_C(0x8000000000000007))==wanted;}
static struct kernel_hub_table tables[200];static uint64_t payload=123;
bool kernel_hub_test(const struct kernel_surfaces *hub,struct boot_pages *pages){
    __asm__ volatile("sti":::"memory");
    const struct kernel_surface *stable_methods=kernel_surface_entries(hub);const struct kernel_hub_table *stable_config=kernel_hub_find_table(hub,"config.version");kernel_surface_call clock=kernel_surface_find(hub,"clock.now_ns"),publish=kernel_surface_find(hub,"hub.publish_tables"),control=kernel_surface_find(hub,"hub.owner_control"),inspect=kernel_surface_find(hub,"hub.owner_info"),pin=kernel_surface_find(hub,"hub.owner_pin");CHECK(stable_config && clock && publish && control && inspect && pin);
    /* A sparse BSS-heavy module crosses the former aggregate 16 MiB bound. */
    struct kmodule_image *large=fixture(pages,"arena-large","arena-large.m",1,NULL);CHECK(large);
    large->memory_bytes=KERNEL_MODULES_BYTES+4096;
    CHECK(!kernel_module_load_owned(pages,large,large->image_bytes,"arena-large",0));
    large->memory_bytes=20*1024*1024;
    CHECK(kernel_module_load_owned(pages,large,large->image_bytes,"arena-large",0));
    struct kernel_hub_owner extent;uint64_t extent_id=kernel_surface_owner(hub,"arena-large.m0");
    CHECK(!CALL(inspect,extent_id,&extent,0).status && extent.bytes==20*1024*1024 && extent.image>=KERNEL_MODULES_ADDRESS && extent.image+extent.bytes<KERNEL_MODULES_LIMIT);
    CHECK(permissions(pages,extent.image,1) && permissions(pages,extent.image+large->writable_offset,UINT64_C(0x8000000000000003)) && !permissions(pages,extent.image+extent.bytes,1));
    CHECK(CALL(kernel_surface_find(hub,"arena-large.m0"),37,0,0).value==42);
    struct kmodule_image *oom=fixture(pages,"arena-oom","arena-oom.m",1,NULL);CHECK(oom);
    oom->memory_bytes=600*1024*1024;uint32_t unchanged=kernel_surface_count(hub);
    CHECK(!kernel_module_load_owned(pages,oom,oom->image_bytes,"arena-oom",0) && kernel_surface_count(hub)==unchanged && !kernel_surface_find(hub,"arena-oom.m0"));
    uint32_t before=kernel_surface_count(hub);struct kmodule_image *h=fixture(pages,"hub-grow","hub-grow.m",600,NULL);CHECK(h);h->format=2;uint64_t reservations=pages->reservations.count;
    CHECK(!kernel_module_load_owned(pages,h,h->image_bytes,"hub-grow",0) && kernel_surface_count(hub)==before && pages->reservations.count==reservations);h->format=KMODULE_FORMAT;
    CHECK(kernel_module_load_owned(pages,h,h->image_bytes,"hub-grow",0) && kernel_surface_count(hub)==before+600);
    kernel_surface_call last=kernel_surface_find(hub,"hub-grow.m599");CHECK(last && CALL(last,37,0,0).value==42);uint64_t grow_owner=kernel_surface_owner(hub,"hub-grow.m0");CHECK(grow_owner);
    for(unsigned i=0;i<200;++i){name(tables[i].name,"hub-grow.t",i);tables[i].address=(uintptr_t)&payload;tables[i].bytes=8;tables[i].count=1;tables[i].entry_bytes=8;tables[i].owner=grow_owner;}
    CHECK(!CALL(publish,tables,200,0).status && stable_methods==kernel_surface_entries(hub) && stable_config==kernel_hub_find_table(hub,"config.version") && !CALL(clock,0,0,0).status);
    CHECK(permissions(pages,KERNEL_METHODS_ADDRESS,UINT64_C(0x8000000000000001)) && permissions(pages,KERNEL_TABLES_ADDRESS,UINT64_C(0x8000000000000001)) && permissions(pages,KERNEL_OWNERS_ADDRESS,UINT64_C(0x8000000000000001)) && permissions(pages,(uintptr_t)last,1));
    CHECK(CALL(control,grow_owner,HUB_OWNER_DRAIN,0).status && CALL(control,0,HUB_OWNER_DRAIN,0).status);
    struct kernel_hub_owner owner;CHECK(!CALL(inspect,grow_owner,&owner,0).status && owner.tables==200 && owner.methods==600);
    kernel_surface_call create=kernel_surface_find(hub,"threads.create"),thread=kernel_surface_find(hub,"threads.control"),step=kernel_surface_find(hub,"event.step"),probe=kernel_surface_find(hub,"probe.thread");CHECK(create && thread && step && probe);
    struct smp_info topology;CHECK(!CALL(kernel_surface_find(hub,"smp.info"),&topology,0,0).status);
    if(topology.count>1){
        struct thread_request remote={.abi=1,.cpu=1,.stack_bytes=16384,.entry=(uintptr_t)last,.major=1};remote.payload.arguments[0]=37;
        struct kernel_surface_result launched=CALL(create,&remote,0,0);CHECK(!launched.status);
        uint64_t until=CALL(clock,0,0,0).value+UINT64_C(3000000000);struct thread_info info;
        do{CHECK(!CALL(thread,THREAD_INFO,launched.value,&info).status && CALL(clock,0,0,0).value<until);if(info.state==THREAD_EXITED)break;__asm__ volatile("pause");}while(1);
        CHECK(!info.result.status && info.result.value==42 && !CALL(thread,THREAD_REAP,launched.value,0).status);
    }
    h=fixture(pages,"hub-live","hub-live.m",2,probe);CHECK(h && kernel_module_load_owned(pages,h,h->image_bytes,"hub-live",0));
    uint64_t live=kernel_surface_owner(hub,"hub-live.m0");kernel_surface_call stale=kernel_surface_find(hub,"hub-live.m1"),work=kernel_surface_find(hub,"hub-live.m0");CHECK(live && stale && work);
    struct probe_thread job={.mode=2};struct thread_request request={.abi=1,.cpu=0,.stack_bytes=16384,.entry=(uintptr_t)work,.major=1};request.payload.arguments[0]=(uintptr_t)&job;
    struct kernel_surface_result launched=CALL(create,&request,0,0);CHECK(!launched.status);uint64_t deadline=CALL(clock,0,0,0).value+UINT64_C(3000000000);struct thread_info ti;
    do{CHECK(!CALL(thread,THREAD_INFO,launched.value,&ti).status && CALL(clock,0,0,0).value<deadline);if(ti.state==THREAD_BLOCKED)break;CHECK(!CALL(step,1,0,0).status);}while(1);
    CHECK(!CALL(inspect,live,&owner,0).status && owner.active==1 && owner.pins==1);
    CHECK(!CALL(control,live,HUB_OWNER_DRAIN,0).status && !kernel_surface_find(hub,"hub-live.m1") && CALL(pin,live,1,0).status && CALL(control,live,HUB_OWNER_RETIRE,0).status);
    CHECK(CALL(stale,37,0,0).value==42 && !CALL(thread,THREAD_WAKE,launched.value,0).status);
    do{CHECK(!CALL(thread,THREAD_INFO,launched.value,&ti).status && CALL(clock,0,0,0).value<deadline);if(ti.state==THREAD_EXITED)break;CHECK(!CALL(step,1,0,0).status);}while(1);
    CHECK(job.done && !job.errors && !CALL(inspect,live,&owner,0).status && !owner.active && owner.pins==1 && CALL(control,live,HUB_OWNER_RETIRE,0).status);
    CHECK(!CALL(thread,THREAD_REAP,launched.value,0).status && !CALL(control,live,HUB_OWNER_RETIRE,0).status && CALL(stale,1,0,0).status);
    /* Queue leases remain until result reclamation, including a draining owner. */
    h=fixture(pages,"hub-queued","hub-queued.m",1,NULL);CHECK(h && kernel_module_load_owned(pages,h,h->image_bytes,"hub-queued",0));
    uint64_t queued=kernel_surface_owner(hub,"hub-queued.m0");work=kernel_surface_find(hub,"hub-queued.m0");kernel_surface_call submit=kernel_surface_find(hub,"scheduler.submit"),run=kernel_surface_find(hub,"scheduler.run"),result=kernel_surface_find(hub,"scheduler.result");CHECK(submit && run && result);
    struct scheduler_request r={.abi=SCHEDULER_REQUEST_ABI,.kind=SCHEDULER_METHOD,.entry_vector=(uintptr_t)work,.entry_major=1};r.payload.arguments[0]=37;struct kernel_surface_result task=CALL(submit,&r,0,0);CHECK(!task.status);
    CHECK(!CALL(control,queued,HUB_OWNER_DRAIN,0).status && CALL(submit,&r,0,0).status && CALL(control,queued,HUB_OWNER_RETIRE,0).status && !CALL(run,1,0,0).status);
    struct scheduler_completion completion;CHECK(!CALL(result,task.value,&completion,1).status && completion.result.value==42 && !CALL(control,queued,HUB_OWNER_RETIRE,0).status && CALL(work,1,0,0).status);
    /* Deferred IRQ routes lease their callback until masked, quiet release. */
    h=fixture(pages,"hub-irq","hub-irq.m",1,NULL);CHECK(h && kernel_module_load_owned(pages,h,h->image_bytes,"hub-irq",0));uint64_t irq_owner=kernel_surface_owner(hub,"hub-irq.m0");work=kernel_surface_find(hub,"hub-irq.m0");
    struct irq_request irq_request={.kind=IRQ_MSI,.owner=1234,.callback=work,.major=1};struct irq_route route;kernel_surface_call route_call=kernel_surface_find(hub,"irq.route"),release=kernel_surface_find(hub,"irq.release");CHECK(route_call && release && !CALL(route_call,&irq_request,&route,0).status);
    CHECK(!CALL(control,irq_owner,HUB_OWNER_DRAIN,0).status && CALL(control,irq_owner,HUB_OWNER_RETIRE,0).status && !CALL(release,route.id,1234,1).status && !CALL(control,irq_owner,HUB_OWNER_RETIRE,0).status);
    print("PASS dynamic hub: 600 late methods, 200 owned tables, stable RO/RX records/stubs, blocked-call drain, queued/thread/IRQ leases, retirement and stale rejection\n");return true;
}
