/* Production controller code with modeled MMIO/DMA/IRQ adapters. */
#define XHCI_TEST
#include "../kmodules/shared/xhci.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static _Alignas(4096) unsigned char registers[65536];
static _Alignas(16) unsigned char hub_memory[16384];
static struct {struct hwenum_table header;struct hwenum_descriptor entries[2];} hardware;
static unsigned producer,producer_cycle=1,lock_depth,irq_callback,master_enabled,active_dma,fail_halt,drop_command,delay_transfer;
static unsigned model_command_tail,model_command_cycle=1,next_slot=1,disabled_slots,short_read,command_count;
static uint64_t ticks,route_id;
static uintptr_t irq_cookie;
static uint32_t *reg(unsigned offset) {return (void *)(registers+offset);}
uint64_t xhci_flags(void) {return 512;}
uint32_t xhci_read32(uintptr_t p) {assert(p>=(uintptr_t)registers && p+4<=(uintptr_t)registers+sizeof(registers));return *(uint32_t *)p;}
static void event(struct trb e) {
    struct controller *c=state.head;struct trb *events=(void *)(c->base->pages.address+8192);
    e.control|=producer_cycle;events[producer]=e;if(++producer==256) {producer=0;producer_cycle^=1;}
    *reg(0x44)|=8;*reg(0x1020)|=1;
}
void xhci_write32(uintptr_t p,uint32_t v) {
    unsigned offset=p-(uintptr_t)registers;assert(offset+4<=sizeof(registers));
    if(offset==0x40) {
        if(v&2) {if(fail_halt){*reg(offset)=2;return;}*reg(offset)=0;*reg(0x44)=1;producer=model_command_tail=0;producer_cycle=model_command_cycle=1;return;}
        *reg(offset)=v;if(!fail_halt) {*reg(0x44)&=~1u;*reg(0x44)|=!(v&1);}return;
    }
    if(offset==0x44) {*reg(offset)&=~v;return;}
    if(offset==0x1020) {*reg(offset)=(*reg(offset)&~(v&1))|(v&2);return;}
    if(offset>=0x440 && offset<0x460 && !(offset&15)) {
        unsigned old=*reg(offset);*reg(offset)=(old&~PORT_CHANGES)|(v&PORT_KEEP);*reg(offset)&=~(v&PORT_CHANGES);
        if(v&((1u<<4)|(1u<<31))) {*reg(offset)|=2|(1u<<21);event((struct trb){.parameter=(uint64_t)((offset-0x440)/16+1)<<24,.status=1u<<24,.control=34u<<10});}return;
    }
    if(offset==0x1034)assert(master_enabled); /* ERSTBA may fetch immediately. */
    *reg(offset)=v;
    if(offset==0x2000 && !drop_command) {
        struct controller *c=state.head;struct trb cmd=c->commands.trbs[model_command_tail];assert((cmd.control&1)==model_command_cycle);
        unsigned type=(cmd.control>>10)&63,slot=cmd.control>>24;++command_count;
        if(type==9)slot=next_slot;
        if(type==10)++disabled_slots;
        if(type==11 || type==12) {
            struct slot *s=&c->slots[slot];copy((void *)s->contexts->pages.address,input(c,s,1),32*c->record.info.context_bytes);
        }
        event((struct trb){.parameter=c->commands.physical+model_command_tail*16,.status=1u<<24,.control=(33u<<10)|(slot<<24)});
        if(++model_command_tail==255) {model_command_tail=0;model_command_cycle^=1;}
    } else if(offset>0x2000 && offset<0x2100 && !delay_transfer) {
        struct controller *c=state.head;unsigned slot=(offset-0x2000)/4;struct endpoint *ep=c->slots[slot].ep[v];
        if(ep->direction)memset((void *)(ep->storage->pages.address+4096),0x5a,ep->bytes);
        if(short_read && v==1)event((struct trb){.parameter=ep->data_pointer,.status=(13u<<24)|(ep->bytes-3),.control=(32u<<10)|(slot<<24)|(v<<16)});
        event((struct trb){.parameter=ep->event_pointer,.status=1u<<24,.control=(32u<<10)|(slot<<24)|(v<<16)});
    }
}
static void deliver(void) {
    if(irq_cookie && (*reg(0x1020)&1)) {
        struct irq_notification n={.id=route_id,.owner=state.head->owner};irq_callback=1;
        assert(!xhci_surface_interrupt((uintptr_t)&n,irq_cookie,0,0,0,0).status);irq_callback=0;
    }
}
static struct kernel_surface_result cpu(MODULE_ARGS) {MODULE_UNUSED();if(a==CPU_ENTER){++lock_depth;return module_result(512,1);}if(a==CPU_LEAVE){assert(lock_depth);--lock_depth;}return module_result(0,1);}
static struct kernel_surface_result alloc(MODULE_ARGS) {MODULE_UNUSED();assert(!lock_depth && !irq_callback);return module_result((uintptr_t)calloc(b,c),1);}
static struct kernel_surface_result release_heap(MODULE_ARGS) {MODULE_UNUSED();assert(!lock_depth && !irq_callback);free((void *)b);return module_result(1,1);}
static struct kernel_surface_result claim(MODULE_ARGS) {MODULE_UNUSED();struct pci_device *out=(void *)c;*out=(struct pci_device){.hardware_index=a,.owner=b,.msix_count=1};out->bars[0]=(struct pci_bar){.address=(uintptr_t)registers,.bytes=sizeof(registers)};return module_result(1,1);}
static struct kernel_surface_result master(MODULE_ARGS) {MODULE_UNUSED();master_enabled=c;return module_result(1,1);}
static struct kernel_surface_result dma_alloc(MODULE_ARGS) {MODULE_UNUSED();assert(!lock_depth && !irq_callback);void *p=aligned_alloc(4096,c*4096);assert(p);memset(p,0,c*4096);*(struct mem_pages *)d=(struct mem_pages){.id=(uintptr_t)p,.physical=(uintptr_t)p,.address=(uintptr_t)p,.count=c,.owner=b};++active_dma;return module_result((uintptr_t)p,1);}
static struct kernel_surface_result dma_free(MODULE_ARGS) {MODULE_UNUSED();assert(!master_enabled && !lock_depth);free((void *)c);--active_dma;return module_result(1,1);}
static struct kernel_surface_result route(MODULE_ARGS) {MODULE_UNUSED();const struct irq_request *r=(void *)a;irq_cookie=r->context;*(struct irq_route *)b=(struct irq_route){.id=++route_id,.owner=r->owner,.kind=IRQ_MSI};return module_result(1,1);}
static struct kernel_surface_result unroute(MODULE_ARGS) {MODULE_UNUSED();irq_cookie=0;return module_result(1,1);}
static struct kernel_surface_result simple(MODULE_ARGS) {MODULE_UNUSED();return module_result(1,1);}
static struct kernel_surface_result clock_now(MODULE_ARGS) {MODULE_UNUSED();ticks+=100000;return module_result(ticks,1);}
static struct kernel_surface_result thread(MODULE_ARGS) {MODULE_UNUSED();if(a==THREAD_YIELD || a==THREAD_BLOCK_UNTIL)deliver();return module_result(a==THREAD_CURRENT?1:0,1);}
static struct kernel_surface_result context(MODULE_ARGS) {MODULE_UNUSED();return module_result(irq_callback,1);}
static struct kernel_surface_result publish(MODULE_ARGS) {MODULE_UNUSED();assert(!strcmp(((struct kernel_hub_table *)a)->name,XHCI_TABLE_NAME));return module_result(1,1);}
static void add(const char *name,kernel_surface_call fn) {struct kernel_surfaces *h=(void *)hub_memory;struct kernel_surface *m=(void *)(hub_memory+h->header_bytes);strcpy(m[h->count].name,name);m[h->count].call=fn;m[h->count++].major=1;}
static void setup(void) {
    memset(registers,0,sizeof(registers));memset(hub_memory,0,sizeof(hub_memory));
    struct kernel_surfaces *h=(void *)hub_memory;h->header_bytes=sizeof(*h);h->magic=KERNEL_SURFACES_MAGIC;h->major=2;
    add("cpu.control",cpu);add("mem.calloc",alloc);add("mem.free",release_heap);add("pci.claim",claim);add("pci.busmaster",master);add("pci.msix",simple);add("pci.dma_alloc",dma_alloc);add("pci.dma_free",dma_free);add("pci.release",simple);add("irq.route",route);add("irq.enable",simple);add("irq.release",unroute);add("clock.now_ns",clock_now);add("threads.control",thread);add("irq.context",context);add("hub.publish_table",publish);add("xhci.interrupt",xhci_surface_interrupt);
    h->tables_offset=sizeof(*h)+h->count*sizeof(struct kernel_surface);h->table_count=1;struct kernel_hub_table *table=(void *)(hub_memory+h->tables_offset);strcpy(table->name,HWENUM_TABLE_NAME);table->address=(uintptr_t)&hardware;
    hardware.header.count=2;hardware.entries[1]=(struct hwenum_descriptor){.kind=HWENUM_PCI};hardware.entries[1].data.pci.class_code=12;hardware.entries[1].data.pci.subclass=3;hardware.entries[1].data.pci.programming_interface=0x30;
    *reg(0)=0x01000040;*reg(4)=2u<<24|1u<<8|8;*reg(16)=1u|(0x80u<<16);*reg(20)=0x2000;*reg(24)=0x1000;*reg(0x44)=1;*reg(0x48)=1;
    *reg(0x200)=2u|2u<<24;*reg(0x204)=0x20425355;*reg(0x208)=1u|2u<<8;*reg(0x440)=1u|1u<<9|1u<<10;
    assert(!xhci_surface_init((uintptr_t)h,1,0,0,0,0).status);
}
static struct host_transfer_result reap(uint64_t controller,uint64_t request,unsigned expected) {
    deliver();struct host_transfer_result r;assert(!xhci_surface_result(controller,request,(uintptr_t)&r,1,0,0).status && r.state==expected);return r;
}
int main(void) {
    setup();struct xhci_info info;assert(!xhci_surface_open(1,(uintptr_t)&info,0,0,0,0).status && info.state==XHCI_ONLINE);
    uint64_t id=info.id;assert(state.table.count==1 && state.table.first && active_dma==1);
    assert(xhci_surface_open(1,(uintptr_t)&info,0,0,0,0).status);
    assert(!xhci_surface_reset_port(id,1,0,0,0,0).status);deliver();struct xhci_port port;assert(!xhci_surface_port(id,1,(uintptr_t)&port,0,0,0).status);
    struct host_device_request device={.port=1,.max_packet=8,.port_generation=port.generation};uint64_t token;
    assert(!xhci_surface_device(id,(uintptr_t)&device,(uintptr_t)&token,0,0,0).status);
    unsigned char buffer[8];struct host_transfer_request request={.device=token,.buffer=buffer,.bytes=8,.setup={128,6,0,1,0,0,8,0}};
    short_read=1;for(unsigned i=0;i<180;++i) {memset(buffer,0xcc,8);struct kernel_surface_result r=xhci_surface_transfer(id,(uintptr_t)&request,0,0,0,0);assert(!r.status);struct host_transfer_result waited;assert(!xhci_surface_wait(id,r.value,(uintptr_t)&waited,ticks+1000000000,0,0).status);struct host_transfer_result result=reap(id,r.value,HOST_COMPLETE);assert(result.bytes==3 && buffer[0]==0x5a && buffer[3]==0xcc);}short_read=0;
    struct host_endpoint_request endpoint={.device=token,.address=0x81,.type=HOST_INTERRUPT,.max_packet=8,.interval=7};assert(!xhci_surface_endpoint(id,(uintptr_t)&endpoint,0,0,0,0).status);
    request.endpoint=0x81;delay_transfer=1;struct kernel_surface_result pending=xhci_surface_transfer(id,(uintptr_t)&request,0,0,0,0);assert(!pending.status);
    struct host_transfer_result timed;assert(xhci_surface_wait(id,pending.value,(uintptr_t)&timed,ticks+200000,0,0).status);
    assert(xhci_surface_close(id,0,0,0,0,0).status);
    assert(xhci_surface_transfer(id,(uintptr_t)&request,0,0,0,0).status);assert(!xhci_surface_cancel(id,token,0x81,0,0,0).status);reap(id,pending.value,HOST_CANCELLED);
    pending=xhci_surface_transfer(id,(uintptr_t)&request,0,0,0,0);assert(!pending.status);
    *reg(0x440)=1u<<17|1u<<9;event((struct trb){.parameter=1u<<24,.status=1u<<24,.control=34u<<10});deliver();reap(id,pending.value,HOST_DISCONNECTED);
    assert(xhci_surface_transfer(id,(uintptr_t)&request,0,0,0,0).status);assert(!xhci_surface_remove(id,token,0,0,0,0).status);
    for(unsigned i=0;i<300;++i)assert(command(state.head,(struct trb){.control=23u<<10},NULL));
    assert(command_count>255);
    unsigned retained=active_dma;fail_halt=1;assert(xhci_surface_close(id,0,0,0,0,0).status && active_dma==retained);fail_halt=0;
    assert(!xhci_surface_close(id,0,0,0,0,0).status && !active_dma && !master_enabled);
    /* A fresh instance has a fresh ID. Bad extended capability offsets fail closed. */
    delay_transfer=0;*reg(0x440)=1|1u<<9|1u<<10;assert(!xhci_surface_open(1,(uintptr_t)&info,0,0,0,0).status && info.id!=id);
    drop_command=1;assert(!command(state.head,(struct trb){.control=23u<<10},NULL));assert(state.head->record.info.state==XHCI_FAILED);drop_command=0;
    assert(!xhci_surface_close(info.id,0,0,0,0,0).status && !active_dma);
    *reg(0x208)=3u|2u<<8;assert(xhci_surface_open(1,(uintptr_t)&info,0,0,0,0).status && !active_dma);
    assert(state.table.count==1);
    *reg(0x208)=1u|2u<<8;*reg(0x200)=1u|(4u<<8)|(1u<<16); /* BIOS refuses ownership. */
    *reg(0x210)=2u|2u<<24;*reg(0x214)=0x20425355;*reg(0x218)=1u|2u<<8;
    assert(xhci_surface_open(1,(uintptr_t)&info,0,0,0,0).status && state.head->firmware_busy && !active_dma);
    assert(xhci_surface_close(info.id,0,0,0,0,0).status); /* No forced firmware reset. */
    puts("PASS xHCI: rings/wrap, short control data, cancellation, detach, stale tokens, restart, timeout and retained DMA on failed halt");
}
