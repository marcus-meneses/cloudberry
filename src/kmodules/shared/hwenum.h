#ifndef CLOUDBERRY_KMODULE_HWENUM_H
#define CLOUDBERRY_KMODULE_HWENUM_H
#include "module.h"
#include <stddef.h>
#include <stdint.h>
#include "../../kernel/surfaces.h"
#include "../../boot/memory.h"

#define HWENUM_TABLE_MAGIC UINT64_C(0x314c425457484243)
#define HWENUM_MEMORY_FIRMWARE_MAP 1u /* Original map; consult memory.reservations before allocation. */
#define HWENUM_TABLE_NAME "hwenum.hardware"
enum hwenum_kind { HWENUM_MEMORY = 1, HWENUM_ACPI_TABLE, HWENUM_CPU,
                   HWENUM_PCI, HWENUM_LOCAL_APIC, HWENUM_IO_APIC, HWENUM_HPET };
enum hwenum_coverage { HWENUM_COV_MEMORY = 1, HWENUM_COV_ACPI = 2,
                      HWENUM_COV_CPU = 4, HWENUM_COV_PCI = 8 };
struct hwenum_descriptor {
    uint32_t kind, flags;
    uint64_t address, length;
    union {
        struct { uint64_t type; } memory;
        struct { char signature[4]; uint8_t revision; } acpi;
        struct { uint32_t uid, apic_id; } cpu;
        struct { uint32_t id, gsi_base; } interrupt;
        struct {
            uint16_t segment, vendor, device;
            uint8_t bus, slot, function, class_code, subclass, programming_interface;
            uint8_t revision, header_type, irq_line, irq_pin;
            uint32_t bars[6]; /* Raw values; probing/sizing is a driver's job. */
        } pci;
    } data;
};
struct hwenum_table {
    uint64_t magic;
    uint16_t major, minor;
    uint32_t coverage;
    uint64_t count, capacity;
    uint32_t entry_bytes, header_bytes;
    void *heap; /* Owning mem context; must outlive the published table. */
    struct hwenum_descriptor entries[];
};
struct hwenum_boot {
    uint64_t rsdp_physical;
    const struct boot_memory_map *memory;
};
/* Synchronous kernel module calls. Allocation/publication/physical reads all
 * go through kernel-mediated hub surfaces. An existing table is returned as-is;
 * the first successful enumeration publishes one boot-lifetime snapshot.
 * Failure frees temporary allocations and publishes nothing. */
struct hwenum_table *hwenum_enumerate(const struct kernel_surfaces *hub, void *heap,
                                     const struct hwenum_boot *boot);
/* Zero-based ordinal within a kind; kind=0 selects every descriptor. */
const struct hwenum_descriptor *hwenum_find(const struct hwenum_table *table,
                                           uint32_t kind, size_t ordinal);
/* dump() -> published descriptor count; formats this module's inventory to
 * the current pre-flight debug sink (port 0xe9). Does not enumerate or mutate. */
struct kernel_surface_result hwenum_surface_dump(MODULE_ARGS);
#endif
