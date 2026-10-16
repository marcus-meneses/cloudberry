/* Test-only transport probes. No enumeration policy enters production modules. */
#include "../kmodules/shared/xhci.h"
#include "../kmodules/shared/threads.h"
#include "probe.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void text(const char *s) {while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));}
static void number(uint64_t v) {char d[24];unsigned n=0;do{d[n++]='0'+v%10;v/=10;}while(v);while(n)__asm__ volatile("outb %0,$0xe9"::"a"(d[--n]));text("\n");}
struct kernel_surface_result probe_xhci_call(MODULE_ARGS) {
    MODULE_UNUSED();struct probe_xhci *job=(void *)a;const struct kernel_surfaces *hub=job->hub;
    kernel_surface_call wait=kernel_surface_find(hub,"xhci.wait");
    kernel_surface_call open=kernel_surface_find(hub,"xhci.open"),close=kernel_surface_find(hub,"xhci.close"),port=kernel_surface_find(hub,"xhci.port"),reset=kernel_surface_find(hub,"xhci.reset_port"),device=kernel_surface_find(hub,"xhci.device"),endpoint=kernel_surface_find(hub,"xhci.endpoint"),transfer=kernel_surface_find(hub,"xhci.transfer"),result=kernel_surface_find(hub,"xhci.result"),cancel=kernel_surface_find(hub,"xhci.cancel"),remove=kernel_surface_find(hub,"xhci.remove"),info=kernel_surface_find(hub,"xhci.info"),thread=kernel_surface_find(hub,"threads.control"),clock=kernel_surface_find(hub,"clock.now_ns");
    struct xhci_info snapshot={0};unsigned stage=0;uint64_t token=0;
#define CHECK(x) do {if(!(x)) {stage=__LINE__;goto fail;}}while(0)
    CHECK(wait && open && close && port && reset && device && endpoint && transfer && result && cancel && remove && info && thread && clock);
    struct kernel_surface_result opened=CALL(open,job->hardware_index,&snapshot,0);job->id=opened.value;CHECK(!opened.status && snapshot.state==XHCI_ONLINE);
    struct xhci_port p={0};unsigned chosen=0;
    /* USB2 and USB3 logical connector numbers have distinct xHCI root ports. */
    for(unsigned i=1;i<=snapshot.ports;++i) {CHECK(!CALL(port,job->id,i,&p).status);if(p.protocol_major==2 && (p.status&1)) {chosen=i;break;}}
    CHECK(chosen);
    CHECK(!CALL(reset,job->id,chosen,0).status && !CALL(port,job->id,chosen,&p).status && (p.status&3)==3);
    struct host_device_request req={.port=chosen,.max_packet=8,.port_generation=p.generation};
    CHECK(!CALL(device,job->id,&req,&token).status);
    struct host_endpoint_request control_ep={.device=token,.type=HOST_CONTROL,.max_packet=64};
    CHECK(!CALL(endpoint,job->id,&control_ep,0).status);
    unsigned char descriptor[64];
    struct host_transfer_request t={.device=token,.bytes=18,.buffer=descriptor,.setup={0x80,6,0,1,0,0,18,0}};
    /* More than 255 TRBs and events: both transfer and event cycles must wrap. */
    for(unsigned iteration=0;iteration<100;++iteration) {
        for(unsigned i=0;i<sizeof(descriptor);++i)descriptor[i]=0xcc;
        struct kernel_surface_result submitted=CALL(transfer,job->id,&t,0);CHECK(!submitted.status);
        struct host_transfer_result completion;uint64_t deadline=CALL(clock,0,0,0).value+1000000000;
        CHECK(!wait(job->id,submitted.value,(uintptr_t)&completion,deadline,0,0).status);
        CHECK(completion.state==HOST_COMPLETE && completion.bytes==18);
        CHECK(!result(job->id,submitted.value,(uintptr_t)&completion,1,0,0).status && descriptor[0]==18 && descriptor[1]==1 && (descriptor[7]==8 || descriptor[7]==64) && descriptor[18]==0xcc);++job->transfers;
    }
    /* Raw SET_CONFIGURATION for the test keyboard, then exercise an interrupt
     * endpoint without interpreting its HID reports. */
    t.bytes=0;t.setup[0]=0;t.setup[1]=9;t.setup[2]=1;t.setup[3]=t.setup[6]=0;
    struct kernel_surface_result submitted=CALL(transfer,job->id,&t,0);CHECK(!submitted.status);
    struct host_transfer_result completion;uint64_t start=CALL(clock,0,0,0).value;
    do {CHECK(!result(job->id,submitted.value,(uintptr_t)&completion,0,0,0).status);CHECK(CALL(clock,0,0,0).value-start<1000000000);if(completion.state==HOST_PENDING)CHECK(!CALL(thread,THREAD_YIELD,0,0).status);}while(completion.state==HOST_PENDING);
    CHECK(completion.state==HOST_COMPLETE && !result(job->id,submitted.value,(uintptr_t)&completion,1,0,0).status);
    struct host_endpoint_request ep={.device=token,.address=0x81,.type=HOST_INTERRUPT,.max_packet=8,.interval=7};CHECK(!CALL(endpoint,job->id,&ep,0).status);
    t.endpoint=0x81;t.bytes=8;submitted=CALL(transfer,job->id,&t,0);CHECK(!submitted.status);
    CHECK(!CALL(cancel,job->id,token,0x81).status);CHECK(!result(job->id,submitted.value,(uintptr_t)&completion,1,0,0).status && completion.state==HOST_CANCELLED);
    if(job->hotplug) {
        uint64_t generation=p.generation;text("WAIT XHCI REMOVE\n");start=CALL(clock,0,0,0).value;
        do {CHECK(!CALL(port,job->id,chosen,&p).status && CALL(clock,0,0,0).value-start<5000000000);if(p.generation==generation)CHECK(!CALL(thread,THREAD_YIELD,0,0).status);}while(p.generation==generation);
        CHECK(!(p.status&1) && CALL(transfer,job->id,&t,0).status);text("WAIT XHCI ADD\n");start=CALL(clock,0,0,0).value;generation=p.generation;
        do {CHECK(!CALL(port,job->id,chosen,&p).status && CALL(clock,0,0,0).value-start<5000000000);if(p.generation==generation)CHECK(!CALL(thread,THREAD_YIELD,0,0).status);}while(p.generation==generation);
        CHECK((p.status&1) && CALL(transfer,job->id,&t,0).status);
    }
    CHECK(!CALL(remove,job->id,token,0).status && CALL(transfer,job->id,&t,0).status);
    CHECK(!CALL(info,job->id,&snapshot,0).status && snapshot.interrupts && snapshot.events>100);job->interrupts=snapshot.interrupts;job->events=snapshot.events;
    /* Close can need an IRQ-worker drain before release accepts the route. */
    start=CALL(clock,0,0,0).value;
    while(CALL(close,job->id,0,0).status) {CHECK(CALL(clock,0,0,0).value-start<1000000000 && !CALL(thread,THREAD_YIELD,0,0).status);}
    uint64_t old=job->id;opened=CALL(open,job->hardware_index,&snapshot,0);job->id=opened.value;CHECK(!opened.status && opened.value!=old);
    start=CALL(clock,0,0,0).value;while(CALL(close,job->id,0,0).status) {CHECK(CALL(clock,0,0,0).value-start<1000000000 && !CALL(thread,THREAD_YIELD,0,0).status);}
    job->done=1;return module_result(1,1);
fail:
    text("XHCI last open stage: ");number(snapshot.stage);text("XHCI last state: ");number(snapshot.state);text("XHCI last interrupts: ");number(snapshot.interrupts);text("XHCI last events: ");number(snapshot.events);
    job->errors=stage;job->done=1;return module_result(stage,0);
#undef CHECK
}
