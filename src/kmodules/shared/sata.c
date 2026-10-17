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
static struct {
    const struct kernel_surfaces *hub;struct pci_device device;struct mem_pages dma;struct irq_route route;struct sata_info info;
    uintptr_t regs,port;uint64_t serial,refs,waiter;unsigned owned,busy,waiting,complete,command_error,msi_programmed,taken;
    kernel_surface_call cpu,thread,clock,context,claim,master,allocate,free,release,ready,msi,route_call,enable,route_free;
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
static int reset(void){
    if(!state.regs)return 1;
    if(state.port)write32(state.port,0x14,0);
    write32(state.regs,4,0x80000001u);
    if(!wait_bits(state.regs,4,1,0))return 0;
    write32(state.regs,4,0x80000000u);return !CALL(state.master,state.device.hardware_index,OWNER,0).status;
}
static int close_owned(void){
    state.info.online=0;
    if(state.route.id && state.msi_programmed){struct pci_msix m={.hardware_index=state.device.hardware_index,.owner=OWNER,.masked=1,.route=state.route};if(CALL(state.msi,&m,0,0).status || CALL(state.enable,state.route.id,OWNER,0).status)return 0;}
    if(state.taken && !reset())return 0;
    if(state.route.id){if(CALL(state.route_free,state.route.id,OWNER,1).status)return 0;state.route=(struct irq_route){0};}
    if(state.dma.id){if(CALL(state.free,state.device.hardware_index,OWNER,state.dma.id).status)return 0;state.dma=(struct mem_pages){0};}
    if(CALL(state.release,state.device.hardware_index,OWNER,1).status)return 0;
    state.owned=state.taken=state.msi_programmed=0;state.regs=state.port=0;state.info.cookie=0;return 1;
}
struct kernel_surface_result sata_surface_init(MODULE_ARGS){
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)a;if(state.hub)return module_result(1,hub==state.hub);
    if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC)return module_result(0,0);
#define BIND(field,name) state.field=kernel_surface_find(hub,name);if(!state.field)return module_result(0,0)
    BIND(cpu,"cpu.control");BIND(thread,"threads.control");BIND(clock,"clock.now_ns");BIND(context,"irq.context");
    BIND(claim,"pci.claim");BIND(master,"pci.busmaster");BIND(allocate,"pci.dma_alloc");BIND(free,"pci.dma_free");BIND(release,"pci.release");BIND(ready,"iommu.ready");
    BIND(msi,"pci.msi");BIND(route_call,"irq.route");BIND(enable,"irq.enable");BIND(route_free,"irq.release");
#undef BIND
    if(CALL(state.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);
    state.hub=hub;return module_result(1,1);
}
/* One non-NCQ slot, preallocated command list/FIS/table/data. No caller pointer
 * reaches hardware. Flush has no PRDT; data direction belongs to command header. */
static int command(unsigned opcode,uint64_t lba,unsigned sectors,unsigned bytes,int write){
    struct command_header *header=(void *)state.dma.address;uint8_t *table=(void *)(state.dma.address+4096);
    for(unsigned i=0;i<256;++i)table[i]=0;
    *header=(struct command_header){.flags=5|(write?64:0),.prdt=bytes?1:0,.table=state.dma.physical+4096};
    table[0]=0x27;table[1]=0x80;table[2]=opcode;table[7]=0x40;
    table[4]=lba;table[5]=lba>>8;table[6]=lba>>16;table[8]=lba>>24;table[9]=lba>>32;table[10]=lba>>40;table[12]=sectors;table[13]=sectors>>8;
    if(bytes){uint32_t *prdt=(void *)(table+128);prdt[0]=(uint32_t)(state.dma.physical+8192);prdt[1]=(state.dma.physical+8192)>>32;prdt[3]=(bytes-1)|0x80000000u;}
    if(!wait_bits(state.port,0x20,0x88,0))return 0;
    write32(state.port,0x10,UINT32_MAX);write32(state.regs,8,UINT32_MAX);
    state.waiter=CALL(state.thread,THREAD_CURRENT,0,0).value;state.waiting=1;state.complete=0;state.command_error=0;
    uint64_t start=now();if(start==UINT64_MAX || start>UINT64_MAX-TIMEOUT){state.waiting=0;return 0;}
    write32(state.port,0x38,1);
    while(!state.complete){uint64_t t=now();if(t==UINT64_MAX || t<start || t-start>=TIMEOUT){++state.info.timeouts;state.waiting=0;return 0;}if(CALL(state.thread,THREAD_BLOCK_UNTIL,start+TIMEOUT,0).status){state.waiting=0;return 0;}}
    state.waiting=0;__asm__ volatile("lfence":::"memory");
    return !state.command_error && !(read32(state.port,0x38)&1) && !(read32(state.port,0x20)&0x89) && header->bytes==bytes && !CALL(state.ready,state.device.hardware_index,OWNER,0).status;
}
struct kernel_surface_result sata_surface_interrupt(MODULE_ARGS){
    MODULE_UNUSED();const struct irq_notification *n=(const void *)a;if(!state.hub || !n || n->id!=state.route.id || n->owner!=OWNER || CALL(state.cpu,CPU_CURRENT,0,0).value || !state.port)return module_result(0,0);
    state.info.interrupts+=n->count;uint32_t cause=read32(state.port,0x10);write32(state.port,0x10,cause);write32(state.regs,8,1u<<state.info.port);
    if(state.waiting && ((cause&0x7d800010u) || !(read32(state.port,0x38)&1))){state.command_error=!!(cause&0x7d800000u);state.complete=1;CALL(state.thread,THREAD_WAKE,state.waiter,0);}
    return module_result(1,1);
}
struct kernel_surface_result sata_surface_open(MODULE_ARGS){
    MODULE_UNUSED();const struct sata_open *r=(const void *)a;if(!foreground() || !r || !b || !block_capacity(c) || r->reserved || r->port>=32 || state.owned || state.serial==UINT64_MAX)return module_result(0,0);
    const struct kernel_hub_table *ref=kernel_hub_find_table(state.hub,HWENUM_TABLE_NAME);if(!ref)return module_result(0,0);
    const struct hwenum_table *hw=(const void *)ref->address;if(r->hardware_index>=hw->count)return module_result(0,0);
    const struct hwenum_descriptor *h=&hw->entries[r->hardware_index];if(h->kind!=HWENUM_PCI || h->data.pci.class_code!=1 || h->data.pci.subclass!=6 || h->data.pci.programming_interface!=1 || __atomic_exchange_n(&state.busy,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    if(state.owned)goto invalid;
    block_zero((void *)b,c);
    if(CALL(state.claim,r->hardware_index,OWNER,&state.device).status)goto invalid;
    state.owned=1;state.info=(struct sata_info){.cookie=++state.serial,.hardware_index=r->hardware_index,.port=r->port,.open_stage=1};state.refs=0;
    if(!state.device.bars[5].address || state.device.bars[5].bytes<0x100+(r->port+1)*0x80)goto fail;
    state.regs=state.device.bars[5].address;
    /* BIOS/OS handoff, if advertised. Never seize a firmware-busy controller. */
    if(read32(state.regs,0x24)&1){write32(state.regs,0x28,read32(state.regs,0x28)|2);if(!wait_bits(state.regs,0x28,0x11,0))goto fail;}
    state.taken=1;state.info.open_stage=2;if(!reset())goto fail;
    uint32_t cap=read32(state.regs,0);state.info.ports=read32(state.regs,0xc);
    if(!(cap&0x80000000u) || !(state.info.ports&(1u<<r->port)) || r->port>(cap&31))goto fail;
    state.port=state.regs+0x100+r->port*0x80;
    state.info.open_stage=3;
    state.info.open_stage=31;if(!wait_bits(state.port,0x18,0xc001,0))goto fail;
    state.info.open_stage=32;if((read32(state.port,0x28)&0xf0f)!=0x103)goto fail;
    state.info.open_stage=34;if(read32(state.port,0x34) || read32(state.port,0x38))goto fail;
    state.info.open_stage=4;if(state.allocate(r->hardware_index,OWNER,3,(uintptr_t)&state.dma,0,0).status)goto fail;
    write32(state.port,0,state.dma.physical);write32(state.port,4,state.dma.physical>>32);write32(state.port,8,state.dma.physical+1024);write32(state.port,12,(state.dma.physical+1024)>>32);
    write32(state.port,0x30,UINT32_MAX);write32(state.port,0x10,UINT32_MAX);write32(state.regs,8,UINT32_MAX);
    state.info.open_stage=5;struct irq_request q={.kind=IRQ_MSI,.owner=OWNER,.callback=kernel_surface_find(state.hub,"sata.interrupt"),.major=1};if(CALL(state.route_call,&q,&state.route,0).status)goto fail;
    struct pci_msix m={.hardware_index=r->hardware_index,.owner=OWNER,.masked=1,.route=state.route};if(CALL(state.msi,&m,0,0).status)goto fail;state.msi_programmed=1;if(CALL(state.enable,state.route.id,OWNER,1).status)goto fail;
    state.info.open_stage=6;if(CALL(state.master,r->hardware_index,OWNER,1).status)goto fail;
    /* The reset signature is valid only after initial D2H FIS reception. */
    write32(state.port,0x18,read32(state.port,0x18)|0x16);
    state.info.open_stage=33;if(!wait_bits(state.port,0x24,UINT32_MAX,0x101))goto fail;
    write32(state.port,0x10,UINT32_MAX);write32(state.regs,8,UINT32_MAX);
    write32(state.port,0x14,0x7d800001u);write32(state.regs,4,0x80000002u);write32(state.port,0x18,read32(state.port,0x18)|1);
    m.masked=0;if(CALL(state.msi,&m,0,0).status)goto fail;
    state.info.open_stage=7;if(!command(0xec,0,0,512,0))goto fail;
    const uint8_t *id=(const void *)(state.dma.address+8192);uint16_t supported=disk_u16(id+166);
    if((disk_u16(id)&0x8000) || (supported&0xc000)!=0x4000 || !(supported&0x400) || !(supported&0x2000))goto fail;
    state.info.blocks=disk_u64(id+200);state.info.block_bytes=512;
    uint16_t sector=disk_u16(id+212);if((sector&0xc000)==0x4000 && (sector&0x1000)){uint32_t words=disk_u32(id+234);if(words<256 || words>2048 || (words&(words-1)))goto fail;state.info.block_bytes=words*2;}
    if(!state.info.blocks || state.info.blocks>(UINT64_C(1)<<48) || state.info.blocks>UINT64_MAX/state.info.block_bytes)goto fail;
    /* Qualify the durability barrier before advertising write capability. */
    state.info.open_stage=8;if(!command(0xea,0,0,0,0))goto fail;
    state.info.online=1;state.info.open_stage=0;state.info.route_id=state.route.id;
    struct block_device disk={.major=1,.minor=1,.block_bytes=state.info.block_bytes,.max_transfer_bytes=4096,.blocks=state.info.blocks,.cookie=state.info.cookie,.read=kernel_surface_find(state.hub,"sata.read"),.write=kernel_surface_find(state.hub,"sata.write"),.flush=kernel_surface_find(state.hub,"sata.flush"),.control=kernel_surface_find(state.hub,"sata.control")};disk.root_read=disk.read;disk.root_cookie=disk.cookie;block_export((void *)b,&disk,c);
    __atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(state.info.cookie,1);
fail:{uint32_t stage=state.info.open_stage;state.info.failed=1;(void)close_owned();__atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(stage,0);}
invalid:__atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(0,0);
}
static struct kernel_surface_result io(uint64_t cookie,uint64_t lba,uint64_t sectors,uintptr_t buffer,size_t capacity,unsigned opcode){
    if(!foreground() || __atomic_exchange_n(&state.busy,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    if(!cookie || cookie!=state.info.cookie || !state.info.online || state.info.failed || lba>state.info.blocks || sectors>state.info.blocks-lba || sectors>4096/state.info.block_bytes || capacity<sectors*state.info.block_bytes || (sectors && !buffer) || buffer>UINTPTR_MAX-sectors*state.info.block_bytes){__atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(0,0);}
    unsigned bytes=sectors*state.info.block_bytes;void *bounce=(void *)(state.dma.address+8192);if(opcode==0x35 && bytes)disk_copy(bounce,(void *)buffer,bytes);
    int ok=(!sectors && opcode!=0xea) || command(opcode,lba,sectors,bytes,opcode==0x35);
    if(ok){if(opcode==0x25){if(bytes)disk_copy((void *)buffer,bounce,bytes);++state.info.reads;}else if(opcode==0x35)++state.info.writes;else ++state.info.flushes;}
    else{state.info.online=0;state.info.failed=1;(void)reset();}
    __atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(ok?bytes:0,ok);
}
struct kernel_surface_result sata_surface_read(MODULE_ARGS){MODULE_UNUSED();return io(a,b,c,d,e,0x25);}
struct kernel_surface_result sata_surface_write(MODULE_ARGS){MODULE_UNUSED();return io(a,b,c,d,e,0x35);}
struct kernel_surface_result sata_surface_flush(MODULE_ARGS){MODULE_UNUSED();return io(a,0,0,0,0,0xea);}
struct kernel_surface_result sata_surface_control(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground() || __atomic_exchange_n(&state.busy,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    int valid=a && a==state.info.cookie && state.owned;
    int ok=valid && (b==BLOCK_RETAIN?(state.info.online && !state.info.failed && state.refs<UINT64_MAX):(b==BLOCK_RELEASE && state.refs));
    if(ok){if(b==BLOCK_RETAIN)++state.refs;else --state.refs;}
    __atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(1,ok);
}
struct kernel_surface_result sata_surface_info(MODULE_ARGS){MODULE_UNUSED();if(!state.hub || !b || CALL(state.cpu,CPU_CURRENT,0,0).value || (a && a!=state.info.cookie))return module_result(0,0);*(struct sata_info *)b=state.info;return module_result(1,1);}
struct kernel_surface_result sata_surface_close(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground() || __atomic_exchange_n(&state.busy,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    int ok=a && a==state.info.cookie && state.owned && !state.refs && close_owned();
    __atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(1,ok);
}
