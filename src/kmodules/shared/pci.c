#include "pci.h"
#include "pci-platform.h"
#include "hwenum.h"
#include "mmio.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
struct dma_record {struct dma_record *next;struct mem_pages pages;int mapped;};
struct device {struct pci_device info;uintptr_t config;uint16_t command;int master,operations,attached;struct dma_record *dma;};
static struct {const struct kernel_surfaces *hub;const struct hwenum_table *hw;const struct mmio_table *mmio;struct device *devices;uintptr_t heap;int iommu;kernel_surface_call cpu,map,alloc,free,pages,release,attach,map_dma,unmap,detach,ready;} state;
static int bsp(void) {return state.cpu && !CALL(state.cpu,CPU_CURRENT,0,0).value;}
static struct device *owned(size_t i,uint64_t owner) {return state.devices && i<state.hw->count && owner && state.devices[i].info.owner==owner?&state.devices[i]:NULL;}
static uintptr_t mapping(size_t hw,unsigned kind,unsigned bar,uint64_t bytes) {
    for(size_t i=0;i<state.mmio->count;++i) {const struct mmio_region *r=&state.mmio->entries[i];
        if(r->hardware_index==hw && r->kind==kind && (kind!=MMIO_PCI_BAR || r->bar==bar)) {
            struct kernel_surface_result m=CALL(state.map,i,bytes,0);return m.status?0:m.value;
        }
    }return 0;
}
struct kernel_surface_result pci_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)a;
    if(state.hub)return module_result(1,state.hub==hub && state.heap==b);
    if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC || !b)return module_result(0,0);
    state.cpu=kernel_surface_find(hub,"cpu.control");if(!bsp())return module_result(0,0);
    const struct kernel_hub_table *hw=kernel_hub_find_table(hub,HWENUM_TABLE_NAME),*mm=kernel_hub_find_table(hub,MMIO_TABLE_NAME);
    if(!hw || !mm)return module_result(0,0);
    state.hw=(const void *)hw->address;state.mmio=(const void *)mm->address;
    state.map=kernel_surface_find(hub,"mmio.map");state.alloc=kernel_surface_find(hub,"mem.calloc");state.free=kernel_surface_find(hub,"mem.free");state.pages=kernel_surface_find(hub,"mem.pages_alloc");state.release=kernel_surface_find(hub,"mem.pages_free");
    state.attach=kernel_surface_find(hub,"iommu.attach");state.map_dma=kernel_surface_find(hub,"iommu.map");state.unmap=kernel_surface_find(hub,"iommu.unmap");state.detach=kernel_surface_find(hub,"iommu.detach");state.ready=kernel_surface_find(hub,"iommu.ready");
    if(!state.map || !state.alloc || !state.free || !state.pages || !state.release)return module_result(0,0);
    struct kernel_surface_result memory=CALL(state.alloc,b,state.hw->count,sizeof(struct device));if(memory.status)return memory;
    state.devices=(void *)memory.value;state.heap=b;
    for(size_t i=0;i<state.hw->count;++i) {const struct hwenum_descriptor *h=&state.hw->entries[i];
        if(h->kind==HWENUM_ACPI_TABLE && ((h->data.acpi.signature[0]=='D' && h->data.acpi.signature[1]=='M' && h->data.acpi.signature[2]=='A' && h->data.acpi.signature[3]=='R') || (h->data.acpi.signature[0]=='I' && h->data.acpi.signature[1]=='V' && h->data.acpi.signature[2]=='R' && h->data.acpi.signature[3]=='S')))state.iommu=1;
    }
    state.hub=hub;return module_result(1,1);
}
static int capabilities(struct device *d) {
    if(!(pci_read16(d->config+6)&16))return 1;
    unsigned offset=*(volatile uint8_t *)(d->config+0x34);uint64_t seen=0;
    while(offset) {
        if(offset<0x40 || offset>0xfc || (offset&3))return 0;
        uint64_t bit=UINT64_C(1)<<(offset/4);if(seen&bit)return 0;seen|=bit;
        unsigned kind=*(volatile uint8_t *)(d->config+offset);
        if(kind==5)pci_write16(d->config+offset+2,pci_read16(d->config+offset+2)&~1u);
        if(kind==0x11) {
            if(offset>0xf4 || d->info.msix_cap)return 0;
            uint16_t control=pci_read16(d->config+offset+2);d->info.msix_cap=offset;d->info.msix_count=(control&2047)+1;
            pci_write16(d->config+offset+2,(control|0x4000)&~0x8000);
        }
        offset=*(volatile uint8_t *)(d->config+offset+1);
    }return 1;
}
static int extended_capabilities(struct device *dev) {
    uint64_t seen[16]={0};unsigned offset=0x100;
    while(offset) {
        if(offset<0x100 || offset>0xffc || (offset&3))return 0;
        unsigned index=offset/4;uint64_t bit=UINT64_C(1)<<(index&63);if(seen[index/64]&bit)return 0;seen[index/64]|=bit;
        uint32_t header=pci_read32(dev->config+offset);if(!header || header==UINT32_MAX)return 1;
        unsigned kind=header&65535;
        /* Ownership of ATS/PRI/PASID translations is deliberately not implied
         * by legacy requester-domain support. Refuse active firmware features. */
        if(kind==0x0f || kind==0x13 || kind==0x1b) {
            unsigned control=kind==0x13?4:6;if(offset+control>4094)return 0;
            if(pci_read16(dev->config+offset+control)&(kind==0x0f?0x8000:1))return 0;
        }
        offset=header>>20;
    }return 1;
}
struct kernel_surface_result pci_surface_claim(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.hub || !bsp() || a>=state.hw->count || !b || !c)return module_result(0,0);
    const struct hwenum_descriptor *h=&state.hw->entries[a];struct device *dev=&state.devices[a];
    if(h->kind!=HWENUM_PCI || (h->data.pci.header_type&127)!=0 || dev->info.owner)return module_result(0,0);
    dev->config=mapping(a,MMIO_PCI_CONFIG,0,0);if(!dev->config)return module_result(0,0);
    dev->command=pci_read16(dev->config+4);pci_write16(dev->config+4,(dev->command&~7u)|0x400);
    dev->info.hardware_index=a;
    if(!capabilities(dev) || !extended_capabilities(dev))goto fail;
    for(unsigned bar=0;bar<6;++bar) {
        uintptr_t address=dev->config+0x10+bar*4;uint32_t low=pci_read32(address),high=0;
        if(!low || low==UINT32_MAX || (low&1))continue;
        unsigned type=(low>>1)&3;if(type!=0 && type!=2)goto fail;
        if(type==2 && bar==5)goto fail;
        if(type==2)high=pci_read32(address+4);
        pci_write32(address,UINT32_MAX);if(type==2)pci_write32(address+4,UINT32_MAX);
        uint32_t mask=pci_read32(address);uint64_t bits=mask&~UINT64_C(15);
        if(type==2)bits|=(uint64_t)pci_read32(address+4)<<32;else bits|=UINT64_C(0xffffffff00000000);
        pci_write32(address,low);if(type==2)pci_write32(address+4,high);
        uint64_t bytes=~bits+1,physical=(low&~UINT64_C(15))|((uint64_t)high<<32);
        if(!bytes || (bytes&(bytes-1)) || bytes<16 || !physical || (physical&(bytes-1)) || bytes>UINT64_MAX-physical)goto fail;
        dev->info.bars[bar]=(struct pci_bar){.physical=physical,.bytes=bytes};
        if(type==2)++bar;
    }
    pci_write16(dev->config+4,(dev->command&~5u)|0x402); /* Decode only; DMA still disabled. */
    for(unsigned bar=0;bar<6;++bar)if(dev->info.bars[bar].bytes) {
        struct pci_bar *r=&dev->info.bars[bar];r->address=mapping(a,MMIO_PCI_BAR,bar,r->bytes);if(!r->address)goto fail;
    }
    dev->info.owner=b;*(struct pci_device *)c=dev->info;return module_result(1,1);
fail:
    pci_write16(dev->config+4,dev->command&~4u);*dev=(struct device){0};return module_result(0,0);
}
struct kernel_surface_result pci_surface_busmaster(MODULE_ARGS) {
    MODULE_UNUSED();struct device *dev=owned(a,b);if(!dev || !bsp() || c>1 || (c && (dev->operations || (state.ready?CALL(state.ready,a,b,0).status:(uintptr_t)state.iommu))))return module_result(0,0);
    uint16_t command=pci_read16(dev->config+4);pci_write16(dev->config+4,c?command|4:command&~4u);dev->master=c;return module_result(1,1);
}
struct kernel_surface_result pci_surface_msix(MODULE_ARGS) {
    MODULE_UNUSED();const struct pci_msix *r=(const void *)a;if(!r || !bsp())return module_result(0,0);
    struct device *dev=owned(r->hardware_index,r->owner);
    if(!dev || !dev->info.msix_cap || r->entry>=dev->info.msix_count || r->masked>1 || r->route.owner!=r->owner || r->route.kind!=IRQ_MSI || !r->route.id)return module_result(0,0);
    uintptr_t cap=dev->config+dev->info.msix_cap;uint32_t table=pci_read32(cap+4);unsigned bar=table&7;
    if(bar>=6)return module_result(0,0);
    struct pci_bar *region=&dev->info.bars[bar];uint64_t offset=(table&~7u)+(uint64_t)r->entry*16;
    if(!region->address || offset>region->bytes || 16>region->bytes-offset)return module_result(0,0);
    uintptr_t entry=region->address+offset;
    pci_write32(entry+12,1);pci_write32(entry,(uint32_t)r->route.message_address);pci_write32(entry+4,r->route.message_address>>32);pci_write32(entry+8,r->route.message_data);
    pci_write16(cap+2,pci_read16(cap+2)|0xc000);pci_write32(entry+12,r->masked);pci_write16(cap+2,pci_read16(cap+2)&~0x4000u);
    return module_result(1,1);
}
static uint64_t lock(void) {return CALL(state.cpu,CPU_ENTER,0,0).value;}
static void unlock(uint64_t flags) {CALL(state.cpu,CPU_LEAVE,flags,0);}
static int begin(struct device *dev,size_t index,uint64_t owner,int idle,int empty) {
    uint64_t flags=lock();int ok=owned(index,owner)==dev && !dev->operations && (!idle || !dev->master) && (!empty || !dev->dma);if(ok)dev->operations=1;unlock(flags);return ok;
}
static struct kernel_surface_result end(struct device *dev,uintptr_t value,int ok) {
    uint64_t flags=lock();dev->operations=0;unlock(flags);return module_result(value,ok);
}
struct kernel_surface_result pci_surface_dma_alloc(MODULE_ARGS) {
    MODULE_UNUSED();struct device *dev=owned(a,b);
    if(!dev || !bsp() || !c || !d || (state.iommu && !state.attach) || !begin(dev,a,b,0,0))return module_result(0,0);
    if(state.attach && !dev->attached) {
        if(CALL(state.attach,a,b,0).status)return end(dev,0,0);
        dev->attached=1;
    }
    struct kernel_surface_result memory=CALL(state.alloc,state.heap,1,sizeof(struct dma_record));if(memory.status)return end(dev,0,0);
    struct dma_record *r=(void *)memory.value;struct kernel_surface_result pages=CALL(state.pages,c,b,&r->pages);
    if(pages.status) {CALL(state.free,state.heap,r,0);return end(dev,0,0);}
    uint64_t flags=lock();r->next=dev->dma;dev->dma=r;unlock(flags);
    struct kernel_surface_result mapped=state.map_dma?CALL(state.map_dma,a,b,&r->pages):module_result(1,1);
    r->mapped=state.map_dma && (!mapped.status || mapped.value);
    /* Once physical storage exists, return its descriptor even on a mapping
     * failure so the owner can retry cleanup. Uncertain pins stay tracked. */
    *(struct mem_pages *)d=r->pages;return end(dev,r->pages.id,!mapped.status);
}
struct kernel_surface_result pci_surface_dma_free(MODULE_ARGS) {
    MODULE_UNUSED();struct device *dev=owned(a,b);if(!dev || !bsp() || dev->master || !begin(dev,a,b,1,0))return module_result(0,0);
    struct dma_record **at=&dev->dma;while(*at && (*at)->pages.id!=c)at=&(*at)->next;
    if(!*at)return end(dev,0,0);
    struct dma_record *r=*at;
    if(r->mapped) {if(CALL(state.unmap,a,b,c).status)return end(dev,0,0);r->mapped=0;}
    if(CALL(state.release,r->pages.id,b,0).status)return end(dev,0,0);
    uint64_t flags=lock();*at=r->next;unlock(flags);CALL(state.free,state.heap,r,0);return end(dev,1,1);
}
struct kernel_surface_result pci_surface_release(MODULE_ARGS) {
    MODULE_UNUSED();struct device *dev=owned(a,b);if(!dev || !bsp() || c!=1 || dev->master || dev->dma || !begin(dev,a,b,1,1))return module_result(0,0);
    if(dev->attached && CALL(state.detach,a,b,1).status)return end(dev,0,0);
    pci_write16(dev->config+4,dev->command&~4u);uint64_t flags=lock();*dev=(struct device){0};unlock(flags);return module_result(1,1);
}
