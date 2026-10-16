#include "mmio.h"
#include "hwenum.h"
static struct {
    const struct kernel_surfaces *hub;
    void *heap;
    struct mmio_table *table;
    kernel_surface_call map;
} state;
static void add(struct mmio_table *table,size_t *count,const struct mmio_region *r) {
    if (table) table->entries[*count]=*r;
    ++*count;
}
static size_t collect(const struct hwenum_table *hw,struct mmio_table *table) {
    size_t count=0;
    for (size_t i=0;i<hw->count;++i) {
        const struct hwenum_descriptor *h=&hw->entries[i];
        struct mmio_region r={.physical=h->address,.bytes=h->length,.hardware_index=i};
        if (h->kind==HWENUM_LOCAL_APIC) r.kind=MMIO_LOCAL_APIC;
        else if (h->kind==HWENUM_IO_APIC) r.kind=MMIO_IO_APIC;
        else if (h->kind==HWENUM_HPET) r.kind=MMIO_HPET;
        else if (h->kind==HWENUM_PCI) r.kind=MMIO_PCI_CONFIG;
        if (r.kind && r.physical && r.bytes && r.bytes<=UINT64_MAX-r.physical) add(table,&count,&r);
        if (h->kind!=HWENUM_PCI) continue;
        unsigned header=h->data.pci.header_type & 127;
        unsigned bars=header==0?6:header==1?2:0;
        for (unsigned bar=0;bar<bars;++bar) {
            uint32_t low=h->data.pci.bars[bar];
            if (!low || low==UINT32_MAX || (low&1)) continue; /* No port I/O. */
            unsigned type=(low>>1)&3;
            if (type==3 || (type==2 && bar+1>=bars)) continue;
            r=(struct mmio_region){.physical=low&~UINT64_C(15),.hardware_index=i,
                .kind=MMIO_PCI_BAR,.bar=bar,.flags=(low&8)?MMIO_PREFETCHABLE:0};
            if (type==2) { r.physical|=(uint64_t)h->data.pci.bars[++bar]<<32; r.flags|=MMIO_BAR64; }
            if (r.physical) add(table,&count,&r);
        }
    }
    return count;
}
struct kernel_surface_result mmio_surface_init(MODULE_ARGS) {
    MODULE_UNUSED(); const struct kernel_surfaces *hub=(const void *)a;
    if (state.table) return module_result((uintptr_t)state.table,state.hub==hub && state.heap==(void *)b);
    if (!hub || hub->magic!=KERNEL_SURFACES_MAGIC || hub->major!=2 || !b) return module_result(0,0);
    const struct kernel_hub_table *ref=kernel_hub_find_table(hub,HWENUM_TABLE_NAME);
    if (!ref || ref->bytes<sizeof(struct hwenum_table)) return module_result(0,0);
    const struct hwenum_table *hw=(const void *)ref->address;
    if (!hw || hw->magic!=HWENUM_TABLE_MAGIC || hw->major!=1 || hw->entry_bytes!=sizeof(hw->entries[0]) ||
        hw->count>(ref->bytes-sizeof(*hw))/sizeof(hw->entries[0]) ||
        hw->count>(SIZE_MAX-sizeof(struct mmio_table))/(7*sizeof(struct mmio_region))) return module_result(0,0);
    kernel_surface_call alloc=kernel_surface_find_version(hub,"mem.calloc",1,0),
        release=kernel_surface_find_version(hub,"mem.free",1,0),
        publish=kernel_surface_find_version(hub,"hub.publish_table",1,0),
        map=kernel_surface_find_version(hub,"hardware.map_mmio",1,0);
    if (!alloc || !release || !publish || !map) return module_result(0,0);
    size_t count=collect(hw,NULL),bytes=sizeof(struct mmio_table)+count*sizeof(struct mmio_region);
    struct kernel_surface_result memory=alloc(b,1,bytes,0,0,0);
    if (memory.status || !memory.value) return module_result(0,0);
    struct mmio_table *table=(void *)memory.value;
    *table=(struct mmio_table){.magic=MMIO_TABLE_MAGIC,.major=1,.entry_bytes=sizeof(struct mmio_region),.count=count};
    collect(hw,table);
    struct kernel_hub_table published={.name=MMIO_TABLE_NAME,.address=(uintptr_t)table,.bytes=bytes,
        .count=count,.entry_bytes=sizeof(struct mmio_region),.entries_offset=sizeof(*table)};
    if (publish((uintptr_t)&published,0,0,0,0,0).status) {
        release(b,(uintptr_t)table,0,0,0,0); return module_result(0,0);
    }
    state.hub=hub; state.heap=(void *)b; state.map=map; state.table=table;
    return module_result((uintptr_t)table,1);
}
struct kernel_surface_result mmio_surface_map(MODULE_ARGS) {
    MODULE_UNUSED();
    if (!state.table || a>=state.table->count) return module_result(0,0);
    struct mmio_region *r=&state.table->entries[a];
    uint64_t bytes=b?b:r->bytes;
    if (!bytes || bytes>UINT64_MAX-r->physical || (r->bytes && bytes!=r->bytes)) return module_result(0,0);
    if (r->flags&MMIO_MAPPED) return module_result(r->virtual_address,1);
    for (size_t i=0;i<state.table->count;++i) {
        const struct mmio_region *other=&state.table->entries[i];
        if (i!=a && other->bytes && r->physical<other->physical+other->bytes &&
            other->physical<r->physical+bytes) return module_result(0,0);
    }
    struct kernel_surface_result mapped=state.map(r->physical,bytes,0,0,0,0);
    if (mapped.status || !mapped.value) return module_result(0,0);
    r->bytes=bytes; r->virtual_address=mapped.value; r->flags|=MMIO_MAPPED;
    return mapped;
}
static uintptr_t access_address(uintptr_t index,uintptr_t offset,uintptr_t width) {
    if (!state.table || index>=state.table->count || (width!=1 && width!=2 && width!=4 && width!=8)) return 0;
    const struct mmio_region *r=&state.table->entries[index];
    if (!(r->flags&MMIO_MAPPED) || offset>r->bytes || width>r->bytes-offset ||
        ((r->physical+offset)&(width-1))) return 0;
    return r->virtual_address+offset;
}
struct kernel_surface_result mmio_surface_read(MODULE_ARGS) {
    MODULE_UNUSED(); uintptr_t address=access_address(a,b,c); if (!address) return module_result(0,0);
    uint64_t value;
    __asm__ volatile("mfence" ::: "memory");
    if (c==1) value=*(volatile uint8_t *)address;
    else if (c==2) value=*(volatile uint16_t *)address;
    else if (c==4) value=*(volatile uint32_t *)address;
    else value=*(volatile uint64_t *)address;
    __asm__ volatile("mfence" ::: "memory");
    return module_result(value,1);
}
struct kernel_surface_result mmio_surface_write(MODULE_ARGS) {
    MODULE_UNUSED(); uintptr_t address=access_address(a,b,c); if (!address) return module_result(0,0);
    if (c<8 && (d>>(c*8))) return module_result(0,0); /* Reject truncation. */
    __asm__ volatile("mfence" ::: "memory");
    if (c==1) *(volatile uint8_t *)address=(uint8_t)d;
    else if (c==2) *(volatile uint16_t *)address=(uint16_t)d;
    else if (c==4) *(volatile uint32_t *)address=(uint32_t)d;
    else *(volatile uint64_t *)address=d;
    __asm__ volatile("mfence" ::: "memory");
    return module_result(1,1);
}
