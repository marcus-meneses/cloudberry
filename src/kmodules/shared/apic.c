#include "apic.h"
#include "apic-platform.h"
#include "mmio.h"
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
#define MASK (1u<<16)
static struct {const struct kernel_surfaces *hub;volatile struct apic_info info;int initialized;} state;
static int mask_ioapics(const struct mmio_table *table,kernel_surface_call map,
                        kernel_surface_call read,kernel_surface_call write) {
    for (size_t i=0;i<table->count;++i) if (table->entries[i].kind==MMIO_IO_APIC) {
        if (CALL(map,i,0,0).status || write(i,0,4,1,0,0).status) return 0;
        struct kernel_surface_result version=CALL(read,i,16,4);
        if (version.status) return 0;
        unsigned maximum=(version.value>>16)&255;
        if (maximum>119) return 0; /* IOREGSEL is 8 bits; two registers per input. */
        for (unsigned input=0;input<=maximum;++input) {
            if (write(i,0,4,0x10+2*input,0,0).status) return 0;
            struct kernel_surface_result low=CALL(read,i,16,4);
            if (low.status || write(i,16,4,low.value|MASK,0,0).status) return 0;
        }
    }
    return 1;
}
static void local_setup(uintptr_t address,uint32_t mode) {
    apic_platform_activate(address,mode);
    apic_register_write(0x350,apic_register_read(0x350)|MASK);
    uint32_t lint1=apic_register_read(0x360);
    if ((lint1&0x700)!=0x400) apic_register_write(0x360,lint1|MASK);
    unsigned maximum=(apic_register_read(0x30)>>16)&255;
    if (maximum>=5) apic_register_write(0x330,apic_register_read(0x330)|MASK);
    if (maximum>=4) apic_register_write(0x340,apic_register_read(0x340)|MASK);
    apic_register_write(0x370,apic_register_read(0x370)|MASK);
    apic_register_write(0x80,0);
    apic_register_write(0xf0,(apic_register_read(0xf0)&~(255u|(1u<<12)))|APIC_SPURIOUS_VECTOR|(1u<<8));
}
struct kernel_surface_result apic_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)a;
    if (state.initialized) return module_result(1,state.hub==hub);
    if (!hub || hub->magic!=KERNEL_SURFACES_MAGIC || hub->major!=2) return module_result(0,0);
    kernel_surface_call claim=kernel_surface_find_version(hub,"inthandle.claim",1,0),
        release=kernel_surface_find_version(hub,"inthandle.release",1,0),
        map=kernel_surface_find_version(hub,"mmio.map",1,0),
        read=kernel_surface_find_version(hub,"mmio.read",1,0),
        write=kernel_surface_find_version(hub,"mmio.write",1,0);
    const struct kernel_hub_table *ref=kernel_hub_find_table(hub,MMIO_TABLE_NAME);
    struct apic_platform platform;
    if (!claim || !release || !map || !read || !write || !ref || !apic_platform_probe(&platform)) return module_result(0,0);
    const struct mmio_table *table=(const void *)ref->address;
    if (!table || ref->bytes<sizeof(*table) || table->magic!=MMIO_TABLE_MAGIC || table->major!=1 ||
        table->entry_bytes!=sizeof(table->entries[0]) || table->count>(ref->bytes-sizeof(*table))/sizeof(table->entries[0])) return module_result(0,0);
    uint64_t flags=apic_irq_save();
    if (claim(APIC_SPURIOUS_VECTOR,(uintptr_t)"apic.spurious",(uintptr_t)&state,(uintptr_t)&state,0,0).status) {
        apic_irq_restore(flags);return module_result(0,0);
    }
    uintptr_t address=0;
    if (platform.mode==APIC_XAPIC) {
        for (size_t i=0;i<table->count;++i) if (table->entries[i].kind==MMIO_LOCAL_APIC && table->entries[i].physical==platform.physical) {
            struct kernel_surface_result mapped=CALL(map,i,0,0);if (!mapped.status) address=mapped.value;break;
        }
        if (!address) goto fail;
    }
    if (!mask_ioapics(table,map,read,write)) goto fail;
    apic_mask_pic();local_setup(address,platform.mode);
    state.hub=hub;state.info=(struct apic_info){.address=address,.physical=platform.physical,
        .mode=platform.mode,.spurious_vector=APIC_SPURIOUS_VECTOR};state.initialized=1;
    apic_irq_restore(flags);return module_result(1,1);
fail:
    CALL(release,APIC_SPURIOUS_VECTOR,&state,0);apic_irq_restore(flags);return module_result(0,0);
}
struct kernel_surface_result apic_surface_info(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.initialized || !a) return module_result(0,0);
    uint64_t flags=apic_irq_save();*(struct apic_info *)a=(struct apic_info){state.info.address,state.info.physical,__atomic_load_n(&state.info.spurious,__ATOMIC_RELAXED),state.info.mode,state.info.spurious_vector};apic_irq_restore(flags);return module_result(1,1);
}
struct kernel_surface_result apic_surface_spurious(MODULE_ARGS) {
    MODULE_UNUSED();const struct inthandle_frame *frame=(const void *)a;
    if (!state.initialized || !frame || frame->vector!=APIC_SPURIOUS_VECTOR || b!=(uintptr_t)&state || c!=INTHANDLE_CPU_ENTRY) return module_result(0,0);
    __atomic_add_fetch(&state.info.spurious,1,__ATOMIC_RELAXED);
    return module_result(1,1);
}

struct kernel_surface_result apic_surface_cpu_init(MODULE_ARGS) {
    MODULE_UNUSED();struct apic_platform platform;
    if(!state.initialized || !apic_platform_probe(&platform) || platform.mode!=state.info.mode || platform.physical!=state.info.physical)return module_result(0,0);
    uint64_t flags=apic_irq_save();local_setup(state.info.address,state.info.mode);apic_irq_restore(flags);return module_result(1,1);
}
