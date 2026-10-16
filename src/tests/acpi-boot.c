#include <stdbool.h>
#include "../kmodules/shared/acpi.h"
#include "../kmodules/shared/platform.h"
#include "../kmodules/shared/pci.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static int equal(const char *a,const char *b) {if(!a || !b)return 0;while(*a && *a==*b){++a;++b;}return *a==*b;}
static bool fail(unsigned line) {
    const char *s="FAIL ACPI boot: ";while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));
    char buf[10];unsigned count=0;do{buf[count++]='0'+line%10;line/=10;}while(line);
    while(count)__asm__ volatile("outb %0,$0xe9"::"a"(buf[--count]));
    __asm__ volatile("outb %0,$0xe9"::"a"((unsigned char)'\n'));return false;
}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
bool kernel_acpi_test(const struct kernel_surfaces *hub) {
    const struct kernel_hub_table *ar=kernel_hub_find_table(hub,ACPI_TABLE_NAME),*pr=kernel_hub_find_table(hub,PCI_TABLE_NAME),*pl=kernel_hub_find_table(hub,PLATFORM_TABLE_NAME);
    CHECK(ar && pr && pl);
    const struct acpi_table *acpi=(const void *)ar->address;const struct pci_table *pci=(const void *)pr->address;const struct platform_table *platform=(const void *)pl->address;
    CHECK(acpi->count && pci->count && platform->count && acpi->generation==1);
    kernel_surface_call info=kernel_surface_find(hub,"acpi.info"),snapshot=kernel_surface_find(hub,"acpi.device"),integer=kernel_surface_find(hub,"acpi.eval_integer"),buffer=kernel_surface_find(hub,"acpi.eval_buffer");
    CHECK(info && snapshot && integer && buffer);
    struct acpi_info state;CHECK(!CALL(info,&state,0,0).status && state.ready && state.stage==6 && !state.runtime_status && !state.sci_enabled && state.associated);
    unsigned roots=0,fixture=0;
    for(size_t i=0;i<acpi->count;++i) {
        const struct acpi_device *dev=&acpi->entries[i];struct acpi_device copy;
        CHECK(dev->reference.id==i+1 && dev->reference.generation==1 && dev->path);
        CHECK(CALL(snapshot,dev->reference.id,2,&copy).status);
        CHECK(!CALL(snapshot,dev->reference.id,1,&copy).status && copy.path==dev->path);
        if(dev->flags&ACPI_PCI_ROOT)++roots;
        if(dev->pci_hardware_index!=UINT64_MAX) {
            int found=0;for(size_t j=0;j<pci->count;++j)if(pci->entries[j].hardware_index==dev->pci_hardware_index){CHECK(pci->entries[j].identity.acpi.id==dev->reference.id && !pci->entries[j].identity.owner);++found;}
            CHECK(found==1);
        }
        if(equal(dev->hid,"CBB0001")) {
            ++fixture;CHECK(dev->status==15 && equal(dev->uid,"7") && dev->resource_count==3 && !dev->flags);
            CHECK(dev->resources[0].kind==DEVICE_MEMORY && dev->resources[0].minimum==0xfedc0000 && dev->resources[0].length==4096);
            CHECK(dev->resources[1].kind==DEVICE_IRQ && dev->resources[1].minimum==17 && dev->resources[1].flags==(DEVICE_RESOURCE_LEVEL|DEVICE_RESOURCE_LOW|DEVICE_RESOURCE_SHARED));
            CHECK(dev->resources[2].kind==DEVICE_IO && dev->resources[2].minimum==0x1234 && dev->resources[2].length==4);
            uint64_t result=99;CHECK(!CALL(integer,&dev->reference,"TINT",&result).status && result==42);
            result=99;CHECK(CALL(integer,&dev->reference,"NONE",&result).status && result==99);
            unsigned char data[16];for(unsigned k=0;k<16;++k)data[k]=0xa5;size_t actual=0;
            CHECK(buffer((uintptr_t)&dev->reference,(uintptr_t)"TBUF",(uintptr_t)data,3,(uintptr_t)&actual,0).status && actual==10);
            for(unsigned k=0;k<16;++k)CHECK(data[k]==0xa5);
            CHECK(!buffer((uintptr_t)&dev->reference,(uintptr_t)"TBUF",(uintptr_t)data,sizeof(data),(uintptr_t)&actual,0).status && actual==10);
            CHECK(data[0]=='c' && data[9]=='y' && data[10]==0xa5);
        } else if(equal(dev->hid,"CBB0002")) {
            ++fixture;CHECK(dev->dependency_count==1 && dev->dependencies[0] && !(dev->flags&ACPI_DEP_ERROR));
            CHECK(equal(acpi->entries[dev->dependencies[0]-1].hid,"CBB0001"));
        } else if(equal(dev->hid,"CBB0003")) {++fixture;CHECK((dev->flags&ACPI_CRS_ERROR) && !dev->resource_count);}
        else if(equal(dev->hid,"CBB0004")) {
            ++fixture;int found=0;for(size_t j=0;j<platform->count;++j)if(platform->entries[j].identity.acpi.id==dev->reference.id)found=1;
            CHECK(found && (dev->flags&ACPI_HAS_ADR) && !(dev->flags&ACPI_PCI_CHILD));
        }
        else if(equal(dev->hid,"CBB0005")) {
            ++fixture;CHECK(dev->resource_count==2 && !(dev->flags&ACPI_CRS_ERROR));
            CHECK(dev->resources[0].minimum==0x100000 && dev->resources[0].length==4096 && dev->resources[0].alignment==256);
            CHECK(dev->resources[1].minimum==0x200000 && dev->resources[1].length==4096 && dev->resources[1].alignment==65536);
        }
    }
    CHECK(roots && (!fixture || fixture==5));
    for(size_t i=0;i<platform->count;++i) {
        const struct platform_device *dev=&platform->entries[i];CHECK(dev->identity.acpi.id && dev->identity.acpi.id<=acpi->count && !dev->identity.owner);
        const struct acpi_device *source=&acpi->entries[dev->identity.acpi.id-1];CHECK(source->pci_hardware_index==UINT64_MAX && !(source->flags&(ACPI_PCI_ROOT|ACPI_PCI_CHILD|ACPI_SERIAL_CHILD)));
    }
    const char *message=fixture?"PASS ACPI AML fixture: resources, dependencies, evaluation, failures and platform classification\n":"PASS ACPI: AML namespace, PCI companions, platform inventory and stable references\n";
    while(*message)__asm__ volatile("outb %0,$0xe9"::"a"(*message++));
    return true;
}
