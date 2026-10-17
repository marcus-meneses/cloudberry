#ifndef CLOUDBERRY_BOOT_MEMORY_H
#define CLOUDBERRY_BOOT_MEMORY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BOOT_PAGE_SIZE 4096u
struct boot_memory_entry { uint64_t base, length, type; };
struct boot_memory_map { uint64_t revision, count; struct boot_memory_entry **entries; };
enum boot_reservation_kind { BOOT_RES_LEDGER = 1, BOOT_RES_MODULE, BOOT_RES_HUB,
    BOOT_RES_PAGE_TABLE, BOOT_RES_MEDIATION, BOOT_RES_HEAP, BOOT_RES_CONTEXT, BOOT_RES_OTHER, BOOT_RES_PAGES, BOOT_RES_STACK, BOOT_RES_SCRUB };
/* ABI3 mappings counts CPU mappings plus DMA pins; SCRUB keeps construction/
 * release ranges unavailable while bulk zeroing runs outside the ledger lock. */
struct boot_reservation { uint64_t base, bytes; uint32_t kind, mappings; uintptr_t owner; };
#define BOOT_RESERVATIONS_PER_CHUNK 126u
struct boot_reservation_chunk {
    struct boot_reservation_chunk *next;
    uint64_t count;
    struct boot_reservation entries[BOOT_RESERVATIONS_PER_CHUNK];
};
struct boot_reservation_table {
    uint64_t magic;
    uint32_t version, entry_bytes;
    uint64_t count;
    struct boot_reservation_chunk *first;
};
#define BOOT_RESERVATION_MAGIC UINT64_C(0x3156534552424343)
struct boot_pages {
    const struct boot_memory_map *map;
    uint64_t hhdm;
    struct boot_reservation_table reservations;
    struct boot_reservation_chunk *last;
};
bool boot_framebuffer_map(struct boot_pages *,uintptr_t,size_t);
bool boot_pages_init(struct boot_pages *, const struct boot_memory_map *, uint64_t hhdm);
uint64_t boot_pages_claim(struct boot_pages *,size_t,uint32_t,uintptr_t);
bool boot_pages_retype(struct boot_pages *,uint64_t,size_t,uint32_t,uint32_t,uintptr_t);
bool boot_pages_retire(struct boot_pages *,uint64_t,size_t,uint32_t,uintptr_t);
bool boot_pages_finish(struct boot_pages *,uint64_t,size_t,uintptr_t);
void boot_pages_zero(struct boot_pages *,uint64_t,size_t);
uint64_t boot_pages_reserve(struct boot_pages *, size_t count, uint32_t kind, uintptr_t owner);
/* Release only exact owned PAGES/STACK allocations, with no managed mappings. */
bool boot_pages_release(struct boot_pages *,uint64_t physical,size_t count,uint32_t kind,uintptr_t owner);
bool boot_pages_pin(struct boot_pages *,uint64_t,size_t,uintptr_t,int);
bool boot_owned_map(struct boot_pages *,uintptr_t address,uint64_t physical,uintptr_t owner);
#define BOOT_UNMAP_BATCH 16u
struct boot_owned_mapping {uintptr_t address;uint64_t physical;uintptr_t owner;};
bool boot_owned_unmap_batch(struct boot_pages *,const struct boot_owned_mapping *,size_t);
bool boot_owned_unmap(struct boot_pages *,uintptr_t address,uint64_t physical,uintptr_t owner);
uint64_t boot_pages_take(struct boot_pages *, size_t count);
void *boot_physical_pointer(const struct boot_pages *, uint64_t physical);
bool boot_map_page(struct boot_pages *, uintptr_t virtual_address, uint64_t physical,
                   bool writable, bool executable);
bool boot_physical_read(struct boot_pages *, uint64_t physical, void *destination, size_t bytes);
/* Boot-lifetime RW/UC/NX device mapping; rejects non-reserved memory ranges. */
void *boot_firmware_map(struct boot_pages *, uint64_t physical, size_t bytes);
void *boot_mmio_map(struct boot_pages *, uint64_t physical, size_t bytes);
#endif
