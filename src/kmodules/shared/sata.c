#include "sata.h"
#include "pci.h"
#include "threads.h"
#include "hwenum.h"
#include "block-util.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
#define OWNER UINT64_C(0x5341544100000001)
#define TIMEOUT UINT64_C(1000000000)
struct command_header {uint16_t flags,prdt;volatile uint32_t bytes;uint64_t table;uint32_t reserved[4];};
_Static_assert(sizeof(struct command_header)==32,"AHCI header");
struct controller {
    struct controller *next;struct pci_device device;struct irq_route route;
    uintptr_t regs;uint64_t owner;unsigned owned,taken,msi_programmed,busy,failed;
};
struct port {
    struct port *next;struct controller *controller;struct mem_pages dma;struct sata_info info;
    uintptr_t port;uint64_t refs,waiter;unsigned waiting,complete,command_error;
};
static struct {
    const struct kernel_surfaces *hub;uintptr_t heap;struct controller *controllers;struct port *ports;
    uint64_t serial,controller_serial;unsigned management;
    kernel_surface_call cpu,thread,clock,context,claim,master,allocate,free,release,ready,msi,route_call,enable,route_free,alloc,drop;
} state;
static uint32_t read32(uintptr_t base,unsigned off){return *(volatile uint32_t *)(base+off);}
static void write32(uintptr_t base,unsigned off,uint32_t value){__asm__ volatile("mfence":::"memory");*(volatile uint32_t *)(base+off)=value;__asm__ volatile("mfence":::"memory");}
static int foreground(void){uint64_t flags;__asm__ volatile("pushfq;pop %0":"=r"(flags));return state.hub && (flags&512) && !CALL(state.cpu,CPU_CURRENT,0,0).value && !CALL(state.cpu,CPU_DEPTH,0,0).value && !CALL(state.context,0,0,0).value && CALL(state.thread,THREAD_CURRENT,0,0).value;}
static uint64_t now(void){struct kernel_surface_result r=CALL(state.clock,0,0,0);return r.status?UINT64_MAX:r.value;}
/* Startup/reset only: no device notification exists yet. Sleep rather than spin. */
static int wait_bits(uintptr_t base,unsigned off,uint32_t mask,uint32_t wanted){
    uint64_t start=now();if(start==UINT64_MAX || start>UINT64_MAX-TIMEOUT)return 0;
    for(;;){if((read32(base,off)&mask)==wanted)return 1;uint64_t t=now();if(t==UINT64_MAX || t<start || t-start>=TIMEOUT || t>UINT64_MAX-1000000 || CALL(state.thread,THREAD_BLOCK_UNTIL,t+1000000,0).status)return 0;}
}
struct kernel_surface_result sata_surface_init(MODULE_ARGS){
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)a;if(state.hub)return module_result(1,hub==state.hub);
    if(!hub || !b || hub->magic!=KERNEL_SURFACES_MAGIC)return module_result(0,0);
#define BIND(field,name) state.field=kernel_surface_find(hub,name);if(!state.field)return module_result(0,0)
    BIND(cpu,"cpu.control");BIND(thread,"threads.control");BIND(clock,"clock.now_ns");BIND(context,"irq.context");
    BIND(claim,"pci.claim");BIND(master,"pci.busmaster");BIND(allocate,"pci.dma_alloc");BIND(free,"pci.dma_free");BIND(release,"pci.release");BIND(ready,"iommu.ready");
    BIND(msi,"pci.msi");BIND(route_call,"irq.route");BIND(enable,"irq.enable");BIND(route_free,"irq.release");
    BIND(alloc,"mem.calloc");BIND(drop,"mem.free");
#undef BIND
    if(CALL(state.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);
    state.heap=b;state.hub=hub;return module_result(1,1);
}
/* One non-NCQ slot, preallocated command list/FIS/table/data. No caller pointer
 * reaches hardware. Flush has no PRDT; data direction belongs to command header. */
static int command(struct port *s,unsigned opcode,uint64_t lba,unsigned sectors,unsigned bytes,int write){
    struct command_header *header=(void *)s->dma.address;uint8_t *table=(void *)(s->dma.address+4096);
    for(unsigned i=0;i<256;++i)table[i]=0;
    *header=(struct command_header){.flags=5|(write?64:0),.prdt=bytes?1:0,.table=s->dma.physical+4096};
    table[0]=0x27;table[1]=0x80;table[2]=opcode;table[7]=0x40;
    table[4]=lba;table[5]=lba>>8;table[6]=lba>>16;table[8]=lba>>24;table[9]=lba>>32;table[10]=lba>>40;table[12]=sectors;table[13]=sectors>>8;
    if(bytes){uint32_t *prdt=(void *)(table+128);prdt[0]=(uint32_t)(s->dma.physical+8192);prdt[1]=(s->dma.physical+8192)>>32;prdt[3]=(bytes-1)|0x80000000u;}
    if(!wait_bits(s->port,0x20,0x88,0))return 0;
    write32(s->port,0x10,UINT32_MAX);write32(s->controller->regs,8,1u<<s->info.port);
    s->waiter=CALL(state.thread,THREAD_CURRENT,0,0).value;s->waiting=1;s->complete=0;s->command_error=0;
    uint64_t start=now();if(start==UINT64_MAX || start>UINT64_MAX-TIMEOUT){s->waiting=0;return 0;}
    write32(s->port,0x38,1);
    while(!s->complete){uint64_t t=now();if(t==UINT64_MAX || t<start || t-start>=TIMEOUT){++s->info.timeouts;s->waiting=0;return 0;}if(CALL(state.thread,THREAD_BLOCK_UNTIL,start+TIMEOUT,0).status){s->waiting=0;return 0;}}
    s->waiting=0;__asm__ volatile("lfence":::"memory");
    return !s->command_error && !(read32(s->port,0x38)&1) && !(read32(s->port,0x20)&0x89) && header->bytes==bytes && !CALL(state.ready,s->controller->device.hardware_index,s->controller->owner,0).status;
}
static void *alloc(size_t bytes){struct kernel_surface_result r=CALL(state.alloc,state.heap,1,bytes);return r.status?NULL:(void *)r.value;}
static void drop(void *p){CALL(state.drop,state.heap,p,0);}
static struct port *find(uint64_t cookie){struct port *only=NULL;for(struct port *p=state.ports;p;p=p->next){if(cookie && p->info.cookie==cookie)return p;if(!cookie){if(only)return NULL;only=p;}}return cookie?NULL:only;}
static struct port *lock_instance(uint64_t cookie){uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;struct port *s=find(cookie);if(s && __atomic_exchange_n(&s->controller->busy,1,__ATOMIC_ACQUIRE))s=NULL;CALL(state.cpu,CPU_LEAVE,flags,0);return s;}
static void forget_port(struct port *s){struct port **p=&state.ports;while(*p!=s)p=&(*p)->next;*p=s->next;drop(s);}
static void forget_controller(struct controller *s){struct controller **p=&state.controllers;while(*p!=s)p=&(*p)->next;*p=s->next;drop(s);}
static int stop(struct port *s){if(!s->port)return 1;write32(s->port,0x14,0);write32(s->port,0x18,read32(s->port,0x18)&~1u);if(!wait_bits(s->port,0x18,0x8000,0))return 0;write32(s->port,0x18,read32(s->port,0x18)&~0x10u);return wait_bits(s->port,0x18,0x4000,0);}
static int reset(struct controller *s){if(!s->regs)return 1;write32(s->regs,4,0x80000001u);if(!wait_bits(s->regs,4,1,0))return 0;write32(s->regs,4,0x80000000u);return !CALL(state.master,s->device.hardware_index,s->owner,0).status;}
static void poison(struct controller *s){s->failed=1;for(struct port *p=state.ports;p;p=p->next)if(p->controller==s){p->info.online=0;p->info.failed=1;} (void)reset(s);}
static int close_controller(struct controller *s){
    if(s->route.id && s->msi_programmed){struct pci_msix m={.hardware_index=s->device.hardware_index,.owner=s->owner,.masked=1,.route=s->route};if(CALL(state.msi,&m,0,0).status || CALL(state.enable,s->route.id,s->owner,0).status)return 0;}
    if(s->taken && !reset(s))return 0;
    if(s->route.id){if(CALL(state.route_free,s->route.id,s->owner,1).status)return 0;s->route=(struct irq_route){0};}
    return 1;
}
static int free_dma(struct port *s,int resume){
    struct controller *c=s->controller;
    if(CALL(state.master,c->device.hardware_index,c->owner,0).status)return 0;
    if(s->dma.id){if(CALL(state.free,c->device.hardware_index,c->owner,s->dma.id).status){if(resume)(void)CALL(state.master,c->device.hardware_index,c->owner,1);return 0;}s->dma=(struct mem_pages){0};}
    if(resume && CALL(state.master,c->device.hardware_index,c->owner,1).status){poison(c);return 0;}return 1;
}
static int open_controller(struct controller *s,uint64_t index){
    if(CALL(state.claim,index,s->owner,&s->device).status)return 0;
    s->owned=1;
    if(!s->device.bars[5].address || s->device.bars[5].bytes<0x180)return 0;
    s->regs=s->device.bars[5].address;
    if(read32(s->regs,0x24)&1){write32(s->regs,0x28,read32(s->regs,0x28)|2);if(!wait_bits(s->regs,0x28,0x11,0))return 0;}
    s->taken=1;if(!reset(s) || !(read32(s->regs,0)&0x80000000u))return 0;
    struct irq_request q={.kind=IRQ_MSI,.owner=s->owner,.callback=kernel_surface_find(state.hub,"sata.interrupt"),.context=(uintptr_t)s,.major=1};
    if(CALL(state.route_call,&q,&s->route,0).status)return 0;
    struct pci_msix m={.hardware_index=index,.owner=s->owner,.masked=1,.route=s->route};if(CALL(state.msi,&m,0,0).status)return 0;s->msi_programmed=1;
    if(CALL(state.enable,s->route.id,s->owner,1).status)return 0;
    write32(s->regs,4,0x80000002u);m.masked=0;return !CALL(state.msi,&m,0,0).status;
}
struct kernel_surface_result sata_surface_interrupt(MODULE_ARGS){
    MODULE_UNUSED();if(!state.hub || CALL(state.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);
    uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;
    const struct irq_notification *n=(const void *)a;struct controller *ctrl=state.controllers;while(ctrl && (uintptr_t)ctrl!=b)ctrl=ctrl->next;
    if(!n || !ctrl || n->id!=ctrl->route.id || n->owner!=ctrl->owner){CALL(state.cpu,CPU_LEAVE,flags,0);return module_result(0,0);}
    uint32_t pending=read32(ctrl->regs,8);
    for(struct port *s=state.ports;s;s=s->next)if(s->controller==ctrl && s->port && (pending&(1u<<s->info.port))){s->info.interrupts+=n->count;uint32_t cause=read32(s->port,0x10);write32(s->port,0x10,cause);write32(ctrl->regs,8,1u<<s->info.port);if(s->waiting && ((cause&0x7d800010u) || !(read32(s->port,0x38)&1))){s->command_error=!!(cause&0x7d800000u);s->complete=1;CALL(state.thread,THREAD_WAKE,s->waiter,0);}}
    CALL(state.cpu,CPU_LEAVE,flags,0);return module_result(1,1);
}
struct kernel_surface_result sata_surface_open(MODULE_ARGS){
    MODULE_UNUSED();const struct sata_open *r=(const void *)a;
    if(!foreground() || !r || !b || !block_capacity(c) || r->reserved || r->port>=32 || state.serial==UINT64_MAX || __atomic_exchange_n(&state.management,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    struct controller *ctrl=NULL;struct port *s=NULL;uint32_t stage=0;int fresh=0;
    block_zero((void *)b,c);if(d)*(uint64_t *)d=0;
    const struct kernel_hub_table *ref=kernel_hub_find_table(state.hub,HWENUM_TABLE_NAME);if(!ref)goto done;
    const struct hwenum_table *hw=(const void *)ref->address;if(r->hardware_index>=hw->count)goto done;
    const struct hwenum_descriptor *h=&hw->entries[r->hardware_index];if(h->kind!=HWENUM_PCI || h->data.pci.class_code!=1 || h->data.pci.subclass!=6 || h->data.pci.programming_interface!=1)goto done;
    for(struct port *p=state.ports;p;p=p->next)if(p->info.hardware_index==r->hardware_index && p->info.port==r->port)goto done;
    for(ctrl=state.controllers;ctrl;ctrl=ctrl->next)if(ctrl->device.hardware_index==r->hardware_index)break;
    if(ctrl){if(ctrl->failed || __atomic_exchange_n(&ctrl->busy,1,__ATOMIC_ACQUIRE))goto done;}
    else {if(state.controller_serial>UINT64_MAX-OWNER || !(ctrl=alloc(sizeof(*ctrl))))goto done;ctrl->owner=OWNER+state.controller_serial++;ctrl->device.hardware_index=r->hardware_index;ctrl->busy=1;ctrl->next=state.controllers;state.controllers=ctrl;fresh=1;}
    s=alloc(sizeof(*s));if(!s){if(fresh)forget_controller(ctrl);else ctrl->busy=0;goto done;}
    s->controller=ctrl;s->info=(struct sata_info){.cookie=++state.serial,.hardware_index=r->hardware_index,.port=r->port,.open_stage=1};s->next=state.ports;state.ports=s;if(d)*(uint64_t *)d=s->info.cookie;
    if(fresh && !open_controller(ctrl,r->hardware_index))goto fail;
    s->info.ports=read32(ctrl->regs,0xc);
    if(ctrl->device.bars[5].bytes<0x100+(r->port+1)*0x80 || !(s->info.ports&(1u<<r->port)) || r->port>(read32(ctrl->regs,0)&31))goto fail;
    s->port=ctrl->regs+0x100+r->port*0x80;
    s->info.open_stage=3;
    s->info.open_stage=31;if(!wait_bits(s->port,0x18,0xc001,0))goto fail;
    s->info.open_stage=32;if((read32(s->port,0x28)&0xf0f)!=0x103)goto fail;
    s->info.open_stage=34;if(read32(s->port,0x34) || read32(s->port,0x38))goto fail;
    s->info.open_stage=4;if(state.allocate(r->hardware_index,ctrl->owner,3,(uintptr_t)&s->dma,0,0).status)goto fail;
    write32(s->port,0,s->dma.physical);write32(s->port,4,s->dma.physical>>32);write32(s->port,8,s->dma.physical+1024);write32(s->port,12,(s->dma.physical+1024)>>32);
    write32(s->port,0x30,UINT32_MAX);write32(s->port,0x10,UINT32_MAX);write32(ctrl->regs,8,1u<<r->port);
    if(CALL(state.master,r->hardware_index,ctrl->owner,1).status)goto fail;
    /* The reset signature is valid only after initial D2H FIS reception. */
    write32(s->port,0x18,read32(s->port,0x18)|0x16);
    s->info.open_stage=33;if(!wait_bits(s->port,0x24,UINT32_MAX,0x101))goto fail;
    write32(s->port,0x10,UINT32_MAX);write32(ctrl->regs,8,1u<<r->port);
    write32(s->port,0x14,0x7d800001u);write32(ctrl->regs,4,0x80000002u);write32(s->port,0x18,read32(s->port,0x18)|1);
    s->info.open_stage=7;if(!command(s,0xec,0,0,512,0))goto fail;
    const uint8_t *id=(const void *)(s->dma.address+8192);uint16_t supported=disk_u16(id+166);
    if((disk_u16(id)&0x8000) || (supported&0xc000)!=0x4000 || !(supported&0x400) || !(supported&0x2000))goto fail;
    s->info.blocks=disk_u64(id+200);s->info.block_bytes=512;
    uint16_t sector=disk_u16(id+212);if((sector&0xc000)==0x4000 && (sector&0x1000)){uint32_t words=disk_u32(id+234);if(words<256 || words>2048 || (words&(words-1)))goto fail;s->info.block_bytes=words*2;}
    if(!s->info.blocks || s->info.blocks>(UINT64_C(1)<<48) || s->info.blocks>UINT64_MAX/s->info.block_bytes)goto fail;
    /* Qualify the durability barrier before advertising write capability. */
    s->info.open_stage=8;if(!command(s,0xea,0,0,0,0))goto fail;
    s->info.online=1;s->info.open_stage=0;s->info.route_id=ctrl->route.id;
    struct block_device disk={.major=1,.minor=1,.block_bytes=s->info.block_bytes,.max_transfer_bytes=4096,.blocks=s->info.blocks,.cookie=s->info.cookie,.read=kernel_surface_find(state.hub,"sata.read"),.write=kernel_surface_find(state.hub,"sata.write"),.flush=kernel_surface_find(state.hub,"sata.flush"),.control=kernel_surface_find(state.hub,"sata.control")};disk.root_read=disk.read;disk.root_cookie=disk.cookie;block_export((void *)b,&disk,c);
    uint64_t cookie=s->info.cookie;ctrl->busy=0;state.management=0;return module_result(cookie,1);
fail:
    stage=s->info.open_stage;s->info.failed=1;s->info.online=0;
    /* Leave a named, retryable cleanup record if hardware/callback quiescence
     * cannot yet be proven. The optional fourth output returns its cookie. */
    if(!ctrl->owned){forget_port(s);forget_controller(ctrl);}
    else {if(!stop(s))poison(ctrl);ctrl->busy=0;}
done:state.management=0;return module_result(stage,0);
}
static struct kernel_surface_result io(uint64_t cookie,uint64_t lba,uint64_t sectors,uintptr_t buffer,size_t capacity,unsigned opcode){
    if(!foreground())return module_result(0,0);
    struct port *s=lock_instance(cookie);if(!s)return module_result(0,0);
    if(s->controller->failed || !s->info.block_bytes || !cookie || cookie!=s->info.cookie || !s->info.online || s->info.failed || lba>s->info.blocks || sectors>s->info.blocks-lba || sectors>4096/s->info.block_bytes || capacity<sectors*s->info.block_bytes || (sectors && !buffer) || buffer>UINTPTR_MAX-sectors*s->info.block_bytes){__atomic_store_n(&s->controller->busy,0,__ATOMIC_RELEASE);return module_result(0,0);}
    unsigned bytes=sectors*s->info.block_bytes;void *bounce=(void *)(s->dma.address+8192);if(opcode==0x35 && bytes)disk_copy(bounce,(void *)buffer,bytes);
    int ok=(!sectors && opcode!=0xea) || command(s,opcode,lba,sectors,bytes,opcode==0x35);
    if(ok){if(opcode==0x25){if(bytes)disk_copy((void *)buffer,bounce,bytes);++s->info.reads;}else if(opcode==0x35)++s->info.writes;else ++s->info.flushes;}
    else{s->info.online=0;s->info.failed=1;if(!stop(s))poison(s->controller);}
    __atomic_store_n(&s->controller->busy,0,__ATOMIC_RELEASE);return module_result(ok?bytes:0,ok);
}
struct kernel_surface_result sata_surface_read(MODULE_ARGS){MODULE_UNUSED();return io(a,b,c,d,e,0x25);}
struct kernel_surface_result sata_surface_write(MODULE_ARGS){MODULE_UNUSED();return io(a,b,c,d,e,0x35);}
struct kernel_surface_result sata_surface_flush(MODULE_ARGS){MODULE_UNUSED();return io(a,0,0,0,0,0xea);}
struct kernel_surface_result sata_surface_control(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground() || !a)return module_result(0,0);struct port *s=lock_instance(a);if(!s)return module_result(0,0);
    int ok=b==BLOCK_RETAIN?(s->info.online && !s->info.failed && !s->controller->failed && s->refs<UINT64_MAX):(b==BLOCK_RELEASE && s->refs);
    if(ok){if(b==BLOCK_RETAIN)++s->refs;else --s->refs;}s->controller->busy=0;return module_result(1,ok);
}
struct kernel_surface_result sata_surface_info(MODULE_ARGS){MODULE_UNUSED();if(!state.hub || !b || CALL(state.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);struct port *s=lock_instance(a);if(!s)return module_result(0,0);*(struct sata_info *)b=s->info;s->controller->busy=0;return module_result(1,1);}
struct kernel_surface_result sata_surface_close(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground() || __atomic_exchange_n(&state.management,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    int ok=0;struct port *s=lock_instance(a);if(!s)goto done;struct controller *ctrl=s->controller;if(s->refs){ctrl->busy=0;goto done;}
    unsigned siblings=0;for(struct port *p=state.ports;p;p=p->next)if(p!=s && p->controller==ctrl)++siblings;
    s->info.online=0;
    if(siblings){if(!stop(s)){poison(ctrl);ctrl->busy=0;goto done;}if(!free_dma(s,!ctrl->failed)){ctrl->busy=0;goto done;}forget_port(s);ctrl->busy=0;ok=1;}
    else {if(!close_controller(ctrl) || !free_dma(s,0) || CALL(state.release,ctrl->device.hardware_index,ctrl->owner,1).status){ctrl->busy=0;goto done;}forget_port(s);forget_controller(ctrl);ok=1;}
done:state.management=0;return module_result(1,ok);
}

struct kernel_surface_result sata_surface_ports(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground() || !b || __atomic_exchange_n(&state.management,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    struct controller *ctrl=state.controllers;while(ctrl && ctrl->device.hardware_index!=a)ctrl=ctrl->next;
    struct pci_device device;int temporary=!ctrl,ok=0;uint64_t owner=0;uintptr_t regs=0;uint32_t ports=0;
    if(ctrl){if(__atomic_exchange_n(&ctrl->busy,1,__ATOMIC_ACQUIRE))goto done;device=ctrl->device;owner=ctrl->owner;regs=ctrl->regs;}
    else {if(state.controller_serial>UINT64_MAX-OWNER)goto done;owner=OWNER+state.controller_serial++;if(CALL(state.claim,a,owner,&device).status)goto done;regs=device.bars[5].address;}
    if(regs && device.bars[5].bytes>=0x180){uint32_t implemented=read32(regs,0xc);for(unsigned p=0;p<32;++p)if((implemented&(1u<<p)) && device.bars[5].bytes>=0x180+p*0x80 && (read32(regs,0x128+p*0x80)&0xf0f)==0x103)ports|=1u<<p;ok=1;}
    if(temporary){if(CALL(state.release,a,owner,1).status)ok=0;}else ctrl->busy=0;
done:state.management=0;if(ok)*(uint32_t *)b=ports;return module_result(ports,ok);
}
