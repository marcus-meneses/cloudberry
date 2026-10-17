#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kmodules/shared/pci.h"
#include "../kmodules/shared/pci-platform.h"
#include "../kmodules/shared/hwenum.h"
#include "../kmodules/shared/mmio.h"
#include "../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static _Alignas(4096) unsigned char config[4096],bar0[16384],bar4[4096];
static struct {struct kernel_surfaces hub;struct kernel_surface methods[13];struct kernel_hub_table tables[3];} hub;
static struct {struct hwenum_table table;struct hwenum_descriptor entries[2];} hardware;
static struct {struct mmio_table table;struct mmio_region entries[3];} mmio;
static int attached,mapped,fail_map,fail_unmap,preempt_enable,fail_publish;
static unsigned allocations;
static uint64_t serial;static void *dma;static int fail_pages;
uint16_t pci_read16(uintptr_t p) {uint16_t value;memcpy(&value,(void *)p,2);return value;}
uint32_t pci_read32(uintptr_t p) {
    uint32_t value;memcpy(&value,(void *)p,4);
    if(value==UINT32_MAX && p==(uintptr_t)config+0x10)return 0xffffc004;
    if(value==UINT32_MAX && p==(uintptr_t)config+0x14)return UINT32_MAX;
    if(value==UINT32_MAX && p==(uintptr_t)config+0x20)return 0xfffff000;
    return value;
}
void pci_write16(uintptr_t p,uint16_t v) {memcpy((void *)p,&v,2);}
void pci_write32(uintptr_t p,uint32_t v) {memcpy((void *)p,&v,4);}
static struct kernel_surface_result cpu(MODULE_ARGS) {MODULE_UNUSED();if(a==CPU_ENTER && preempt_enable) {preempt_enable=0;assert(!pci_surface_busmaster(0,99,1,0,0,0).status); }return module_result(0,1);}
static struct kernel_surface_result allocate(MODULE_ARGS) {MODULE_UNUSED();void *p=calloc(b,c);if(p)++allocations;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result release(MODULE_ARGS) {MODULE_UNUSED();free((void *)b);--allocations;return module_result(1,1);}
static struct kernel_surface_result map(MODULE_ARGS) {
    MODULE_UNUSED();if(a==0)return module_result((uintptr_t)config,b==0);
    if(a==1)return module_result((uintptr_t)bar0,b==sizeof(bar0));
    return module_result((uintptr_t)bar4,a==2 && b==sizeof(bar4));
}
static struct kernel_surface_result pages(MODULE_ARGS) {
    MODULE_UNUSED();if(fail_pages)return module_result(0,0);
    assert(!dma);dma=aligned_alloc(4096,a*4096);assert(dma);memset(dma,0,a*4096);
    *(struct mem_pages *)c=(struct mem_pages){.id=++serial,.physical=(uintptr_t)dma,.address=(uintptr_t)dma,.count=a,.owner=b};return module_result(serial,1);
}
static struct kernel_surface_result free_pages(MODULE_ARGS) {MODULE_UNUSED();assert(a==serial && b==99 && dma && !mapped);free(dma);dma=NULL;return module_result(1,1);}
static struct kernel_surface_result attach(MODULE_ARGS) {MODULE_UNUSED();assert(!a && b==99);attached=1;return module_result(1,1);}
static struct kernel_surface_result map_dma(MODULE_ARGS) {MODULE_UNUSED();assert(attached && !mapped && c);mapped=1;return module_result(1,!fail_map);}
static struct kernel_surface_result unmap_dma(MODULE_ARGS) {MODULE_UNUSED();assert(mapped && c==serial);if(fail_unmap)return module_result(0,0);mapped=0;return module_result(1,1);}
static struct kernel_surface_result detach(MODULE_ARGS) {MODULE_UNUSED();assert(attached && !mapped && c==1);attached=0;return module_result(1,1);}
static struct kernel_surface_result ready(MODULE_ARGS) {MODULE_UNUSED();return module_result(attached,attached);}
static struct kernel_surface_result read_config(MODULE_ARGS) {
    MODULE_UNUSED();if(a>sizeof(config) || c>sizeof(config)-a)return module_result(0,0);
    memcpy((void *)b,config+a,c);return module_result(c,1);
}
static struct kernel_surface_result publish(MODULE_ARGS) {
    MODULE_UNUSED();if(fail_publish)return module_result(0,0);hub.tables[2]=*(struct kernel_hub_table *)a;hub.hub.table_count=3;return module_result(1,1);
}
int main(int argc,char **argv) {
    int translated=argc>1 && argv[1][0]=='t';
    const char *names[]={"cpu.control","mem.calloc","mem.free","mmio.map","mem.pages_alloc","mem.pages_free","hardware.read","hub.publish_table","iommu.attach","iommu.map","iommu.unmap","iommu.detach","iommu.ready"};
    kernel_surface_call calls[]={cpu,allocate,release,map,pages,free_pages,read_config,publish,attach,map_dma,unmap_dma,detach,ready};
    hub.hub=(struct kernel_surfaces){.magic=KERNEL_SURFACES_MAGIC,.major=KERNEL_HUB_ABI,.count=translated?13:8,.header_bytes=offsetof(__typeof__(hub),methods),.tables_offset=offsetof(__typeof__(hub),tables),.table_count=2};
    for(unsigned i=0;i<hub.hub.count;++i) {strcpy(hub.methods[i].name,names[i]);hub.methods[i].call=calls[i];hub.methods[i].major=1;}
    hardware.table.count=argc>1?2:1;hardware.entries[0].kind=HWENUM_PCI;
    if(argc>1) {hardware.entries[1].kind=HWENUM_ACPI_TABLE;memcpy(hardware.entries[1].data.acpi.signature,"DMAR",4);}
    mmio.table.count=3;mmio.entries[0]=(struct mmio_region){.kind=MMIO_PCI_CONFIG};mmio.entries[1]=(struct mmio_region){.kind=MMIO_PCI_BAR};mmio.entries[2]=(struct mmio_region){.kind=MMIO_PCI_BAR,.bar=4};
    strcpy(hub.tables[0].name,HWENUM_TABLE_NAME);hub.tables[0].address=(uintptr_t)&hardware;
    strcpy(hub.tables[1].name,MMIO_TABLE_NAME);hub.tables[1].address=(uintptr_t)&mmio;
    pci_write16((uintptr_t)config+4,7);pci_write16((uintptr_t)config+6,16);config[0x34]=0x40;config[0x40]=0x11;
    pci_write32((uintptr_t)config+0x10,0x10000004);pci_write32((uintptr_t)config+0x20,0x20000000);pci_write32((uintptr_t)config+0x44,4);
    fail_publish=1;assert(CALL(pci_surface_init,&hub.hub,1,0).status && !allocations);
    fail_publish=0;assert(!CALL(pci_surface_init,&hub.hub,1,0).status && allocations==2);
    struct pci_firmware_access access={.width=4,.offset=0x10};
    assert(!CALL(pci_surface_firmware_read,&access,0,0).status && access.value==0x10000004);
    access.value=123;assert(CALL(pci_surface_firmware_write,&access,0,0).status); /* BAR denied */
    access.offset=0x80;assert(CALL(pci_surface_firmware_write,&access,0,0).status); /* capability area denied */
    config[0x34]=0xa0;config[0xa1]=0x40;
    assert(CALL(pci_surface_firmware_write,&access,0,0).status); /* non-monotonic capability chain */
    config[0x34]=0x40;config[0xa1]=0;
    pci_write16((uintptr_t)config+6,0);assert(!CALL(pci_surface_firmware_write,&access,0,0).status && pci_read32((uintptr_t)config+0x80)==123);
    access.offset=0x100;assert(CALL(pci_surface_firmware_write,&access,0,0).status); /* extended caps denied */
    access.offset=0x81;assert(CALL(pci_surface_firmware_read,&access,0,0).status); /* alignment */
    pci_write16((uintptr_t)config+6,16);
    struct pci_record record;assert(!CALL(pci_surface_device,0,&record,0).status && record.identity.id==DEVICE_PCI_ID(0));
    struct device_acpi_ref ref={42,1};assert(!CALL(pci_surface_associate,0,&ref,0).status);
    struct device_acpi_ref other={43,1};assert(CALL(pci_surface_associate,0,&other,0).status);
    struct pci_device dev;assert(CALL(pci_surface_claim,0,0,&dev).status);
    assert(!CALL(pci_surface_claim,0,99,&dev).status && dev.acpi.id==42 && dev.msix_count==1 && dev.bars[0].bytes==16384 && dev.bars[4].bytes==4096);
    assert(CALL(pci_surface_associate,0,&ref,0).status);
    assert(pci_read32((uintptr_t)config+0x10)==0x10000004 && !(pci_read16((uintptr_t)config+4)&4));
    assert(CALL(pci_surface_claim,0,100,&dev).status && CALL(pci_surface_release,0,100,1).status);
    struct mem_pages buffer;
    if(argc>1 && !translated) {
        assert(CALL(pci_surface_busmaster,0,99,1).status && pci_surface_dma_alloc(0,99,1,(uintptr_t)&buffer,0,0).status);
    } else {
        if(translated) {
            fail_map=1;assert(pci_surface_dma_alloc(0,99,1,(uintptr_t)&buffer,0,0).status && buffer.id && dma);
            fail_unmap=1;assert(CALL(pci_surface_dma_free,0,99,buffer.id).status && dma && mapped);
            assert(CALL(pci_surface_release,0,99,1).status);fail_unmap=0;fail_map=0;
            assert(!CALL(pci_surface_dma_free,0,99,buffer.id).status && !dma && !mapped);
        }
        fail_pages=1;assert(pci_surface_dma_alloc(0,99,1,(uintptr_t)&buffer,0,0).status);fail_pages=0;
        assert(!pci_surface_dma_alloc(0,99,1,(uintptr_t)&buffer,0,0).status);
        if(translated) {preempt_enable=1;assert(CALL(pci_surface_dma_free,0,99,buffer.id).status && dma && mapped);assert(!CALL(pci_surface_busmaster,0,99,0).status);}
        assert(CALL(pci_surface_release,0,99,1).status && CALL(pci_surface_dma_free,0,100,buffer.id).status);
        assert(!CALL(pci_surface_busmaster,0,99,1).status && CALL(pci_surface_dma_free,0,99,buffer.id).status);
        assert(!CALL(pci_surface_busmaster,0,99,0).status && !CALL(pci_surface_dma_free,0,99,buffer.id).status);
    }
    struct pci_msix irq={.hardware_index=0,.owner=99,.route={.id=1,.owner=99,.kind=IRQ_MSI,.message_address=0xfee00000,.message_data=48}};
    assert(!CALL(pci_surface_msix,&irq,0,0).status && pci_read32((uintptr_t)bar4)==0xfee00000 && pci_read32((uintptr_t)bar4+8)==48);
    irq.entry=1;assert(CALL(pci_surface_msix,&irq,0,0).status);
    assert(CALL(pci_surface_release,0,99,0).status && !CALL(pci_surface_release,0,99,1).status);
    config[0x41]=0x40;assert(CALL(pci_surface_claim,0,99,&dev).status);config[0x41]=0;
    assert(!CALL(pci_surface_claim,0,99,&dev).status && !CALL(pci_surface_release,0,99,1).status);
    pci_write32((uintptr_t)config+0x100,0x0001000f);pci_write16((uintptr_t)config+0x106,0x8000);
    assert(CALL(pci_surface_claim,0,99,&dev).status);pci_write16((uintptr_t)config+0x106,0);
    pci_write32((uintptr_t)config+0x100,0x1001000f);assert(CALL(pci_surface_claim,0,99,&dev).status);
    pci_write32((uintptr_t)config+0x100,0);assert(!CALL(pci_surface_claim,0,99,&dev).status && !CALL(pci_surface_release,0,99,1).status);
    /* MSI is separate from MSI-X: owned one-vector programming, 32/64-bit
     * address layouts, masking, upper-address rejection and malformed extent. */
    config[0x41]=0x60;config[0x60]=5;pci_write16((uintptr_t)config+0x62,0x1f1);
    assert(!CALL(pci_surface_claim,0,99,&dev).status);
    irq.entry=0;irq.masked=0;irq.route.message_address=UINT64_C(0x1fee00000);
    assert(!CALL(pci_surface_msi,&irq,0,0).status);
    assert(pci_read32((uintptr_t)config+0x64)==0xfee00000 && pci_read32((uintptr_t)config+0x68)==1 && pci_read16((uintptr_t)config+0x6c)==48 && !(pci_read16((uintptr_t)config+0x62)&0x70));
    irq.owner=100;assert(CALL(pci_surface_msi,&irq,0,0).status);irq.owner=99;
    irq.entry=1;assert(CALL(pci_surface_msi,&irq,0,0).status);irq.entry=0;
    irq.masked=1;assert(!CALL(pci_surface_msi,&irq,0,0).status && !(pci_read16((uintptr_t)config+0x62)&1) && pci_read32((uintptr_t)config+0x70)==UINT32_MAX);
    assert(!CALL(pci_surface_release,0,99,1).status);
    pci_write16((uintptr_t)config+0x62,0);
    assert(!CALL(pci_surface_claim,0,99,&dev).status && CALL(pci_surface_msi,&irq,0,0).status);
    irq.route.message_address=0xfee00000;irq.masked=0;
    assert(!CALL(pci_surface_msi,&irq,0,0).status && pci_read16((uintptr_t)config+0x68)==48 && (pci_read16((uintptr_t)config+0x62)&1));
    irq.masked=1;assert(!CALL(pci_surface_msi,&irq,0,0).status && !CALL(pci_surface_release,0,99,1).status);
    config[0x41]=0xf0;config[0xf0]=5;pci_write16((uintptr_t)config+0xf2,0x180);
    assert(CALL(pci_surface_claim,0,99,&dev).status);config[0x41]=0;
    puts("PASS PCI: BAR probe/restore, capability cycles, ownership, MSI/MSI-X bounds, DMA retention/preemption, active ATS/cycles and conservative fallback");
    return 0;
}
