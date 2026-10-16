#ifndef CLOUDBERRY_KMODULE_MMIO_H
#define CLOUDBERRY_KMODULE_MMIO_H
#include "module.h"
#define MMIO_TABLE_NAME "mmio.regions"
#define MMIO_TABLE_MAGIC UINT64_C(0x314f494d4d424343)
enum mmio_kind { MMIO_LOCAL_APIC=1, MMIO_IO_APIC, MMIO_HPET, MMIO_PCI_CONFIG, MMIO_PCI_BAR };
enum mmio_flags { MMIO_PREFETCHABLE=1, MMIO_BAR64=2, MMIO_MAPPED=4 };
struct mmio_region {
    uint64_t physical, bytes;
    uintptr_t virtual_address;
    uint64_t hardware_index;
    uint32_t kind, flags, bar, reserved;
};
struct mmio_table {
    uint64_t magic;
    uint16_t major, minor;
    uint32_t entry_bytes;
    uint64_t count;
    struct mmio_region entries[];
};
/* Mediated foreground calls use the global lock; not allocating ISR calls. Trusted kernel pointers only.
 * init(hub,heap) -> boot-lifetime table generated from hwenum.hardware; idempotent.
 * map(index,bytes) -> RW/UC/NX address. Zero bytes uses the known extent.
 * PCI BARs start unsized: their driver must supply an extent from its contract
 * or a separate safe BAR-sizing procedure. Once mapped it cannot be resized.
 * read(index,offset,width) -> value; write(index,offset,width,value) -> 1.
 * Width is 1/2/4/8 bytes; accesses require mapping, natural alignment and bounds.
 * Device contracts determine which widths/registers are actually legal.
 * Table indexes are zero-based. Failure is reported in result.status.
 * Mappings live until reboot; no device enabling, IRQ setup or DMA implied. */
struct kernel_surface_result mmio_surface_init(MODULE_ARGS);
struct kernel_surface_result mmio_surface_map(MODULE_ARGS);
struct kernel_surface_result mmio_surface_read(MODULE_ARGS);
struct kernel_surface_result mmio_surface_write(MODULE_ARGS);
#endif
