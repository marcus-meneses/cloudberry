#ifndef CLOUDBERRY_KERNEL_SURFACES_H
#define CLOUDBERRY_KERNEL_SURFACES_H
#include <stddef.h>
#include <stdint.h>
#define KERNEL_SURFACES_ADDRESS UINT64_C(0xffffffffa1000000)
#define KERNEL_SURFACES_MAGIC UINT64_C(0x3153465255534243)
enum kernel_surface_status { KERNEL_SURFACE_OK, KERNEL_SURFACE_FAILED };
/* System V x86-64: six words in RDI/RSI/RDX/RCX/R8/R9;
 * value in RAX, status in RDX. Caller stack; trusted kernel pointers only. */
struct kernel_surface_result { uintptr_t value, status; };
typedef struct kernel_surface_result (*kernel_surface_call)(uintptr_t, uintptr_t,
    uintptr_t, uintptr_t, uintptr_t, uintptr_t);
struct kernel_surface {
    char name[32];
    kernel_surface_call call;
    uint16_t major, minor;
    uint32_t reserved;
};
struct kernel_surfaces {
    uint64_t magic;
    uint16_t major, minor;
    uint32_t page_bytes;
    uint32_t header_bytes, entry_bytes, count, reserved;
    uint64_t physical_address;
    uint64_t mem_context_bytes, mem_context_alignment;
    uint32_t tables_offset, table_entry_bytes, table_count, table_capacity;
};
struct kernel_hub_table {
    char name[32];
    uintptr_t address;
    uint64_t bytes, count;
    uint32_t entry_bytes, entries_offset;
};
static inline const struct kernel_surface *kernel_surface_entries(const struct kernel_surfaces *page) {
    return (const struct kernel_surface *)((const unsigned char *)page + page->header_bytes);
}
static inline const struct kernel_hub_table *kernel_hub_tables(const struct kernel_surfaces *hub) {
    return (const struct kernel_hub_table *)((const unsigned char *)hub + hub->tables_offset);
}
static inline int kernel_hub_name_equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static inline kernel_surface_call kernel_surface_find(const struct kernel_surfaces *hub, const char *name) {
    const struct kernel_surface *entries = kernel_surface_entries(hub);
    for (uint32_t i = 0; i < hub->count; ++i)
        if (kernel_hub_name_equal(entries[i].name, name)) return entries[i].call;
    return NULL;
}
static inline kernel_surface_call kernel_surface_find_version(const struct kernel_surfaces *hub,
                                                              const char *name, uint16_t major, uint16_t minor) {
    const struct kernel_surface *entries=kernel_surface_entries(hub);
    for (uint32_t i=0;i<hub->count;++i)
        if (kernel_hub_name_equal(entries[i].name,name) && entries[i].major==major && entries[i].minor>=minor)
            return entries[i].call;
    return NULL;
}
static inline const struct kernel_hub_table *kernel_hub_find_table(const struct kernel_surfaces *hub, const char *name) {
    const struct kernel_hub_table *tables = kernel_hub_tables(hub);
    for (uint32_t i = 0; i < hub->table_count; ++i)
        if (kernel_hub_name_equal(tables[i].name, name)) return &tables[i];
    return NULL;
}
#endif
