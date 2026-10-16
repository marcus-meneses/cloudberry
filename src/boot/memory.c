#include "memory.h"
#define ADDRESS_MASK UINT64_C(0x000ffffffffff000)
#define PRESENT UINT64_C(1)
#define WRITABLE UINT64_C(2)
#define LARGE UINT64_C(128)
#define NX (UINT64_C(1) << 63)

_Static_assert(sizeof(struct boot_reservation_chunk) <= BOOT_PAGE_SIZE, "reservation ledger page");

void *boot_physical_pointer(const struct boot_pages *pages, uint64_t physical) {
    return (void *)(uintptr_t)(pages->hhdm + physical);
}
static uint64_t find_gap(struct boot_pages *pages, uint64_t bytes) {
    for (uint64_t i = 0; i < pages->map->count; ++i) {
        const struct boot_memory_entry *entry = pages->map->entries[i];
        if (!entry || entry->type != 0 || entry->base > UINT64_MAX - entry->length) continue;
        uint64_t start = entry->base < BOOT_PAGE_SIZE ? BOOT_PAGE_SIZE : entry->base;
        if (start > UINT64_MAX - 4095) continue;
        start = (start + 4095) & ~UINT64_C(4095);
        uint64_t end = entry->base + entry->length;
        while (start <= end && bytes <= end - start && start <= UINT64_MAX - pages->hhdm &&
               bytes - 1 <= UINT64_MAX - pages->hhdm - start) {
            uint64_t next = start;
            for (const struct boot_reservation_chunk *chunk = pages->reservations.first; chunk; chunk = chunk->next)
                for (uint64_t j = 0; j < chunk->count; ++j) {
                    const struct boot_reservation *r = &chunk->entries[j];
                    if (start < r->base + r->bytes && r->base < start + bytes && r->base + r->bytes > next)
                        next = r->base + r->bytes;
                }
            if (next == start) return start;
            start = next;
        }
    }
    return 0;
}
static void clear_pages(struct boot_pages *pages, uint64_t physical, size_t bytes) {
    unsigned char *p = boot_physical_pointer(pages, physical);
    for (size_t i = 0; i < bytes; ++i) p[i] = 0;
}
static void record(struct boot_pages *pages, uint64_t physical, uint64_t bytes, uint32_t kind, uintptr_t owner) {
    pages->last->entries[pages->last->count++] = (struct boot_reservation){physical, bytes, kind, 0, owner};
    ++pages->reservations.count;
}
bool boot_pages_init(struct boot_pages *pages, const struct boot_memory_map *map, uint64_t hhdm) {
    if (!pages || !map || !map->entries || !map->count) return false;
    *pages = (struct boot_pages){ .map = map, .hhdm = hhdm,
        .reservations = { .magic = BOOT_RESERVATION_MAGIC, .version = 3,
                          .entry_bytes = sizeof(struct boot_reservation) } };
    uint64_t physical = find_gap(pages, BOOT_PAGE_SIZE);
    if (!physical) return false;
    clear_pages(pages, physical, BOOT_PAGE_SIZE);
    pages->last = pages->reservations.first = boot_physical_pointer(pages, physical);
    record(pages, physical, BOOT_PAGE_SIZE, BOOT_RES_LEDGER, 0);
    return true;
}
uint64_t boot_pages_claim(struct boot_pages *pages, size_t count, uint32_t kind, uintptr_t owner) {
    if (!pages || !pages->last || !count || count > SIZE_MAX / BOOT_PAGE_SIZE) return 0;
    /* Keep one slot for recording the next ledger page in its predecessor. */
    if (pages->last->count >= BOOT_RESERVATIONS_PER_CHUNK - 1) {
        uint64_t metadata = find_gap(pages, BOOT_PAGE_SIZE);
        if (!metadata) return 0;
        clear_pages(pages, metadata, BOOT_PAGE_SIZE);
        record(pages, metadata, BOOT_PAGE_SIZE, BOOT_RES_LEDGER, 0);
        pages->last->next = boot_physical_pointer(pages, metadata);
        pages->last = pages->last->next;
    }
    uint64_t bytes = count * BOOT_PAGE_SIZE, physical = find_gap(pages, bytes);
    if (!physical) return 0;
    record(pages, physical, bytes, kind, owner);
    return physical;
}
void boot_pages_zero(struct boot_pages *pages,uint64_t physical,size_t count) {clear_pages(pages,physical,count*BOOT_PAGE_SIZE);}
uint64_t boot_pages_reserve(struct boot_pages *pages,size_t count,uint32_t kind,uintptr_t owner) {
    uint64_t physical=boot_pages_claim(pages,count,kind,owner);
    if(physical)boot_pages_zero(pages,physical,count);
    return physical;
}
bool boot_pages_retype(struct boot_pages *pages,uint64_t physical,size_t count,uint32_t from,uint32_t to,uintptr_t owner) {
    if(!pages || !count || count>SIZE_MAX/BOOT_PAGE_SIZE)return false;
    for(struct boot_reservation_chunk *chunk=pages->reservations.first;chunk;chunk=chunk->next)
        for(size_t i=0;i<chunk->count;++i) {struct boot_reservation *r=&chunk->entries[i];
            if(r->base==physical && r->bytes==count*BOOT_PAGE_SIZE && r->kind==from && r->owner==owner && !r->mappings) {r->kind=to;return true;}
        }
    return false;
}
bool boot_pages_retire(struct boot_pages *pages,uint64_t physical,size_t count,uint32_t kind,uintptr_t owner) {
    return (kind==BOOT_RES_PAGES || kind==BOOT_RES_STACK) && boot_pages_retype(pages,physical,count,kind,BOOT_RES_SCRUB,owner);
}
bool boot_pages_finish(struct boot_pages *pages,uint64_t physical,size_t count,uintptr_t owner) {
    if(!pages || !count || count>SIZE_MAX/BOOT_PAGE_SIZE)return false;
    for(struct boot_reservation_chunk *chunk=pages->reservations.first;chunk;chunk=chunk->next)
        for(size_t i=0;i<chunk->count;++i) {struct boot_reservation *r=&chunk->entries[i];
            if(r->base==physical && r->bytes==count*BOOT_PAGE_SIZE && r->kind==BOOT_RES_SCRUB && r->owner==owner && !r->mappings) {
                chunk->entries[i]=chunk->entries[--chunk->count];--pages->reservations.count;return true;
            }
        }
    return false;
}
uint64_t boot_pages_take(struct boot_pages *pages, size_t count) {
    return boot_pages_reserve(pages, count, BOOT_RES_OTHER, 0);
}
static bool map_page(struct boot_pages *pages, uintptr_t address, uint64_t physical,
                     bool writable, bool executable, bool uncached) {
    uint64_t cr3, cr4, efer_low, efer_high;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile ("rdmsr" : "=a"(efer_low), "=d"(efer_high) : "c"(0xc0000080u));
    (void)efer_high;
    /* Bootstrap mapper deliberately supports four-level paging with NX enabled. */
    if ((cr4 & (1u << 12)) || !(efer_low & (1u << 11)) ||
        (address & (BOOT_PAGE_SIZE - 1)) || !physical || (physical & ~ADDRESS_MASK) ||
        (writable && executable) ||
        ((address >> 47) != 0 && (address >> 47) != UINT64_C(0x1ffff))) return false;
    uint64_t *table = boot_physical_pointer(pages, cr3 & ADDRESS_MASK);
    for (unsigned shift = 39; shift > 12; shift -= 9) {
        uint64_t *entry = &table[(address >> shift) & 511];
        if (!(*entry & PRESENT)) {
            uint64_t child = boot_pages_reserve(pages, 1, BOOT_RES_PAGE_TABLE, address);
            if (!child) return false;
            *entry = child | PRESENT | WRITABLE;
        } else if ((*entry & LARGE) || (executable && (*entry & NX)) ||
                   (writable && !(*entry & WRITABLE))) return false;
        table = boot_physical_pointer(pages, *entry & ADDRESS_MASK);
    }
    uint64_t *entry = &table[(address >> 12) & 511];
    if (*entry & PRESENT) {
        /* Reuse the one UC window; MMIO may upgrade RO to RW, never downgrade.
         * Accessed/dirty bits are hardware-maintained, not mapping attributes. */
        uint64_t expected=physical | PRESENT | NX | 24;
        if (!uncached || (*entry & ~(UINT64_C(32|64)|WRITABLE))!=expected) return false;
        if (writable && !(*entry & WRITABLE)) {
            *entry |= WRITABLE;
            __asm__ volatile("invlpg (%0)" : : "r"(address) : "memory");
        }
        return true;
    }
    *entry = physical | PRESENT | (writable ? WRITABLE : 0) | (executable ? 0 : NX) | (uncached ? 24 : 0);
    __asm__ volatile ("invlpg (%0)" : : "r"(address) : "memory");
    return true;
}

bool boot_map_page(struct boot_pages *pages, uintptr_t address, uint64_t physical,
                   bool writable, bool executable) {
    return map_page(pages, address, physical, writable, executable, false);
}

bool boot_physical_read(struct boot_pages *pages, uint64_t physical, void *destination, size_t bytes) {
    /* A separate 32 TiB firmware/MMIO mapping window, outside Limine's HHDM. */
    const uint64_t base = UINT64_C(0xffffc00000000000), limit = UINT64_C(1) << 45;
    if (!bytes) return true;
    if (!destination || physical >= limit || bytes > limit - physical) return false;
    unsigned char *out = destination;
    while (bytes) {
        uint64_t aligned = physical & ~(uint64_t)(BOOT_PAGE_SIZE - 1);
        size_t offset = physical - aligned, amount = BOOT_PAGE_SIZE - offset;
        if (amount > bytes) amount = bytes;
        if (!map_page(pages, base + aligned, aligned, false, false, true)) return false;
        const volatile unsigned char *in = (const volatile unsigned char *)(uintptr_t)(base + physical);
        if (amount == 4 && !(physical & 3)) {
            uint32_t value = *(const volatile uint32_t *)in;
            for (unsigned i = 0; i < 4; ++i) out[i] = (unsigned char)(value >> (i * 8));
        } else for (size_t i = 0; i < amount; ++i) out[i] = in[i];
        physical += amount; out += amount; bytes -= amount;
    }
    return true;
}

void *boot_mmio_map(struct boot_pages *pages,uint64_t physical,size_t bytes) {
    const uint64_t base=UINT64_C(0xffffc00000000000),limit=UINT64_C(1)<<45;
    if (!pages || !pages->map || !physical || !bytes || physical>=limit || bytes>limit-physical) return NULL;
    uint64_t start=physical & ~UINT64_C(4095),end=(physical+bytes+4095) & ~UINT64_C(4095);
    /* PCD/PWT selects PAT entry 3. Refuse a non-UC boot configuration. */
    uint32_t pat_low,pat_high;
    __asm__ volatile("rdmsr" : "=a"(pat_low),"=d"(pat_high) : "c"(0x277));
    (void)pat_high;
    if ((pat_low>>24)!=0) return NULL;
    /* Never turn RAM, ACPI, loader/kernel storage or the framebuffer into UC
     * device pages. Reserved ranges and gaps are permitted for hardware MMIO. */
    for (size_t i=0;i<pages->map->count;++i) {
        const struct boot_memory_entry *m=pages->map->entries[i];
        if (!m || m->length>UINT64_MAX-m->base) return NULL;
        if (m->type!=1 && start<m->base+m->length && m->base<end) return NULL;
    }
    for (uint64_t page=start;page<end;page+=BOOT_PAGE_SIZE)
        if (!map_page(pages,base+page,page,true,false,true)) return NULL;
    return (void *)(uintptr_t)(base+physical);
}

static struct boot_reservation *owned_page(struct boot_pages *pages,uint64_t physical,uintptr_t owner) {
    if (!pages || !physical || (physical&4095)) return NULL;
    for (struct boot_reservation_chunk *c=pages->reservations.first;c;c=c->next)
        for (size_t i=0;i<c->count;++i) {
            struct boot_reservation *r=&c->entries[i];
            if ((r->kind==BOOT_RES_PAGES || r->kind==BOOT_RES_STACK) && r->owner==owner &&
                physical>=r->base && physical-r->base<r->bytes) return r;
        }
    return NULL;
}
bool boot_pages_release(struct boot_pages *pages,uint64_t physical,size_t count,uint32_t kind,uintptr_t owner) {
    if(!boot_pages_retire(pages,physical,count,kind,owner))return false;
    boot_pages_zero(pages,physical,count);return boot_pages_finish(pages,physical,count,owner);
}
static uint64_t *owned_leaf(struct boot_pages *pages,uintptr_t address) {
    if ((address&4095) || ((address>>47)!=0 && (address>>47)!=UINT64_C(0x1ffff))) return NULL;
    uint64_t cr3;__asm__ volatile("mov %%cr3,%0" : "=r"(cr3));
    uint64_t *table=boot_physical_pointer(pages,cr3&ADDRESS_MASK);
    for (unsigned shift=39;shift>12;shift-=9) {
        uint64_t entry=table[(address>>shift)&511];
        if (!(entry&PRESENT) || (entry&LARGE)) return NULL;
        table=boot_physical_pointer(pages,entry&ADDRESS_MASK);
    }
    return &table[(address>>12)&511];
}
bool boot_owned_map(struct boot_pages *pages,uintptr_t address,uint64_t physical,uintptr_t owner) {
    struct boot_reservation *r=owned_page(pages,physical,owner);
    if (!r || r->mappings==UINT32_MAX || !map_page(pages,address,physical,true,false,false)) return false;
    ++r->mappings;return true;
}
bool boot_owned_unmap(struct boot_pages *pages,uintptr_t address,uint64_t physical,uintptr_t owner) {
    struct boot_reservation *r=owned_page(pages,physical,owner);
    if (!r || !r->mappings) return false;
    uint64_t *leaf=owned_leaf(pages,address);
    if (!leaf || (*leaf&~UINT64_C(96))!=(physical|PRESENT|WRITABLE|NX)) return false;
    *leaf=0;__asm__ volatile("invlpg (%0)" :: "r"(address) : "memory");--r->mappings;return true;
}

/* Validate the whole bounded batch before clearing any leaf. The caller owns
 * the page-table lock and performs one acknowledged SMP shootdown afterwards. */
bool boot_owned_unmap_batch(struct boot_pages *pages,const struct boot_owned_mapping *items,size_t count) {
    if(!items || !count || count>BOOT_UNMAP_BATCH)return false;
    for(size_t i=0;i<count;++i) {
        const struct boot_owned_mapping *m=&items[i];
        struct boot_reservation *r=owned_page(pages,m->physical,m->owner);
        uint64_t *leaf=owned_leaf(pages,m->address);
        if(!r || !r->mappings || !leaf || (*leaf&~UINT64_C(96))!=(m->physical|PRESENT|WRITABLE|NX))return false;
        size_t uses=0;
        for(size_t j=0;j<count;++j) {
            if(j!=i && items[j].address==m->address)return false;
            if(owned_page(pages,items[j].physical,items[j].owner)==r)++uses;
        }
        if(uses>r->mappings)return false;
    }
    for(size_t i=0;i<count;++i)if(!boot_owned_unmap(pages,items[i].address,items[i].physical,items[i].owner))return false;
    return true;
}

bool boot_pages_pin(struct boot_pages *pages,uint64_t physical,size_t count,uintptr_t owner,int add) {
    if(!pages || !count || count>SIZE_MAX/4096 || (add!=0 && add!=1))return false;
    for(struct boot_reservation_chunk *chunk=pages->reservations.first;chunk;chunk=chunk->next)
        for(size_t i=0;i<chunk->count;++i) {struct boot_reservation *r=&chunk->entries[i];
            if(r->base!=physical || r->bytes!=count*4096 || r->owner!=owner || r->kind!=BOOT_RES_PAGES)continue;
            if(add && r->mappings<UINT32_MAX) {++r->mappings;return true;}
            if(!add && r->mappings) {--r->mappings;return true;}
            return false;
        }
    return false;
}
