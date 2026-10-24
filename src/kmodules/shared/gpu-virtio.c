/* Modern PCI virtio-gpu transport and private display-provider IO gateway.
 * No GPU takeover or OpenGL renderer is hidden in enumeration. Hardware work is explicit and owned per instance. */
#include "gpu-virtio.h"
#include "virtio/pci.h"
#include "virtio/gpu-wire.h"
#include "module-load.h"
#include "pci.h"
#include "iommu.h"
#include "hwenum.h"
#include "render-selection.h"
#include "display-boot.h"
#include "threads.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
#define COMMAND_BYTES 16384u
#define REQUEST_OFFSET 4096u
#define RESPONSE_OFFSET (REQUEST_OFFSET+COMMAND_BYTES)
#define QUEUE_PAGES ((RESPONSE_OFFSET+COMMAND_BYTES)/4096u)
#define TIMEOUT_NS UINT64_C(500000000)
struct resource {
    struct resource *next;
    struct gpu_virtio_resource public;
    struct mem_pages dma;
    unsigned created,attached,texture;
};
struct attachment {struct attachment *next;uint32_t context,resource;};
struct context {
    struct context *next;
    uint32_t id;
};
struct controller {
    struct gpu_virtio_info *info;
    struct pci_device device;
    struct virtio_layout layout;
    struct mem_pages dma;
    struct irq_route route;
    struct virtq queue;
    struct resource *resources;
    struct context *contexts;
    struct attachment *attachments;
    uintptr_t common,notify,isr,configuration;
    uint64_t owner,fence,reader,bridge_process;
    uint32_t resource_serial,context_serial;
    unsigned owned,owner_pinned,busy,waiting,completed,completion_error;
    uint32_t response_bytes;
    uintptr_t probe_common,probe_vga;
    uint16_t boot_vga[10];
    unsigned qualified;
    uint16_t segment;
    uint8_t bus,slot,function;
};
static struct {
    const struct kernel_surfaces *hub;
    uintptr_t heap;
    uint64_t module_owner,serial;
    unsigned management,published;
    struct controller *controllers,*selected;
    const struct hwenum_table *hardware;
    struct gpu_virtio_table table;
    kernel_surface_call alloc,drop,config_read,pci_info,map_mmio,boot_info,claim,master,dma_alloc,dma_free,release;
    kernel_surface_call irq_route,irq_enable,irq_release,msix,clock,thread,cpu,irq_context,dma_ready,iommu_info,pin;
}
state;
static void copy(void *to,const void *from,size_t n){
    unsigned char *d=to;
    const unsigned char *s=from;
    for(size_t i=0;i<n;i++)d[i]=s[i];
}
static void zero(void *to,size_t n){
    unsigned char *d=to;
    for(size_t i=0;i<n;i++)d[i]=0;
}
static void *allocate(size_t n){
    struct kernel_surface_result r=CALL(state.alloc,state.heap,1,n);
    return r.status?NULL:(void *)r.value;
}
static void drop(void *p){
    if(p)CALL(state.drop,state.heap,p,0);
}
static uint8_t r8(uintptr_t p){
    return *(volatile uint8_t *)p;
}
static uint16_t r16(uintptr_t p){
    return *(volatile uint16_t *)p;
}
#ifndef GPU_VIRTIO_READ32
#define GPU_VIRTIO_READ32(p) (*(volatile uint32_t *)(p))
#endif
static uint32_t r32(uintptr_t p){
    return GPU_VIRTIO_READ32(p);
}
static void barrier(void){
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
static void w8(uintptr_t p,uint8_t v){
    barrier();
    *(volatile uint8_t *)p=v;
    barrier();
}
#ifndef GPU_VIRTIO_WRITE16
#define GPU_VIRTIO_WRITE16(p,v) (*(volatile uint16_t *)(p)=(v))
#endif
static void w16(uintptr_t p,uint16_t v){
    barrier();
    GPU_VIRTIO_WRITE16(p,v);
    barrier();
}
static void w32(uintptr_t p,uint32_t v){
    barrier();
    *(volatile uint32_t *)p=v;
    barrier();
}
static void w64(uintptr_t p,uint64_t v){
    w32(p,v);
    w32(p+4,v>>32);
}
static uint64_t now(void){
    struct kernel_surface_result r=CALL(state.clock,0,0,0);
    return r.status?UINT64_MAX:r.value;
}
static int foreground(void){
    uint64_t flags;
    __asm__ volatile("pushfq;pop %0":"=r"(flags));
    return state.hub&&(flags&512)&&!CALL(state.cpu,CPU_CURRENT,0,0).value&&!CALL(state.cpu,CPU_DEPTH,0,0).value&&!CALL(state.irq_context,0,0,0).value&&CALL(state.thread,THREAD_CURRENT,0,0).value;
}
static struct controller *hardware(uint64_t index){
    for(uint64_t i=0;i<state.table.count;i++)if(state.controllers[i].info->hardware_index==index)return state.controllers+i;
    return NULL;
}
static struct controller *instance(uint64_t cookie){
    if(!cookie)return NULL;
    for(uint64_t i=0;i<state.table.count;i++)if(state.controllers[i].info->cookie==cookie&&(state.controllers[i].owned||state.controllers[i].owner_pinned))return state.controllers+i;
    return NULL;
}
static struct controller *lock(uint64_t cookie){
    struct controller *s=instance(cookie);
    if(!foreground()||!s||__atomic_exchange_n(&s->busy,1,__ATOMIC_ACQUIRE))return NULL;
    return s;
}
static void unlock(struct controller *s){
    __atomic_store_n(&s->busy,0,__ATOMIC_RELEASE);
}
static int failed(struct controller *s,unsigned error){
    s->info->state=GPU_FAILED;
    s->info->error=error;
    return 0;
}
static int config_snapshot(struct controller *s,unsigned char out[256]){
    for(unsigned i=0;i<256;i+=4){
        struct pci_firmware_access r={
            .segment=s->segment,.bus=s->bus,.slot=s->slot,.function=s->function,.offset=i,.width=4
        };
        if(CALL(state.config_read,&r,0,0).status)return 0;
        for(unsigned b=0;b<4;b++)out[i+b]=r.value>>(8*b);
    }
    return 1;
}
struct kernel_surface_result gpu_virtio_init(MODULE_ARGS){
    MODULE_UNUSED();
    const struct module_init_context *ctx=(const void *)a;
    if(!ctx||ctx->abi!=2||!ctx->hub||!ctx->heap||state.hub)return module_result(0,0);
    state.heap=ctx->heap;
    state.module_owner=ctx->owner;
#define BIND(field,name) do{state.field=kernel_surface_find(ctx->hub,name);if(!state.field)goto fail;}while(0)
    BIND(alloc,"mem.calloc");
    BIND(drop,"mem.free");
    BIND(config_read,"pci.firmware_read");
    BIND(pci_info,"pci.device");
    BIND(map_mmio,"hardware.map_mmio");
    BIND(boot_info,"display.boot_info");
    state.claim=kernel_surface_find_version(ctx->hub,"pci.claim",1,1);if(!state.claim)goto fail;
    BIND(master,"pci.busmaster");
    BIND(dma_alloc,"pci.dma_alloc");
    BIND(dma_free,"pci.dma_free");
    BIND(release,"pci.release");
    BIND(msix,"pci.msix");
    BIND(irq_route,"irq.route");
    BIND(irq_enable,"irq.enable");
    BIND(irq_release,"irq.release");
    BIND(clock,"clock.now_ns");
    BIND(thread,"threads.control");
    BIND(cpu,"cpu.control");
    BIND(irq_context,"irq.context");
    BIND(dma_ready,"iommu.ready");
    BIND(iommu_info,"iommu.info");
    BIND(pin,"hub.owner_pin");
#undef BIND
    const struct kernel_hub_table *t=kernel_hub_find_table(ctx->hub,HWENUM_TABLE_NAME);
    if(!t||t->bytes<sizeof(struct hwenum_table))goto fail;
    const struct hwenum_table *hw=(const void *)t->address;
    if(hw->magic!=HWENUM_TABLE_MAGIC||hw->major!=1||hw->entry_bytes!=sizeof(struct hwenum_descriptor)||hw->count>(t->bytes-sizeof(*hw))/sizeof(struct hwenum_descriptor))goto fail;
    state.hardware=hw;
    size_t count=0;
    for(size_t i=0;i<hw->count;i++){
        const struct hwenum_descriptor *d=hw->entries+i;
        if(d->kind==HWENUM_PCI&&d->data.pci.vendor==0x1af4&&d->data.pci.device==0x1050&&d->data.pci.class_code==3)++count;
    }
    if(count>SIZE_MAX/sizeof(struct controller)||count>SIZE_MAX/sizeof(struct gpu_virtio_info))goto fail;
    state.controllers=allocate((count?count:1)*sizeof(*state.controllers));
    struct gpu_virtio_info *infos=allocate((count?count:1)*sizeof(*infos));
    state.table=(struct gpu_virtio_table){
        .bytes=sizeof(state.table),.major=1,.count=count,.devices=infos
    };
    if(!state.controllers||!infos)goto fail;
    count=0;
    for(size_t i=0;i<hw->count;i++){
        const struct hwenum_descriptor *d=hw->entries+i;
        if(d->kind!=HWENUM_PCI||d->data.pci.vendor!=0x1af4||d->data.pci.device!=0x1050||d->data.pci.class_code!=3)continue;
        struct controller *s=state.controllers+count;
        s->info=infos+count++;
        *s->info=(struct gpu_virtio_info){
            .hardware_index=i,.state=GPU_DISCOVERED
        };
        s->segment=d->data.pci.segment;
        s->bus=d->data.pci.bus;
        s->slot=d->data.pci.slot;
        s->function=d->data.pci.function;
        unsigned char configuration[256];
        if(!config_snapshot(s,configuration)||!virtio_pci_layout(configuration,&s->layout))failed(s,GPU_CONFIG);
    }
    state.hub=ctx->hub;
    return module_result(count,1);
    fail:if(state.drop){
        drop(state.controllers);
        drop((void *)state.table.devices);
    }
    state=(__typeof__(state)){
        0
    };
    return module_result(0,0);
}
struct kernel_surface_result gpu_virtio_discover(MODULE_ARGS){
    MODULE_UNUSED();
    if(!foreground())return module_result(0,0);
    if(state.published)return module_result(state.table.count,1);
    kernel_surface_call publish=kernel_surface_find(state.hub,"hub.publish_table");
    struct kernel_hub_table t={
        .owner=state.module_owner,.name=GPU_VIRTIO_TABLE,.address=(uintptr_t)&state.table,.bytes=sizeof(state.table),.count=1,.entry_bytes=sizeof(state.table)
    };
    if(!publish||CALL(publish,&t,0,0).status)return module_result(0,0);
    state.published=1;
#ifdef CB_GPU_DISCOVER_TEST
    extern void CB_GPU_DISCOVER_TEST(void);
    CB_GPU_DISCOVER_TEST();
#endif
    return module_result(state.table.count,1);
}
struct kernel_surface_result gpu_virtio_info(MODULE_ARGS){
    MODULE_UNUSED();
    struct controller *s=hardware(a);
    if(!foreground()||!s||!b)return module_result(0,0);
    *(struct gpu_virtio_info *)b=*s->info;
    return module_result(1,1);
}
static int reset_device(struct controller *s){
    w8(s->common+20,0);
    uint64_t start=now();
    if(start==UINT64_MAX||start>UINT64_MAX-TIMEOUT_NS)return 0;
    for(unsigned tries=0;tries<1024;tries++){
        if(!r8(s->common+20))return 1;
        uint64_t t=now();
        if(t==UINT64_MAX||t<start||t>=start+TIMEOUT_NS||CALL(state.thread,THREAD_BLOCK_UNTIL,t+1000000,0).status)return 0;
    }
    return 0;
}
static int quiesce(struct controller *s){
    if(s->route.id){
        struct pci_msix m={
            .hardware_index=s->info->hardware_index,.owner=s->owner,.masked=1,.route=s->route
        };
        if(CALL(state.msix,&m,0,0).status||CALL(state.irq_enable,s->route.id,s->owner,0).status)return 0;
    }
    if(!s->common||!reset_device(s))return 0;
    return !CALL(state.master,s->info->hardware_index,s->owner,0).status;
}
static int release_pin(struct controller *s){
    if(s->owner_pinned){if(CALL(state.pin,state.module_owner,0,0).status)return failed(s,GPU_CLEANUP);s->owner_pinned=0;}return 1;
}
static int close_controller(struct controller *s){
    if(!s->owned){int ok=release_pin(s);if(ok)s->info->state=GPU_CLOSED;return ok;}
    if(!quiesce(s))return failed(s,GPU_CLEANUP);
    if(s->route.id){
        if(CALL(state.irq_release,s->route.id,s->owner,1).status)return failed(s,GPU_CLEANUP);
        s->route=(struct irq_route){
            0
        };
    }
    /* Reset acknowledged: all host resource/backing references are gone. */
    while(s->resources){
        struct resource *r=s->resources;
        if(r->dma.id&&CALL(state.dma_free,s->info->hardware_index,s->owner,r->dma.id).status)return failed(s,GPU_CLEANUP);
        s->resources=r->next;
        drop(r);
    }
    while(s->attachments){struct attachment *p=s->attachments;s->attachments=p->next;drop(p);}
    while(s->contexts){
        struct context *c=s->contexts;
        s->contexts=c->next;
        drop(c);
    }
    if(s->dma.id){
        if(CALL(state.dma_free,s->info->hardware_index,s->owner,s->dma.id).status)return failed(s,GPU_CLEANUP);
        s->dma=(struct mem_pages){
            0
        };
    }
    if(CALL(state.release,s->info->hardware_index,s->owner,1).status)return failed(s,GPU_CLEANUP);
    s->owned=0;
    s->common=s->notify=s->isr=s->configuration=0;
    s->waiting=0;
    s->info->resources=s->info->contexts=0;
    s->info->state=GPU_CLOSED;
    return release_pin(s);
}
static int take(struct controller *s){
    uint32_t bytes=0;
    int result=virtq_complete(&s->queue,&bytes);
    if(result){
        s->response_bytes=bytes;
        s->completion_error=result<0;
        s->completed=1;
        if(s->waiting)CALL(state.thread,THREAD_WAKE,s->reader,0);
    }
    return result;
}
struct kernel_surface_result gpu_virtio_interrupt(MODULE_ARGS){
    MODULE_UNUSED();
    const struct irq_notification *n=(void *)a;
    struct controller *s=NULL;
    for(uint64_t i=0;i<state.table.count;i++)if((uintptr_t)(state.controllers+i)==b)s=state.controllers+i;
    if(!state.hub||!n||!s||!s->owned||n->id!=s->route.id||n->owner!=s->owner)return module_result(0,0);
    s->info->interrupts+=n->count;
    if(s->isr)(void)r8(s->isr);
    if(s->queue.pending)(void)take(s);
    return module_result(1,1);
}
/* Returns one fenced response in the owned DMA bounce area. Late/invalid
 * responses never authorize reuse; timeout/protocol faults require reset. */
#ifndef GPU_PROFILE_ISSUE
#define GPU_PROFILE_ISSUE(type,start,blocks) ((void)(type),(void)(start),(void)(blocks))
#endif
static int issue(struct controller *s,uint32_t type,uint32_t context,const void *payload,size_t bytes,const void *tail,size_t tail_bytes,uint32_t expected){
    if(s->info->state!=GPU_ONLINE||bytes>COMMAND_BYTES-sizeof(struct vg_header)||tail_bytes>COMMAND_BYTES-sizeof(struct vg_header)-bytes||s->fence==UINT64_MAX)return 0;
    unsigned char *request=(void *)(s->dma.address+REQUEST_OFFSET),*response=(void *)(s->dma.address+RESPONSE_OFFSET);
    struct vg_header h={
        .type=type,.flags=1,.fence=++s->fence,.context=context
    };
    copy(request,&h,sizeof(h));
    if(bytes)copy(request+sizeof(h),payload,bytes);
    if(tail_bytes)copy(request+sizeof(h)+bytes,tail,tail_bytes);
    zero(response,COMMAND_BYTES);
    uint64_t start=now();
    if(start==UINT64_MAX||start>UINT64_MAX-TIMEOUT_NS)return failed(s,GPU_TIMEOUT);
    s->reader=CALL(state.thread,THREAD_CURRENT,0,0).value;
    s->completed=s->completion_error=0;
    s->waiting=1;
    if(!virtq_publish(&s->queue,s->dma.physical+REQUEST_OFFSET,sizeof(h)+bytes+tail_bytes,s->dma.physical+RESPONSE_OFFSET,COMMAND_BYTES)){
        s->waiting=0;
        return failed(s,GPU_PROTOCOL);
    }
    ++s->info->commands;
    w16(s->notify,0);
    unsigned profile_blocks=0;
    while(!s->completed){
        (void)take(s);
        if(s->completed)break;
        uint64_t t=now();
        if(t==UINT64_MAX||t<start||t>=start+TIMEOUT_NS){
            s->waiting=0;
            ++s->info->timeouts;
            return failed(s,GPU_TIMEOUT);
        }
        ++profile_blocks;
        if(CALL(state.thread,THREAD_BLOCK_UNTIL,start+TIMEOUT_NS,0).status){
            s->waiting=0;
            return failed(s,GPU_TIMEOUT);
        }
    }
    s->waiting=0;
    if(s->completion_error||CALL(state.dma_ready,s->info->hardware_index,s->owner,0).status)return failed(s,GPU_PROTOCOL);
    const struct vg_header *reply=(const void *)response;
    if(s->response_bytes<sizeof(*reply)||reply->flags!=1||reply->fence!=h.fence)return failed(s,GPU_PROTOCOL);
    s->info->last_fence=h.fence;
    if(reply->type>=VG_ERROR&&reply->type<VG_ERROR+256){
        s->info->error=GPU_DEVICE_ERROR;
        return 0;
    }
    if((expected==VG_OK && s->response_bytes!=sizeof(*reply)) || !vg_reply_valid(reply,s->response_bytes,h.fence,expected))return failed(s,GPU_PROTOCOL);
    s->info->error=GPU_OK;
    GPU_PROFILE_ISSUE(type,start,profile_blocks);
    return 1;
}
static int configure(struct controller *s){
    s->info->stage=1;
    uint64_t bars=(UINT64_C(1)<<s->layout.common.bar)|(UINT64_C(1)<<s->layout.notify.bar)|(UINT64_C(1)<<s->layout.isr.bar)|(UINT64_C(1)<<s->layout.device.bar);
    if(state.claim(s->info->hardware_index,s->owner,(uintptr_t)&s->device,bars,0,0).status)return failed(s,GPU_OWNERSHIP);
    s->owned=1;
    const struct virtio_region *regions[]={
        &s->layout.common,&s->layout.notify,&s->layout.isr,&s->layout.device
    };
    uintptr_t *targets[]={
        &s->common,&s->notify,&s->isr,&s->configuration
    };
    for(unsigned i=0;i<4;i++){
        const struct virtio_region *r=regions[i];
        const struct pci_bar *bar=&s->device.bars[r->bar];
        if(!bar->address||!virtio_region_fits(r,bar->bytes))return failed(s,GPU_CONFIG);
        *targets[i]=bar->address+r->offset;
    }
    s->info->stage=2;
    if(!reset_device(s))return failed(s,GPU_TIMEOUT);
    w8(s->common+20,3);
    w32(s->common,0);
    uint64_t offered=r32(s->common+4);
    w32(s->common,1);
    offered|=(uint64_t)r32(s->common+4)<<32;
    s->info->offered=offered;
    struct iommu_info platform_info={0};if(CALL(state.iommu_info,&platform_info,0,0).status || (platform_info.backend!=IOMMU_NONE && !(offered&(UINT64_C(1)<<33))))return failed(s,GPU_FEATURES);
    if(!(offered&(UINT64_C(1)<<32)))return failed(s,GPU_FEATURES);
    s->info->accepted=(UINT64_C(1)<<32)|(offered&((UINT64_C(1)<<33)|1));
    s->info->virgl=offered&1;
    w32(s->common+8,0);
    w32(s->common+12,s->info->accepted);
    w32(s->common+8,1);
    w32(s->common+12,s->info->accepted>>32);
    w8(s->common+20,11);
    if(!(r8(s->common+20)&8))return failed(s,GPU_FEATURES);
    s->info->stage=3;
    if(r16(s->common+18)<2||!s->device.msix_count)return failed(s,GPU_IRQ);
    w16(s->common+16,0xffff);
    w16(s->common+22,0);
    uint16_t maximum=r16(s->common+24),depth=VIRTQ_MAX_DEPTH;
    while(depth>maximum&&depth>2)depth>>=1;
    if(maximum<2)return failed(s,GPU_CONFIG);
    if(state.dma_alloc(s->info->hardware_index,s->owner,QUEUE_PAGES,(uintptr_t)&s->dma,0,0).status)return failed(s,GPU_MEMORY);
    if(!virtq_setup(&s->queue,(void *)s->dma.address,4096,depth))return failed(s,GPU_CONFIG);
    uint64_t notify_offset;
    if(!virtio_notify_offset(&s->layout,r16(s->common+30),&notify_offset))return failed(s,GPU_CONFIG);
    s->notify+=notify_offset;
    s->info->stage=4;
    struct irq_request request={
        .kind=IRQ_MSI,.owner=s->owner,.callback=kernel_surface_find(state.hub,"gpu_virtio.interrupt"),.context=(uintptr_t)s,.major=1
    };
    if(!request.callback||CALL(state.irq_route,&request,&s->route,0).status)return failed(s,GPU_IRQ);
    struct pci_msix m={
        .hardware_index=s->info->hardware_index,.owner=s->owner,.masked=1,.route=s->route
    };
    if(CALL(state.msix,&m,0,0).status)return failed(s,GPU_IRQ);
    w16(s->common+24,depth);
    w16(s->common+26,0);
    if(r16(s->common+26)!=0)return failed(s,GPU_IRQ);
    w64(s->common+32,s->dma.physical);
    w64(s->common+40,s->dma.physical+256);
    w64(s->common+48,s->dma.physical+512);
    w16(s->common+28,1);
    if(r16(s->common+28)!=1)return failed(s,GPU_CONFIG);
    s->info->stage=5;
    if(CALL(state.master,s->info->hardware_index,s->owner,1).status||CALL(state.irq_enable,s->route.id,s->owner,1).status)return failed(s,GPU_IRQ);
    m.masked=0;
    if(CALL(state.msix,&m,0,0).status)return failed(s,GPU_IRQ);
    w8(s->common+20,15);
    if(r8(s->common+20)!=15)return failed(s,GPU_FEATURES);
    unsigned stable=0;
    for(unsigned tries=0;tries<128;tries++){
        uint8_t generation=r8(s->common+21);
        s->info->scanouts=r32(s->configuration+8);
        s->info->capsets=r32(s->configuration+12);
        barrier();
        if(generation==r8(s->common+21)){
            stable=1;
            break;
        }
    }
    if(!stable||s->info->scanouts>16)return failed(s,GPU_CONFIG);
    s->info->stage=6;
    s->info->state=GPU_ONLINE;
    if(!issue(s,VG_DISPLAY_INFO,0,NULL,0,NULL,0,VG_OK_DISPLAY)||s->response_bytes!=sizeof(struct vg_display))return failed(s,GPU_PROTOCOL);
    s->info->stage=7;
    return 1;
}
struct kernel_surface_result gpu_virtio_open(MODULE_ARGS){
    MODULE_UNUSED();
    if(!foreground()||!b||__atomic_exchange_n(&state.management,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    struct controller *s=hardware(a);
    int ok=0;
    if(!s||s->owned||s->owner_pinned||(s->info->error&&s->info->state!=GPU_CLOSED)||state.serial==UINT32_MAX)goto done;
    if(__atomic_exchange_n(&s->busy,1,__ATOMIC_ACQUIRE))goto done;
    if(CALL(state.pin,state.module_owner,1,0).status){unlock(s);goto done;}s->owner_pinned=1;s->info->error=GPU_OK;
    s->info->cookie=++state.serial;
    s->owner=UINT64_C(0x5647505500000000)|state.serial;
    s->fence=0;
    s->info->last_fence=0;
    ok=configure(s);
    if(!ok){
        unsigned error=s->info->error;
        (void)close_controller(s);
        if(s->info->state!=GPU_FAILED)s->info->error=error;
    }
    *(struct gpu_virtio_info *)b=*s->info;
    unlock(s);
    done:__atomic_store_n(&state.management,0,__ATOMIC_RELEASE);
    return module_result(s?s->info->cookie:0,ok);
}
struct kernel_surface_result gpu_virtio_close(MODULE_ARGS){
    MODULE_UNUSED();
    struct controller *s=lock(a);
    if(!s)return module_result(0,0);
    int ok=close_controller(s);
    unlock(s);
    return module_result(1,ok);
}
struct kernel_surface_result gpu_virtio_display(MODULE_ARGS){
    MODULE_UNUSED();
    struct controller *s=lock(a);
    if(!s)return module_result(0,0);
    int ok=b&&c>=sizeof(struct vg_display)&&issue(s,VG_DISPLAY_INFO,0,NULL,0,NULL,0,VG_OK_DISPLAY)&&s->response_bytes==sizeof(struct vg_display);
    if(ok)copy((void *)b,(void *)(s->dma.address+RESPONSE_OFFSET),sizeof(struct vg_display));
    unlock(s);
    return module_result(sizeof(struct vg_display),ok);
}
static struct resource *resource(struct controller *s,uint32_t id){
    for(struct resource *r=s->resources;r;r=r->next)if(r->public.id==id)return r;
    return NULL;
}
static int free_dma(struct controller *s,struct mem_pages *dma){
    if(!dma->id)return 1;
    /* Fenced detach/unref (or acknowledged full reset at close) precedes DMA
     * unmapping. This pauses new bus mastering; it is not itself a DMA drain. */
    if(CALL(state.master,s->info->hardware_index,s->owner,0).status)return failed(s,GPU_CLEANUP);
    if(CALL(state.dma_free,s->info->hardware_index,s->owner,dma->id).status)return failed(s,GPU_CLEANUP);
    *dma=(struct mem_pages){
        0
    };
    if(CALL(state.master,s->info->hardware_index,s->owner,1).status)return failed(s,GPU_CLEANUP);
    return 1;
}
static int detach_attachment(struct controller *s,struct attachment *p){
    struct vg_ref ref={p->resource,0};if(!issue(s,VG_CTX_DETACH,p->context,&ref,sizeof(ref),NULL,0,VG_OK))return 0;
    struct attachment **at=&s->attachments;while(*at&&*at!=p)at=&(*at)->next;if(!*at)return failed(s,GPU_PROTOCOL);*at=p->next;drop(p);return 1;
}
static int destroy_resource(struct controller *s,struct resource *r){
    for(struct attachment *p=s->attachments,*next;p;p=next){next=p->next;if(p->resource==r->public.id&&!detach_attachment(s,p))return 0;}
    struct vg_ref ref={
        r->public.id,0
    };
    if(r->attached){
        if(!issue(s,VG_DETACH_BACKING,0,&ref,sizeof(ref),NULL,0,VG_OK))return 0;
        r->attached=0;
    }
    if(r->created){
        if(!issue(s,VG_UNREF,0,&ref,sizeof(ref),NULL,0,VG_OK))return 0;
        r->created=0;
    }
    if(!free_dma(s,&r->dma))return 0;
    struct resource **at=&s->resources;
    while(*at&&*at!=r)at=&(*at)->next;
    if(!*at)return failed(s,GPU_PROTOCOL);
    *at=r->next;
    --s->info->resources;
    drop(r);
    return 1;
}
struct kernel_surface_result gpu_virtio_create(MODULE_ARGS){
    MODULE_UNUSED();
    struct controller *s=lock(a);
    if(!s)return module_result(0,0);
    int ok=0;
    struct resource *r=NULL;
    if(!b||!c||!d||b>UINT32_MAX||c>UINT32_MAX||b>UINT32_MAX/4/c||s->info->state!=GPU_ONLINE||s->resource_serial==UINT32_MAX)goto done;
    size_t bytes=b*c*4;
    r=allocate(sizeof(*r));
    if(!r)goto done;
    r->public=(struct gpu_virtio_resource){
        .cookie=a,.id=++s->resource_serial,.width=b,.height=c,.bytes=bytes
    };
    r->next=s->resources;
    s->resources=r;
    ++s->info->resources;
    if(state.dma_alloc(s->info->hardware_index,s->owner,(bytes+4095)/4096,(uintptr_t)&r->dma,0,0).status)goto rollback;
    r->public.pixels=r->dma.address;
    struct vg_create_2d create={
        r->public.id,2,b,c
    };
    if(!issue(s,VG_CREATE_2D,0,&create,sizeof(create),NULL,0,VG_OK))goto rollback;
    r->created=1;
    struct vg_attach attach={
        .resource=r->public.id,.count=1,.address=r->dma.physical,.length=bytes
    };
    if(!issue(s,VG_ATTACH_BACKING,0,&attach,sizeof(attach),NULL,0,VG_OK))goto rollback;
    r->attached=1;
    *(struct gpu_virtio_resource *)d=r->public;
    ok=1;
    goto done;
    rollback:if(s->info->state==GPU_ONLINE)(void)destroy_resource(s,r);
    done:unlock(s);
    return module_result(ok?r->public.id:0,ok);
}
struct kernel_surface_result gpu_virtio_destroy(MODULE_ARGS){
    MODULE_UNUSED();
    struct controller *s=lock(a);
    if(!s)return module_result(0,0);
    struct resource *r=b<=UINT32_MAX?resource(s,b):NULL;
    int ok=r&&s->info->state==GPU_ONLINE&&destroy_resource(s,r);
    unlock(s);
    return module_result(1,ok);
}
struct kernel_surface_result gpu_virtio_present(MODULE_ARGS){
    MODULE_UNUSED();
    if(!a)return module_result(0,0);
    struct gpu_virtio_present p=*(const struct gpu_virtio_present *)a;
    struct controller *s=lock(p.cookie);
    if(!s)return module_result(0,0);
    struct resource *r=resource(s,p.resource);
    int ok=0;
    if(!r||(!r->attached&&!r->texture)||!p.width||!p.height||p.scanout>=s->info->scanouts||p.x>r->public.width||p.y>r->public.height||p.width>r->public.width-p.x||p.height>r->public.height-p.y)goto done;
    struct vg_transfer transfer={
        .rect={
            p.x,p.y,p.width,p.height
        },.offset=((uint64_t)p.y*r->public.width+p.x)*4,.resource=p.resource
    };
    if(!r->texture&&!issue(s,VG_TRANSFER_2D,0,&transfer,sizeof(transfer),NULL,0,VG_OK))goto done;
    struct vg_scanout scanout={
        .rect={
            0,0,r->public.width,r->public.height
        },.scanout=p.scanout,.resource=p.resource
    };
    if(!issue(s,VG_SCANOUT,0,&scanout,sizeof(scanout),NULL,0,VG_OK))goto done;
    struct vg_flush flush={
        .rect=transfer.rect,.resource=p.resource
    };
    ok=issue(s,VG_FLUSH,0,&flush,sizeof(flush),NULL,0,VG_OK);
    done:unlock(s);
    return module_result(1,ok);
}
struct kernel_surface_result gpu_virtio_capset(MODULE_ARGS){
    MODULE_UNUSED();
    struct controller *s=lock(a);
    if(!s)return module_result(0,0);
    int ok=0;
    size_t bytes=0;
    if(!b)goto done;
    struct gpu_virtio_capset *out=(void *)b;
    if(out->index>=s->info->capsets||!s->info->virgl)goto done;
    struct vg_cap_request query={
        out->index,0
    };
    if(!issue(s,VG_CAPSET_INFO,0,&query,sizeof(query),NULL,0,VG_OK_CAPSET_INFO)||s->response_bytes!=sizeof(struct vg_cap_info))goto done;
    struct vg_cap_info result;
    copy(&result,(void *)(s->dma.address+RESPONSE_OFFSET),sizeof(result));
    if(!result.id||!result.version||!result.bytes)goto done;
    out->id=result.id;
    out->version=result.version;
    out->bytes=result.bytes;
    bytes=result.bytes;
    if(!c){
        ok=1;
        goto done;
    }
    if(bytes>d||bytes>COMMAND_BYTES-sizeof(struct vg_header))goto done;
    query=(struct vg_cap_request){
        result.id,result.version
    };
    if(!issue(s,VG_CAPSET,0,&query,sizeof(query),NULL,0,VG_OK_CAPSET)||s->response_bytes!=sizeof(struct vg_header)+bytes)goto done;
    copy((void *)c,(void *)(s->dma.address+RESPONSE_OFFSET+sizeof(struct vg_header)),bytes);
    ok=1;
    done:unlock(s);
    return module_result(bytes,ok);
}
static struct context *find_context(struct controller *s,uint32_t id){
    for(struct context *p=s->contexts;p;p=p->next)if(p->id==id)return p;
    return NULL;
}
struct kernel_surface_result gpu_virtio_context(MODULE_ARGS){
    MODULE_UNUSED();
    struct controller *s=lock(a);
    if(!s)return module_result(0,0);
    uint32_t id=0;
    int ok=0;
    if(!s->info->virgl||s->info->state!=GPU_ONLINE||b>UINT32_MAX)goto done;
    if(!b){
        if(s->context_serial==UINT32_MAX)goto done;
        struct context *p=allocate(sizeof(*p));
        if(!p)goto done;
        p->id=++s->context_serial;
        p->next=s->contexts;
        s->contexts=p;
        ++s->info->contexts;
        id=p->id;
        struct vg_context create={
            .name_bytes=10,.name="Cloudberry"
        };
        ok=issue(s,VG_CTX_CREATE,id,&create,sizeof(create),NULL,0,VG_OK);
        if(!ok&&s->info->state==GPU_ONLINE){
            s->contexts=p->next;
            --s->info->contexts;
            drop(p);
        }
    }
    else {
        struct context **p=&s->contexts;
        while(*p&&(*p)->id!=b)p=&(*p)->next;
        if(!*p)goto done;
        id=b;
        for(struct attachment *entry=s->attachments,*next;entry;entry=next){next=entry->next;if(entry->context==id&&!detach_attachment(s,entry))goto done;}
        if(!issue(s,VG_CTX_DESTROY,id,NULL,0,NULL,0,VG_OK))goto done;
        struct context *old=*p;
        *p=old->next;
        --s->info->contexts;
        drop(old);
        ok=1;
    }
    done:unlock(s);
    return module_result(id,ok);
}
struct kernel_surface_result gpu_virtio_submit(MODULE_ARGS){
    MODULE_UNUSED();
    struct controller *s=lock(a);
    if(!s)return module_result(0,0);
    int ok=0;
    if(b<=UINT32_MAX&&find_context(s,b)&&c&&d&&!(d&3)&&d<=COMMAND_BYTES-sizeof(struct vg_header)-sizeof(struct vg_submit)&&c<=UINTPTR_MAX-d){
        struct vg_submit submit={
            d,0
        };
        ok=issue(s,VG_SUBMIT_3D,b,&submit,sizeof(submit),(void *)c,d,VG_OK);
    }
    unlock(s);
    return module_result(1,ok);
}

struct kernel_surface_result gpu_virtio_texture(MODULE_ARGS){
    struct controller *s=lock(a);if(!s)return module_result(0,0);int ok=0;struct resource *r=NULL;
    if(!b||!c||!e||b>UINT32_MAX||c>UINT32_MAX||b>UINT32_MAX/4/c||(d!=1&&d!=2)||f>1||!s->info->virgl||s->info->state!=GPU_ONLINE||s->resource_serial==UINT32_MAX)goto done;
    r=allocate(sizeof(*r));if(!r)goto done;r->texture=1;r->public=(struct gpu_virtio_resource){.cookie=a,.id=++s->resource_serial,.width=b,.height=c,.bytes=f?0:b*c*4};r->next=s->resources;s->resources=r;++s->info->resources;
    if(!f){if(state.dma_alloc(s->info->hardware_index,s->owner,(r->public.bytes+4095)/4096,(uintptr_t)&r->dma,0,0).status)goto rollback;r->public.pixels=r->dma.address;}
    struct vg_texture texture={.resource=r->public.id,.target=2,.format=d,.bind=10,.width=b,.height=c,.depth=1,.array_size=1};
    if(!issue(s,VG_CREATE_3D,0,&texture,sizeof(texture),NULL,0,VG_OK))goto rollback;
    r->created=1;
    if(!f){struct vg_attach attach={.resource=r->public.id,.count=1,.address=r->dma.physical,.length=r->public.bytes};if(!issue(s,VG_ATTACH_BACKING,0,&attach,sizeof(attach),NULL,0,VG_OK))goto rollback;r->attached=1;}
    *(struct gpu_virtio_resource *)e=r->public;ok=1;goto done;
rollback:if(s->info->state==GPU_ONLINE)(void)destroy_resource(s,r);
done:unlock(s);return module_result(ok?r->public.id:0,ok);
}
struct kernel_surface_result gpu_virtio_attach(MODULE_ARGS){
    MODULE_UNUSED();struct controller *s=lock(a);if(!s)return module_result(0,0);int ok=0;if(b>UINT32_MAX||c>UINT32_MAX||d>1||!find_context(s,b)||!resource(s,c)||s->info->state!=GPU_ONLINE)goto done;
    struct attachment *p=s->attachments;while(p&&(p->context!=b||p->resource!=c))p=p->next;
    if(!d){ok=!p||detach_attachment(s,p);goto done;}if(p){ok=1;goto done;}
    p=allocate(sizeof(*p));if(!p)goto done;*p=(struct attachment){.next=s->attachments,.context=b,.resource=c};s->attachments=p;
    struct vg_ref ref={c,0};ok=issue(s,VG_CTX_ATTACH,b,&ref,sizeof(ref),NULL,0,VG_OK);if(!ok&&s->info->state==GPU_ONLINE){s->attachments=p->next;drop(p);}
done:unlock(s);return module_result(1,ok);
}
struct kernel_surface_result gpu_virtio_upload(MODULE_ARGS){
    MODULE_UNUSED();struct controller *s=lock(a);if(!s)return module_result(0,0);int ok=0;struct resource *r=c<=UINT32_MAX?resource(s,c):NULL;if(b>UINT32_MAX||!find_context(s,b)||!r||!r->texture||!r->attached)goto done;
    struct attachment *p=s->attachments;while(p&&(p->context!=b||p->resource!=c))p=p->next;if(!p)goto done;
    struct vg_rect rect={0,0,r->public.width,r->public.height};if(d)rect=*(const struct vg_rect *)d;
    if(e || f || !rect.width || !rect.height || rect.x>r->public.width || rect.y>r->public.height || rect.width>r->public.width-rect.x || rect.height>r->public.height-rect.y)goto done;
    struct vg_upload upload={.x=rect.x,.y=rect.y,.width=rect.width,.height=rect.height,.depth=1,.offset=((uint64_t)rect.y*r->public.width+rect.x)*4,.resource=c,.stride=r->public.width*4,.layer_stride=r->public.bytes};ok=issue(s,VG_TRANSFER_TO_3D,b,&upload,sizeof(upload),NULL,0,VG_OK);
done:unlock(s);return module_result(1,ok);
}

struct kernel_surface_result gpu_virtio_readback(MODULE_ARGS){
    MODULE_UNUSED();
    struct controller *s=lock(a);
    if(!s)return module_result(0,0);
    int ok=0;
    struct resource *r=b<=UINT32_MAX?resource(s,b):NULL;
    if(!s->info->virgl||!r||!r->attached||c>UINT32_MAX)goto done;
    if(c){
        if(!find_context(s,c))goto done;
        struct attachment *p=s->attachments;
        while(p&&(p->context!=c||p->resource!=b))p=p->next;
        if(!p)goto done;
    }
    struct vg_upload read={.width=r->public.width,.height=r->public.height,.depth=1,
        .resource=b,.stride=r->public.width*4,.layer_stride=r->public.bytes};
    ok=issue(s,VG_TRANSFER_FROM_3D,c,&read,sizeof(read),NULL,0,VG_OK);
done:
    unlock(s);
    return module_result(1,ok);
}

#include "gpu-virtio-io.inc"

#include "gpu-virtio-render.inc"
