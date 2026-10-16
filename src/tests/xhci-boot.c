#include "../kmodules/shared/xhci.h"
#include "../kmodules/shared/hwenum.h"
#include "../kmodules/shared/threads.h"
#include <stdbool.h>
#include "probe.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void print(const char *s) {while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));}
static void number(uint64_t n) {char d[24];unsigned i=0;do{d[i++]='0'+n%10;n/=10;}while(n);while(i)__asm__ volatile("outb %0,$0xe9"::"a"(d[--i]));print("\n");}
bool kernel_xhci_test(const struct kernel_surfaces *hub) {
    const struct kernel_hub_table *hw=kernel_hub_find_table(hub,HWENUM_TABLE_NAME),*registry=kernel_hub_find_table(hub,XHCI_TABLE_NAME);
    if(!hw || !registry)return false;
    const struct xhci_table *table=(void *)registry->address;if(table->magic!=XHCI_TABLE_MAGIC || table->count)return false;
    const struct hwenum_table *hardware=(void *)hw->address;size_t index=hardware->count;
    for(size_t i=0;i<hardware->count;++i)if(hardware->entries[i].kind==HWENUM_PCI && hardware->entries[i].data.pci.class_code==12 && hardware->entries[i].data.pci.subclass==3 && hardware->entries[i].data.pci.programming_interface==0x30){index=i;break;}
    if(index==hardware->count)return false;
    kernel_surface_call create=kernel_surface_find(hub,"threads.create"),control=kernel_surface_find(hub,"threads.control"),step=kernel_surface_find(hub,"event.step"),now=kernel_surface_find(hub,"clock.now_ns"),entry=kernel_surface_find(hub,"probe.xhci");
    if(!create || !control || !step || !now || !entry)return false;
    struct probe_xhci job={.hub=hub,.hardware_index=index};
#ifdef XHCI_HOTPLUG_TEST
    job.hotplug=1;
#endif
    struct thread_request request={.abi=1,.cpu=0,.stack_bytes=32768,.entry=(uintptr_t)entry,.major=1};request.payload.arguments[0]=(uintptr_t)&job;
    struct kernel_surface_result t=CALL(create,&request,0,0);if(t.status)return false;uint64_t start=CALL(now,0,0,0).value;
    struct thread_info info;
    for(;;) {if(CALL(control,THREAD_INFO,t.value,&info).status)return false;if(info.state==THREAD_EXITED)break;
        if(CALL(step,1,0,0).status || CALL(now,0,0,0).value-start>15000000000)return false;
    }
    if(job.errors || !job.done || info.result.status) {print("FAIL xHCI probe stage ");number(job.errors);return false;}
    if(CALL(control,THREAD_REAP,t.value,0).status)return false;
    print("MEASURE xHCI transfers: ");number(job.transfers);print("MEASURE xHCI interrupts: ");number(job.interrupts);print("MEASURE xHCI events: ");number(job.events);
    print("PASS xHCI: controller IRQ/DMA, root port, slot/address, control ring wrap, interrupt endpoint cancel, close/reopen\n");return true;
}
