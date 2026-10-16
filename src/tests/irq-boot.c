#include "../kmodules/shared/irq.h"
#include "../kmodules/shared/mmio.h"
#include "probe.h"
#include "../kmodules/shared/scheduler.h"
#include <stdbool.h>
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static bool failure(unsigned line) {
    const char *s="FAIL irq-boot.c line ";while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));
    char digits[12];unsigned n=0;do{digits[n++]='0'+line%10;line/=10;}while(line);
    while(n){__asm__ volatile("outb %0,$0xe9"::"a"(digits[--n]));}__asm__ volatile("outb %0,$0xe9"::"a"((unsigned char)'\n'));return false;
}
#define CHECK(x) do{if(!(x))return failure(__LINE__);}while(0)
bool kernel_irq_test(const struct kernel_surfaces *hub) {
    kernel_surface_call route=kernel_surface_find(hub,"irq.route"),enable=kernel_surface_find(hub,"irq.enable"),release=kernel_surface_find(hub,"irq.release"),info=kernel_surface_find(hub,"irq.info"),step=kernel_surface_find(hub,"event.step"),read=kernel_surface_find(hub,"mmio.read"),write=kernel_surface_find(hub,"mmio.write");
    CHECK(route && enable && release && info && step && read && write);
    const struct kernel_hub_table *ref=kernel_hub_find_table(hub,MMIO_TABLE_NAME);CHECK(ref);
    const struct mmio_table *table=(const void *)ref->address;size_t index=table->count;
    for(size_t i=0;i<table->count;++i)if(table->entries[i].kind==MMIO_HPET){index=i;break;}
    CHECK(index<table->count);
    struct kernel_surface_result caps=CALL(read,index,0x100,8),old_config=CALL(read,index,0x10,8),old_timer=CALL(read,index,0x100,8),period=CALL(read,index,0,8);
    CHECK(!caps.status && !old_config.status && !old_timer.status && !period.status && (caps.value&(1u<<5)));
    unsigned gsi;for(gsi=16;gsi<32;++gsi)if((caps.value>>32)&(1u<<gsi))break;CHECK(gsi<32);
    struct probe_irq probe={.index=index};struct irq_request request={.kind=IRQ_IOAPIC,.gsi=gsi,.level=1,.owner=77,.callback=kernel_surface_find(hub,"probe.irq"),.context=(uintptr_t)&probe,.major=1};
    struct irq_route installed,other;CHECK(!CALL(route,&request,&installed,0).status && installed.masked && installed.id);
    CHECK(CALL(route,&request,&other,0).status && CALL(enable,installed.id,78,1).status);
    request.kind=IRQ_MSI;request.gsi=0;request.level=0;
    CHECK(!CALL(route,&request,&other,0).status && other.vector!=installed.vector && other.id!=installed.id && (other.message_address&0xfff00000)==0xfee00000 && (other.message_data&255)==other.vector);
    CHECK(CALL(release,other.id,77,0).status && !CALL(release,other.id,77,1).status && CALL(info,other.id,&(struct irq_info){0},0).status);
    CHECK(!write(index,0x100,8,old_timer.value&~4u,0,0).status && !write(index,0x10,8,old_config.value&~2u,0,0).status && !write(index,0x20,8,1,0,0).status);
    uint64_t config=(old_timer.value&~(UINT64_C(4)|8|256|16384|(UINT64_C(31)<<9)))|2|4|((uint64_t)gsi<<9);
    CHECK(!write(index,0x100,8,config,0,0).status && !CALL(enable,installed.id,77,1).status);
    kernel_surface_call submit=kernel_surface_find(hub,"scheduler.submit"),result=kernel_surface_find(hub,"scheduler.result");
    struct scheduler_request slow={.abi=SCHEDULER_REQUEST_ABI,.kind=SCHEDULER_METHOD,.entry_vector=(uintptr_t)kernel_surface_find(hub,"probe.delay"),.entry_major=1,.payload={.arguments={200000000}}};
    struct kernel_surface_result job=CALL(submit,&slow,0,0);CHECK(!job.status);struct scheduler_completion completion;
    for(unsigned i=0;i<128;++i) {CHECK(!CALL(step,1,0,0).status && !CALL(result,job.value,&completion,0).status);if(completion.state==SCHEDULER_RUNNING)break;}
    CHECK(completion.state==SCHEDULER_RUNNING);
    for(unsigned test=0;test<2;++test) {
        struct kernel_surface_result counter=CALL(read,index,0xf0,8);CHECK(!counter.status);
        uint64_t delta=UINT64_C(2000000000000)/(period.value>>32);
        CHECK(!write(index,0x108,8,counter.value+delta,0,0).status);
        for(unsigned wake=0;probe.callbacks==test && wake<128;++wake) {
            struct irq_info state;CHECK(!CALL(info,installed.id,&state,0).status);
            if(state.pending)CHECK(state.route.masked);
            CHECK(!CALL(step,4,0,0).status);
        }
        if(!test)CHECK(!CALL(result,job.value,&completion,0).status && completion.state==SCHEDULER_RUNNING);
        CHECK(probe.callbacks==test+1 && !probe.errors);
    }
    struct irq_info status;CHECK(!CALL(info,installed.id,&status,0).status && status.delivered>=2 && status.drained==2 && !status.pending && status.max_defer_ticks);
    CHECK(!write(index,0x100,8,old_timer.value&~4u,0,0).status && !CALL(enable,installed.id,77,0).status && !write(index,0x20,8,1,0,0).status);
    CHECK(!CALL(release,installed.id,77,1).status && !write(index,0x100,8,old_timer.value,0,0).status && !write(index,0x10,8,old_config.value,0,0).status);
    for(unsigned i=0;i<1024;++i) {CHECK(!CALL(result,job.value,&completion,0).status);if(completion.state==SCHEDULER_COMPLETE)break;CHECK(!CALL(step,1,0,0).status);}
    CHECK(completion.state==SCHEDULER_COMPLETE && !CALL(result,job.value,&completion,1).status);
    const char *s="PASS device IRQ: real level HPET/IOAPIC delivery, mask/EOI, preemptible worker isolation, deferred callback/rearm, MSI messages and safe release\n";while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));return true;
}
