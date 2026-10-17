#include "../kernel/modules.h"
#include "../kernel/cpu.h"
#include "../kmodules/shared/mem-spaces.h"
#include "../kmodules/shared/mem-pages.h"
#include "../kmodules/shared/threads.h"
#include "probe.h"
#include "space-fault.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void print(const char *p){while(*p)__asm__ volatile("outb %0,$0xe9"::"a"(*p++));}
static bool failure(unsigned line){print("FAIL spaces-boot line ");char d[12];unsigned n=0;do{d[n++]='0'+line%10;line/=10;}while(line);while(n)__asm__ volatile("outb %0,$0xe9"::"a"(d[--n]));print("\n");return false;}
#define CHECK(x) do{if(!(x))return failure(__LINE__);}while(0)
static bool cycle(const struct kernel_surfaces *hub,struct boot_pages *pages){
    kernel_surface_call create=kernel_surface_find(hub,"mem.space_create"),map=kernel_surface_find(hub,"mem.space_map"),unmap=kernel_surface_find(hub,"mem.space_unmap"),protect=kernel_surface_find(hub,"mem.space_protect"),query=kernel_surface_find(hub,"mem.space_query"),info=kernel_surface_find(hub,"mem.space_info"),destroy=kernel_surface_find(hub,"mem.space_destroy");
    kernel_surface_call alloc=kernel_surface_find(hub,"mem.pages_alloc"),free=kernel_surface_find(hub,"mem.pages_free"),thread=kernel_surface_find(hub,"threads.create"),control=kernel_surface_find(hub,"threads.control"),step=kernel_surface_find(hub,"event.step"),clock=kernel_surface_find(hub,"clock.now_ns"),entry=kernel_surface_find(hub,"probe.space");
    CHECK(create && map && unmap && protect && query && info && destroy && entry);
    struct mem_space_info a,b,snapshot;struct mem_pages first,second,replacement,code_a,code_b,hole_a,hole_b,canary;
    CHECK(!CALL(create,101,&a,0).status && !CALL(create,102,&b,0).status && a.id!=b.id);
    CHECK(!CALL(alloc,1,101,&first).status && !CALL(alloc,1,102,&second).status && !CALL(alloc,1,101,&replacement).status);
    *(uint64_t *)first.address=0x1111;*(uint64_t *)second.address=0x2222;*(uint64_t *)replacement.address=0x3333;
    CHECK(!CALL(alloc,1,101,&code_a).status && !CALL(alloc,1,102,&code_b).status && !CALL(alloc,1,101,&hole_a).status && !CALL(alloc,1,102,&hole_b).status);
    const unsigned char code[]={0xb8,42,0,0,0,0xc3};for(unsigned i=0;i<sizeof(code);++i){((unsigned char *)code_a.address)[i]=code[i];((unsigned char *)code_b.address)[i]=code[i];}
    *(uint64_t *)hole_a.address=0x7777;*(uint64_t *)hole_b.address=0x7777;
    const uint64_t va=0x400000;
    CHECK(!map(a.id,101,va+4096,code_a.physical,0,0).status && !map(b.id,102,va+4096,code_b.physical,0,0).status && !map(a.id,101,va+8192,hole_a.physical,0,0).status && !map(b.id,102,va+8192,hole_b.physical,0,0).status);
    CHECK(!map(a.id,101,va,first.physical,MEM_SPACE_WRITE,0).status && !map(b.id,102,va,second.physical,MEM_SPACE_WRITE,0).status);
    CHECK(map(a.id,102,va+4096,second.physical,1,0).status && map(a.id,101,0,first.physical,1,0).status && map(a.id,101,va+4096,first.physical,3,0).status && map(a.id,101,KERNEL_SURFACES_ADDRESS,first.physical,1,0).status);
    struct mem_space_mapping mapping;CHECK(!query(a.id,101,va,(uintptr_t)&mapping,0,0).status && mapping.physical==first.physical && mapping.flags==1);
    CHECK(CALL(free,first.id,101,0).status && CALL(destroy,a.id,102,0).status);
    uint64_t *at=boot_physical_pointer(pages,a.root),*bt=boot_physical_pointer(pages,b.root),*master=boot_physical_pointer(pages,pages->kernel_root);
    for(unsigned i=256;i<512;++i)CHECK(at[i]==master[i] && bt[i]==master[i] && !(at[i]&4));
    const struct kernel_hub_table *cpus=kernel_hub_find_table(hub,"smp.cpus");CHECK(cpus);
    unsigned count=cpus->count>1?3:2;uint64_t ids[3];struct probe_space probes[3]={{.root=a.root,.address=va,.expected=0x1111,.next=0x3333},{.root=b.root,.address=va,.expected=0x2222,.next=0x2222},{.root=a.root,.address=va,.expected=0x1111,.next=0x3333,.hold_preemption=1}};
    for(unsigned i=0;i<count;++i){struct thread_request r={.abi=2,.cpu=i==2?1:0,.process_id=i==1?102:101,.address_space=i==1?b.id:a.id,.stack_bytes=32768,.entry=(uintptr_t)entry,.major=1};r.payload.arguments[0]=(uintptr_t)&probes[i];struct kernel_surface_result created=CALL(thread,&r,0,0);CHECK(!created.status);ids[i]=created.value;}
    struct thread_request wrong={.abi=2,.cpu=0,.process_id=102,.address_space=a.id,.stack_bytes=32768,.entry=(uintptr_t)entry,.major=1};CHECK(CALL(thread,&wrong,0,0).status);
    __asm__ volatile("sti":::"memory");CHECK(CALL(kernel_surface_find(hub,"mem.space_switch"),a.id,101,0).status);
    uint64_t deadline=CALL(clock,0,0,0).value+UINT64_C(15000000000);
    for(;;){unsigned ready=0;for(unsigned i=0;i<count;++i)ready+=__atomic_load_n(&probes[i].phase,__ATOMIC_ACQUIRE)==1;if(ready==count)break;CHECK(!CALL(step,1,0,0).status && CALL(clock,0,0,0).value<deadline);}
    CHECK(kernel_cpu_space()==0);uintptr_t cr3;__asm__ volatile("mov %%cr3,%0":"=r"(cr3));CHECK(cr3==pages->kernel_root);
    CHECK(!CALL(info,a.id,101,&snapshot).status && snapshot.leases==count-1 && snapshot.active<=count-1);
    struct cpu_record *cpu=(void *)cpus->address;
    if(count==3){
        /* Freeze root transitions under the same lock as map/protect. Sampling
         * active counters without this can catch a legitimate timer transition. */
        uint64_t held;
        for(;;){held=kernel_cpu_enter();if(__atomic_load_n(&cpu[1].address_space,__ATOMIC_ACQUIRE)==a.id)break;kernel_cpu_leave(held);CHECK(!CALL(step,1,0,0).status && CALL(clock,0,0,0).value<deadline);}
        uint64_t ack=__atomic_load_n(&cpu[1].tlb_ack,__ATOMIC_ACQUIRE);
        CHECK(!protect(a.id,101,va,0,0,0).status && __atomic_load_n(&cpu[1].tlb_ack,__ATOMIC_ACQUIRE)>ack);kernel_cpu_leave(held);
    }else CHECK(!protect(a.id,101,va,0,0,0).status);
    CHECK(!query(a.id,101,va,(uintptr_t)&mapping,0,0).status && !mapping.flags && CALL(destroy,a.id,101,0).status);
    CHECK(!protect(b.id,102,va,0,0,0).status && !protect(a.id,101,va+4096,MEM_SPACE_EXEC,0,0).status && !protect(b.id,102,va+4096,MEM_SPACE_EXEC,0,0).status);
    for(unsigned i=0;i<count;++i)__atomic_store_n(&probes[i].command,1,__ATOMIC_RELEASE);
    for(;;){unsigned ready=0;for(unsigned i=0;i<count;++i)ready+=__atomic_load_n(&probes[i].phase,__ATOMIC_ACQUIRE)==2;if(ready==count)break;CHECK(!CALL(step,1,0,0).status && CALL(clock,0,0,0).value<deadline);}
    /* Revoke an already executed page and remove an already read page while
     * CPU1 still uses this root. Post-change guarded accesses must really fault. */
    if(count==3){uint64_t held;for(;;){held=kernel_cpu_enter();if(__atomic_load_n(&cpu[1].address_space,__ATOMIC_ACQUIRE)==a.id)break;kernel_cpu_leave(held);CHECK(!CALL(step,1,0,0).status && CALL(clock,0,0,0).value<deadline);}
        CHECK(!protect(a.id,101,va+4096,0,0,0).status && !CALL(unmap,a.id,101,va+8192).status);kernel_cpu_leave(held);
    }else CHECK(!protect(a.id,101,va+4096,0,0,0).status && !CALL(unmap,a.id,101,va+8192).status);
    CHECK(!protect(b.id,102,va+4096,0,0,0).status && !CALL(unmap,b.id,102,va+8192).status);
    CHECK(!CALL(free,hole_a.id,101,0).status && !CALL(free,hole_b.id,102,0).status && !CALL(alloc,1,999,&canary).status);*(uint64_t *)canary.address=0x8888;
    for(unsigned i=0;i<count;++i)__atomic_store_n(&probes[i].command,2,__ATOMIC_RELEASE);
    for(;;){unsigned blocked=0;for(unsigned i=0;i<count;++i){struct thread_info t;CHECK(!CALL(control,THREAD_INFO,ids[i],&t).status);blocked+=t.state==THREAD_BLOCKED;}if(blocked==count)break;CHECK(!CALL(step,1,0,0).status && CALL(clock,0,0,0).value<deadline);}
    CHECK(!CALL(info,a.id,101,&snapshot).status && !snapshot.active && CALL(destroy,a.id,101,0).status);
    CHECK(!CALL(unmap,a.id,101,va).status && !CALL(free,first.id,101,0).status && !map(a.id,101,va,replacement.physical,0,0).status);
    for(unsigned i=0;i<count;++i)CHECK(!CALL(control,THREAD_WAKE,ids[i],0).status);
    for(;;){unsigned exited=0;for(unsigned i=0;i<count;++i){struct thread_info t;CHECK(!CALL(control,THREAD_INFO,ids[i],&t).status);exited+=t.state==THREAD_EXITED;}if(exited==count)break;CHECK(!CALL(step,1,0,0).status && CALL(clock,0,0,0).value<deadline);}
    CHECK(CALL(destroy,a.id,101,0).status);
    for(unsigned i=0;i<count;++i){struct thread_info t;CHECK(!CALL(control,THREAD_INFO,ids[i],&t).status && !t.result.status && !probes[i].errors && probes[i].phase==4 && t.switches>20);CHECK(!CALL(control,THREAD_REAP,ids[i],0).status);}
    CHECK(!CALL(info,a.id,101,&snapshot).status && !snapshot.active && !snapshot.leases);
    CHECK(!CALL(destroy,a.id,101,0).status && !CALL(destroy,b.id,102,0).status && CALL(info,a.id,101,&snapshot).status);
    CHECK(!CALL(free,second.id,102,0).status && !CALL(free,replacement.id,101,0).status && kernel_cpu_space()==0);
    CHECK(!CALL(free,code_a.id,101,0).status && !CALL(free,code_b.id,102,0).status && !CALL(free,canary.id,999,0).status);
    struct mem_space_info fresh;CHECK(!CALL(create,101,&fresh,0).status && fresh.id>a.id && !CALL(destroy,fresh.id,101,0).status);
    /* Completed a cycle; the outer test checks allocation and fault totals. */
    return true;
}

static uint64_t owned_count(struct boot_pages *pages){uint64_t n=0;for(struct boot_reservation_chunk *c=pages->reservations.first;c;c=c->next)for(size_t i=0;i<c->count;++i){unsigned kind=c->entries[i].kind;if(kind==BOOT_RES_SPACE_TABLE || kind==BOOT_RES_PAGES || kind==BOOT_RES_STACK)++n;}return n;}
bool kernel_spaces_test(const struct kernel_surfaces *hub,struct boot_pages *pages){
    const struct kernel_hub_table *cpus=kernel_hub_find_table(hub,"smp.cpus");CHECK(cpus && cpus->count);
    kernel_surface_call configure=kernel_surface_find(hub,"probe.space_fault_config");CHECK(configure);
    kernel_surface_call page_alloc=kernel_surface_find(hub,"mem.pages_alloc"),page_free=kernel_surface_find(hub,"mem.pages_free");
    struct mem_pages storage;CHECK(cpus->count<=SIZE_MAX/sizeof(struct space_fault_slot) && !CALL(page_alloc,(cpus->count*sizeof(struct space_fault_slot)+4095)/4096,998,&storage).status);
    struct space_fault_slot *slots=(void *)storage.address;CHECK(!CALL(configure,slots,cpus->count,0).status);
    /* event.step lazily creates two permanent deferred-worker stacks. Warm it
     * before the test allocation baseline, rather than misclassifying them. */
    CHECK(!CALL(kernel_surface_find(hub,"event.step"),1,0,0).status);
    uint64_t baseline=owned_count(pages);
    for(unsigned i=0;i<8;++i){CHECK(cycle(hub,pages));CHECK(owned_count(pages)==baseline);}
    uint64_t faults=0;for(uint64_t i=0;i<cpus->count;++i){CHECK(!slots[i].armed);faults+=slots[i].seen;}
    CHECK(faults==8*5*(cpus->count>1?3:2) && !CALL(configure,0,0,0).status && !CALL(page_free,storage.id,998,0).status);
    print("PASS address spaces: 8 cycles; exact RO/NX/unmapped faults, restored CPU-local gates, active SMP revocation, reuse, preemption and zero owned-page/table/stack leaks\n");return true;
}
