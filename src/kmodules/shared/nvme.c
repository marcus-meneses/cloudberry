#include "nvme.h"
#include "block.h"
#include "pci.h"
#include "threads.h"
#include "hwenum.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
#define OWNER UINT64_C(0x4e564d4500000001)
#define DEPTH 16u
#define READ_TIMEOUT_NS UINT64_C(500000000)
struct command {uint32_t op,nsid;uint64_t reserved,metadata,prp1,prp2;uint32_t cdw[6];};
struct completion {uint32_t result,reserved;uint16_t sq_head,sq_id,cid,status;};
_Static_assert(sizeof(struct command)==64,"NVMe command size");
_Static_assert(sizeof(struct completion)==16,"NVMe completion size");
struct queue {struct command *sq;volatile struct completion *cq;uint16_t tail,head,cid;unsigned phase,id;};
static struct {
    const struct kernel_surfaces *hub;struct pci_device device;struct mem_pages dma;struct irq_route route;
    struct nvme_info info;struct queue admin,io;uintptr_t registers;uint64_t stride,timeout_ns;
    int owned,busy,waiting,complete,completion_failed;uint16_t expected;
    uint64_t reader,cookie,serial,refs;
    kernel_surface_call claim,master,msix,allocate,free,release,irq_route,irq_enable,irq_release,control,clock,cpu,irq_context,dma_ready;
} state;
static uint32_t r32(size_t offset) {return *(volatile uint32_t *)(state.registers+offset);}
static void w32(size_t offset,uint32_t value) {__asm__ volatile("mfence":::"memory");*(volatile uint32_t *)(state.registers+offset)=value;__asm__ volatile("mfence":::"memory");}
static void w64(size_t offset,uint64_t value) {*(volatile uint64_t *)(state.registers+offset)=value;__asm__ volatile("mfence":::"memory");}
static int bsp(void) {return state.cpu && !CALL(state.cpu,CPU_CURRENT,0,0).value;}
static int foreground(void) {
    uint64_t flags;__asm__ volatile("pushfq;pop %0":"=r"(flags));
    return bsp() && (flags&512) && !CALL(state.cpu,CPU_DEPTH,0,0).value && !CALL(state.irq_context,0,0,0).value;
}
static uint64_t time_now(void) {struct kernel_surface_result r=CALL(state.clock,0,0,0);return r.status?UINT64_MAX:r.value;}
static int pause_wait(void) {
    struct kernel_surface_result t=CALL(state.control,THREAD_CURRENT,0,0);
    if(t.status)return 0;
    if(t.value)return !CALL(state.control,THREAD_YIELD,0,0).status;
    __asm__ volatile("pause");return 1;
}
static int ready(unsigned wanted) {
    uint64_t start=time_now();if(start==UINT64_MAX)return 0;
    for(unsigned i=0;i<2000000;++i) {
        if((r32(0x1c)&1)==wanted)return 1;
        uint64_t now=time_now();if(now==UINT64_MAX || now<start || now-start>state.timeout_ns || !pause_wait())return 0;
    }return 0;
}
static void ring(struct queue *q,int completion) {w32(0x1000+(2*q->id+(completion?1:0))*state.stride,completion?q->head:q->tail);}
static void submit(struct queue *q,struct command command) {
    command.op|=(uint32_t)(++q->cid)<<16;state.expected=q->cid;
    q->sq[q->tail]=command;q->tail=(q->tail+1)%DEPTH;ring(q,0);
}
/* Single outstanding command per queue; a stale/malformed completion fails the
 * instance rather than associating a result with the wrong request. */
static int take(struct queue *q,uint32_t *result) {
    volatile struct completion *c=&q->cq[q->head];uint16_t status=c->status;
    if((status&1)!=q->phase)return 0;
    __asm__ volatile("lfence":::"memory");
    int ok=c->cid==state.expected && c->sq_id==q->id && c->sq_head<DEPTH && !(status>>1);
    if(result)*result=c->result;
    q->head=(q->head+1)%DEPTH;if(!q->head)q->phase^=1;ring(q,1);return ok?1:-1;
}
static int admin(struct command cmd) {
    submit(&state.admin,cmd);uint64_t start=time_now();if(start==UINT64_MAX)return 0;
    for(unsigned i=0;i<2000000;++i) {
        int result=take(&state.admin,NULL);if(result)return result>0;
        uint64_t now=time_now();if(now==UINT64_MAX || now<start || now-start>state.timeout_ns || (r32(0x1c)&2) || !pause_wait())return 0;
    }return 0;
}
static int disable(void) {
    if(state.route.id) {
        struct pci_msix m={.hardware_index=state.device.hardware_index,.owner=OWNER,.masked=1,.route=state.route};
        if(CALL(state.msix,&m,0,0).status || CALL(state.irq_enable,state.route.id,OWNER,0).status)return 0;
    }
    w32(0x14,r32(0x14)&~1u);
    if(!ready(0))return 0;
    return !CALL(state.master,state.device.hardware_index,OWNER,0).status;
}
struct kernel_surface_result nvme_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)a;
    if(state.hub)return module_result(1,state.hub==hub);
    if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC)return module_result(0,0);
    state.cpu=kernel_surface_find(hub,"cpu.control");if(!bsp())return module_result(0,0);
    state.claim=kernel_surface_find(hub,"pci.claim");state.master=kernel_surface_find(hub,"pci.busmaster");state.msix=kernel_surface_find(hub,"pci.msix");state.allocate=kernel_surface_find(hub,"pci.dma_alloc");state.free=kernel_surface_find(hub,"pci.dma_free");state.release=kernel_surface_find(hub,"pci.release");
    state.irq_route=kernel_surface_find(hub,"irq.route");state.irq_enable=kernel_surface_find(hub,"irq.enable");state.irq_release=kernel_surface_find(hub,"irq.release");state.control=kernel_surface_find(hub,"threads.control");state.clock=kernel_surface_find(hub,"clock.now_ns");
    state.dma_ready=kernel_surface_find(hub,"iommu.ready");
    state.irq_context=kernel_surface_find(hub,"irq.context");
    if(!state.dma_ready || !state.irq_context || !state.claim || !state.master || !state.msix || !state.allocate || !state.free || !state.release || !state.irq_route || !state.irq_enable || !state.irq_release || !state.control || !state.clock)return module_result(0,0);
    state.hub=hub;return module_result(1,1);
}
static struct kernel_surface_result close_owned(void);
struct kernel_surface_result nvme_surface_open(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.hub || !foreground() || !b || c>=UINT32_MAX)return module_result(0,0);
    const struct kernel_hub_table *ref=kernel_hub_find_table(state.hub,"hwenum.hardware");
    if(!ref)return module_result(0,0);
    /* Enumeration owns class interpretation; never claim an arbitrary device. */
    const struct hwenum_table *hw=(const void *)ref->address;
    if(a>=hw->count || hw->entries[a].kind!=HWENUM_PCI || hw->entries[a].data.pci.class_code!=1 || hw->entries[a].data.pci.subclass!=8 || hw->entries[a].data.pci.programming_interface!=2)return module_result(0,0);
    if(__atomic_exchange_n(&state.busy,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    if(state.serial==UINT64_MAX || state.owned || CALL(state.claim,a,OWNER,&state.device).status) {__atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(0,0);}
    state.owned=1;state.info=(struct nvme_info){.hardware_index=a};state.timeout_ns=500000000;
    if(!state.device.bars[0].address || state.device.bars[0].bytes<0x1008 || !state.device.msix_count)goto fail;
    state.info.open_stage=2;state.registers=state.device.bars[0].address;
    uint64_t cap=*(volatile uint64_t *)state.registers;
    state.timeout_ns=((cap>>24)&255)*UINT64_C(500000000);if(!state.timeout_ns)state.timeout_ns=500000000;
    state.stride=UINT64_C(4)<<((cap>>32)&15);
    if((cap&65535)<DEPTH-1 || !(cap&(UINT64_C(1)<<37)) || ((cap>>48)&15) || state.device.bars[0].bytes<0x1000+4*state.stride)goto fail;
    state.info.open_stage=3;w32(0x14,r32(0x14)&~1u);if(!ready(0))goto fail;
    state.info.open_stage=4;
    if(state.allocate(a,OWNER,6,(uintptr_t)&state.dma,0,0).status)goto fail;
    state.admin=(struct queue){.sq=(void *)state.dma.address,.cq=(void *)(state.dma.address+4096),.phase=1};
    state.io=(struct queue){.sq=(void *)(state.dma.address+8192),.cq=(void *)(state.dma.address+12288),.phase=1,.id=1};
    w32(0x24,(DEPTH-1)|((DEPTH-1)<<16));w64(0x28,state.dma.physical);w64(0x30,state.dma.physical+4096);
    if(CALL(state.master,a,OWNER,1).status)goto fail;
    state.info.open_stage=5;w32(0x14,1|(6u<<16)|(4u<<20));if(!ready(1))goto fail;
    struct command identify={.op=6,.prp1=state.dma.physical+20480,.cdw={1}};
    state.info.open_stage=6;if(!admin(identify))goto fail;
    state.info.open_stage=7;identify.nsid=c?c-1:0;identify.cdw[0]=2;if(!admin(identify))goto fail;
    uint32_t nsid=*(uint32_t *)(state.dma.address+20480);if(!nsid || nsid==UINT32_MAX || (c && nsid!=c))goto fail;
    state.info.open_stage=8;identify.nsid=nsid;identify.cdw[0]=0;if(!admin(identify))goto fail;
    const unsigned char *ns=(const void *)(state.dma.address+20480);unsigned format=ns[26]&15,shift=ns[128+format*4+2];
    if((ns[26]&0xf0) || ns[29] || format>ns[25] || *(const uint16_t *)(ns+128+format*4) || shift<9 || shift>12)goto fail;
    state.info.blocks=*(const uint64_t *)ns;state.info.block_bytes=1u<<shift;state.info.namespace_id=nsid;if(!state.info.blocks)goto fail;
    state.info.open_stage=9;struct irq_request route={.kind=IRQ_MSI,.owner=OWNER,.callback=kernel_surface_find(state.hub,"nvme.interrupt"),.major=1};
    if(CALL(state.irq_route,&route,&state.route,0).status)goto fail;
    state.info.open_stage=10;struct pci_msix m={.hardware_index=a,.owner=OWNER,.masked=1,.route=state.route};if(CALL(state.msix,&m,0,0).status)goto fail;
    struct command cq={.op=5,.prp1=state.dma.physical+12288,.cdw={1|((DEPTH-1)<<16),3}};
    state.info.open_stage=11;if(!admin(cq))goto fail;
    struct command sq={.op=1,.prp1=state.dma.physical+8192,.cdw={1|((DEPTH-1)<<16),1|(1u<<16)}};
    state.info.open_stage=12;if(!admin(sq) || CALL(state.irq_enable,state.route.id,OWNER,1).status)goto fail;
    m.masked=0;if(CALL(state.msix,&m,0,0).status)goto fail;
    state.cookie=++state.serial;state.refs=0;state.info.open_stage=0;state.info.route_id=state.route.id;state.info.online=1;*(struct nvme_info *)b=state.info;__atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(1,1);
fail:
    state.info.failed=1;
    /* Cleanup is retryable and retains ownership on uncertain quiescence. */
    (void)close_owned();uintptr_t stage=state.info.open_stage;__atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(stage,0);
}
struct kernel_surface_result nvme_surface_interrupt(MODULE_ARGS) {
    MODULE_UNUSED();const struct irq_notification *n=(const void *)a;
    if(!state.hub || !bsp() || !n || n->id!=state.route.id || n->owner!=OWNER)return module_result(0,0);
    state.info.interrupts+=n->count;
    if(state.waiting) {int result=take(&state.io,NULL);if(result) {state.completion_failed=result<0;state.complete=1;CALL(state.control,THREAD_WAKE,state.reader,0);}}
    return module_result(1,1);
}
static struct kernel_surface_result transfer(uint64_t lba,uint64_t blocks,uintptr_t buffer,unsigned opcode,uint64_t cookie) {
    if(!state.hub || !foreground() || !CALL(state.control,THREAD_CURRENT,0,0).value)return module_result(0,0);
    if(__atomic_exchange_n(&state.busy,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    uint64_t completed=0;
    if((cookie && cookie!=state.cookie) || !state.info.online || state.info.failed || lba>state.info.blocks || blocks>state.info.blocks-lba || blocks>SIZE_MAX/state.info.block_bytes || (blocks && !buffer) || buffer>UINTPTR_MAX-blocks*state.info.block_bytes)goto invalid;
    state.reader=CALL(state.control,THREAD_CURRENT,0,0).value;
    uint64_t remaining=blocks;unsigned char *payload=(void *)buffer;
    do {
        unsigned count=4096/state.info.block_bytes;if(count>remaining)count=remaining;
        size_t bytes=count*state.info.block_bytes;unsigned char *bounce=(void *)(state.dma.address+16384);
        if(opcode==1)for(size_t i=0;i<bytes;++i)bounce[i]=payload[i];
        state.waiting=1;state.complete=0;state.completion_failed=0;
        struct command cmd={.op=opcode,.nsid=state.info.namespace_id};
        if(opcode){if(!count)break;cmd.prp1=state.dma.physical+16384;cmd.cdw[0]=(uint32_t)lba;cmd.cdw[1]=lba>>32;cmd.cdw[2]=count-1;}
        uint64_t start=time_now();if(start==UINT64_MAX || start>UINT64_MAX-READ_TIMEOUT_NS)goto fail;
        submit(&state.io,cmd);
        while(!state.complete) {
            uint64_t now=time_now();
            if(now==UINT64_MAX || now<start || now-start>READ_TIMEOUT_NS || (r32(0x1c)&2)){++state.info.timeouts;goto fail;}
            if(CALL(state.control,THREAD_BLOCK_UNTIL,start+READ_TIMEOUT_NS,0).status)goto fail;
        }
        state.waiting=0;if(state.completion_failed || CALL(state.dma_ready,state.device.hardware_index,OWNER,0).status)goto fail;
        if(opcode==2){for(size_t i=0;i<bytes;++i)payload[i]=bounce[i];++state.info.reads;}
        if(bytes){payload+=bytes;}lba+=count;remaining-=count;completed+=bytes;
    }while(remaining);
    state.waiting=0;__atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(completed,1);
fail:
    state.waiting=0;state.info.failed=1;state.info.online=0;(void)disable();
invalid:
    __atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(completed,0);
}
struct kernel_surface_result nvme_surface_read(MODULE_ARGS){MODULE_UNUSED();return transfer(a,b,c,2,0);}
struct kernel_surface_result nvme_surface_block(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground() || !a || !block_capacity(b) || __atomic_exchange_n(&state.busy,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    if(!state.info.online || state.info.failed){__atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(0,0);}
    struct block_device disk={.major=1,.minor=1,.block_bytes=state.info.block_bytes,.max_transfer_bytes=4096,.blocks=state.info.blocks,.cookie=state.cookie,
        .read=kernel_surface_find(state.hub,"nvme.block_read"),.control=kernel_surface_find(state.hub,"nvme.control"),.write=kernel_surface_find(state.hub,"nvme.write"),.flush=kernel_surface_find(state.hub,"nvme.flush")};
    disk.root_read=disk.read;disk.root_cookie=disk.cookie;block_export((void *)a,&disk,b);__atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(disk.cookie,1);
}
static int block_request(uintptr_t cookie,uint64_t lba,uint64_t count,uintptr_t buffer,size_t capacity){
    return foreground() && cookie && cookie==state.cookie && state.owned && state.info.online && !state.info.failed && lba<=state.info.blocks && count<=state.info.blocks-lba && count<=4096/state.info.block_bytes && capacity>=count*state.info.block_bytes && (!count || buffer) && buffer<=UINTPTR_MAX-count*state.info.block_bytes;
}
struct kernel_surface_result nvme_surface_block_read(MODULE_ARGS){MODULE_UNUSED();return block_request(a,b,c,d,e)?transfer(b,c,d,2,a):module_result(0,0);}
struct kernel_surface_result nvme_surface_write(MODULE_ARGS){MODULE_UNUSED();return block_request(a,b,c,d,e)?transfer(b,c,d,1,a):module_result(0,0);}
struct kernel_surface_result nvme_surface_flush(MODULE_ARGS){MODULE_UNUSED();return block_request(a,0,0,0,0)?transfer(0,0,0,0,a):module_result(0,0);}
struct kernel_surface_result nvme_surface_control(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground() || __atomic_exchange_n(&state.busy,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    int valid=a && a==state.cookie && state.owned;
    int ok=valid && (b==BLOCK_RETAIN?(state.info.online && !state.info.failed && state.refs<UINT64_MAX):(b==BLOCK_RELEASE && state.refs));
    if(ok){if(b==BLOCK_RETAIN)++state.refs;else --state.refs;}__atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(1,ok);
}
struct kernel_surface_result nvme_surface_info(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.hub || !bsp() || !a)return module_result(0,0);
    uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;
    *(struct nvme_info *)a=state.info;CALL(state.cpu,CPU_LEAVE,flags,0);return module_result(1,1);
}
static struct kernel_surface_result close_owned(void) {
    state.info.online=0;
    if(state.registers && !disable())goto fail;
    if(state.route.id) {
        if(CALL(state.irq_release,state.route.id,OWNER,1).status)goto fail;
        state.route=(struct irq_route){0};
    }
    if(state.dma.id) {
        if(CALL(state.free,state.device.hardware_index,OWNER,state.dma.id).status)goto fail;
        state.dma=(struct mem_pages){0};
    }
    if(CALL(state.release,state.device.hardware_index,OWNER,1).status)goto fail;
    state.owned=0;state.registers=0;state.cookie=0;return module_result(1,1);
fail:
    state.info.failed=1;return module_result(0,0);
}

struct kernel_surface_result nvme_surface_close(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.hub || !foreground() || __atomic_exchange_n(&state.busy,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    struct kernel_surface_result result=state.owned && !state.refs?close_owned():module_result(0,0);
    __atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return result;
}
