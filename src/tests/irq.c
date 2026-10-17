#include "../kmodules/shared/irq.h"
#include "../kmodules/shared/apic.h"
#include "../kmodules/shared/hwenum.h"
#include "../kmodules/shared/mmio.h"
#include "../kernel/cpu.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static struct {struct kernel_surfaces hub;struct kernel_surface methods[6];struct kernel_hub_table tables[3];} fixture;
static struct hwenum_table hardware;static struct mmio_table regions;static struct cpu_record cpu={.lapic_id=42};
static uint32_t registers[1024];static uintptr_t contexts[256];static unsigned depth,allocations,calls;static uint64_t count;static int failed;
static struct kernel_surface_result control(MODULE_ARGS) {MODULE_UNUSED();if(a==CPU_CURRENT)return module_result(0,1);if(a==CPU_DEPTH)return module_result(depth,1);if(a==CPU_ENTER){++depth;return module_result(0,1);}assert(a==CPU_LEAVE && depth);--depth;return module_result(1,1);}
static struct kernel_surface_result allocate(MODULE_ARGS) {MODULE_UNUSED();++allocations;return module_result((uintptr_t)calloc(b,c),1);}
static struct kernel_surface_result claim(MODULE_ARGS) {MODULE_UNUSED();assert(a>=32 && a<224 && b && c==d);if(contexts[a])return module_result(0,0);contexts[a]=c;return module_result(1,1);}
static struct kernel_surface_result release(MODULE_ARGS) {MODULE_UNUSED();assert(a<224 && contexts[a]==b);contexts[a]=0;return module_result(1,1);}
static struct kernel_surface_result apic(MODULE_ARGS) {MODULE_UNUSED();*(struct apic_info *)a=(struct apic_info){.address=(uintptr_t)registers,.mode=APIC_XAPIC};return module_result(1,1);}
static struct kernel_surface_result callback(MODULE_ARGS) {MODULE_UNUSED();const struct irq_notification *n=(void *)a;assert(!depth && b==123 && !c && n->owner==55 && n->kind==IRQ_MSI);assert(CALL(irq_surface_drain,1,0,0).status);++calls;count=n->count;return module_result(1,!failed);}
static struct kernel_surface_result ticks(MODULE_ARGS) {MODULE_UNUSED();static uint64_t value;return module_result(++value,1);}
int main(void) {
    fixture.hub=(struct kernel_surfaces){.magic=KERNEL_SURFACES_MAGIC,.major=KERNEL_HUB_ABI,.header_bytes=offsetof(__typeof__(fixture),methods),.count=6,.tables_offset=offsetof(__typeof__(fixture),tables),.table_count=3};
    const char *names[]={"cpu.control","mem.calloc","inthandle.claim","inthandle.release","apic.info","cpu.ticks"};kernel_surface_call methods[]={control,allocate,claim,release,apic,ticks};
    for(unsigned i=0;i<6;++i){strcpy(fixture.methods[i].name,names[i]);fixture.methods[i].call=methods[i];fixture.methods[i].major=1;}
    fixture.tables[0]=(struct kernel_hub_table){.name=HWENUM_TABLE_NAME,.address=(uintptr_t)&hardware};fixture.tables[1]=(struct kernel_hub_table){.name=MMIO_TABLE_NAME,.address=(uintptr_t)&regions};fixture.tables[2]=(struct kernel_hub_table){.name="smp.cpus",.address=(uintptr_t)&cpu};
    assert(!CALL(irq_surface_init,&fixture.hub,1,0).status && allocations==1);
    /* A validated mediated callback address; use an existing fixture slot. */
    fixture.methods[4]=(struct kernel_surface){.name="test.callback",.call=callback,.major=1};
    struct irq_request request={.kind=IRQ_MSI,.owner=55,.callback=callback,.context=123,.major=1};struct irq_route route;
    assert(!CALL(irq_surface_route,&request,&route,0).status && route.masked && route.message_address==(UINT64_C(0xfee00000)|(42u<<12)) && route.message_data==(route.vector|(1u<<14)));
    assert(CALL(irq_surface_enable,route.id,56,1).status && !CALL(irq_surface_enable,route.id,55,1).status);
    assert(CALL(irq_surface_release,route.id,55,1).status);
    struct inthandle_frame frame={.vector=route.vector};
    for(unsigned i=0;i<3;++i)assert(!CALL(irq_surface_interrupt,&frame,contexts[route.vector],0).status);
    assert(allocations==1 && CALL(irq_surface_pending,0,0,0).value==1 && !CALL(irq_surface_drain,1,0,0).status && calls==1 && count==3);
    struct irq_info info;assert(!CALL(irq_surface_info,route.id,&info,0).status && info.delivered==3 && info.drained==3 && !info.pending);
    failed=1;assert(!CALL(irq_surface_interrupt,&frame,contexts[route.vector],0).status && CALL(irq_surface_drain,1,0,0).status);
    assert(!CALL(irq_surface_info,route.id,&info,0).status && info.pending==1 && info.failures==1);
    failed=0;assert(!CALL(irq_surface_drain,1,0,0).status && count==1);
    assert(!CALL(irq_surface_enable,route.id,55,0).status && CALL(irq_surface_release,route.id,55,0).status);
    uint32_t *irr=&registers[(0x200+(route.vector/32)*16)/4];*irr=1u<<(route.vector%32);assert(CALL(irq_surface_release,route.id,55,1).status);*irr=0;
    assert(!CALL(irq_surface_release,route.id,55,1).status && CALL(irq_surface_info,route.id,&info,0).status);
    struct irq_route next;assert(!CALL(irq_surface_route,&request,&next,0).status && next.id!=route.id && next.vector==route.vector);
    assert(!CALL(irq_surface_release,next.id,55,1).status && !depth && allocations==1);
    puts("PASS: MSI composition, notification coalescing, unlocked callbacks, failure retention, owner/quiescence/IRR checks and stale IDs");
    return 0;
}
