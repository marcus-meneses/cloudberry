#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/mmio.h"
#include "../kmodules/shared/hwenum.h"
#include "../kmodules/shared/mem.h"
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static _Alignas(16) unsigned char arena[65536],registers[4096];
static struct mem_heap heap;
static struct { struct kernel_surfaces hub; struct kernel_surface methods[4]; struct kernel_hub_table tables[2]; } fixture;
static struct { struct hwenum_table header; struct hwenum_descriptor entries[6]; } hardware;
static bool fail_alloc,fail_publish,fail_map;
static unsigned allocations,mappings;
static struct kernel_surface_result allocate(MODULE_ARGS) {
    MODULE_UNUSED();void *p=fail_alloc?NULL:mem_calloc((void *)a,b,c);if (p) ++allocations;
    return module_result((uintptr_t)p,p!=NULL);
}
static struct kernel_surface_result release(MODULE_ARGS) {
    MODULE_UNUSED();mem_free((void *)a,(void *)b);--allocations;return module_result(0,1);
}
static struct kernel_surface_result publish(MODULE_ARGS) {
    MODULE_UNUSED();if (fail_publish) return module_result(0,0);
    assert(fixture.hub.table_count==1);
    fixture.tables[fixture.hub.table_count++]=*(const struct kernel_hub_table *)a;
    return module_result(1,1);
}
static struct kernel_surface_result map(MODULE_ARGS) {
    MODULE_UNUSED();if (fail_map) return module_result(0,0);
    assert(a>=0xf0000000 && b && b<=sizeof(registers));++mappings;
    return module_result((uintptr_t)registers,1);
}
int main(void) {
    assert(CALL(mmio_surface_map,0,0,0).status==KERNEL_SURFACE_FAILED);
    assert(mem_init(&heap,arena,sizeof(arena)));
    fixture.hub=(struct kernel_surfaces){.magic=KERNEL_SURFACES_MAGIC,.major=2,
        .header_bytes=offsetof(__typeof__(fixture),methods),.count=4,
        .tables_offset=offsetof(__typeof__(fixture),tables),.table_count=1};
    const char *names[]={"mem.calloc","mem.free","hub.publish_table","hardware.map_mmio"};
    kernel_surface_call calls[]={allocate,release,publish,map};
    for (unsigned i=0;i<4;++i) {strcpy(fixture.methods[i].name,names[i]);fixture.methods[i].call=calls[i];fixture.methods[i].major=1;}
    hardware.header=(struct hwenum_table){.magic=HWENUM_TABLE_MAGIC,.major=1,.count=6,.entry_bytes=sizeof(struct hwenum_descriptor)};
    hardware.entries[0]=(struct hwenum_descriptor){.kind=HWENUM_HPET,.address=0xfed00000,.length=1024};
    hardware.entries[1]=(struct hwenum_descriptor){.kind=HWENUM_LOCAL_APIC,.address=0xfee00000,.length=4096};
    hardware.entries[2]=(struct hwenum_descriptor){.kind=HWENUM_IO_APIC,.address=0xfec00000,.length=4096};
    hardware.entries[3]=(struct hwenum_descriptor){.kind=HWENUM_PCI,.address=0xf0000000,.length=4096,
        .data.pci={.bars={0x1000000c,1,0x3001,0xf1000000,0,0}}};
    hardware.entries[4]=(struct hwenum_descriptor){.kind=HWENUM_MEMORY,.address=0x100000,.length=4096};
    hardware.entries[5]=(struct hwenum_descriptor){.kind=HWENUM_PCI,.address=0xf0001000,.length=4096,
        .data.pci={.header_type=1,.bars={0xf2000000,0,0xf3000000}}}; /* bridge: only two BARs */
    fixture.tables[0]=(struct kernel_hub_table){.name=HWENUM_TABLE_NAME,.address=(uintptr_t)&hardware,.bytes=sizeof(hardware)};
    fail_alloc=true;assert(CALL(mmio_surface_init,&fixture.hub,&heap,0).status==KERNEL_SURFACE_FAILED);
    fail_alloc=false;fail_publish=true;
    assert(CALL(mmio_surface_init,&fixture.hub,&heap,0).status==KERNEL_SURFACE_FAILED && !allocations);
    fail_publish=false;
    struct kernel_surface_result initialized=CALL(mmio_surface_init,&fixture.hub,&heap,0);
    assert(!initialized.status && initialized.value && allocations==1);
    struct mmio_table *table=(void *)initialized.value;
    assert(table->count==8 && table->magic==MMIO_TABLE_MAGIC && !mappings);
    assert(kernel_hub_find_table(&fixture.hub,MMIO_TABLE_NAME)->address==initialized.value);
    assert(CALL(mmio_surface_init,&fixture.hub,&heap,0).value==initialized.value && allocations==1);
    assert(CALL(mmio_surface_init,&fixture.hub,arena,0).status==KERNEL_SURFACE_FAILED);
    assert(table->entries[4].physical==UINT64_C(0x110000000) && !table->entries[4].bytes &&
        table->entries[4].bar==0 && table->entries[4].flags==(MMIO_BAR64|MMIO_PREFETCHABLE));
    assert(table->entries[5].physical==0xf1000000 && table->entries[5].bar==3);
    assert(CALL(mmio_surface_read,0,0,8).status==KERNEL_SURFACE_FAILED);
    fail_map=true;assert(CALL(mmio_surface_map,0,0,0).status==KERNEL_SURFACE_FAILED && !table->entries[0].virtual_address);
    fail_map=false;
    assert(CALL(mmio_surface_map,0,0,0).value==(uintptr_t)registers && mappings==1);
    assert(CALL(mmio_surface_map,0,1024,0).value==(uintptr_t)registers && mappings==1);
    assert(CALL(mmio_surface_map,0,4096,0).status==KERNEL_SURFACE_FAILED);
    for (unsigned width=1;width<=8;width*=2) {
        uintptr_t value=width==8?UINT64_C(0x123456789abcdef0):(UINT64_C(1)<<(width*8))-1;
        assert(!mmio_surface_write(0,16,width,value,0,0).status);
        assert(CALL(mmio_surface_read,0,16,width).value==value);
        assert(CALL(mmio_surface_read,0,1024-width,width).status==KERNEL_SURFACE_OK);
        assert(CALL(mmio_surface_read,0,1024,width).status==KERNEL_SURFACE_FAILED);
        if (width>1) assert(CALL(mmio_surface_read,0,1,width).status==KERNEL_SURFACE_FAILED);
    }
    assert(mmio_surface_write(0,0,1,256,0,0).status==KERNEL_SURFACE_FAILED);
    assert(mmio_surface_write(0,1024,8,1,0,0).status==KERNEL_SURFACE_FAILED);
    assert(CALL(mmio_surface_read,0,SIZE_MAX,8).status==KERNEL_SURFACE_FAILED);
    assert(CALL(mmio_surface_read,0,0,3).status==KERNEL_SURFACE_FAILED);
    assert(CALL(mmio_surface_read,99,0,4).status==KERNEL_SURFACE_FAILED);
    assert(CALL(mmio_surface_map,99,0,0).status==KERNEL_SURFACE_FAILED);
    assert(CALL(mmio_surface_map,4,0,0).status==KERNEL_SURFACE_FAILED);
    assert(!CALL(mmio_surface_map,4,4096,0).status && table->entries[4].bytes==4096);
    assert(CALL(mmio_surface_map,4,8192,0).status==KERNEL_SURFACE_FAILED);
    assert(CALL(mmio_surface_map,5,UINT64_MAX,0).status==KERNEL_SURFACE_FAILED);
    /* Oversized unsized BAR claim must not swallow another known device. */
    assert(CALL(mmio_surface_map,5,0x10000000,0).status==KERNEL_SURFACE_FAILED);
    mem_free(&heap,table);--allocations;assert(!allocations);
    void *whole=mem_malloc(&heap,sizeof(arena)-64);assert(whole);mem_free(&heap,whole);
    puts("PASS: MMIO table generation, BAR decoding, publication/failure cleanup, mapping, widths/alignment/bounds and read/write");
    return 0;
}
