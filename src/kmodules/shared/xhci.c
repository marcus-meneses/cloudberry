#include "xhci.h"
#include "xhci-platform.h"
#include "pci.h"
#include "hwenum.h"
#include "threads.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
#define RING_ENTRIES 256u
#define PORT_CHANGES (0x7fu<<17)
#define PORT_KEEP ((15u<<5)|(1u<<9)|(3u<<14)|(7u<<25))
#define TIMEOUT UINT64_C(1000000000)
struct trb {uint64_t parameter;uint32_t status,control;};
struct dma {struct dma *next;struct mem_pages pages;};
struct ring {struct trb *trbs;uint64_t physical;unsigned tail,cycle;};
struct endpoint {
    struct ring ring;struct dma *storage;struct host_endpoint_request definition;
    struct host_transfer_result result;void *buffer;uint32_t bytes,direction;
    uint64_t event_pointer,data_pointer,setup_pointer,waiter;unsigned short_bytes,short_seen,configured,halted;
};
struct slot {uint64_t token,port_generation;unsigned port,addressed;struct dma *contexts;struct endpoint *ep[32];};
struct controller {
    struct xhci_record record;struct controller *next;struct pci_device pci;
    struct dma *dma,*base;struct slot *slots;struct irq_route route;struct ring commands;
    uintptr_t registers,op,runtime,doorbells;uint64_t extent,owner,command_pointer;
    unsigned event_head,event_cycle,command_wait,command_code,command_slot,busy,claimed,routed,master,callback_active,firmware_busy;
    uint32_t hcc;uint64_t command_events;
};
static struct {
    const struct kernel_surfaces *hub;const struct hwenum_table *hw;uintptr_t heap;
    struct controller *head;struct xhci_table table;uint64_t serial;unsigned opening;
    kernel_surface_call cpu,alloc,free,claim,master,msix,dma_alloc,dma_free,release,route,enable,unroute,now,thread,irq_context;
} state;
static struct kernel_surface_result failure(void) {return module_result(0,0);}
static uint64_t lock(void) {return CALL(state.cpu,CPU_ENTER,0,0).value;}
static void unlock(uint64_t flags) {CALL(state.cpu,CPU_LEAVE,flags,0);}
static uint64_t next_id(void) {uint64_t flags=lock();uint64_t id=state.serial==UINT64_MAX?0:++state.serial;unlock(flags);return id;}
static void notify(struct endpoint *ep) {if(ep->waiter)CALL(state.thread,THREAD_WAKE,ep->waiter,0);}
static int bsp(void) {return state.hub && !CALL(state.cpu,CPU_CURRENT,0,0).value;}
static int foreground(void) {return bsp() && (xhci_flags()&512) && !CALL(state.cpu,CPU_DEPTH,0,0).value && !CALL(state.irq_context,0,0,0).value;}
static uint64_t now(void) {struct kernel_surface_result r=CALL(state.now,0,0,0);return r.status?UINT64_MAX:r.value;}
static int pause_work(void) {
    struct kernel_surface_result r=CALL(state.thread,THREAD_CURRENT,0,0);
    if(r.status)return 0;
    if(r.value)return !CALL(state.thread,THREAD_YIELD,0,0).status;
    __asm__ volatile("pause");return 1;
}
static int wait_bits(uintptr_t p,uint32_t mask,uint32_t value) {
    uint64_t start=now();if(start==UINT64_MAX)return 0;
    for(unsigned i=0;i<2000000;++i) {
        if((xhci_read32(p)&mask)==value)return 1;
        uint64_t t=now();if(t==UINT64_MAX || t<start || t-start>TIMEOUT || !pause_work())return 0;
    }return 0;
}
static void write64(uintptr_t p,uint64_t v) {xhci_write32(p,(uint32_t)v);xhci_write32(p+4,v>>32);}
static void zero(void *p,size_t n) {unsigned char *v=p;while(n--)*v++=0;}
static void copy(void *to,const void *from,size_t n) {unsigned char *d=to;const unsigned char *s=from;while(n--)*d++=*s++;}
static void *allocate(size_t n,size_t size) {struct kernel_surface_result r=CALL(state.alloc,state.heap,n,size);return r.status?NULL:(void *)r.value;}
static struct controller *find(uint64_t id) {for(struct controller *c=state.head;c;c=c->next)if(c->record.info.id==id)return c;
    return NULL;}
static int begin(struct controller *c,uint64_t id) {
    if(!c || !foreground() || !CALL(state.thread,THREAD_CURRENT,0,0).value)return 0;
    uint64_t flags=lock();int ok=c->record.info.id==id && !c->busy && c->record.info.state==XHCI_ONLINE;if(ok)c->busy=1;unlock(flags);return ok;
}
static struct kernel_surface_result finish(struct controller *c,uintptr_t value,int ok) {uint64_t flags=lock();c->busy=0;unlock(flags);return module_result(value,ok);}
static struct dma *dma_allocate(struct controller *c,size_t pages) {
    struct dma *d=allocate(1,sizeof(*d));if(!d)return NULL;
    struct kernel_surface_result r=state.dma_alloc(c->pci.hardware_index,c->owner,pages,(uintptr_t)&d->pages,0,0);
    /* Failed mappings may still return retained ownership needing cleanup. */
    if(d->pages.id) {d->next=c->dma;c->dma=d;}
    else {CALL(state.free,state.heap,d,0);return NULL;}
    if(r.status)return NULL;
    if(!(c->hcc&1) && (d->pages.physical>UINT32_MAX || d->pages.count*4096-1>UINT32_MAX-d->pages.physical))return NULL;
    return d;
}
static void ring_init(struct ring *r,struct dma *d,unsigned page) {
    *r=(struct ring){.trbs=(void *)(d->pages.address+page*4096),.physical=d->pages.physical+page*4096,.cycle=1};
    r->trbs[255]=(struct trb){.parameter=r->physical,.control=(6u<<10)|2};
}
static uint64_t enqueue(struct ring *r,struct trb trb) {
    unsigned i=r->tail;trb.control=(trb.control&~1u)|r->cycle;
    r->trbs[i].parameter=trb.parameter;r->trbs[i].status=trb.status;
    __atomic_store_n(&r->trbs[i].control,trb.control,__ATOMIC_RELEASE);
    uint64_t pointer=r->physical+i*16;
    if(++r->tail==255) {__atomic_store_n(&r->trbs[255].control,(6u<<10)|2|r->cycle,__ATOMIC_RELEASE);r->tail=0;r->cycle^=1;}
    return pointer;
}
static int available(struct controller *c,uint64_t offset,uint64_t bytes) {return offset<=c->extent && bytes<=c->extent-offset;}
static uintptr_t port_register(struct controller *c,unsigned port) {return c->op+0x400+(port-1)*16;}
static void complete_disconnected(struct controller *c,unsigned port) {
    for(unsigned s=1;s<=c->record.info.slots;++s)if(c->slots[s].token && c->slots[s].port==port)
        for(unsigned e=1;e<32;++e)if(c->slots[s].ep[e] && c->slots[s].ep[e]->result.state==HOST_PENDING)
            {c->slots[s].ep[e]->result.state=HOST_DISCONNECTED;notify(c->slots[s].ep[e]);}
}
static void port_update(struct controller *c,unsigned port,int initial) {
    struct xhci_port *p=&c->record.ports[port-1];uint32_t status=xhci_read32(port_register(c,port));
    /* CSC also catches a detach/reattach whose final CCS matches the old value. */
    if(initial || ((p->status^status)&1) || (status&(1u<<17))) {
        ++p->generation;if(!p->generation) {c->record.info.state=XHCI_FAILED;return;}
        if(!initial)complete_disconnected(c,port);
    }
    p->status=status;if(status&PORT_CHANGES)++p->changes;
    if(status&PORT_CHANGES)xhci_write32(port_register(c,port),(status&PORT_KEEP)|(status&PORT_CHANGES));
}
static void mark_failed(struct controller *c) {
    if(c->record.info.state==XHCI_FAILED)return;
    c->record.info.state=XHCI_FAILED;
    for(unsigned s=1;s<=c->record.info.slots;++s)if(c->slots && c->slots[s].token)
        for(unsigned e=1;e<32;++e)if(c->slots[s].ep[e] && c->slots[s].ep[e]->result.state==HOST_PENDING){c->slots[s].ep[e]->result.state=HOST_FAILED;notify(c->slots[s].ep[e]);}
}
/* Called under the short CPU lock. Bounded, no allocation and no client calls. */
static unsigned drain(struct controller *c) {
    struct trb *events=(void *)(c->base->pages.address+8192);unsigned n=0;
    while(n<RING_ENTRIES) {
        struct trb *p=&events[c->event_head];uint32_t control=__atomic_load_n(&p->control,__ATOMIC_ACQUIRE);
        if((control&1)!=c->event_cycle)break;
        struct trb event={.parameter=p->parameter,.status=p->status,.control=control};
        unsigned type=(control>>10)&63,code=event.status>>24;
        if(type==33) {
            if(c->command_wait && (event.parameter&~UINT64_C(15))==c->command_pointer) {c->command_code=code;c->command_slot=control>>24;c->command_wait=0;++c->command_events;}
            else mark_failed(c);
        } else if(type==34) {
            unsigned port=event.parameter>>24;if(port && port<=c->record.info.ports)port_update(c,port,0);else mark_failed(c);
        } else if(type==32) {
            unsigned slot=control>>24,ep=(control>>16)&31;
            if(slot && slot<=c->record.info.slots && ep && c->slots[slot].ep[ep]) {
                struct endpoint *endpoint=c->slots[slot].ep[ep];
                if(endpoint->result.state==HOST_PENDING) {
                    uint64_t pointer=event.parameter&~UINT64_C(15);unsigned residual=event.status&0xffffff;
                    if(pointer!=endpoint->event_pointer && pointer!=endpoint->data_pointer && pointer!=endpoint->setup_pointer)mark_failed(c);
                    else if(code==13 && ep==1 && pointer==endpoint->data_pointer && pointer!=endpoint->event_pointer) {
                        if(residual>endpoint->bytes)mark_failed(c);else {endpoint->short_seen=1;endpoint->short_bytes=endpoint->bytes-residual;}
                    } else {
                        endpoint->result.completion_code=code;
                        endpoint->result.state=(code==1 || code==13)?HOST_COMPLETE:HOST_FAILED;
                        if(endpoint->result.state==HOST_FAILED)endpoint->halted=1;
                        if(residual>endpoint->bytes) {endpoint->result.state=HOST_FAILED;endpoint->result.bytes=0;}
                        else endpoint->result.bytes=endpoint->short_seen?endpoint->short_bytes:endpoint->bytes-residual;
                        notify(endpoint);
                    }
                }
            } else mark_failed(c);
        } else if(type==37)mark_failed(c);
        else mark_failed(c); /* No silent event-ring loss or unknown event policy. */
        ++c->record.info.events;++n;if(++c->event_head==RING_ENTRIES) {c->event_head=0;c->event_cycle^=1;}
    }
    write64(c->runtime+0x38,c->base->pages.physical+8192+c->event_head*16+8); /* EHB RW1C */
    return n;
}
struct kernel_surface_result xhci_surface_interrupt(MODULE_ARGS) {
    MODULE_UNUSED();struct controller *ctrl=(void *)b;const struct irq_notification *notice=(const void *)a;
    if(!bsp() || !ctrl || !notice || notice->id!=ctrl->route.id || notice->owner!=ctrl->owner || !ctrl->base)return failure();
    uint64_t flags=lock();ctrl->callback_active=1;++ctrl->record.info.interrupts;
    if(ctrl->record.info.state!=XHCI_CLOSED) {
        xhci_write32(ctrl->runtime+0x20,3); /* IP RW1C, IE retained. */
        xhci_write32(ctrl->op+4,1u<<3); /* EINT RW1C */
        (void)drain(ctrl);
        if(xhci_read32(ctrl->op+4)&((1u<<2)|(1u<<12)))mark_failed(ctrl);
    }
    ctrl->callback_active=0;unlock(flags);return module_result(1,1);
}
static int command(struct controller *c,struct trb cmd,unsigned *slot) {
    uint64_t flags=lock();
    if(c->command_wait || c->record.info.state!=XHCI_ONLINE) {unlock(flags);return 0;}
    c->command_code=0;c->command_slot=0;c->command_wait=1;c->command_pointer=enqueue(&c->commands,cmd);xhci_write32(c->doorbells,0);unlock(flags);
    uint64_t start=now();if(start==UINT64_MAX)goto failed;
    for(unsigned i=0;i<2000000;++i) {
        flags=lock();unsigned waiting=c->command_wait,code=c->command_code,found=c->command_slot,online=c->record.info.state==XHCI_ONLINE;unlock(flags);
        if(!online)return 0;
        if(!waiting) {if(slot)*slot=found;return code==1;}
        uint64_t t=now();if(t==UINT64_MAX || t<start || t-start>TIMEOUT || !pause_work())goto failed;
    }
failed:
    flags=lock();mark_failed(c);unlock(flags);return 0;
}
static int capabilities(struct controller *c) {
    unsigned offset=(c->hcc>>16)*4,previous=0;
    while(offset) {
        if(offset<0x20 || offset<=previous || !available(c,offset,4))return 0;
        uint32_t cap=xhci_read32(c->registers+offset);unsigned kind=cap&255,next=(cap>>8)&255;
        if(kind==1) {
            if(!available(c,offset,8))return 0;
            xhci_write32(c->registers+offset,cap|(1u<<24));
            c->firmware_busy=1;
            if(!wait_bits(c->registers+offset,1u<<16,0))return 0;
            c->firmware_busy=0;
            /* Disable SMI enables; acknowledge only defined RW1C statuses. */
            xhci_write32(c->registers+offset+4,0xe0000000u);
        } else if(kind==2) {
            if(!available(c,offset,16) || xhci_read32(c->registers+offset+4)!=0x20425355)return 0;
            uint32_t ports=xhci_read32(c->registers+offset+8);unsigned first=ports&255,count=(ports>>8)&255,ids=ports>>28;
            unsigned major=cap>>24,minor=(cap>>16)&255,type=xhci_read32(c->registers+offset+12)&31;
            if(!first || !count || first>c->record.info.ports || count>c->record.info.ports-first+1 || (major!=2 && major!=3) || !available(c,offset+16,ids*4))return 0;
            for(unsigned i=first;i<first+count;++i) {
                struct xhci_port *p=&c->record.ports[i-1];if(p->protocol_major)return 0;
                p->protocol_major=major;p->protocol_minor=minor;p->slot_type=type;p->speed_id_count=ids;
                for(unsigned k=0;k<ids;++k)p->speed_ids[k]=xhci_read32(c->registers+offset+16+k*4);
            }
        }
        previous=offset;if(next && offset>UINT32_MAX-next*4)return 0;
    offset=next?offset+next*4:0;
    }
    for(unsigned p=0;p<c->record.info.ports;++p)if(!c->record.ports[p].protocol_major)return 0;
    return 1;
}
static int halt_reset(struct controller *c) {
    if(!c->op)return 1;
    xhci_write32(c->op,xhci_read32(c->op)&~5u);
    if(!wait_bits(c->op+4,1,1))return 0;
    xhci_write32(c->op,2);
    return wait_bits(c->op,2,0) && wait_bits(c->op+4,1u<<11,0);
}
static int cleanup(struct controller *c) {
    if(c->firmware_busy)return 0; /* Never reset a controller still owned by firmware. */
    if(c->routed) {
        struct pci_msix m={.hardware_index=c->pci.hardware_index,.owner=c->owner,.masked=1,.route=c->route};
        if(CALL(state.msix,&m,0,0).status || CALL(state.enable,c->route.id,c->owner,0).status)return 0;
    }
    if(!halt_reset(c))return 0;
    if(c->master && CALL(state.master,c->pci.hardware_index,c->owner,0).status)return 0;
    c->master=0;
    if(c->routed) {
        /* irq.release refuses queued/running notifications; retry after worker drains. */
        if(CALL(state.unroute,c->route.id,c->owner,1).status)return 0;
        c->routed=0;
    }
    while(c->dma) {struct dma *d=c->dma;
        if(CALL(state.dma_free,c->pci.hardware_index,c->owner,d->pages.id).status)return 0;
        c->dma=d->next;CALL(state.free,state.heap,d,0);
    }
    c->base=NULL;
    if(c->claimed && CALL(state.release,c->pci.hardware_index,c->owner,1).status)return 0;
    c->claimed=0;
    if(c->slots) {
        for(unsigned s=1;s<=c->record.info.slots;++s)for(unsigned e=1;e<32;++e)if(c->slots[s].ep[e])CALL(state.free,state.heap,c->slots[s].ep[e],0);
        CALL(state.free,state.heap,c->slots,0);c->slots=NULL;
    }
    c->record.info.state=XHCI_CLOSED;return 1;
}
struct kernel_surface_result xhci_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)a;
    if(state.hub)return module_result(1,state.hub==hub && state.heap==b);
    if(!hub || !b)return failure();
    state.hub=hub;state.heap=b;
#define GET(field,name) state.field=kernel_surface_find_version(hub,name,1,0);if(!state.field)goto fail
    GET(cpu,"cpu.control");GET(alloc,"mem.calloc");GET(free,"mem.free");GET(claim,"pci.claim");GET(master,"pci.busmaster");GET(msix,"pci.msix");GET(dma_alloc,"pci.dma_alloc");GET(dma_free,"pci.dma_free");GET(release,"pci.release");GET(route,"irq.route");GET(enable,"irq.enable");GET(unroute,"irq.release");GET(now,"clock.now_ns");GET(thread,"threads.control");GET(irq_context,"irq.context");
#undef GET
    const struct kernel_hub_table *hw=kernel_hub_find_table(hub,HWENUM_TABLE_NAME);
    kernel_surface_call publish=kernel_surface_find_version(hub,"hub.publish_table",1,0);
    if(!bsp() || !hw || !publish)goto fail;
    state.hw=(const void *)hw->address;
    state.table=(struct xhci_table){.magic=XHCI_TABLE_MAGIC,.major=1,.record_bytes=sizeof(struct xhci_record)};
    struct kernel_hub_table table={.name=XHCI_TABLE_NAME,.address=(uintptr_t)&state.table,.bytes=sizeof(state.table),.count=1,.entry_bytes=sizeof(state.table)};
    if(CALL(publish,&table,0,0).status)goto fail;
    return module_result(1,1);
fail:state.hub=NULL;return failure();
}
static struct kernel_surface_result open_controller(MODULE_ARGS) {
    MODULE_UNUSED();if(!foreground() || !CALL(state.thread,THREAD_CURRENT,0,0).value || !b || a>=state.hw->count || state.serial==UINT64_MAX)return failure();
    const struct hwenum_descriptor *h=&state.hw->entries[a];
    if(h->kind!=HWENUM_PCI || h->data.pci.class_code!=12 || h->data.pci.subclass!=3 || h->data.pci.programming_interface!=0x30)return failure();
    struct controller *ctrl=NULL;
    for(struct controller *p=state.head;p;p=p->next)if(p->record.info.hardware_index==a) {if(p->record.info.state!=XHCI_CLOSED)return failure();ctrl=p;break;}
    uint64_t flags;
    if(ctrl) {
        flags=lock();ctrl->busy=1;ctrl->record.info.state=XHCI_OPENING;struct xhci_port *old_ports=ctrl->record.ports;ctrl->record.ports=NULL;unlock(flags);
        struct controller *next=ctrl->next;struct xhci_record *record_next=ctrl->record.next;
        if(old_ports)CALL(state.free,state.heap,old_ports,0);
        flags=lock();zero(ctrl,sizeof(*ctrl));ctrl->next=next;ctrl->record.next=record_next;ctrl->busy=1;ctrl->record.info.hardware_index=a;ctrl->record.info.state=XHCI_OPENING;unlock(flags);
    } else {
        ctrl=allocate(1,sizeof(*ctrl));if(!ctrl)return failure();
        flags=lock();ctrl->next=state.head;state.head=ctrl;ctrl->record.next=state.table.first;state.table.first=&ctrl->record;++state.table.count;unlock(flags);
    }
    ctrl->owner=next_id();if(!ctrl->owner)return failure();
    ctrl->record.info=(struct xhci_info){.id=ctrl->owner,.generation=ctrl->owner,.hardware_index=a,.state=XHCI_OPENING};ctrl->busy=1;
    if(CALL(state.claim,a,ctrl->owner,&ctrl->pci).status)goto failed;
    ctrl->claimed=1;ctrl->record.info.stage=1;
    ctrl->registers=ctrl->pci.bars[0].address;ctrl->extent=ctrl->pci.bars[0].bytes;
    if(!ctrl->registers || ctrl->extent<0x20 || !ctrl->pci.msix_count)goto failed;
    uint32_t cap=xhci_read32(ctrl->registers),hcs=xhci_read32(ctrl->registers+4),hcs2=xhci_read32(ctrl->registers+8);
    ctrl->hcc=xhci_read32(ctrl->registers+16);unsigned length=cap&255;
    ctrl->record.info.version=cap>>16;ctrl->record.info.slots=hcs&255;ctrl->record.info.ports=hcs>>24;ctrl->record.info.context_bytes=(ctrl->hcc&4)?64:32;
    ctrl->record.info.scratchpads=((hcs2>>27)&31)|((hcs2>>16)&0x3e0);
    uint32_t db=xhci_read32(ctrl->registers+20)&~3u,rt=xhci_read32(ctrl->registers+24)&~31u;
    if(length<0x20 || ctrl->record.info.version<0x100 || !ctrl->record.info.slots || !((hcs>>8)&2047) || !ctrl->record.info.ports || !available(ctrl,length,0x400+ctrl->record.info.ports*16) || !available(ctrl,db,(ctrl->record.info.slots+1)*4) || !available(ctrl,rt,0x40))goto failed;
    ctrl->op=ctrl->registers+length;ctrl->doorbells=ctrl->registers+db;ctrl->runtime=ctrl->registers+rt;
    ctrl->record.ports=allocate(ctrl->record.info.ports,sizeof(struct xhci_port));ctrl->slots=allocate(ctrl->record.info.slots+1,sizeof(struct slot));
    if(!ctrl->record.ports || !ctrl->slots || !capabilities(ctrl))goto failed;
    ctrl->record.info.stage=2;
    if(!wait_bits(ctrl->op+4,1u<<11,0) || !halt_reset(ctrl) || !(xhci_read32(ctrl->op+8)&1))goto failed;
    ctrl->record.info.stage=3;
    ctrl->base=dma_allocate(ctrl,4);if(!ctrl->base)goto failed;
    ring_init(&ctrl->commands,ctrl->base,1);ctrl->event_cycle=1;
    uint64_t *dcbaa=(void *)ctrl->base->pages.address;
    if(ctrl->record.info.scratchpads) {
        struct dma *array=dma_allocate(ctrl,(ctrl->record.info.scratchpads*8+4095)/4096);if(!array)goto failed;
        uint64_t *p=(void *)array->pages.address;dcbaa[0]=array->pages.physical;
        for(unsigned i=0;i<ctrl->record.info.scratchpads;++i) {struct dma *scratch=dma_allocate(ctrl,1);if(!scratch)goto failed;
    p[i]=scratch->pages.physical;}
    }
    struct {uint64_t base;uint32_t size,reserved;} *erst=(void *)(ctrl->base->pages.address+12288);
    erst->base=ctrl->base->pages.physical+8192;erst->size=RING_ENTRIES;
    /* ERSTBA programming may immediately fetch its DMA segment descriptor.
     * All buffers are mapped and the controller is reset/halted at this point. */
    if(CALL(state.master,a,ctrl->owner,1).status)goto failed;
    ctrl->master=1;
    write64(ctrl->op+0x30,ctrl->base->pages.physical);write64(ctrl->op+0x18,ctrl->commands.physical|1);xhci_write32(ctrl->op+0x38,ctrl->record.info.slots);
    xhci_write32(ctrl->runtime+0x28,1);write64(ctrl->runtime+0x30,ctrl->base->pages.physical+12288);write64(ctrl->runtime+0x38,ctrl->base->pages.physical+8192);xhci_write32(ctrl->runtime+0x24,0);
    struct irq_request request={.kind=IRQ_MSI,.owner=ctrl->owner,.callback=kernel_surface_find_version(state.hub,"xhci.interrupt",1,0),.context=(uintptr_t)ctrl,.major=1};
    if(!request.callback || CALL(state.route,&request,&ctrl->route,0).status)goto failed;
    ctrl->routed=1;ctrl->record.info.route_id=ctrl->route.id;
    struct pci_msix m={.hardware_index=a,.owner=ctrl->owner,.route=ctrl->route};
    if(CALL(state.msix,&m,0,0).status || CALL(state.enable,ctrl->route.id,ctrl->owner,1).status)goto failed;
    ctrl->record.info.stage=4;
    if(ctrl->hcc&8) { /* PPC: power all root ports before exposing their state. */
        int changed=0;for(unsigned p=1;p<=ctrl->record.info.ports;++p) {uintptr_t reg=port_register(ctrl,p);uint32_t status=xhci_read32(reg);
            if(!(status&(1u<<9))) {xhci_write32(reg,(status&PORT_KEEP)|(1u<<9));changed=1;}
        }
        if(changed) {uint64_t start=now(),t=start;unsigned tries=0;
            do {t=now();if(t==UINT64_MAX || t<start || ++tries>2000000 || !pause_work())goto failed;}while(t-start<20000000);
        }
    }
    xhci_write32(ctrl->runtime+0x20,2);xhci_write32(ctrl->op,5);
    if(!wait_bits(ctrl->op+4,1,0))goto failed;
    flags=lock();for(unsigned p=1;p<=ctrl->record.info.ports;++p) {ctrl->record.ports[p-1].number=p;port_update(ctrl,p,1);}ctrl->record.info.state=XHCI_ONLINE;ctrl->record.info.stage=5;unlock(flags);
    /* Prove command/event DMA and real IRQ completion before publishing ready. */
    if(!command(ctrl,(struct trb){.control=23u<<10},NULL))goto failed;
    *(struct xhci_info *)b=ctrl->record.info;return finish(ctrl,ctrl->record.info.id,1);
failed:
    flags=lock();mark_failed(ctrl);unlock(flags);
    (void)cleanup(ctrl);*(struct xhci_info *)b=ctrl->record.info;return finish(ctrl,ctrl->record.info.id,0);
}
struct kernel_surface_result xhci_surface_open(MODULE_ARGS) {
    if(!foreground())return failure();
    uint64_t flags=lock();if(state.opening) {unlock(flags);return failure();}state.opening=1;unlock(flags);
    struct kernel_surface_result r=open_controller(a,b,c,d,e,f);
    flags=lock();state.opening=0;unlock(flags);return r;
}
struct kernel_surface_result xhci_surface_info(MODULE_ARGS) {
    MODULE_UNUSED();struct controller *p=find(a);if(!bsp() || !p || !b)return failure();
    uint64_t flags=lock();if(p->record.info.id!=a) {unlock(flags);return failure();}*(struct xhci_info *)b=p->record.info;unlock(flags);return module_result(1,1);
}
struct kernel_surface_result xhci_surface_port(MODULE_ARGS) {
    MODULE_UNUSED();struct controller *p=find(a);if(!bsp() || !p || !c || !b || b>p->record.info.ports || !p->record.ports)return failure();
    uint64_t flags=lock();if(p->record.info.id!=a || !p->record.ports || b>p->record.info.ports) {unlock(flags);return failure();}*(struct xhci_port *)c=p->record.ports[b-1];unlock(flags);return module_result(1,1);
}
struct kernel_surface_result xhci_surface_reset_port(MODULE_ARGS) {
    MODULE_UNUSED();struct controller *p=find(a);if(!begin(p,a))return failure();
    if(!b || b>p->record.info.ports)return finish(p,0,0);
    for(unsigned s=1;s<=p->record.info.slots;++s)if(p->slots[s].token && p->slots[s].port==b)return finish(p,0,0);
    uintptr_t reg=port_register(p,b);uint32_t status=xhci_read32(reg);if(!(status&1))return finish(p,0,0);
    unsigned reset=p->record.ports[b-1].protocol_major==3?(1u<<31):(1u<<4);
    xhci_write32(reg,(status&PORT_KEEP)|reset);
    int ok=wait_bits(reg,reset,0) && (xhci_read32(reg)&3)==3;
    uint64_t flags=lock();port_update(p,b,0);unlock(flags);return finish(p,1,ok);
}
static struct slot *device_find(struct controller *c,uint64_t token,unsigned *index,int connected) {
    if(!token || !c || !c->slots)return NULL;
    for(unsigned s=1;s<=c->record.info.slots;++s)if(c->slots[s].token==token) {
        struct slot *slot=&c->slots[s];struct xhci_port *p=&c->record.ports[slot->port-1];
        if(connected && (p->generation!=slot->port_generation || (p->status&3)!=3))return NULL;
        if(index)*index=s;
        return slot;
    }return NULL;
}
static unsigned endpoint_index(unsigned address) {return address==0?1:(address&15)*2+!!(address&128);}
static struct endpoint *endpoint_allocate(struct controller *c,struct slot *slot,unsigned dci) {
    if(slot->ep[dci])return slot->ep[dci];
    struct endpoint *ep=allocate(1,sizeof(*ep));if(!ep)return NULL;
    ep->storage=dma_allocate(c,2);if(!ep->storage) {CALL(state.free,state.heap,ep,0);return NULL;}
    ring_init(&ep->ring,ep->storage,0);slot->ep[dci]=ep;return ep;
}
static uint32_t *input(struct controller *c,struct slot *s,unsigned index) {return (void *)(s->contexts->pages.address+4096+index*c->record.info.context_bytes);}
static void context_endpoint(struct controller *c,struct slot *s,unsigned dci,struct endpoint *ep,unsigned type,unsigned packet,unsigned interval,unsigned burst,unsigned esit) {
    uint32_t *ctx=input(c,s,dci+1);ctx[0]=interval<<16;ctx[1]=(3u<<1)|(type<<3)|(burst<<8)|(packet<<16);
    ctx[2]=(uint32_t)ep->ring.physical|1;ctx[3]=ep->ring.physical>>32;ctx[4]=(dci==1?8u:packet)|(esit<<16);
}
struct kernel_surface_result xhci_surface_device(MODULE_ARGS) {
    MODULE_UNUSED();struct controller *p=find(a);const struct host_device_request *r=(const void *)b;if(!r || !c || !begin(p,a))return failure();
    if(!r->port || r->port>p->record.info.ports || !r->max_packet || r->max_packet>512 || state.serial==UINT64_MAX)return finish(p,0,0);
    struct xhci_port port=p->record.ports[r->port-1];if((port.status&3)!=3 || port.generation!=r->port_generation)return finish(p,0,0);
    for(unsigned i=1;i<=p->record.info.slots;++i)if(p->slots[i].token && p->slots[i].port==r->port)return finish(p,0,0);
    unsigned slot=0;if(!command(p,(struct trb){.control=(9u<<10)|(port.slot_type<<16)},&slot) || !slot || slot>p->record.info.slots)return finish(p,0,0);
    struct slot *s=&p->slots[slot];s->port=r->port;s->port_generation=port.generation;s->token=next_id();
    for(unsigned i=1;i<32;++i)if(s->ep[i] && (s->ep[i]->result.id || s->ep[i]->waiter))goto fail;
    if(!s->contexts)s->contexts=dma_allocate(p,2);
    struct endpoint *ep=endpoint_allocate(p,s,1);
    if(!s->contexts || !ep)goto fail;
    zero((void *)s->contexts->pages.address,8192);zero(ep->ring.trbs,4096);ring_init(&ep->ring,ep->storage,0);
    uint64_t *dcbaa=(void *)p->base->pages.address;dcbaa[slot]=s->contexts->pages.physical;
    input(p,s,0)[1]=3;uint32_t *ctx=input(p,s,1);ctx[0]=((port.status>>10)&15)<<20|1u<<27;ctx[1]=r->port<<16;
    context_endpoint(p,s,1,ep,4,r->max_packet,0,0,0);
    if(!command(p,(struct trb){.parameter=s->contexts->pages.physical+4096,.control=(11u<<10)|(slot<<24)},NULL))goto fail;
    uint64_t flags=lock();struct xhci_port *latest=&p->record.ports[r->port-1];int connected=latest->generation==r->port_generation && (latest->status&3)==3;
    if(connected) {s->addressed=1;ep->configured=1;ep->halted=0;ep->definition=(struct host_endpoint_request){.device=s->token,.type=HOST_CONTROL,.max_packet=r->max_packet};*(uint64_t *)c=s->token;}
    unlock(flags);if(!connected)goto fail;
    return finish(p,s->token,1);
fail:
    if(command(p,(struct trb){.control=(10u<<10)|(slot<<24)},NULL)) {s->token=0;s->addressed=0;}
    else {uint64_t flags=lock();mark_failed(p);unlock(flags);}
    return finish(p,0,0);
}
struct kernel_surface_result xhci_surface_endpoint(MODULE_ARGS) {
    MODULE_UNUSED();struct controller *p=find(a);const struct host_endpoint_request *r=(const void *)b;if(!r || !begin(p,a))return failure();
    unsigned number;
    struct slot *s=device_find(p,r->device,&number,1);
    if(s && s->addressed && !r->address && r->type==HOST_CONTROL && r->max_packet && r->max_packet<=512 && !r->interval && !r->max_burst && !r->max_esit) {
        struct endpoint *ep=s->ep[1];if(!ep || ep->result.id || ep->halted)return finish(p,0,0);
        zero((void *)(s->contexts->pages.address+4096),4096);input(p,s,0)[1]=2;
        copy(input(p,s,2),(void *)(s->contexts->pages.address+p->record.info.context_bytes),p->record.info.context_bytes);
        input(p,s,2)[1]=(input(p,s,2)[1]&65535)|(r->max_packet<<16);
        if(!command(p,(struct trb){.parameter=s->contexts->pages.physical+4096,.control=(13u<<10)|(number<<24)},NULL))return finish(p,0,0);
        ep->definition.max_packet=r->max_packet;return finish(p,1,1);
    }
    if(!s || !s->addressed || !(r->address&15) || (r->address&~143u) || (r->type!=HOST_BULK && r->type!=HOST_INTERRUPT) || !r->max_packet || r->max_packet>1024 || r->interval>15 || r->max_burst>15 || r->max_esit>65535)return finish(p,0,0);
    unsigned dci=endpoint_index(r->address);struct endpoint *ep=endpoint_allocate(p,s,dci);if(!ep || ep->configured)return finish(p,0,0);
    zero(ep->ring.trbs,4096);ring_init(&ep->ring,ep->storage,0);ep->halted=0;
    zero((void *)(s->contexts->pages.address+4096),4096);
    /* Configure Endpoint requires a copy of the current output Slot Context. */
    copy(input(p,s,1),(void *)s->contexts->pages.address,p->record.info.context_bytes);
    unsigned entries=(input(p,s,1)[0]>>27)&31;if(dci>entries)input(p,s,1)[0]=(input(p,s,1)[0]&~(31u<<27))|(dci<<27);
    input(p,s,0)[1]=1u|(1u<<dci);unsigned type=r->type==HOST_BULK?2:3;if(r->address&128)type+=4;
    context_endpoint(p,s,dci,ep,type,r->max_packet,r->interval,r->max_burst,r->max_esit);
    if(!command(p,(struct trb){.parameter=s->contexts->pages.physical+4096,.control=(12u<<10)|(number<<24)},NULL))return finish(p,0,0);
    uint64_t flags=lock();ep->configured=1;ep->definition=*r;unlock(flags);return finish(p,1,1);
}
struct kernel_surface_result xhci_surface_transfer(MODULE_ARGS) {
    MODULE_UNUSED();struct controller *p=find(a);const struct host_transfer_request *r=(const void *)b;
    if(!foreground() || !p || !r || r->bytes>4096 || (r->bytes && !r->buffer) || (r->endpoint&~143u) || (r->endpoint && !(r->endpoint&15)) || state.serial==UINT64_MAX)return failure();
    uint64_t flags=lock();unsigned number;struct slot *s=device_find(p,r->device,&number,1);unsigned dci=endpoint_index(r->endpoint);
    struct endpoint *ep=s?s->ep[dci]:NULL;
    if(p->record.info.id!=a || p->busy || p->record.info.state!=XHCI_ONLINE || !s || !s->addressed || !ep || !ep->configured || ep->halted || ep->result.id) {unlock(flags);return failure();}
    unsigned direction=dci==1?!!(r->setup[0]&128):!!(r->endpoint&128);
    if(dci==1 && ((unsigned)r->setup[6]|((unsigned)r->setup[7]<<8))!=r->bytes) {unlock(flags);return failure();}
    ep->result=(struct host_transfer_result){.id=next_id(),.state=HOST_PENDING};ep->buffer=r->buffer;ep->bytes=r->bytes;ep->direction=direction;ep->short_seen=0;
    /* At most one 4KiB copy here; no allocation or callbacks under this lock. */
    void *buffer=(void *)(ep->storage->pages.address+4096);if(direction)zero(buffer,4096);else if(r->bytes)copy(buffer,r->buffer,r->bytes);
    if(dci==1) {
        uint64_t setup=0;for(unsigned i=0;i<8;++i)setup|=(uint64_t)r->setup[i]<<(i*8);
        ep->setup_pointer=enqueue(&ep->ring,(struct trb){.parameter=setup,.status=8,.control=(2u<<10)|(1u<<6)|((r->bytes?(direction?3u:2u):0u)<<16)});
        ep->data_pointer=0;if(r->bytes)ep->data_pointer=enqueue(&ep->ring,(struct trb){.parameter=ep->storage->pages.physical+4096,.status=r->bytes,.control=(3u<<10)|(1u<<2)|(direction<<16)});
        ep->event_pointer=enqueue(&ep->ring,(struct trb){.control=(4u<<10)|(1u<<5)|((!r->bytes || !direction)?1u<<16:0)});
    } else ep->data_pointer=ep->event_pointer=enqueue(&ep->ring,(struct trb){.parameter=ep->storage->pages.physical+4096,.status=r->bytes,.control=(1u<<10)|(1u<<5)|(1u<<2)});
    xhci_write32(p->doorbells+number*4,dci);uint64_t id=ep->result.id;unlock(flags);return module_result(id,1);
}
static struct endpoint *request_find(struct controller *p,uint64_t id) {
    if(!id || !p->slots)return NULL;
    for(unsigned s=1;s<=p->record.info.slots;++s)for(unsigned e=1;e<32;++e)if(p->slots[s].ep[e] && p->slots[s].ep[e]->result.id==id)return p->slots[s].ep[e];
    return NULL;
}
struct kernel_surface_result xhci_surface_result(MODULE_ARGS) {
    MODULE_UNUSED();struct controller *p=find(a);if(!foreground() || !p || !c || d>1)return failure();
    uint64_t flags=lock();struct endpoint *ep=request_find(p,b);if(p->record.info.id!=a || !ep) {unlock(flags);return failure();}
    *(struct host_transfer_result *)c=ep->result;
    if(d && ep->waiter) {unlock(flags);return failure();}
    if(d && ep->result.state!=HOST_PENDING) {
        if(ep->result.state==HOST_COMPLETE && ep->direction && ep->result.bytes)copy(ep->buffer,(void *)(ep->storage->pages.address+4096),ep->result.bytes);
        ep->result=(struct host_transfer_result){0};ep->buffer=NULL;
    }
    unlock(flags);return module_result(1,1);
}
struct kernel_surface_result xhci_surface_cancel(MODULE_ARGS) {
    MODULE_UNUSED();struct controller *p=find(a);if(!begin(p,a))return failure();
    unsigned number;
    struct slot *s=device_find(p,b,&number,0);unsigned dci=endpoint_index(c);
    struct endpoint *ep=s && !(c&~143u) && (!c || (c&15))?s->ep[dci]:NULL;
    if(!ep || !ep->configured)return finish(p,0,0);
    uint32_t *out=(void *)(s->contexts->pages.address+dci*p->record.info.context_bytes);unsigned epstate=out[0]&7;
    unsigned pending=ep->result.state==HOST_PENDING;
    unsigned cmd=epstate==2?14:15;
    if(!command(p,(struct trb){.control=(cmd<<10)|(dci<<16)|(number<<24)},NULL))return finish(p,0,0);
    if(!command(p,(struct trb){.parameter=(ep->ring.physical+ep->ring.tail*16)|ep->ring.cycle,.control=(16u<<10)|(dci<<16)|(number<<24)},NULL))return finish(p,0,0);
    uint64_t flags=lock();if(pending && ep->result.state!=HOST_COMPLETE && ep->result.state!=HOST_DISCONNECTED)ep->result.state=HOST_CANCELLED;ep->halted=0;notify(ep);unlock(flags);return finish(p,1,1);
}
struct kernel_surface_result xhci_surface_remove(MODULE_ARGS) {
    MODULE_UNUSED();struct controller *p=find(a);if(!begin(p,a))return failure();
    unsigned number;
    struct slot *s=device_find(p,b,&number,0);if(!s)return finish(p,0,0);
    if(!command(p,(struct trb){.control=(10u<<10)|(number<<24)},NULL))return finish(p,0,0);
    uint64_t flags=lock();for(unsigned i=1;i<32;++i)if(s->ep[i]) {if(s->ep[i]->result.state==HOST_PENDING)s->ep[i]->result.state=HOST_DISCONNECTED;notify(s->ep[i]);s->ep[i]->configured=0;}
    s->addressed=0;s->token=0;((uint64_t *)p->base->pages.address)[number]=0;unlock(flags);return finish(p,1,1);
}
struct kernel_surface_result xhci_surface_close(MODULE_ARGS) {
    MODULE_UNUSED();struct controller *p=find(a);if(!foreground() || !CALL(state.thread,THREAD_CURRENT,0,0).value || !p)return failure();
    uint64_t flags=lock();if(p->record.info.id!=a || p->busy || p->callback_active) {unlock(flags);return failure();}
    if(p->record.info.state==XHCI_CLOSED) {unlock(flags);return module_result(1,1);}
    /* Do not drop retained results or a completion waiter's storage on close. */
    if(p->slots)for(unsigned slot=1;slot<=p->record.info.slots;++slot)for(unsigned ep=1;ep<32;++ep)
        if(p->slots[slot].ep[ep] && (p->slots[slot].ep[ep]->result.id || p->slots[slot].ep[ep]->waiter)) {unlock(flags);return failure();}
    p->busy=1;p->record.info.state=XHCI_QUIESCING;unlock(flags);
    int ok=cleanup(p);if(!ok) {flags=lock();mark_failed(p);unlock(flags);}return finish(p,1,ok);
}

struct kernel_surface_result xhci_surface_wait(MODULE_ARGS) {
    MODULE_UNUSED();struct controller *p=find(a);if(!foreground() || !p || !c || !d)return failure();
    uint64_t current=CALL(state.thread,THREAD_CURRENT,0,0).value;if(!current)return failure();
    for(;;) {
        uint64_t time=now(),flags=lock();struct endpoint *ep=request_find(p,b);
        if(p->record.info.id!=a || !ep || (ep->waiter && ep->waiter!=current)) {unlock(flags);return failure();}
        if(ep->result.state!=HOST_PENDING) {*(struct host_transfer_result *)c=ep->result;ep->waiter=0;unlock(flags);return module_result(1,1);}
        if(time==UINT64_MAX || time>=d) {ep->waiter=0;unlock(flags);return failure();}
        ep->waiter=current;unlock(flags);
        /* Wake credits cover completion between the check and blocking. */
        if(CALL(state.thread,THREAD_BLOCK_UNTIL,d,0).status) {flags=lock();ep->waiter=0;unlock(flags);return failure();}
    }
}
