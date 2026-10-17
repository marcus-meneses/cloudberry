#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kmodules/shared/acpi/association.h"
#include "../kmodules/shared/platform.h"
#include "../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static struct {struct kernel_surfaces hub;struct kernel_surface methods[4];struct kernel_hub_table tables[2];} hub;
static int fail_publish;
static unsigned allocations;
static struct kernel_surface_result cpu(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result alloc(MODULE_ARGS){MODULE_UNUSED();void *p=calloc(b,c);if(p)++allocations;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result release(MODULE_ARGS){MODULE_UNUSED();free((void *)b);--allocations;return module_result(1,1);}
static struct kernel_surface_result publish(MODULE_ARGS){MODULE_UNUSED();if(fail_publish)return module_result(0,0);hub.tables[1]=*(struct kernel_hub_table *)a;hub.hub.table_count=2;return module_result(1,1);}
int main(void) {
    struct pci_record records[]={
        {.hardware_index=10,.segment=7,.bus=64,.slot=1,.header_type=1,.secondary_bus=65,.subordinate_bus=70},
        {.hardware_index=11,.segment=7,.bus=65,.slot=2,.function=3},
        {.hardware_index=12,.segment=0,.bus=65,.slot=2,.function=3},
    };
    struct pci_table pci={.count=3,.entries=records};
    struct acpi_device devs[]={
        {.reference={1,1},.flags=ACPI_PCI_ROOT,.segment=7,.bus=64,.hid="PNP0A08"},
        {.reference={2,1},.parent_id=1,.flags=ACPI_HAS_ADR,.adr=1<<16},
        {.reference={3,1},.parent_id=2,.flags=ACPI_HAS_ADR,.adr=(2<<16)|3},
        {.reference={4,1},.hid="CBB0001",.status=15},
        {.reference={5,1},.parent_id=4,.flags=ACPI_HAS_ADR,.adr=5,.hid="CBB0002",.status=1},
        {.reference={6,1},.flags=ACPI_SERIAL_CHILD,.hid="CBB0003",.status=15},
    };
    acpi_match_pci(devs,6,&pci);
    assert(devs[1].pci_hardware_index==10 && devs[2].pci_hardware_index==11);
    assert(devs[4].pci_hardware_index==UINT64_MAX && !(devs[4].flags&ACPI_PCI_CHILD));
    /* Distinct segment, nested bridge routing and non-PCI _ADR stay distinct. */
    struct acpi_device duplicate[7];memcpy(duplicate,devs,sizeof(devs));duplicate[6]=devs[1];duplicate[6].reference.id=7;
    acpi_match_pci(duplicate,7,&pci);
    assert(duplicate[1].pci_hardware_index==UINT64_MAX && duplicate[2].pci_hardware_index==UINT64_MAX && duplicate[6].pci_hardware_index==UINT64_MAX);
    duplicate[6]=devs[0];duplicate[6].reference.id=7;acpi_match_pci(duplicate,7,&pci);
    assert(duplicate[1].pci_hardware_index==UINT64_MAX && duplicate[2].pci_hardware_index==UINT64_MAX);
    struct acpi_table acpi={.magic=ACPI_TABLE_MAGIC,.major=1,.entry_bytes=sizeof(struct acpi_device),.generation=1,.count=6,.entries=devs};
    hub.hub=(struct kernel_surfaces){.magic=KERNEL_SURFACES_MAGIC,.major=KERNEL_HUB_ABI,.count=4,.header_bytes=offsetof(__typeof__(hub),methods),.tables_offset=offsetof(__typeof__(hub),tables),.table_count=1};
    const char *names[]={"cpu.control","mem.calloc","mem.free","hub.publish_table"};kernel_surface_call calls[]={cpu,alloc,release,publish};
    for(unsigned i=0;i<4;++i){strcpy(hub.methods[i].name,names[i]);hub.methods[i].call=calls[i];}
    strcpy(hub.tables[0].name,ACPI_TABLE_NAME);hub.tables[0].address=(uintptr_t)&acpi;
    fail_publish=1;assert(CALL(platform_surface_init,&hub.hub,1,0).status && !allocations);
    fail_publish=0;assert(!CALL(platform_surface_init,&hub.hub,1,0).status && allocations==1);
    const struct platform_table *table=(const void *)hub.tables[1].address;
    assert(table->count==2 && table->entries[0].identity.acpi.id==4 && table->entries[1].identity.parent_id==DEVICE_PLATFORM_ID(4));
    struct platform_device out={0};assert(CALL(platform_surface_device,DEVICE_PLATFORM_ID(4),2,&out).status);
    assert(!CALL(platform_surface_device,DEVICE_PLATFORM_ID(4),1,&out).status && out.identity.flags==7 && !out.identity.owner);
    free((void *)table->entries);
    puts("PASS ACPI identities: segmented bridge matching, duplicate refusal, descendant invalidation, platform filtering/parentage, generation and publication rollback");
    return 0;
}
