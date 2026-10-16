#include "../kmodules/shared/usb-hub.h"
#include "../kmodules/shared/threads.h"
#include "../kmodules/shared/mem-pages.h"
#include "probe.h"
#include "../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void print(const char *s){while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));}
static void number(uint64_t value){char bytes[24];unsigned n=0;do{bytes[n++]='0'+value%10;value/=10;}while(value);while(n)__asm__ volatile("outb %0,$0xe9"::"a"(bytes[--n]));print("\n");}
struct kernel_surface_result probe_hubs_call(MODULE_ARGS) {
    MODULE_UNUSED();struct probe_usb *job=(void *)a;const struct kernel_surfaces *hub=job->hub;unsigned error=0;
    kernel_surface_call start=kernel_surface_find(hub,"usb.start"),stop=kernel_surface_find(hub,"usb.stop"),info=kernel_surface_find(hub,"usb.info"),watch=kernel_surface_find(hub,"usb.watch"),device=kernel_surface_find(hub,"usb.device"),claim=kernel_surface_find(hub,"usb.claim"),hstart=kernel_surface_find(hub,"usb_hub.start"),hstop=kernel_surface_find(hub,"usb_hub.stop"),hinfo=kernel_surface_find(hub,"usb_hub.info"),thread=kernel_surface_find(hub,"threads.control"),clock=kernel_surface_find(hub,"clock.now_ns"),cpu=kernel_surface_find(hub,"cpu.control");
#define CHECK(x) do{if(!(x)){error=__LINE__;goto failed;}}while(0)
    CHECK(start && stop && info && watch && device && claim && hstart && hstop && hinfo && thread && clock && cpu);
    CHECK(!CALL(watch,1,0,0).status && !CALL(hstart,0,0,0).status && !CALL(start,0,0,0).status);
    struct usb_info status;struct usb_hub_info hubs;uint64_t deadline=CALL(clock,0,0,0).value+15000000000;
    do {CHECK(!CALL(info,&status,0,0).status && !CALL(hinfo,&hubs,0,0).status && !status.failed && !hubs.failed && CALL(clock,0,0,0).value<deadline);if(status.configured<7)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.configured<7);
    CHECK(hubs.running==2 && hubs.records==2);
    const struct kernel_hub_table *ref=kernel_hub_find_table(hub,USB_TABLE_NAME);CHECK(ref);const struct usb_table *table=(void *)ref->address;
    struct usb_ref child={0},grandchild={0};uint64_t flags=CALL(cpu,CPU_ENTER,0,0).value;
    for(const struct usb_device *dev=table->first;dev;dev=dev->next) {
        if(dev->identity.parent_id && dev->depth==1 && !dev->hub)child=(struct usb_ref){dev->identity.id,dev->identity.generation};
        if(dev->depth==2 && !dev->hub)grandchild=(struct usb_ref){dev->identity.id,dev->identity.generation};
    }
    CALL(cpu,CPU_LEAVE,flags,0);CHECK(child.id && grandchild.id && table->count==7);
    struct usb_device old;CHECK(!CALL(device,&grandchild,&old,0).status && old.root_port && old.port==1);
    print("WAIT HUB CHILD REMOVE\n");deadline=CALL(clock,0,0,0).value+5000000000;
    do {CHECK(!CALL(info,&status,0,0).status && CALL(clock,0,0,0).value<deadline);if(status.configured!=6)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.configured!=6);
    CHECK(CALL(claim,&child,0,99).status);print("WAIT HUB CHILD ADD\n");deadline=CALL(clock,0,0,0).value+5000000000;
    do {CHECK(!CALL(info,&status,0,0).status && !status.failed && CALL(clock,0,0,0).value<deadline);if(status.configured!=7)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.configured!=7);
    CHECK(CALL(device,&child,&old,0).status && table->count==7);
    /* Leave an actual interrupt read blocked in a grandchild while physically
     * detaching the upstream hub, while its downstream devices are still active. */
    CHECK(!CALL(claim,&grandchild,0,99).status);
    struct probe_usb_wait pending={.hub=hub,.id=grandchild.id,.generation=grandchild.generation};
    kernel_surface_call create=kernel_surface_find(hub,"threads.create"),entry=kernel_surface_find(hub,"probe.usb_wait");CHECK(create && entry);
    struct thread_request r={.abi=1,.cpu=0,.stack_bytes=16384,.entry=(uintptr_t)entry,.major=1};r.payload.arguments[0]=(uintptr_t)&pending;
    struct kernel_surface_result waiter=CALL(create,&r,0,0);CHECK(!waiter.status);struct thread_info ti;deadline=CALL(clock,0,0,0).value+1000000000;
    do {CHECK(!CALL(thread,THREAD_INFO,waiter.value,&ti).status && !pending.done && CALL(clock,0,0,0).value<deadline);if(ti.state!=THREAD_BLOCKED)CHECK(!CALL(thread,THREAD_YIELD,0,0).status);}while(ti.state!=THREAD_BLOCKED);
    print("WAIT HUB REMOVE\n");deadline=CALL(clock,0,0,0).value+5000000000;
    do {CHECK(!CALL(info,&status,0,0).status && !CALL(hinfo,&hubs,0,0).status && !status.failed && CALL(clock,0,0,0).value<deadline);if(status.configured!=3 || hubs.running)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.configured!=3 || hubs.running);
    do {CHECK(!CALL(thread,THREAD_INFO,waiter.value,&ti).status && CALL(clock,0,0,0).value<deadline);if(ti.state!=THREAD_EXITED)CHECK(!CALL(thread,THREAD_YIELD,0,0).status);}while(ti.state!=THREAD_EXITED);
    CHECK(pending.done && pending.status && pending.state==HOST_DISCONNECTED && !CALL(thread,THREAD_REAP,waiter.value,0).status);
    CHECK(CALL(claim,&grandchild,0,99).status);print("WAIT HUB ADD\n");deadline=CALL(clock,0,0,0).value+10000000000;
    do {CHECK(!CALL(info,&status,0,0).status && !CALL(hinfo,&hubs,0,0).status && !status.failed && !hubs.failed && CALL(clock,0,0,0).value<deadline);if(status.configured!=7)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.configured!=7);
    CHECK(table->count==7 && hubs.records==2 && CALL(device,&grandchild,&old,0).status);
    /* Restart only the hub class twice: root attachment and USB bus survive,
     * children are rebound, blocked I/O fails, and every worker stack is reaped. */
    kernel_surface_call pages=kernel_surface_find(hub,"mem.pages_info");CHECK(pages);
    struct usb_ref root_hub={0};flags=CALL(cpu,CPU_ENTER,0,0).value;
    for(const struct usb_device *dev=table->first;dev;dev=dev->next)if(dev->hub && !dev->depth)root_hub=(struct usb_ref){dev->identity.id,dev->identity.generation};
    CALL(cpu,CPU_LEAVE,flags,0);CHECK(root_hub.id);
    struct mem_pages_info baseline,stopped,restarted;CHECK(!CALL(pages,&baseline,0,0).status);
    for(unsigned cycle=0;cycle<2;++cycle) {
        flags=CALL(cpu,CPU_ENTER,0,0).value;
        for(const struct usb_device *dev=table->first;dev;dev=dev->next)if(dev->depth==2 && !dev->hub && dev->state==USB_CONFIGURED)grandchild=(struct usb_ref){dev->identity.id,dev->identity.generation};
        CALL(cpu,CPU_LEAVE,flags,0);
        CHECK(!CALL(claim,&grandchild,0,99).status);
        pending=(struct probe_usb_wait){.hub=hub,.id=grandchild.id,.generation=grandchild.generation};
        waiter=CALL(create,&r,0,0);CHECK(!waiter.status);deadline=CALL(clock,0,0,0).value+1000000000;
        do {CHECK(!CALL(thread,THREAD_INFO,waiter.value,&ti).status && !pending.done && CALL(clock,0,0,0).value<deadline);if(ti.state!=THREAD_BLOCKED)CHECK(!CALL(thread,THREAD_YIELD,0,0).status);}while(ti.state!=THREAD_BLOCKED);
        CHECK(!CALL(hstop,0,0,0).status);deadline=CALL(clock,0,0,0).value+5000000000;
        do {CHECK(!CALL(hinfo,&hubs,0,0).status && CALL(clock,0,0,0).value<deadline);if(hubs.manager)CHECK(!CALL(thread,THREAD_YIELD,0,0).status);}while(hubs.manager);
        CHECK(!hubs.running && !CALL(hstop,0,0,0).status);
        do {CHECK(!CALL(thread,THREAD_INFO,waiter.value,&ti).status && CALL(clock,0,0,0).value<deadline);if(ti.state!=THREAD_EXITED)CHECK(!CALL(thread,THREAD_YIELD,0,0).status);}while(ti.state!=THREAD_EXITED);
        CHECK(pending.done && pending.status && pending.state==HOST_DISCONNECTED && !CALL(thread,THREAD_REAP,waiter.value,0).status);
        CHECK(!CALL(info,&status,0,0).status && status.running==1 && status.configured==4 && !status.failed);
        CHECK(!CALL(device,&root_hub,&old,0).status && old.state==USB_CONFIGURED && CALL(claim,&grandchild,0,99).status);
        CHECK(!CALL(pages,&stopped,0,0).status && stopped.stacks+3==baseline.stacks && stopped.stack_pages+24==baseline.stack_pages);
        CHECK(!CALL(hstart,0,0,0).status);deadline=CALL(clock,0,0,0).value+10000000000;
        do {CHECK(!CALL(info,&status,0,0).status && !CALL(hinfo,&hubs,0,0).status && !status.failed && !hubs.failed && CALL(clock,0,0,0).value<deadline);if(status.configured!=7 || hubs.running!=2)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.configured!=7 || hubs.running!=2);
        CHECK(hubs.records==2 && table->count==7 && !CALL(device,&root_hub,&old,0).status && old.state==USB_CONFIGURED && CALL(device,&grandchild,&old,0).status);
        CHECK(!CALL(pages,&restarted,0,0).status && restarted.stacks==baseline.stacks && restarted.stack_pages==baseline.stack_pages);
    }
    print("PASS USB hub class restart: unchanged root, blocked child I/O, fresh descendants and worker stack reclamation (two cycles)\n");
    CHECK(!CALL(hstop,0,0,0).status);deadline=CALL(clock,0,0,0).value+5000000000;
    do {CHECK(!CALL(hinfo,&hubs,0,0).status && CALL(clock,0,0,0).value<deadline);if(hubs.manager)CHECK(!CALL(thread,THREAD_YIELD,0,0).status);}while(hubs.manager);
    CHECK(!CALL(hstop,0,0,0).status && !CALL(stop,0,0,0).status);
    do {CHECK(!CALL(info,&status,0,0).status && CALL(clock,0,0,0).value<deadline);if(status.running)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.running);
    CHECK(!status.failed && !CALL(stop,0,0,0).status && !CALL(watch,0,0,0).status);
    job->done=1;print("PASS USB hubs: nested enumeration, child hotplug, active subtree disconnect, stale refs, reattach, class restart and shutdown\n");return module_result(1,1);
failed:
    print("USB HUB probe error: ");number(error);
    const struct kernel_hub_table *hr=kernel_hub_find_table(hub,USB_HUB_TABLE_NAME);
    if(hr){const struct usb_hub_table *ht=(void *)hr->address;for(const struct usb_hub_record *p=ht->first;p;p=p->next){print("hub state/error: ");number(p->state);number(p->error);}}
    if(watch)CALL(watch,0,0,0);
    if(hstop)CALL(hstop,0,0,0);
    if(stop)CALL(stop,0,0,0);
    job->done=1;job->errors=error;return module_result(error,0);
#undef CHECK
}
