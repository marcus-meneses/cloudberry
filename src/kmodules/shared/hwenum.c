#include "hwenum.h"

struct scan {
    void *heap;
    kernel_surface_call alloc, resize, release, read, publish;
    struct hwenum_table *table;
};
#define CALL(method, a, b, c) method((uintptr_t)(a), (uintptr_t)(b), (uintptr_t)(c), 0, 0, 0)
static void zero(void *pointer, size_t size) {
    unsigned char *p = pointer;
    for (size_t i = 0; i < size; ++i) p[i] = 0;
}
static uint16_t u16(const unsigned char *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
static uint32_t u32(const unsigned char *p) { return (uint32_t)u16(p) | (uint32_t)u16(p + 2) << 16; }
static uint64_t u64(const unsigned char *p) { return (uint64_t)u32(p) | (uint64_t)u32(p + 4) << 32; }
static bool read_bytes(struct scan *s, uint64_t address, void *out, size_t size) {
    if (size > UINT64_MAX - address) return false;
    struct kernel_surface_result r = CALL(s->read, address, out, size);
    return r.status == KERNEL_SURFACE_OK && r.value == size;
}
static bool checksum(struct scan *s, uint64_t address, size_t size) {
    unsigned char buffer[256], sum = 0;
    while (size) {
        size_t amount = size < sizeof(buffer) ? size : sizeof(buffer);
        if (!read_bytes(s, address, buffer, amount)) return false;
        for (size_t i = 0; i < amount; ++i) sum = (unsigned char)(sum + buffer[i]);
        address += amount; size -= amount;
    }
    return sum == 0;
}
static struct hwenum_descriptor *append(struct scan *s, uint32_t kind) {
    struct hwenum_table *t = s->table;
    if (t->count == t->capacity) {
        size_t capacity = t->capacity * 2;
        if (capacity < t->capacity || capacity > (SIZE_MAX - sizeof(*t)) / sizeof(t->entries[0])) return NULL;
        size_t bytes = sizeof(*t) + capacity * sizeof(t->entries[0]);
        struct kernel_surface_result r = CALL(s->resize, s->heap, t, bytes);
        if (r.status || !r.value) return NULL;
        s->table = t = (struct hwenum_table *)r.value;
        t->capacity = capacity;
    }
    struct hwenum_descriptor *d = &t->entries[t->count++];
    zero(d, sizeof(*d)); d->kind = kind;
    return d;
}
static bool acpi_header(struct scan *s, uint64_t address, unsigned char header[36]) {
    return address && read_bytes(s, address, header, 36) && u32(header + 4) >= 36 &&
           checksum(s, address, u32(header + 4));
}
static bool signature(const unsigned char *p, const char *name) {
    return p[0] == name[0] && p[1] == name[1] && p[2] == name[2] && p[3] == name[3];
}
static bool add_acpi(struct scan *s, uint64_t address, const unsigned char *header) {
    for (size_t i = 0; i < s->table->count; ++i)
        if (s->table->entries[i].kind == HWENUM_ACPI_TABLE && s->table->entries[i].address == address) return true;
    struct hwenum_descriptor *d = append(s, HWENUM_ACPI_TABLE);
    if (!d) return false;
    d->address = address; d->length = u32(header + 4);
    for (unsigned i = 0; i < 4; ++i) d->data.acpi.signature[i] = (char)header[i];
    d->data.acpi.revision = header[8];
    return true;
}
static bool madt(struct scan *s, uint64_t address, uint32_t length) {
    unsigned char fixed[8];
    if (length < 44 || !read_bytes(s, address + 36, fixed, 8)) return false;
    struct hwenum_descriptor *local = append(s, HWENUM_LOCAL_APIC);
    if (!local) return false;
    size_t local_index = s->table->count - 1;
    local->address = u32(fixed); local->length = 4096; local->flags = u32(fixed + 4);
    for (size_t offset = 44; offset < length;) {
        unsigned char record[16];
        if (length - offset < 2 || !read_bytes(s, address + offset, record, 2) ||
            record[1] < 2 || record[1] > length - offset) return false;
        size_t size = record[1];
        bool known = record[0] == 0 || record[0] == 1 || record[0] == 5 || record[0] == 9;
        size_t required = record[0] == 0 ? 8 : record[0] == 9 ? 16 : 12;
        if (known) {
            if (size < required || !read_bytes(s, address + offset, record, required)) return false;
            if (record[0] == 5) s->table->entries[local_index].address = u64(record + 4);
            else {
                struct hwenum_descriptor *d = append(s, record[0] == 1 ? HWENUM_IO_APIC : HWENUM_CPU);
                if (!d) return false;
                if (record[0] == 1) {
                    d->address = u32(record + 4); d->length = 4096;
                    d->data.interrupt.id = record[2]; d->data.interrupt.gsi_base = u32(record + 8);
                } else {
                    d->data.cpu.uid = record[0] == 0 ? record[2] : u32(record + 12);
                    d->data.cpu.apic_id = record[0] == 0 ? record[3] : u32(record + 4);
                    d->flags = u32(record + (record[0] == 0 ? 4 : 8));
                }
            }
        }
        offset += size;
    }
    s->table->coverage |= HWENUM_COV_CPU;
    return true;
}
static bool pci_function(struct scan *s, uint64_t ecam, uint16_t segment,
                         unsigned bus, unsigned slot, unsigned function, bool *multifunction) {
    uint64_t address = ecam + ((uint64_t)bus << 20) + ((uint64_t)slot << 15) + ((uint64_t)function << 12);
    unsigned char config[64];
    if (!read_bytes(s, address, config, 4)) return false;
    if (u16(config) == 0xffff) { *multifunction = false; return true; }
    for (unsigned offset = 4; offset < sizeof(config); offset += 4)
        if (!read_bytes(s, address + offset, config + offset, 4)) return false;
    struct hwenum_descriptor *d = append(s, HWENUM_PCI);
    if (!d) return false;
    d->address = address; d->length = 4096;
    d->data.pci.segment = segment; d->data.pci.bus = (uint8_t)bus;
    d->data.pci.slot = (uint8_t)slot; d->data.pci.function = (uint8_t)function;
    d->data.pci.vendor = u16(config); d->data.pci.device = u16(config + 2);
    d->data.pci.revision = config[8]; d->data.pci.programming_interface = config[9];
    d->data.pci.subclass = config[10]; d->data.pci.class_code = config[11];
    d->data.pci.header_type = config[14]; d->data.pci.irq_line = config[60]; d->data.pci.irq_pin = config[61];
    unsigned bars = (config[14] & 127) == 0 ? 6 : (config[14] & 127) == 1 ? 2 : 0;
    for (unsigned i = 0; i < bars; ++i) d->data.pci.bars[i] = u32(config + 16 + i * 4);
    *multifunction = (config[14] & 128) != 0;
    return true;
}
static bool mcfg(struct scan *s, uint64_t address, uint32_t length) {
    if (length < 44 || (length - 44) % 16) return false;
    for (size_t offset = 44; offset < length; offset += 16) {
        unsigned char entry[16];
        if (!read_bytes(s, address + offset, entry, 16)) return false;
        uint64_t base = u64(entry);
        unsigned first = entry[10], last = entry[11];
        uint16_t segment = u16(entry + 8);
        if (!base || (base & ((1u << 20) - 1)) || first > last ||
            base > UINT64_MAX - (((uint64_t)last + 1) << 20)) return false;
        /* Reject overlapping ranges in a segment to avoid duplicate descriptors. */
        for (size_t previous = 44; previous < offset; previous += 16) {
            unsigned char other[16];
            if (!read_bytes(s, address + previous, other, 16)) return false;
            if (u16(other + 8) == segment && first <= other[11] && other[10] <= last) return false;
        }
        for (unsigned bus = first; bus <= last; ++bus)
            for (unsigned slot = 0; slot < 32; ++slot) {
                bool multi;
                if (!pci_function(s, base, segment, bus, slot, 0, &multi)) return false;
                if (multi) for (unsigned function = 1; function < 8; ++function)
                    if (!pci_function(s, base, segment, bus, slot, function, &multi)) return false;
            }
    }
    s->table->coverage |= HWENUM_COV_PCI;
    return true;
}
static bool firmware(struct scan *s, uint64_t rsdp) {
    unsigned char pointer[36], header[36];
    if (!read_bytes(s, rsdp, pointer, 20) ||
        !signature(pointer, "RSD ") || !signature(pointer + 4, "PTR ") || !checksum(s, rsdp, 20)) return false;
    uint64_t root = u32(pointer + 16);
    size_t width = 4;
    if (pointer[15] >= 2) {
        if (!read_bytes(s, rsdp, pointer, 36) || u32(pointer + 20) < 36 ||
            !checksum(s, rsdp, u32(pointer + 20))) return false;
        if (u64(pointer + 24)) { root = u64(pointer + 24); width = 8; }
    }
    if (!acpi_header(s, root, header) || !signature(header, width == 8 ? "XSDT" : "RSDT") ||
        (u32(header + 4) - 36) % width || !add_acpi(s, root, header)) return false;
    uint32_t root_length = u32(header + 4);
    for (size_t offset = 36; offset < root_length; offset += width) {
        unsigned char bytes[8];
        if (!read_bytes(s, root + offset, bytes, width)) return false;
        uint64_t address = width == 8 ? u64(bytes) : u32(bytes);
        if (!acpi_header(s, address, header) || !add_acpi(s, address, header)) return false;
        uint32_t length = u32(header + 4);
        if (signature(header, "APIC") && !madt(s, address, length)) return false;
        if (signature(header, "MCFG") && !mcfg(s, address, length)) return false;
        if (signature(header, "HPET")) {
            unsigned char timer[12];
            if (length < 56 || !read_bytes(s, address + 40, timer, 12) || timer[0] != 0) return false;
            struct hwenum_descriptor *d = append(s, HWENUM_HPET);
            if (!d) return false;
            d->address = u64(timer + 4); d->length = 1024;
        }
        if (signature(header, "FACP") && length >= 44) {
            unsigned char field[8];
            if (!read_bytes(s, address + 40, field, 4)) return false;
            uint64_t dsdt = u32(field);
            if (length >= 148) {
                if (!read_bytes(s, address + 140, field, 8)) return false;
                if (u64(field)) dsdt = u64(field);
            }
            if (dsdt && (!acpi_header(s, dsdt, header) || !signature(header, "DSDT") ||
                         !add_acpi(s, dsdt, header))) return false;
        }
    }
    s->table->coverage |= HWENUM_COV_ACPI;
    return true;
}

struct hwenum_table *hwenum_enumerate(const struct kernel_surfaces *hub, void *heap,
                                     const struct hwenum_boot *boot) {
    if (!hub || hub->magic != KERNEL_SURFACES_MAGIC || hub->major != KERNEL_HUB_ABI ||
        !heap || !boot || !boot->memory || !boot->memory->entries) return NULL;
    const struct kernel_hub_table *existing = kernel_hub_find_table(hub, HWENUM_TABLE_NAME);
    if (existing) return (struct hwenum_table *)existing->address;
    struct scan s;
    zero(&s, sizeof(s)); s.heap = heap;
    s.alloc = kernel_surface_find_version(hub, "mem.calloc", 1, 0); s.resize = kernel_surface_find_version(hub, "mem.realloc", 1, 0);
    s.release = kernel_surface_find_version(hub, "mem.free", 1, 0); s.read = kernel_surface_find_version(hub, "hardware.read", 1, 0);
    s.publish = kernel_surface_find_version(hub, "hub.publish_table", 1, 0);
    if (!s.alloc || !s.resize || !s.release || !s.read || !s.publish) return NULL;
    size_t initial_bytes = sizeof(struct hwenum_table) + 16 * sizeof(struct hwenum_descriptor);
    struct kernel_surface_result allocation = CALL(s.alloc, heap, 1, initial_bytes);
    if (allocation.status || !allocation.value) return NULL;
    s.table = (struct hwenum_table *)allocation.value;
    s.table->magic = HWENUM_TABLE_MAGIC; s.table->major = 1; s.table->capacity = 16;
    s.table->header_bytes = sizeof(*s.table); s.table->entry_bytes = sizeof(struct hwenum_descriptor);
    s.table->heap = heap;
    for (uint64_t i = 0; i < boot->memory->count; ++i) {
        const struct boot_memory_entry *entry = boot->memory->entries[i];
        if (!entry || entry->base > UINT64_MAX - entry->length) goto fail;
        struct hwenum_descriptor *d = append(&s, HWENUM_MEMORY);
        if (!d) goto fail;
        d->address = entry->base; d->length = entry->length; d->data.memory.type = entry->type; d->flags = HWENUM_MEMORY_FIRMWARE_MAP;
    }
    s.table->coverage |= HWENUM_COV_MEMORY;
    if (!firmware(&s, boot->rsdp_physical)) goto fail;
    struct kernel_hub_table published;
    zero(&published, sizeof(published));
    const char *name = HWENUM_TABLE_NAME;
    for (size_t i = 0; name[i]; ++i) published.name[i] = name[i];
    published.owner=kernel_surface_owner(hub,"hwenum.enumerate");
    published.address = (uintptr_t)s.table;
    published.bytes = sizeof(*s.table) + s.table->count * sizeof(s.table->entries[0]);
    published.count = s.table->count; published.entry_bytes = sizeof(s.table->entries[0]);
    published.entries_offset = sizeof(*s.table);
    if (CALL(s.publish, &published, 0, 0).status) goto fail;
    return s.table;
fail:
    CALL(s.release, heap, s.table, 0);
    return NULL;
}
const struct hwenum_descriptor *hwenum_find(const struct hwenum_table *table, uint32_t kind, size_t ordinal) {
    if (!table || table->magic != HWENUM_TABLE_MAGIC || table->major != 1 ||
        table->entry_bytes != sizeof(struct hwenum_descriptor)) return NULL;
    for (size_t i = 0; i < table->count; ++i)
        if ((!kind || table->entries[i].kind == kind) && ordinal-- == 0) return &table->entries[i];
    return NULL;
}
