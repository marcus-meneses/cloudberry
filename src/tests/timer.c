#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/timer.h"
#include "../kmodules/shared/timer-platform.h"
#include "../kmodules/shared/apic.h"
#include "../kmodules/shared/threads.h"
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static struct {struct kernel_surfaces hub;struct kernel_surface methods[5];} fixture;
static uint64_t time_ns,count_start,irq_flags;
static uint32_t initial,registers[64];
static uintptr_t context,owner;
static unsigned claims,eois;
static bool busy=true;
void timer_platform_configure(uintptr_t address,uint32_t mode) {assert(address==0x100000 && mode==TIMER_XAPIC);}
uint32_t timer_register_read(uint32_t offset) {assert(offset==0x390);uint64_t elapsed=(time_ns-count_start)/16;return elapsed>=initial?0:initial-(uint32_t)elapsed;}
void timer_register_write(uint32_t offset,uint32_t value) {
    assert(offset==0x320 || offset==0x380 || offset==0x3e0 || offset==0xb0);
    if (offset==0xb0) {assert(value==0);++eois;}
    if (offset==0x380) {initial=value;count_start=time_ns;}
    registers[offset/16]=value;
}
uint64_t timer_irq_save(void) {uint64_t flags=irq_flags;irq_flags=0;return flags;}
void timer_irq_restore(uint64_t flags) {irq_flags=flags;}
static struct kernel_surface_result now(MODULE_ARGS) {MODULE_UNUSED();time_ns+=100000;return module_result(time_ns,1);}
static struct kernel_surface_result claim(MODULE_ARGS) {
    MODULE_UNUSED();assert(a==TIMER_VECTOR && b && c && d);
    if (busy) return module_result(0,0);
    assert(!claims);claims=1;owner=d;context=c;return module_result(1,1);
}
static struct kernel_surface_result release(MODULE_ARGS) {MODULE_UNUSED();assert(a==TIMER_VECTOR && b==owner);claims=0;return module_result(1,1);}
static struct kernel_surface_result info(MODULE_ARGS) {
    MODULE_UNUSED();*(struct apic_info *)a=(struct apic_info){.address=0x100000,.physical=0xfee00000,.mode=APIC_XAPIC};return module_result(1,1);
}
static int deferred;
static struct kernel_surface_result tick(MODULE_ARGS) {MODULE_UNUSED();assert(eois && a);return module_result(deferred?THREAD_TICK_DEFERRED:THREAD_TICK_HANDLED,1);}
int main(void) {
    fixture.hub=(struct kernel_surfaces){.magic=KERNEL_SURFACES_MAGIC,.major=2,.count=5,.header_bytes=offsetof(__typeof__(fixture),methods)};
    const char *names[]={"clock.now_ns","inthandle.claim","inthandle.release","apic.info","threads.tick"};kernel_surface_call calls[]={now,claim,release,info,tick};
    for (unsigned i=0;i<5;++i) {strcpy(fixture.methods[i].name,names[i]);fixture.methods[i].call=calls[i];fixture.methods[i].major=1;}
    irq_flags=0x200;
    assert(CALL(timer_surface_arm,0,0,0).status==KERNEL_SURFACE_FAILED);
    assert(CALL(timer_surface_init,&fixture.hub,0,0).status==KERNEL_SURFACE_FAILED && !claims && irq_flags==0x200);
    busy=false;assert(!CALL(timer_surface_init,&fixture.hub,0,0).status && claims==1 && irq_flags==0x200);
    struct timer_info value;assert(!CALL(timer_surface_info,&value,0,0).status && value.frequency_hz==62500000 && !value.armed);
    assert(!CALL(timer_surface_init,&fixture.hub,0,0).status);
    assert(CALL(timer_surface_init,0,0,0).status==KERNEL_SURFACE_FAILED);
    assert(CALL(timer_surface_arm,0,0,0).value==1 && initial==1);
    assert(CALL(timer_surface_arm,UINT64_MAX,0,0).value==UINT32_MAX);
    struct inthandle_frame frame={.vector=TIMER_VECTOR};
    assert(timer_surface_interrupt((uintptr_t)&frame,context,INTHANDLE_DEFERRED,0,0,0).status==KERNEL_SURFACE_FAILED && !eois);
    for (unsigned i=0;i<3;++i) assert(!timer_surface_interrupt((uintptr_t)&frame,context,INTHANDLE_CPU_ENTRY,0,0,0).status);
    assert(eois==3 && CALL(timer_surface_take,0,0,0).value==3 && CALL(timer_surface_take,0,0,0).value==0);
    CALL(timer_surface_info,&value,0,0);assert(value.interrupts==3 && !value.pending && !value.armed);
    uint64_t before=time_ns;deferred=1;assert(!timer_surface_interrupt((uintptr_t)&frame,context,INTHANDLE_CPU_ENTRY,0,0,0).status);
    CALL(timer_surface_info,&value,0,0);assert(value.armed && initial==125000 && time_ns==before);
    deferred=0;assert(!timer_surface_interrupt((uintptr_t)&frame,context,INTHANDLE_CPU_ENTRY,0,0,0).status);
    CALL(timer_surface_info,&value,0,0);assert(!value.armed && eois==5);
    assert(!CALL(timer_surface_stop,0,0,0).status && !initial && (registers[0x320/16]&(1u<<16)) && irq_flags==0x200);
    puts("PASS: timer owns only timer/EOI registers; calibration, count bounds, notifications and IF preservation");return 0;
}
