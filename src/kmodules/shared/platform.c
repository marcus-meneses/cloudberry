#include "platform.h"
#include "acpi.h"
#include "pci.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static struct platform_table table;
static const struct kernel_surfaces *saved_hub;
static uintptr_t saved_heap;
static int eligible(const struct acpi_device *dev) {
    return dev->pci_hardware_index==UINT64_MAX && !(dev->flags&(ACPI_PCI_ROOT|ACPI_PCI_CHILD|ACPI_SERIAL_CHILD|ACPI_ASSOCIATION_ERROR)) && (dev->hid || dev->cid_count);
}
struct kernel_surface_result platform_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)a;
    if(saved_hub)return module_result(1,saved_hub==hub && saved_heap==b);
    if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC || !b)return module_result(0,0);
    kernel_surface_call cpu=kernel_surface_find(hub,"cpu.control"),alloc=kernel_surface_find(hub,"mem.calloc"),release=kernel_surface_find(hub,"mem.free"),publish=kernel_surface_find(hub,"hub.publish_table");
    const struct kernel_hub_table *ref=kernel_hub_find_table(hub,ACPI_TABLE_NAME);
    if(!cpu || CALL(cpu,CPU_CURRENT,0,0).value || !alloc || !release || !publish || !ref)return module_result(0,0);
    const struct acpi_table *acpi=(const void *)ref->address;
    if(acpi->magic!=ACPI_TABLE_MAGIC || acpi->major!=1 || acpi->entry_bytes!=sizeof(struct acpi_device))return module_result(0,0);
    size_t count=0;for(size_t i=0;i<acpi->count;++i)if(eligible(&acpi->entries[i]))++count;
    struct kernel_surface_result memory=CALL(alloc,b,count?count:1,sizeof(struct platform_device));if(memory.status)return memory;
    struct platform_device *entries=(void *)memory.value;count=0;
    for(size_t i=0;i<acpi->count;++i) {
        const struct acpi_device *dev=&acpi->entries[i];if(!eligible(dev))continue;
        uint32_t flags=(dev->status&1?DEVICE_PRESENT:0)|(dev->status&2?DEVICE_ENABLED:0)|(dev->status&8?DEVICE_FUNCTIONING:0);
        if(dev->flags&ACPI_CRS_ERROR)flags|=DEVICE_RESOURCE_ERROR;
        entries[count++]=(struct platform_device){.identity={.id=DEVICE_PLATFORM_ID(dev->reference.id),.generation=1,.bus=DEVICE_BUS_PLATFORM,.flags=flags,.acpi=dev->reference},.path=dev->path,.hid=dev->hid,.uid=dev->uid,.resource_count=dev->resource_count,.cid_count=dev->cid_count,.resources=dev->resources,.cids=dev->cids};
    }
    for(size_t i=0;i<count;++i) {
        const struct acpi_device *ancestor=&acpi->entries[entries[i].identity.acpi.id-1];
        for(uint64_t parent=ancestor->parent_id;parent && parent<=acpi->count;parent=acpi->entries[parent-1].parent_id) {
            const struct acpi_device *p=&acpi->entries[parent-1];
            if(p->pci_hardware_index!=UINT64_MAX){entries[i].identity.parent_id=DEVICE_PCI_ID(p->pci_hardware_index);break;}
            if(eligible(p)){entries[i].identity.parent_id=DEVICE_PLATFORM_ID(p->reference.id);break;}
        }
    }
    table=(struct platform_table){.magic=PLATFORM_TABLE_MAGIC,.major=1,.entry_bytes=sizeof(struct platform_device),.count=count,.generation=1,.entries=entries};
    struct kernel_hub_table published={.name=PLATFORM_TABLE_NAME,.address=(uintptr_t)&table,.bytes=sizeof(table),.count=1,.entry_bytes=sizeof(table)};
    if(CALL(publish,&published,0,0).status){CALL(release,b,entries,0);table=(struct platform_table){0};return module_result(0,0);}
    saved_hub=hub;saved_heap=b;return module_result(count,1);
}
struct kernel_surface_result platform_surface_device(MODULE_ARGS) {
    MODULE_UNUSED();if(!saved_hub || b!=table.generation || !c)return module_result(0,0);
    for(size_t i=0;i<table.count;++i)if(table.entries[i].identity.id==a){*(struct platform_device *)c=table.entries[i];return module_result(1,1);}
    return module_result(0,0);
}
