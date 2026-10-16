#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/mem.h"
#include "../kmodules/shared/hwenum.h"

static _Alignas(16) unsigned char arena[1024 * 1024];
static struct mem_heap heap;
static struct {
    struct kernel_surfaces hub;
    struct kernel_surface methods[5];
    struct kernel_hub_table tables[1];
} fixture;
static unsigned char firmware[2048], config[32][8][64];
static struct boot_memory_entry ranges[40];
static struct boot_memory_entry *range_pointers[40];
static struct boot_memory_map map;
static struct hwenum_boot boot;
static size_t reads, allocations, allocation_limit;
static bool fail_allocations, fail_publication;
static struct kernel_surface_result result(uintptr_t value, bool ok) {
    return (struct kernel_surface_result){value, ok ? KERNEL_SURFACE_OK : KERNEL_SURFACE_FAILED};
}
#define ARGS uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f
#define IGNORE() do { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; } while (0)
static struct kernel_surface_result alloc(ARGS) {
    IGNORE(); ++allocations;
    void *p = (fail_allocations || allocations > allocation_limit) ? NULL : mem_calloc((void *)a, b, c);
    return result((uintptr_t)p, p != NULL);
}
static struct kernel_surface_result resize(ARGS) {
    IGNORE(); ++allocations;
    void *p = (fail_allocations || allocations > allocation_limit) ? NULL : mem_realloc((void *)a, (void *)b, c);
    return result((uintptr_t)p, p != NULL);
}
static struct kernel_surface_result release(ARGS) {
    IGNORE(); mem_free((void *)a, (void *)b); return result(0, true);
}
static struct kernel_surface_result read_physical(ARGS) {
    IGNORE(); ++reads;
    if (a >= 0x1000 && a - 0x1000 <= sizeof(firmware) && c <= sizeof(firmware) - (a - 0x1000)) {
        memcpy((void *)b, firmware + a - 0x1000, c); return result(c, true);
    }
    const uintptr_t ecam = 0x100000 + (2 << 20);
    if (a >= ecam && a - ecam < (1 << 20) && c == 4) {
        uintptr_t offset = a - ecam;
        unsigned slot = (offset >> 15) & 31, function = (offset >> 12) & 7, reg = offset & 4095;
        if (reg > 60 || reg % 4) return result(0, false);
        memcpy((void *)b, config[slot][function] + reg, 4); return result(4, true);
    }
    return result(0, false);
}
static struct kernel_surface_result publish(ARGS) {
    IGNORE(); const struct kernel_hub_table *record = (const void *)a;
    if (fail_publication || fixture.hub.table_count) return result(0, false);
    fixture.tables[0] = *record; fixture.hub.table_count = 1;
    return result(record->address, true);
}
static void put16(unsigned char *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
static void put32(unsigned char *p, uint32_t n) { put16(p, n); put16(p + 2, n >> 16); }
static void put64(unsigned char *p, uint64_t n) { put32(p, n); put32(p + 4, n >> 32); }
static void fix_checksum(unsigned char *p, size_t bytes, size_t checksum_offset) {
    unsigned char sum = 0; p[checksum_offset] = 0;
    for (size_t i = 0; i < bytes; ++i) sum = (unsigned char)(sum + p[i]);
    p[checksum_offset] = (unsigned char)-sum;
}
static void table(unsigned offset, const char *signature, size_t bytes) {
    memcpy(firmware + offset, signature, 4); put32(firmware + offset + 4, bytes);
    firmware[offset + 8] = 1;
}
static void reset(void) {
    assert(mem_init(&heap, arena, sizeof arena));
    memset(&fixture, 0, sizeof fixture); memset(firmware, 0, sizeof firmware);
    memset(config, 255, sizeof config);
    fixture.hub.magic = KERNEL_SURFACES_MAGIC; fixture.hub.major = 2; fixture.hub.minor = 0;
    fixture.hub.header_bytes = offsetof(__typeof__(fixture), methods);
    fixture.hub.entry_bytes = sizeof(struct kernel_surface); fixture.hub.count = 5;
    fixture.hub.tables_offset = offsetof(__typeof__(fixture), tables);
    fixture.hub.table_entry_bytes = sizeof(struct kernel_hub_table); fixture.hub.table_capacity = 1;
    const char *names[] = {"mem.calloc", "mem.realloc", "mem.free", "hardware.read", "hub.publish_table"};
    kernel_surface_call calls[] = {alloc, resize, release, read_physical, publish};
    for (unsigned i = 0; i < 5; ++i) { strcpy(fixture.methods[i].name, names[i]); fixture.methods[i].call = calls[i]; fixture.methods[i].major=1; }
    for (unsigned i = 0; i < 40; ++i) {
        ranges[i] = (struct boot_memory_entry){0x100000 + (uint64_t)i * 4096, 4096, i % 8};
        range_pointers[i] = &ranges[i];
    }
    map = (struct boot_memory_map){0, 40, range_pointers};
    boot = (struct hwenum_boot){0x1000, &map};
    memcpy(firmware, "RSD PTR ", 8); firmware[15] = 2; put32(firmware + 20, 36); put64(firmware + 24, 0x1100);
    fix_checksum(firmware, 20, 8); fix_checksum(firmware, 36, 32);
    table(0x100, "XSDT", 36 + 4 * 8);
    put64(firmware + 0x100 + 36, 0x1200); put64(firmware + 0x100 + 44, 0x1300);
    put64(firmware + 0x100 + 52, 0x1400); put64(firmware + 0x100 + 60, 0x1500);
    fix_checksum(firmware + 0x100, 68, 9);
    table(0x200, "APIC", 44 + 8 + 16 + 12 + 12);
    unsigned char *m = firmware + 0x200;
    put32(m + 36, 0xfee00000);
    m[44] = 0; m[45] = 8; m[46] = 7; m[47] = 3; put32(m + 48, 1);
    m[52] = 9; m[53] = 16; put32(m + 56, 257); put32(m + 60, 3); put32(m + 64, 42);
    m[68] = 1; m[69] = 12; m[70] = 2; put32(m + 72, 0xfec00000); put32(m + 76, 32);
    m[80] = 5; m[81] = 12; put64(m + 84, UINT64_C(0x1fee00000));
    fix_checksum(m, 92, 9);
    table(0x300, "MCFG", 60); put64(firmware + 0x300 + 44, 0x100000);
    put16(firmware + 0x300 + 52, 9); firmware[0x300 + 54] = 2; firmware[0x300 + 55] = 2;
    fix_checksum(firmware + 0x300, 60, 9);
    table(0x400, "HPET", 56); put64(firmware + 0x400 + 44, 0xfed00000);
    fix_checksum(firmware + 0x400, 56, 9);
    table(0x500, "FACP", 148); put64(firmware + 0x500 + 140, 0x1600);
    fix_checksum(firmware + 0x500, 148, 9);
    table(0x600, "DSDT", 36); fix_checksum(firmware + 0x600, 36, 9);
    for (unsigned i = 3; i <= 5; ++i) {
        memset(config[i][0], 0, 64); put16(config[i][0], 0x1234); put16(config[i][0] + 2, i);
        config[i][0][11] = i == 5 ? 12 : 1;
        config[i][0][10] = i == 5 ? 3 : i == 4 ? 6 : 8;
        config[i][0][9] = i == 5 ? 48 : i == 4 ? 1 : 2;
    }
    config[3][0][14] = 128;
    memcpy(config[3][2], config[3][0], 64); config[3][2][14] = 0;
    put32(config[3][2] + 16, 0xf0000004);
    reads = allocations = 0; allocation_limit = SIZE_MAX; fail_allocations = fail_publication = false;
}
static void recovered(void) {
    assert(!fixture.hub.table_count);
    void *p = mem_malloc(&heap, sizeof arena - 64);
    assert(p); mem_free(&heap, p);
}
int main(void) {
    reset();
    struct hwenum_table *t = hwenum_enumerate(&fixture.hub, &heap, &boot);
    assert(t && t->magic == HWENUM_TABLE_MAGIC && t->coverage == 15 && t->count > 40);
    assert(t->count <= t->capacity && allocations >= 3);
    const struct kernel_hub_table *published = kernel_hub_find_table(&fixture.hub, HWENUM_TABLE_NAME);
    assert(published && published->address == (uintptr_t)t && published->count == t->count);
    assert(published->bytes == sizeof(*t) + t->count * sizeof(t->entries[0]));
    const struct hwenum_descriptor *d = hwenum_find(t, HWENUM_CPU, 1);
    assert(d && d->data.cpu.uid == 42 && d->data.cpu.apic_id == 257 && d->flags == 3);
    d = hwenum_find(t, HWENUM_LOCAL_APIC, 0); assert(d && d->address == UINT64_C(0x1fee00000));
    d = hwenum_find(t, HWENUM_IO_APIC, 0); assert(d && d->data.interrupt.gsi_base == 32);
    d = hwenum_find(t, HWENUM_PCI, 1);
    assert(d && d->data.pci.segment == 9 && d->data.pci.bus == 2 && d->data.pci.slot == 3 &&
        d->data.pci.function == 2 && d->data.pci.bars[0] == 0xf0000004);
    assert(!hwenum_find(t, 999, 0) && !hwenum_find(t, HWENUM_CPU, SIZE_MAX));
    size_t old_reads = reads, old_allocations = allocations;
    assert(hwenum_enumerate(&fixture.hub, &heap, &boot) == t);
    assert(reads == old_reads && allocations == old_allocations && fixture.hub.table_count == 1);
    /* Host fixture teardown only; published table has boot lifetime in production. */
    fixture.hub.table_count = 0; mem_free(&heap, t); recovered();

    reset(); firmware[8] ^= 1; assert(!hwenum_enumerate(&fixture.hub, &heap, &boot)); recovered();
    reset(); firmware[0x200 + 45] = 1; fix_checksum(firmware + 0x200, 92, 9);
    assert(!hwenum_enumerate(&fixture.hub, &heap, &boot)); recovered();
    reset(); firmware[0x300 + 54] = 3; fix_checksum(firmware + 0x300, 60, 9);
    assert(!hwenum_enumerate(&fixture.hub, &heap, &boot)); recovered();
    reset(); fail_allocations = true;
    assert(!hwenum_enumerate(&fixture.hub, &heap, &boot)); recovered();
    reset(); allocation_limit = 2;
    assert(!hwenum_enumerate(&fixture.hub, &heap, &boot)); recovered();
    reset(); put64(firmware + 0x100 + 36, 0xdead0000); fix_checksum(firmware + 0x100, 68, 9);
    assert(!hwenum_enumerate(&fixture.hub, &heap, &boot)); recovered();
    reset(); fail_publication = true;
    assert(!hwenum_enumerate(&fixture.hub, &heap, &boot)); recovered();
    puts("PASS: hwenum ACPI/PCI parsing, nonzero segment/bus, sparse multifunction, vector growth, publication and rollback");
    return 0;
}
