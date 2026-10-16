#include "module.h"
#include "hwenum.h"
struct kernel_surface_result hwenum_surface_enumerate(MODULE_ARGS) {
    MODULE_UNUSED();
    const struct kernel_surfaces *hub = (const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    void *table = hwenum_enumerate(hub, (void *)a, (const void *)b);
    return module_result((uintptr_t)table, table != NULL);
}
struct kernel_surface_result hwenum_surface_find(MODULE_ARGS) {
    MODULE_UNUSED();
    const void *record = hwenum_find((const void *)a, (uint32_t)b, c);
    return module_result((uintptr_t)record, record != NULL);
}

static void debug(const char *message) {
    while (*message) __asm__ volatile("outb %0,$0xe9" :: "a"((uint8_t)*message++));
}
static void debug_hex(uint64_t value,unsigned width) {
    static const char digits[]="0123456789abcdef";
    char text[17];text[width]=0;
    for (unsigned i=0;i<width;++i) {text[width-i-1]=digits[value&15];value>>=4;}
    debug(text);
}
static void debug_address(uintptr_t address) {debug("0x");debug_hex(address,16);}
static void debug_decimal(uint64_t value) {
    char text[21];
    size_t index = sizeof(text) - 1;
    text[index] = 0;
    do { text[--index] = (char)('0' + value % 10); value /= 10; } while (value);
    debug(text + index);
}
struct kernel_surface_result hwenum_surface_dump(MODULE_ARGS) {
    MODULE_UNUSED();
    const struct kernel_surfaces *hub=(const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    const struct kernel_hub_table *record=kernel_hub_find_table(hub,HWENUM_TABLE_NAME);
    if (!record || record->bytes<sizeof(struct hwenum_table)) return module_result(0,0);
    const struct hwenum_table *table=(const void *)record->address;
    if (!table || table->magic!=HWENUM_TABLE_MAGIC || table->major!=1 ||
        table->entry_bytes!=sizeof(struct hwenum_descriptor) || table->header_bytes!=sizeof(*table) ||
        table->count>(record->bytes-sizeof(*table))/sizeof(struct hwenum_descriptor)) return module_result(0,0);
    debug("hwenum.hardware ("); debug_decimal(table->count); debug(" descriptors)\n");
    for (uint64_t i = 0; i < table->count; ++i) {
        const struct hwenum_descriptor *d = &table->entries[i];
        debug("hw ");
        switch (d->kind) {
            case HWENUM_MEMORY: {
                const char *type;
                switch (d->data.memory.type) {
                    case 0: type = "firmware-usable"; break;
                    case 1: type = "reserved"; break;
                    case 2: type = "acpi-reclaimable"; break;
                    case 3: type = "acpi-nvs"; break;
                    case 4: type = "bad"; break;
                    case 5: type = "bootloader-reclaimable"; break;
                    case 6: type = "kernel/modules"; break;
                    case 7: type = "framebuffer"; break;
                    default: type = "unknown"; break;
                }
                debug("memory "); debug(type); debug(" type="); debug_decimal(d->data.memory.type);
                break;
            }
            case HWENUM_ACPI_TABLE: {
                char signature[5];
                for (unsigned j = 0; j < 4; ++j) signature[j] = d->data.acpi.signature[j];
                signature[4] = 0;
                debug("acpi "); debug(signature); debug(" revision="); debug_decimal(d->data.acpi.revision);
                break;
            }
            case HWENUM_CPU:
                debug("cpu uid="); debug_decimal(d->data.cpu.uid);
                debug(" apic="); debug_decimal(d->data.cpu.apic_id);
                debug(" flags=0x"); debug_hex(d->flags, 8);
                break;
            case HWENUM_PCI:
                debug("pci "); debug_hex(d->data.pci.segment, 4); debug(":");
                debug_hex(d->data.pci.bus, 2); debug(":"); debug_hex(d->data.pci.slot, 2); debug(".");
                debug_hex(d->data.pci.function, 1);
                debug(" id="); debug_hex(d->data.pci.vendor, 4); debug(":"); debug_hex(d->data.pci.device, 4);
                debug(" class="); debug_hex(d->data.pci.class_code, 2); debug(":");
                debug_hex(d->data.pci.subclass, 2); debug(":"); debug_hex(d->data.pci.programming_interface, 2);
                debug(" revision="); debug_hex(d->data.pci.revision, 2);
                debug(" header="); debug_hex(d->data.pci.header_type, 2);
                debug(" irq="); debug_decimal(d->data.pci.irq_line); debug("/"); debug_decimal(d->data.pci.irq_pin);
                for (unsigned j = 0; j < 6; ++j) if (d->data.pci.bars[j]) {
                    debug(" bar"); debug_decimal(j); debug("=0x"); debug_hex(d->data.pci.bars[j], 8);
                }
                break;
            case HWENUM_LOCAL_APIC:
                debug("local-apic flags=0x"); debug_hex(d->flags, 8);
                break;
            case HWENUM_IO_APIC:
                debug("io-apic id="); debug_decimal(d->data.interrupt.id);
                debug(" gsi-base="); debug_decimal(d->data.interrupt.gsi_base);
                break;
            case HWENUM_HPET: debug("hpet"); break;
            default: debug("unknown kind="); debug_decimal(d->kind); break;
        }
        if (d->kind != HWENUM_CPU) {
            debug(" address="); debug_address(d->address); debug(" bytes="); debug_decimal(d->length);
        }
        debug("\n");
    }
    return module_result(table->count,1);
}
