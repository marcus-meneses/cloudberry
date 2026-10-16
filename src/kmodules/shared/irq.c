#include "irq.h"
#include "threads.h"
#include "apic.h"
#include "hwenum.h"
#include "mmio.h"
#include "../../kernel/cpu.h"
#define MASK (1u<<16)
struct slot {
    struct irq_info info;
    uintptr_t controller,context;uint64_t pending_since;
    uint32_t input,low,level,armed,running;
    kernel_surface_call callback;
};
static struct {const struct kernel_surfaces *hub;struct slot *slots;kernel_surface_call ticks,cpu,claim,release;struct apic_info apic;const struct hwenum_table *hw;const struct mmio_table *mmio;uint64_t serial;uint32_t destination,cursor;int initialized,draining;} state;
static struct kernel_surface_result call(kernel_surface_call fn,uintptr_t a,uintptr_t b,uintptr_t c) {return fn(a,b,c,0,0,0);}
static int bsp(void) {return state.cpu && !call(state.cpu,CPU_CURRENT,0,0).value;}
static uint64_t lock(void) {return call(state.cpu,CPU_ENTER,0,0).value;}
static void unlock(uint64_t flags) {call(state.cpu,CPU_LEAVE,flags,0);}
static uint32_t io_read(uintptr_t base,uint32_t reg) {*(volatile uint32_t *)base=reg;return *(volatile uint32_t *)(base+16);}
static void io_write(uintptr_t base,uint32_t reg,uint32_t value) {*(volatile uint32_t *)base=reg;*(volatile uint32_t *)(base+16)=value;}
static void eoi(void) {
    if(state.apic.mode==APIC_X2APIC)__asm__ volatile("wrmsr"::"c"(0x80b),"a"(0),"d"(0):"memory");
    else *(volatile uint32_t *)(state.apic.address+0xb0)=0;
}
static uint32_t local_read(uint32_t offset) {
    if(state.apic.mode==APIC_X2APIC) {uint32_t a,d;__asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(0x800+offset/16));return a;}
    return *(volatile uint32_t *)(state.apic.address+offset);
}
static uint64_t ticks(void) {return state.ticks(0,0,0,0,0,0).value;}
static void increment(uint64_t *at,uint64_t n,uint64_t *overflow) {
    uint64_t old=__atomic_load_n(at,__ATOMIC_RELAXED),next;
    do {next=n>UINT64_MAX-old?UINT64_MAX:old+n;}while(!__atomic_compare_exchange_n(at,&old,next,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED));
    if(next==UINT64_MAX && overflow)__atomic_store_n(overflow,1,__ATOMIC_RELAXED);
}
static struct slot *find(uint64_t id) {for(unsigned i=32;i<224;++i)if(state.slots[i].info.route.id==id)return &state.slots[i];return NULL;}
struct kernel_surface_result irq_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)a;
    if(state.initialized)return module_result(1,state.hub==hub);
    if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC || hub->major!=2 || !b)return module_result(0,0);
    state.cpu=kernel_surface_find_version(hub,"cpu.control",1,0);if(!bsp())return module_result(0,0);
    state.ticks=kernel_surface_find_version(hub,"cpu.ticks",1,0);
    state.claim=kernel_surface_find_version(hub,"inthandle.claim",1,0);state.release=kernel_surface_find_version(hub,"inthandle.release",1,0);
    kernel_surface_call alloc=kernel_surface_find_version(hub,"mem.calloc",1,0),apic=kernel_surface_find_version(hub,"apic.info",1,0);
    const struct kernel_hub_table *hw=kernel_hub_find_table(hub,HWENUM_TABLE_NAME),*mmio=kernel_hub_find_table(hub,MMIO_TABLE_NAME),*cpus=kernel_hub_find_table(hub,"smp.cpus");
    if(!state.ticks || !state.claim || !state.release || !alloc || !apic || !hw || !mmio || !cpus || call(apic,(uintptr_t)&state.apic,0,0).status)return module_result(0,0);
    state.destination=((const struct cpu_record *)cpus->address)[0].lapic_id;
    state.hw=(const void *)hw->address;state.mmio=(const void *)mmio->address;
    struct kernel_surface_result allocated=call(alloc,b,256,sizeof(struct slot));if(allocated.status)return allocated;
    state.slots=(void *)allocated.value;state.hub=hub;state.cursor=32;state.initialized=1;return module_result(1,1);
}
struct kernel_surface_result irq_surface_route(MODULE_ARGS) {
    MODULE_UNUSED();const struct irq_request *r=(const void *)a;
    if(!state.initialized || !bsp() || !r || !b || !r->owner || r->reserved || r->level>1 || r->active_low>1 || state.serial==UINT64_MAX || state.destination>255 ||
       (r->kind!=IRQ_IOAPIC && r->kind!=IRQ_MSI) || (r->kind==IRQ_MSI && (r->level || r->active_low || r->gsi)))return module_result(0,0);
    int found=0;const struct kernel_surface *methods=kernel_surface_entries(state.hub);
    for(size_t i=0;i<state.hub->count;++i)if(methods[i].call==r->callback && methods[i].major==r->major && methods[i].minor>=r->minor)found=1;
    if(!found)return module_result(0,0);
    uint64_t flags=lock();uintptr_t controller=0;uint32_t input=0;
    if(r->kind==IRQ_IOAPIC) {
        for(unsigned i=32;i<224;++i)if(state.slots[i].info.route.id && state.slots[i].info.route.kind==IRQ_IOAPIC && state.slots[i].info.route.gsi==r->gsi) {unlock(flags);return module_result(0,0);}
        for(size_t i=0;i<state.hw->count;++i) {const struct hwenum_descriptor *h=&state.hw->entries[i];if(h->kind!=HWENUM_IO_APIC || r->gsi<h->data.interrupt.gsi_base)continue;
            for(size_t m=0;m<state.mmio->count;++m) {const struct mmio_region *region=&state.mmio->entries[m];if(region->kind!=MMIO_IO_APIC || region->physical!=h->address)continue;
                kernel_surface_call map=kernel_surface_find_version(state.hub,"mmio.map",1,0);struct kernel_surface_result mapped=call(map,m,0,0);if(mapped.status)continue;
                unsigned max=(io_read(mapped.value,1)>>16)&255;
                if(max>119 || r->gsi-h->data.interrupt.gsi_base>max)continue;
                controller=mapped.value;input=r->gsi-h->data.interrupt.gsi_base;break;
            }
            if(controller)break;
        }
        if(!controller) {unlock(flags);return module_result(0,0);}
    }
    struct slot *slot=NULL;unsigned vector;
    for(vector=32;vector<224;++vector) {struct slot *candidate=&state.slots[vector];
        if(candidate->info.route.id)continue;
        if(!state.claim(vector,(uintptr_t)"irq.interrupt",(uintptr_t)candidate,(uintptr_t)candidate,0,0).status) {slot=candidate;break;}
    }
    if(!slot) {unlock(flags);return module_result(0,0);}
    *slot=(struct slot){.info={.route={.id=++state.serial,.owner=r->owner,.kind=r->kind,.vector=vector,.gsi=r->gsi,.masked=1,
        .message_address=UINT64_C(0xfee00000)|((uint64_t)state.destination<<12),.message_data=vector|(1u<<14)}},.controller=controller,.input=input,
        .low=vector|(r->level?(1u<<15):0)|(r->active_low?(1u<<13):0),.level=r->level,.callback=r->callback,.context=r->context};
    if(controller) {io_write(controller,0x10+input*2,slot->low|MASK);io_write(controller,0x11+input*2,state.destination<<24);}
    *(struct irq_route *)b=slot->info.route;unlock(flags);return module_result(slot->info.route.id,1);
}
struct kernel_surface_result irq_surface_enable(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.initialized || !bsp() || c>1)return module_result(0,0);
    uint64_t flags=lock();struct slot *s=find(a);int ok=s && s->info.route.owner==b;
    if(ok) {s->armed=c;s->info.route.masked=!c;if(s->controller)io_write(s->controller,0x10+s->input*2,s->low|(c?0:MASK));}
    unlock(flags);return module_result(ok,ok);
}
struct kernel_surface_result irq_surface_release(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.initialized || !bsp() || c!=1)return module_result(0,0);
    uint64_t flags=lock();struct slot *s=find(a);int ok=0;
    if(s && s->info.route.owner==b && !s->armed && !s->running && !__atomic_load_n(&s->info.pending,__ATOMIC_ACQUIRE)) {
        unsigned vector=s->info.route.vector,bit=1u<<(vector&31),offset=(vector/32)*16;
        if((!s->controller || !(io_read(s->controller,0x10+s->input*2)&(1u<<14))) && !(local_read(0x100+offset)&bit) && !(local_read(0x200+offset)&bit) && !call(state.release,vector,(uintptr_t)s,0).status) {*s=(struct slot){0};ok=1;}
    }
    unlock(flags);return module_result(ok,ok);
}
struct kernel_surface_result irq_surface_info(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.initialized || !bsp() || !b)return module_result(0,0);
    uint64_t flags=lock();struct slot *s=find(a);if(s)*(struct irq_info *)b=s->info;unlock(flags);return module_result(s!=NULL,s!=NULL);
}
struct kernel_surface_result irq_surface_pending(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.initialized || !bsp())return module_result(0,0);
    size_t pending=0;for(unsigned i=32;i<224;++i)if(__atomic_load_n(&state.slots[i].info.pending,__ATOMIC_ACQUIRE))++pending;
    return module_result(pending,1);
}
struct kernel_surface_result irq_surface_interrupt(MODULE_ARGS) {
    MODULE_UNUSED();const struct inthandle_frame *frame=(const void *)a;struct slot *s=(void *)b;
    if(!state.initialized || !bsp() || !frame || frame->vector<32 || frame->vector>=224 || s!=&state.slots[frame->vector] || !s->info.route.id || c!=INTHANDLE_CPU_ENTRY)return module_result(0,0);
    increment(&s->info.delivered,1,&s->info.overflow);
    if(s->level && s->controller) {io_write(s->controller,0x10+s->input*2,s->low|MASK);s->info.route.masked=1;}
    if(s->armed) {if(!__atomic_load_n(&s->info.pending,__ATOMIC_RELAXED))s->pending_since=ticks();increment(&s->info.pending,1,&s->info.overflow);}
    eoi();return module_result(1,1);
}
static uint64_t callback_thread;static int callback_active;
struct kernel_surface_result irq_surface_context(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.initialized || !bsp())return module_result(0,0);
    kernel_surface_call control=kernel_surface_find(state.hub,"threads.control");
    return module_result(callback_active && (control?control(THREAD_CURRENT,0,0,0,0,0).value:0)==callback_thread,1);
}
struct kernel_surface_result irq_surface_drain(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.initialized || !bsp() || !a || state.draining)return module_result(0,0);
    uint64_t entry_flags;__asm__ volatile("pushfq;pop %0":"=r"(entry_flags));
    if(!(entry_flags&512) || call(state.cpu,CPU_DEPTH,0,0).value)return module_result(0,0);
    if(__atomic_exchange_n(&state.draining,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    size_t done=0;
    unsigned first=state.cursor;
    for(unsigned offset=0;offset<192 && done<a;++offset) {
        unsigned i=32+(first-32+offset)%192;
        uint64_t flags=lock();struct slot *s=&state.slots[i];uint64_t pending=__atomic_exchange_n(&s->info.pending,0,__ATOMIC_ACQ_REL);
        if(!s->info.route.id || !pending) {unlock(flags);continue;}
        uint64_t start=ticks();if(start>=s->pending_since) {uint64_t delay=start-s->pending_since;increment(&s->info.total_defer_ticks,delay,&s->info.overflow);if(delay>s->info.max_defer_ticks)s->info.max_defer_ticks=delay;}
        uint64_t pending_since=s->pending_since;
        s->running=1;struct irq_notification n={s->info.route.id,s->info.route.owner,pending,i,s->info.route.gsi,s->info.route.kind,0};unlock(flags);
        kernel_surface_call control=kernel_surface_find(state.hub,"threads.control");
        callback_thread=control?call(control,THREAD_CURRENT,0,0).value:0;callback_active=1;
        struct kernel_surface_result delivered=call(s->callback,(uintptr_t)&n,s->context,0);
        callback_active=0;callback_thread=0;flags=lock();s->running=0;
        if(delivered.status) {if(!s->info.pending || pending_since<s->pending_since)s->pending_since=pending_since;increment(&s->info.failures,1,NULL);increment(&s->info.pending,pending,&s->info.overflow);unlock(flags);__atomic_store_n(&state.draining,0,__ATOMIC_RELEASE);return module_result(done,0);}
        increment(&s->info.drained,pending,&s->info.overflow);state.cursor=i==223?32:i+1;unlock(flags);++done;
    }
    __atomic_store_n(&state.draining,0,__ATOMIC_RELEASE);return module_result(done,1);
}
