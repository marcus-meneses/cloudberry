#include "../kmodules/shared/usb.h"
#include "../kmodules/shared/threads.h"
#include "probe.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void print(const char *s){while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));}
struct kernel_surface_result probe_usb_wait(MODULE_ARGS) {
    MODULE_UNUSED();struct probe_usb_wait *job=(void *)a;
    kernel_surface_call transfer=kernel_surface_find(job->hub,"usb.transfer"),clock=kernel_surface_find(job->hub,"clock.now_ns");
    struct usb_io io={.device={job->id,job->generation},.owner=99,.interface=0,.endpoint=0x81,.bytes=8,.buffer=job->bytes};
    struct host_transfer_result result={0};
    for(unsigned i=0;i<4;++i) {
        io.deadline_ns=CALL(clock,0,0,0).value+10000000000;
        struct kernel_surface_result r=CALL(transfer,&io,&result,0);job->status=r.status;job->state=result.state;
        if(r.status)break;
    }
    job->done=1;return module_result(1,1);
}
struct kernel_surface_result probe_usb_call(MODULE_ARGS) {
    MODULE_UNUSED();struct probe_usb *job=(void *)a;const struct kernel_surfaces *hub=job->hub;unsigned line=0;
    kernel_surface_call watch=kernel_surface_find(hub,"usb.watch");
    kernel_surface_call start=kernel_surface_find(hub,"usb.start"),stop=kernel_surface_find(hub,"usb.stop"),info=kernel_surface_find(hub,"usb.info"),device=kernel_surface_find(hub,"usb.device"),interface=kernel_surface_find(hub,"usb.interface"),configuration=kernel_surface_find(hub,"usb.configuration"),claim=kernel_surface_find(hub,"usb.claim"),release=kernel_surface_find(hub,"usb.release"),control=kernel_surface_find(hub,"usb.control"),thread=kernel_surface_find(hub,"threads.control"),clock=kernel_surface_find(hub,"clock.now_ns");
#define CHECK(x) do{if(!(x)){line=__LINE__;goto failed;}}while(0)
    CHECK(watch && start && stop && info && device && interface && configuration && claim && release && control && thread && clock);
    CHECK(!CALL(watch,1,0,0).status);
    CHECK(!CALL(start,0,0,0).status);struct usb_info status;uint64_t deadline=CALL(clock,0,0,0).value+10000000000;
    do {CHECK(!CALL(info,&status,0,0).status && !status.failed && CALL(clock,0,0,0).value<deadline);if(status.configured<3)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.configured<3);
    const struct kernel_hub_table *ref=kernel_hub_find_table(hub,USB_TABLE_NAME);CHECK(ref);
    const struct usb_table *table=(void *)ref->address;CHECK(table->magic==USB_TABLE_MAGIC && table->count==3);
    struct usb_ref keyboard={0};unsigned hid=0,storage=0,keyboard_port=0;
    kernel_surface_call cpu=kernel_surface_find(hub,"cpu.control");CHECK(cpu);
    struct usb_ref refs[3];unsigned count=0;uint64_t flags=CALL(cpu,1,0,0).value;
    for(const struct usb_device *p=table->first;p && count<3;p=p->next)refs[count++]=(struct usb_ref){p->identity.id,p->identity.generation};
    CALL(cpu,2,flags,0);CHECK(count==3);
    for(unsigned i=0;i<count;++i) {
        struct usb_device dev;struct usb_interface intf;CHECK(!CALL(device,&refs[i],&dev,0).status && dev.state==USB_CONFIGURED && dev.configuration && dev.interfaces);
        CHECK(!CALL(interface,&refs[i],0,&intf).status && !intf.owner && !intf.alternate);
        unsigned char bytes[4096];for(unsigned k=0;k<sizeof(bytes);++k)bytes[k]=0xcc;size_t actual=0;
        CHECK(configuration((uintptr_t)&refs[i],0,(uintptr_t)bytes,1,(uintptr_t)&actual,0).status && actual>9 && bytes[0]==0xcc);
        CHECK(!configuration((uintptr_t)&refs[i],0,(uintptr_t)bytes,sizeof(bytes),(uintptr_t)&actual,0).status && bytes[0]==9 && bytes[1]==2);
        if(intf.class_code==3){++hid;if(intf.protocol==1){keyboard=refs[i];keyboard_port=dev.port;}}
        if(intf.class_code==8) {
            ++storage;CHECK(!CALL(claim,&refs[i],intf.number,77).status);
            struct usb_io io={.device=refs[i],.owner=78,.interface=intf.number,.bytes=1,.buffer=bytes,.setup={0xa1,0xfe,0,0,intf.number,0,1,0}};struct host_transfer_result result;
            CHECK(CALL(control,&io,&result,0).status);io.owner=77;
            CHECK(!CALL(control,&io,&result,0).status && result.bytes==1 && bytes[0]==0);
            CHECK(!CALL(release,&refs[i],intf.number,77).status);
        }
    }
    CHECK(hid==2 && storage==1 && keyboard.id);job->devices=count;
    if(job->hotplug) {
        print("WAIT USB REMOVE\n");deadline=CALL(clock,0,0,0).value+5000000000;
        do {CHECK(!CALL(info,&status,0,0).status && !status.failed && CALL(clock,0,0,0).value<deadline);if(status.configured!=2)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.configured!=2);
        struct usb_device old;CHECK(!CALL(device,&keyboard,&old,0).status && old.state==USB_DETACHED);
        CHECK(CALL(claim,&keyboard,0,99).status);print("WAIT USB ADD\n");deadline=CALL(clock,0,0,0).value+5000000000;
        do {CHECK(!CALL(info,&status,0,0).status && !status.failed && CALL(clock,0,0,0).value<deadline);if(status.configured!=3)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.configured!=3);
        CHECK(table->count==3 && CALL(device,&keyboard,&old,0).status);
    }
    flags=CALL(cpu,1,0,0).value;
    for(const struct usb_device *p=table->first;p;p=p->next)if(p->identity.id==keyboard.id || (p->port==keyboard_port && p->state==USB_CONFIGURED))keyboard=(struct usb_ref){p->identity.id,p->identity.generation};
    CALL(cpu,2,flags,0);
    CHECK(!CALL(claim,&keyboard,0,99).status);
    struct probe_usb_wait pending={.hub=hub,.id=keyboard.id,.generation=keyboard.generation};
    kernel_surface_call create=kernel_surface_find(hub,"threads.create"),wait_entry=kernel_surface_find(hub,"probe.usb_wait");CHECK(create && wait_entry);
    struct thread_request waiting={.abi=1,.cpu=0,.stack_bytes=16384,.entry=(uintptr_t)wait_entry,.major=1};waiting.payload.arguments[0]=(uintptr_t)&pending;
    struct kernel_surface_result child=CALL(create,&waiting,0,0);CHECK(!child.status);
    deadline=CALL(clock,0,0,0).value+1000000000;struct thread_info child_info;
    do {CHECK(!CALL(thread,THREAD_INFO,child.value,&child_info).status && !pending.done && CALL(clock,0,0,0).value<deadline);if(child_info.state!=THREAD_BLOCKED)CHECK(!CALL(thread,THREAD_YIELD,0,0).status);}while(child_info.state!=THREAD_BLOCKED);
    CHECK(!CALL(stop,0,0,0).status);deadline=CALL(clock,0,0,0).value+5000000000;
    do {CHECK(!CALL(info,&status,0,0).status && CALL(clock,0,0,0).value<deadline);if(status.running)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.running);
    CHECK(!status.failed && !CALL(stop,0,0,0).status);
    do {CHECK(!CALL(thread,THREAD_INFO,child.value,&child_info).status && CALL(clock,0,0,0).value<deadline);if(child_info.state!=THREAD_EXITED)CHECK(!CALL(thread,THREAD_YIELD,0,0).status);}while(child_info.state!=THREAD_EXITED);
    CHECK(pending.done && pending.status && (pending.state==HOST_CANCELLED || pending.state==HOST_DISCONNECTED) && !CALL(thread,THREAD_REAP,child.value,0).status);
    /* Restart must enumerate even when new controller port generations begin
     * at one again; connector records must not accumulate across restarts. */
    CHECK(!CALL(start,0,0,0).status);deadline=CALL(clock,0,0,0).value+5000000000;
    do {CHECK(!CALL(info,&status,0,0).status && !status.failed && CALL(clock,0,0,0).value<deadline);if(status.configured<3)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.configured<3);
    CHECK(table->count==3 && !CALL(stop,0,0,0).status);
    do {CHECK(!CALL(info,&status,0,0).status && CALL(clock,0,0,0).value<deadline);if(status.running)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.running);
    CHECK(!status.failed && !CALL(stop,0,0,0).status && !CALL(watch,0,0,0).status);job->done=1;
    print("PASS USB core: keyboard/mouse/storage enumeration, descriptors, scoped interface control, IRQ-driven hotplug, pending-I/O shutdown and restart\n");return module_result(1,1);
failed:
    if(watch)CALL(watch,0,0,0);
    if(stop)CALL(stop,0,0,0);
    job->errors=line;job->done=1;return module_result(line,0);
#undef CHECK
}
