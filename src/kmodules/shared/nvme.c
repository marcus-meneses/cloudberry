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
struct controller {
    struct controller *next;struct pci_device device;struct mem_pages dma;struct irq_route route;
    struct nvme_info info;struct queue admin,io;uintptr_t registers;uint64_t stride,timeout_ns,owner;
    int owned,busy,waiting,complete,completion_failed;uint16_t expected;uint64_t reader;
};
struct namespace {struct namespace *next;struct controller *controller;struct nvme_info info;uint64_t cookie,refs;};
static struct {
    const struct kernel_surfaces *hub;uintptr_t heap;struct controller *controllers;struct namespace *namespaces;
    uint64_t serial,controller_serial;unsigned management;
    kernel_surface_call claim,master,msix,allocate,free,release,irq_route,irq_enable,irq_release,control,clock,cpu,irq_context,dma_ready,alloc,drop;
} state;
static uint32_t r32(struct controller *s,size_t offset) {return *(volatile uint32_t *)(s->registers+offset);}
static void w32(struct controller *s,size_t offset,uint32_t value) {__asm__ volatile("mfence":::"memory");*(volatile uint32_t *)(s->registers+offset)=value;__asm__ volatile("mfence":::"memory");}
static void w64(struct controller *s,size_t offset,uint64_t value) {*(volatile uint64_t *)(s->registers+offset)=value;__asm__ volatile("mfence":::"memory");}
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
static int ready(struct controller *s,unsigned wanted) {
    uint64_t start=time_now();if(start==UINT64_MAX)return 0;
    for(unsigned i=0;i<2000000;++i) {
        if((r32(s,0x1c)&1)==wanted)return 1;
        uint64_t now=time_now();if(now==UINT64_MAX || now<start || now-start>s->timeout_ns || !pause_wait())return 0;
    }return 0;
}
static void ring(struct controller *s,struct queue *q,int completion) {w32(s,0x1000+(2*q->id+(completion?1:0))*s->stride,completion?q->head:q->tail);}
static void submit(struct controller *s,struct queue *q,struct command command) {
    command.op|=(uint32_t)(++q->cid)<<16;s->expected=q->cid;
    q->sq[q->tail]=command;q->tail=(q->tail+1)%DEPTH;ring(s,q,0);
}
/* Single outstanding command per queue; a stale/malformed completion fails the
 * instance rather than associating a result with the wrong request. */
static int take(struct controller *s,struct queue *q,uint32_t *result) {
    volatile struct completion *c=&q->cq[q->head];uint16_t status=c->status;
    if((status&1)!=q->phase)return 0;
    __asm__ volatile("lfence":::"memory");
    int ok=c->cid==s->expected && c->sq_id==q->id && c->sq_head<DEPTH && !(status>>1);
    if(result)*result=c->result;
    q->head=(q->head+1)%DEPTH;if(!q->head)q->phase^=1;ring(s,q,1);return ok?1:-1;
}
static int admin(struct controller *s,struct command cmd) {
    submit(s,&s->admin,cmd);uint64_t start=time_now();if(start==UINT64_MAX)return 0;
    for(unsigned i=0;i<2000000;++i) {
        int result=take(s,&s->admin,NULL);if(result)return result>0;
        uint64_t now=time_now();if(now==UINT64_MAX || now<start || now-start>s->timeout_ns || (r32(s,0x1c)&2) || !pause_wait())return 0;
    }return 0;
}
static int disable(struct controller *s) {
    if(s->route.id) {
        struct pci_msix m={.hardware_index=s->device.hardware_index,.owner=s->owner,.masked=1,.route=s->route};
        if(CALL(state.msix,&m,0,0).status || CALL(state.irq_enable,s->route.id,s->owner,0).status)return 0;
    }
    w32(s,0x14,r32(s,0x14)&~1u);
    if(!ready(s,0))return 0;
    return !CALL(state.master,s->device.hardware_index,s->owner,0).status;
}
struct kernel_surface_result nvme_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)a;
    if(state.hub)return module_result(1,state.hub==hub);
    if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC || !b)return module_result(0,0);
    state.cpu=kernel_surface_find(hub,"cpu.control");if(!bsp())return module_result(0,0);
    state.claim=kernel_surface_find(hub,"pci.claim");state.master=kernel_surface_find(hub,"pci.busmaster");state.msix=kernel_surface_find(hub,"pci.msix");state.allocate=kernel_surface_find(hub,"pci.dma_alloc");state.free=kernel_surface_find(hub,"pci.dma_free");state.release=kernel_surface_find(hub,"pci.release");
    state.irq_route=kernel_surface_find(hub,"irq.route");state.irq_enable=kernel_surface_find(hub,"irq.enable");state.irq_release=kernel_surface_find(hub,"irq.release");state.control=kernel_surface_find(hub,"threads.control");state.clock=kernel_surface_find(hub,"clock.now_ns");
    state.dma_ready=kernel_surface_find(hub,"iommu.ready");
    state.irq_context=kernel_surface_find(hub,"irq.context");
    if(!state.dma_ready || !state.irq_context || !state.claim || !state.master || !state.msix || !state.allocate || !state.free || !state.release || !state.irq_route || !state.irq_enable || !state.irq_release || !state.control || !state.clock)return module_result(0,0);
    state.alloc=kernel_surface_find(hub,"mem.calloc");state.drop=kernel_surface_find(hub,"mem.free");
    if(!state.alloc || !state.drop)return module_result(0,0);
    state.hub=hub;state.heap=b;return module_result(1,1);
}
static struct kernel_surface_result close_owned(struct controller *s) {
    s->info.online=0;
    if(s->registers && !disable(s))goto fail;
    if(s->route.id) {
        if(CALL(state.irq_release,s->route.id,s->owner,1).status)goto fail;
        s->route=(struct irq_route){0};
    }
    if(s->dma.id) {
        if(CALL(state.free,s->device.hardware_index,s->owner,s->dma.id).status)goto fail;
        s->dma=(struct mem_pages){0};
    }
    if(CALL(state.release,s->device.hardware_index,s->owner,1).status)goto fail;
    s->owned=0;s->registers=0;return module_result(1,1);
fail:
    s->info.failed=1;return module_result(0,0);
}

static int open_controller(struct controller *s){
    uint64_t a=s->info.hardware_index;
    if(CALL(state.claim,a,s->owner,&s->device).status)return 0;
    s->owned=1;s->info=(struct nvme_info){.hardware_index=a};s->timeout_ns=500000000;
    if(!s->device.bars[0].address || s->device.bars[0].bytes<0x1008 || !s->device.msix_count)goto fail;
    s->info.open_stage=2;s->registers=s->device.bars[0].address;
    uint64_t cap=*(volatile uint64_t *)s->registers;
    s->timeout_ns=((cap>>24)&255)*UINT64_C(500000000);if(!s->timeout_ns)s->timeout_ns=500000000;
    s->stride=UINT64_C(4)<<((cap>>32)&15);
    if((cap&65535)<DEPTH-1 || !(cap&(UINT64_C(1)<<37)) || ((cap>>48)&15) || s->device.bars[0].bytes<0x1000+4*s->stride)goto fail;
    s->info.open_stage=3;w32(s,0x14,r32(s,0x14)&~1u);if(!ready(s,0))goto fail;
    s->info.open_stage=4;
    if(state.allocate(a,s->owner,6,(uintptr_t)&s->dma,0,0).status)goto fail;
    s->admin=(struct queue){.sq=(void *)s->dma.address,.cq=(void *)(s->dma.address+4096),.phase=1};
    s->io=(struct queue){.sq=(void *)(s->dma.address+8192),.cq=(void *)(s->dma.address+12288),.phase=1,.id=1};
    w32(s,0x24,(DEPTH-1)|((DEPTH-1)<<16));w64(s,0x28,s->dma.physical);w64(s,0x30,s->dma.physical+4096);
    if(CALL(state.master,a,s->owner,1).status)goto fail;
    s->info.open_stage=5;w32(s,0x14,1|(6u<<16)|(4u<<20));if(!ready(s,1))goto fail;
    struct command identify={.op=6,.prp1=s->dma.physical+20480,.cdw={1}};
    s->info.open_stage=6;if(!admin(s,identify))goto fail;
    s->info.open_stage=9;struct irq_request route={.kind=IRQ_MSI,.owner=s->owner,.callback=kernel_surface_find(state.hub,"nvme.interrupt"),.major=1,.context=(uintptr_t)s};
    if(CALL(state.irq_route,&route,&s->route,0).status)goto fail;
    s->info.open_stage=10;struct pci_msix m={.hardware_index=a,.owner=s->owner,.masked=1,.route=s->route};if(CALL(state.msix,&m,0,0).status)goto fail;
    struct command cq={.op=5,.prp1=s->dma.physical+12288,.cdw={1|((DEPTH-1)<<16),3}};
    s->info.open_stage=11;if(!admin(s,cq))goto fail;
    struct command sq={.op=1,.prp1=s->dma.physical+8192,.cdw={1|((DEPTH-1)<<16),1|(1u<<16)}};
    s->info.open_stage=12;if(!admin(s,sq) || CALL(state.irq_enable,s->route.id,s->owner,1).status)goto fail;
    m.masked=0;if(CALL(state.msix,&m,0,0).status)goto fail;
    s->info.online=1;s->info.open_stage=0;s->info.route_id=s->route.id;return 1;
fail:s->info.failed=1;(void)close_owned(s);return 0;
}

static struct namespace *find(uint64_t cookie){
    struct namespace *only=NULL;
    for(struct namespace *n=state.namespaces;n;n=n->next){if(cookie && n->cookie==cookie)return n;if(!cookie){if(only)return NULL;only=n;}}
    return cookie?NULL:only;
}
static struct namespace *lock_instance(uint64_t cookie){
    uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;struct namespace *n=find(cookie);
    if(n && __atomic_exchange_n(&n->controller->busy,1,__ATOMIC_ACQUIRE))n=NULL;
    CALL(state.cpu,CPU_LEAVE,flags,0);return n;
}
static void *alloc(size_t bytes){struct kernel_surface_result r=CALL(state.alloc,state.heap,1,bytes);return r.status?NULL:(void *)r.value;}
static void drop(void *p){CALL(state.drop,state.heap,p,0);}
static void snapshot(struct namespace *n,struct nvme_info *out){*out=n->info;out->hardware_index=n->controller->info.hardware_index;out->reads=n->controller->info.reads;out->interrupts=n->controller->info.interrupts;out->timeouts=n->controller->info.timeouts;out->route_id=n->controller->route.id;out->online=n->controller->info.online && !n->controller->info.failed;out->failed=n->controller->info.failed;out->open_stage=n->controller->info.open_stage;}
static void forget_namespace(struct namespace *n){struct namespace **p=&state.namespaces;while(*p!=n)p=&(*p)->next;*p=n->next;drop(n);}
static void forget_controller(struct controller *s){struct controller **p=&state.controllers;while(*p!=s)p=&(*p)->next;*p=s->next;drop(s);}
struct kernel_surface_result nvme_surface_open(MODULE_ARGS){
    MODULE_UNUSED();if(!state.hub || !foreground() || !b || c>=UINT32_MAX || state.serial==UINT64_MAX || __atomic_exchange_n(&state.management,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    if(d)*(uint64_t *)d=0;
    uintptr_t error=0;struct namespace *n=NULL;struct controller *s=NULL;int fresh=0;
    const struct kernel_hub_table *ref=kernel_hub_find_table(state.hub,HWENUM_TABLE_NAME);if(!ref)goto done;
    const struct hwenum_table *hw=(const void *)ref->address;
    if(a>=hw->count || hw->entries[a].kind!=HWENUM_PCI || hw->entries[a].data.pci.class_code!=1 || hw->entries[a].data.pci.subclass!=8 || hw->entries[a].data.pci.programming_interface!=2)goto done;
    n=alloc(sizeof(*n));if(!n)goto done;n->cookie=++state.serial;if(d)*(uint64_t *)d=n->cookie;
    for(s=state.controllers;s;s=s->next)if(s->info.hardware_index==a)break;
    if(s){if(s->info.failed || !s->info.online || __atomic_exchange_n(&s->busy,1,__ATOMIC_ACQUIRE))goto done;}
    else {
        if(state.controller_serial>UINT64_MAX-OWNER || !(s=alloc(sizeof(*s))))goto done;
        s->owner=OWNER+state.controller_serial++;s->info.hardware_index=a;s->busy=1;s->next=state.controllers;state.controllers=s;fresh=1;
        if(!open_controller(s)){error=s->info.open_stage;if(s->owned){n->controller=s;n->next=state.namespaces;state.namespaces=n;snapshot(n,(void *)b);n=NULL;s->busy=0;}else forget_controller(s);goto done;}
    }
    n->controller=s;
    struct command identify={.op=6,.nsid=c?c-1:0,.prp1=s->dma.physical+20480,.cdw={2}};
    if(!admin(s,identify)){s->info.failed=1;s->info.online=0;(void)disable(s);goto failed;}
    uint32_t nsid=*(uint32_t *)(s->dma.address+20480);if(!nsid || nsid==UINT32_MAX || (c && nsid!=c))goto failed;
    for(struct namespace *other=state.namespaces;other;other=other->next)if(other->controller==s && other->info.namespace_id==nsid)goto failed;
    identify.nsid=nsid;identify.cdw[0]=0;if(!admin(s,identify)){s->info.failed=1;s->info.online=0;(void)disable(s);goto failed;}
    const unsigned char *ns=(const void *)(s->dma.address+20480);unsigned format=ns[26]&15,shift=ns[128+format*4+2];
    if((ns[26]&0xf0) || ns[29] || format>ns[25] || *(const uint16_t *)(ns+128+format*4) || shift<9 || shift>12)goto failed;
    n->info.blocks=*(const uint64_t *)ns;n->info.block_bytes=1u<<shift;n->info.namespace_id=nsid;
    if(!n->info.blocks || n->info.blocks>UINT64_MAX/n->info.block_bytes)goto failed;
    uint64_t cookie=n->cookie;n->next=state.namespaces;state.namespaces=n;snapshot(n,(void *)b);__atomic_store_n(&s->busy,0,__ATOMIC_RELEASE);__atomic_store_n(&state.management,0,__ATOMIC_RELEASE);return module_result(cookie,1);
failed:
    if(fresh){struct kernel_surface_result closed=close_owned(s);if(!closed.status)forget_controller(s);else{s->busy=0;n->controller=s;n->next=state.namespaces;state.namespaces=n;snapshot(n,(void *)b);n=NULL;}}
    else __atomic_store_n(&s->busy,0,__ATOMIC_RELEASE);
done:if(n)drop(n);__atomic_store_n(&state.management,0,__ATOMIC_RELEASE);return module_result(error,0);
}
struct kernel_surface_result nvme_surface_interrupt(MODULE_ARGS){
    MODULE_UNUSED();const struct irq_notification *n=(const void *)a;struct controller *s=state.controllers;
    while(s && (uintptr_t)s!=b)s=s->next;
    if(!state.hub || !bsp() || !n || !s || n->id!=s->route.id || n->owner!=s->owner)return module_result(0,0);
    s->info.interrupts+=n->count;if(s->waiting){int result=take(s,&s->io,NULL);if(result){s->completion_failed=result<0;s->complete=1;CALL(state.control,THREAD_WAKE,s->reader,0);}}return module_result(1,1);
}
static struct kernel_surface_result transfer(uint64_t lba,uint64_t blocks,uintptr_t buffer,unsigned opcode,uint64_t cookie,int bounded,size_t capacity) {
    if(!state.hub || !foreground() || !CALL(state.control,THREAD_CURRENT,0,0).value)return module_result(0,0);
    struct namespace *n=lock_instance(cookie);if(!n)return module_result(0,0);struct controller *s=n->controller;
    uint64_t completed=0;
    if(!n->info.block_bytes || (bounded && blocks>4096/n->info.block_bytes) || !s->info.online || s->info.failed || lba>n->info.blocks || blocks>n->info.blocks-lba || blocks>SIZE_MAX/n->info.block_bytes || capacity<blocks*n->info.block_bytes || (blocks && !buffer) || buffer>UINTPTR_MAX-blocks*n->info.block_bytes)goto invalid;
    s->reader=CALL(state.control,THREAD_CURRENT,0,0).value;
    uint64_t remaining=blocks;unsigned char *payload=(void *)buffer;
    do {
        unsigned count=4096/n->info.block_bytes;if(count>remaining)count=remaining;
        size_t bytes=count*n->info.block_bytes;unsigned char *bounce=(void *)(s->dma.address+16384);
        if(opcode==1)for(size_t i=0;i<bytes;++i)bounce[i]=payload[i];
        s->waiting=1;s->complete=0;s->completion_failed=0;
        struct command cmd={.op=opcode,.nsid=n->info.namespace_id};
        if(opcode){if(!count)break;cmd.prp1=s->dma.physical+16384;cmd.cdw[0]=(uint32_t)lba;cmd.cdw[1]=lba>>32;cmd.cdw[2]=count-1;}
        uint64_t start=time_now();if(start==UINT64_MAX || start>UINT64_MAX-READ_TIMEOUT_NS)goto fail;
        submit(s,&s->io,cmd);
        while(!s->complete) {
            uint64_t now=time_now();
            if(now==UINT64_MAX || now<start || now-start>READ_TIMEOUT_NS || (r32(s,0x1c)&2)){++s->info.timeouts;goto fail;}
            if(CALL(state.control,THREAD_BLOCK_UNTIL,start+READ_TIMEOUT_NS,0).status)goto fail;
        }
        s->waiting=0;if(s->completion_failed || CALL(state.dma_ready,s->device.hardware_index,s->owner,0).status)goto fail;
        if(opcode==2){for(size_t i=0;i<bytes;++i)payload[i]=bounce[i];++s->info.reads;}
        if(bytes){payload+=bytes;}lba+=count;remaining-=count;completed+=bytes;
    }while(remaining);
    s->waiting=0;__atomic_store_n(&s->busy,0,__ATOMIC_RELEASE);return module_result(completed,1);
fail:
    s->waiting=0;s->info.failed=1;s->info.online=0;(void)disable(s);
invalid:
    __atomic_store_n(&s->busy,0,__ATOMIC_RELEASE);return module_result(completed,0);
}

struct kernel_surface_result nvme_surface_read(MODULE_ARGS){MODULE_UNUSED();return transfer(a,b,c,2,d,0,SIZE_MAX);}
struct kernel_surface_result nvme_surface_block(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground() || !a || !block_capacity(b))return module_result(0,0);struct namespace *n=lock_instance(c);if(!n)return module_result(0,0);
    struct controller *s=n->controller;if(!s->info.online || s->info.failed){s->busy=0;return module_result(0,0);}
    struct block_device disk={.major=1,.minor=1,.block_bytes=n->info.block_bytes,.max_transfer_bytes=4096,.blocks=n->info.blocks,.cookie=n->cookie,.read=kernel_surface_find(state.hub,"nvme.block_read"),.control=kernel_surface_find(state.hub,"nvme.control"),.write=kernel_surface_find(state.hub,"nvme.write"),.flush=kernel_surface_find(state.hub,"nvme.flush")};disk.root_read=disk.read;disk.root_cookie=disk.cookie;block_export((void *)a,&disk,b);uint64_t cookie=n->cookie;s->busy=0;return module_result(cookie,1);
}
static struct kernel_surface_result block_io(uint64_t cookie,uint64_t lba,uint64_t count,uintptr_t data,size_t capacity,unsigned op){return cookie?transfer(lba,count,data,op,cookie,1,capacity):module_result(0,0);}
struct kernel_surface_result nvme_surface_block_read(MODULE_ARGS){MODULE_UNUSED();return block_io(a,b,c,d,e,2);}
struct kernel_surface_result nvme_surface_write(MODULE_ARGS){MODULE_UNUSED();return block_io(a,b,c,d,e,1);}
struct kernel_surface_result nvme_surface_flush(MODULE_ARGS){MODULE_UNUSED();return block_io(a,0,0,0,0,0);}
struct kernel_surface_result nvme_surface_control(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground() || !a)return module_result(0,0);struct namespace *n=lock_instance(a);if(!n)return module_result(0,0);
    uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;int ok=b==BLOCK_RETAIN?(n->controller->info.online && !n->controller->info.failed && n->refs<UINT64_MAX):(b==BLOCK_RELEASE && n->refs);if(ok){if(b==BLOCK_RETAIN)++n->refs;else --n->refs;}CALL(state.cpu,CPU_LEAVE,flags,0);n->controller->busy=0;return module_result(1,ok);
}
struct kernel_surface_result nvme_surface_info(MODULE_ARGS){MODULE_UNUSED();if(!state.hub || !bsp() || !a)return module_result(0,0);struct namespace *n=lock_instance(b);if(!n)return module_result(0,0);uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;snapshot(n,(void *)a);CALL(state.cpu,CPU_LEAVE,flags,0);n->controller->busy=0;return module_result(1,1);}
struct kernel_surface_result nvme_surface_close(MODULE_ARGS){
    MODULE_UNUSED();if(!state.hub || !foreground() || __atomic_exchange_n(&state.management,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    int ok=0;struct namespace *n=lock_instance(a);if(!n)goto done;struct controller *s=n->controller;if(n->refs){s->busy=0;goto done;}
    unsigned siblings=0;for(struct namespace *other=state.namespaces;other;other=other->next)if(other->controller==s && other!=n)++siblings;
    if(siblings){forget_namespace(n);__atomic_store_n(&s->busy,0,__ATOMIC_RELEASE);ok=1;}
    else {struct kernel_surface_result closed=close_owned(s);if(!closed.status){forget_namespace(n);forget_controller(s);ok=1;}else __atomic_store_n(&s->busy,0,__ATOMIC_RELEASE);}
done:__atomic_store_n(&state.management,0,__ATOMIC_RELEASE);return module_result(1,ok);
}
