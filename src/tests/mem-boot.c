#include "../kernel/modules.h"
#include "../kmodules/shared/mem.h"
#include "../kmodules/shared/hwenum.h"
#include "../kmodules/shared/inthandle.h"
#include "../kmodules/shared/config.h"

extern bool kernel_scheduler_test(const struct kernel_surfaces *);

static bool test_failure(unsigned line) {
    const char *message="FAIL mem-boot.c line ";
    while (*message) __asm__ volatile("outb %0,$0xe9" :: "a"(*message++));
    char digits[10];unsigned n=0;
    do { digits[n++]=(char)('0'+line%10);line/=10; } while (line);
    while (n) __asm__ volatile("outb %0,$0xe9" :: "a"(digits[--n]));
    __asm__ volatile("outb %0,$0xe9" :: "a"((unsigned char)'\n'));
    return false;
}
static bool equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static kernel_surface_call find(const struct kernel_surfaces *page, const char *name) {
    const struct kernel_surface *entries = kernel_surface_entries(page);
    for (uint32_t i = 0; i < page->count; ++i)
        if (equal(entries[i].name, name)) return entries[i].call;
    return NULL;
}
static bool mapping_flags(struct boot_pages *pages, uintptr_t address, uint64_t expected) {
    uint64_t cr3, cr0;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    if (!(cr0 & (1u << 16))) return test_failure(__LINE__); /* Supervisor writes honor read-only pages. */
    uint64_t *table = boot_physical_pointer(pages, cr3 & UINT64_C(0x000ffffffffff000));
    for (unsigned shift = 39; shift > 12; shift -= 9) {
        uint64_t entry = table[(address >> shift) & 511];
        if (!(entry & 1) || (entry & 128)) return test_failure(__LINE__);
        table = boot_physical_pointer(pages, entry & UINT64_C(0x000ffffffffff000));
    }
    uint64_t entry = table[(address >> 12) & 511];
    return (entry & (UINT64_C(0x8000000000000007))) == expected;
}
#define CALL(method, a, b, c) method((uintptr_t)(a), (uintptr_t)(b), (uintptr_t)(c), 0, 0, 0)
bool kernel_mem_test(const struct kernel_surfaces *page, struct boot_pages *pages) {
    if (page->magic != KERNEL_SURFACES_MAGIC || page->major != 2 || page->minor != 0 ||
        page->page_bytes != KERNEL_HUB_BYTES || page->header_bytes != sizeof(*page) ||
        page->entry_bytes != sizeof(struct kernel_surface) || page->count < 18 ||
        page->physical_address % 4096 || page->mem_context_bytes != sizeof(struct mem_heap) ||
        page->mem_context_alignment < _Alignof(struct mem_heap)) return test_failure(__LINE__);
    if (!mapping_flags(pages, KERNEL_SURFACES_ADDRESS, UINT64_C(0x8000000000000001)) ||
        !mapping_flags(pages, KERNEL_MODULES_ADDRESS, 1) ||
        !mapping_flags(pages, KERNEL_MEDIATION_ADDRESS, 1)) return test_failure(__LINE__);
    for(size_t offset=0;offset<KERNEL_HUB_BYTES;offset+=BOOT_PAGE_SIZE)
        if(!mapping_flags(pages,KERNEL_SURFACES_ADDRESS+offset,UINT64_C(0x8000000000000001))) return test_failure(__LINE__);
    for(size_t offset=0;offset<KERNEL_MEDIATION_PAGES*BOOT_PAGE_SIZE;offset+=BOOT_PAGE_SIZE)
        if(!mapping_flags(pages,KERNEL_MEDIATION_ADDRESS+offset,1)) return test_failure(__LINE__);
    const struct kernel_surface *entries = kernel_surface_entries(page);
    for (size_t i = 0; i < page->count; ++i) {
        if (!entries[i].call || !entries[i].major) return test_failure(__LINE__);
    }
    kernel_surface_call init = find(page, "mem.init"), alloc = find(page, "mem.malloc"),
        release = find(page, "mem.free"), zero = find(page, "mem.calloc"),
        resize = find(page, "mem.realloc"), align = find(page, "mem.aligned_alloc"),
        usable = find(page, "mem.usable_size");
    if (!init || !alloc || !release || !zero || !resize || !align || !usable) return test_failure(__LINE__);
    uint64_t physical = boot_pages_take(pages, 1);
    if (!physical || physical == page->physical_address) return test_failure(__LINE__);
    struct mem_heap heap;
    struct kernel_surface_result r = CALL(init, &heap, boot_physical_pointer(pages, physical), 4096);
    if (r.status || r.value != 1) return test_failure(__LINE__);
    r = CALL(alloc, &heap, 64, 0);
    if (r.status || !r.value || r.value % MEM_ALIGNMENT) return test_failure(__LINE__);
    unsigned char *original = (void *)r.value;
    for (size_t i = 0; i < 64; ++i) original[i] = (unsigned char)(i + 1);
    r = CALL(zero, &heap, 4, 16);
    if (r.status || !r.value) return test_failure(__LINE__);
    unsigned char *zeroed = (void *)r.value;
    for (size_t i = 0; i < 64; ++i) if (zeroed[i]) return test_failure(__LINE__);
    r = CALL(resize, &heap, original, 512);
    if (r.status || !r.value) return test_failure(__LINE__);
    unsigned char *grown = (void *)r.value;
    for (size_t i = 0; i < 64; ++i) if (grown[i] != i + 1) return test_failure(__LINE__);
    r = CALL(usable, grown, 0, 0);
    if (r.status || r.value < 512) return test_failure(__LINE__);
    r = CALL(resize, &heap, grown, SIZE_MAX);
    if (r.status != KERNEL_SURFACE_FAILED || r.value) return test_failure(__LINE__);
    for (size_t i = 0; i < 64; ++i) if (grown[i] != i + 1) return test_failure(__LINE__);
    r = CALL(zero, &heap, SIZE_MAX, 2);
    if (r.status != KERNEL_SURFACE_FAILED || r.value) return test_failure(__LINE__);
    r = CALL(align, &heap, 256, 256);
    if (r.status || !r.value || r.value % 256) return test_failure(__LINE__);
    if (CALL(release, &heap, r.value, 0).status || CALL(release, &heap, grown, 0).status ||
        CALL(release, &heap, zeroed, 0).status) return test_failure(__LINE__);
    r = CALL(alloc, &heap, 4096 - 64, 0); /* Complete coalescing recovers whole arena. */
    if (r.status || !r.value) return test_failure(__LINE__);
    if (CALL(release, &heap, r.value, 0).status) return test_failure(__LINE__);
    kernel_surface_call config_info=find(page,"config.info"),batch=find(page,"hub.publish_tables");struct config_info configured;
    if(!config_info || !batch || CALL(config_info,&configured,0,0).status || !configured.ready || configured.count!=5)return test_failure(__LINE__);
    const struct kernel_hub_table *config_version=kernel_hub_find_table(page,"config.version"),*directory=kernel_hub_find_table(page,"config.drivers.directory");
    const struct kernel_hub_table *partition=kernel_hub_find_table(page,"config.drivers.partition_guid");
    if(!config_version || !directory || !partition)return test_failure(__LINE__);
    const struct config_value *v=(const void *)config_version->address,*dir=(const void *)directory->address;
    if(v->major!=1 || v->type!=CONFIG_UINT || v->bytes!=8 || *(const uint64_t *)v->data!=1 || dir->type!=CONFIG_STRING || !equal(dir->data,"/boot/system/drivers"))return test_failure(__LINE__);
    const struct config_value *identity=(const void *)partition->address;
    if(identity->major!=1 || identity->type!=CONFIG_GUID || identity->bytes!=sizeof(struct config_guid) || !identity->data)return test_failure(__LINE__);
    const unsigned char *guid=identity->data;unsigned nonzero=0;for(unsigned i=0;i<sizeof(struct config_guid);++i)nonzero|=guid[i];
    if(!nonzero)return test_failure(__LINE__);
    uint32_t expected_tables=11+configured.count;
    struct kernel_hub_table transaction[2]={*config_version,*directory};
    const char fresh[]="test.config.atomic";for(unsigned i=0;i<sizeof(fresh);++i)transaction[0].name[i]=fresh[i];
    if(CALL(batch,transaction,2,0).status!=KERNEL_SURFACE_FAILED || kernel_hub_find_table(page,fresh) || page->table_count!=expected_tables)return test_failure(__LINE__);
    transaction[1]=transaction[0];
    if(CALL(batch,transaction,2,0).status!=KERNEL_SURFACE_FAILED || kernel_hub_find_table(page,fresh) || page->table_count!=expected_tables)return test_failure(__LINE__);
    const char *config_pass="PASS config: Limine text handoff, typed owned descriptors and atomic publication\n";
    while(*config_pass)__asm__ volatile("outb %0,$0xe9"::"a"(*config_pass++));
    const struct kernel_hub_table *hardware = kernel_hub_find_table(page, HWENUM_TABLE_NAME);
    if (!hardware || page->table_count != expected_tables || hardware->entry_bytes != sizeof(struct hwenum_descriptor) ||
        hardware->entries_offset != sizeof(struct hwenum_table)) return test_failure(__LINE__);
    const struct hwenum_table *table = (const struct hwenum_table *)hardware->address;
    if (table->magic != HWENUM_TABLE_MAGIC || table->count != hardware->count || !table->count ||
        table->coverage != (HWENUM_COV_MEMORY | HWENUM_COV_ACPI | HWENUM_COV_CPU | HWENUM_COV_PCI)) return test_failure(__LINE__);
    kernel_surface_call find_hw = find(page, "hwenum.find"), publish = find(page, "hub.publish_table");
    if (!find_hw || !publish) return test_failure(__LINE__);
    bool nvme = false, sata = false, xhci = false, cpu = false, memory = false;
    for (size_t i = 0; i < table->count; ++i) {
        r = CALL(find_hw, table, 0, i);
        if (r.status || r.value != (uintptr_t)&table->entries[i]) return test_failure(__LINE__);
        const struct hwenum_descriptor *d = (const struct hwenum_descriptor *)r.value;
        if (d->kind == HWENUM_CPU) cpu = true;
        if (d->kind == HWENUM_MEMORY) memory = true;
        if (d->kind == HWENUM_PCI) {
            if (d->data.pci.class_code == 1 && d->data.pci.subclass == 8) nvme = true;
            if (d->data.pci.class_code == 1 && d->data.pci.subclass == 6) sata = true;
            if (d->data.pci.class_code == 12 && d->data.pci.subclass == 3 &&
                d->data.pci.programming_interface == 48) xhci = true;
        }
    }
    if (!nvme || !sata || !xhci || !cpu || !memory) return test_failure(__LINE__);
    if (CALL(publish, hardware, 0, 0).status != KERNEL_SURFACE_FAILED || page->table_count != expected_tables) return test_failure(__LINE__);
    struct kernel_hub_table invalid = *hardware;
    invalid.name[0] = 'x'; invalid.entry_bytes = 0;
    if (CALL(publish, &invalid, 0, 0).status != KERNEL_SURFACE_FAILED) return test_failure(__LINE__);
    invalid.entry_bytes = hardware->entry_bytes; invalid.count = UINT64_MAX;
    if (CALL(publish, &invalid, 0, 0).status != KERNEL_SURFACE_FAILED) return test_failure(__LINE__);
    invalid.count = hardware->count; invalid.address = UINTPTR_MAX - 1;
    if (CALL(publish, &invalid, 0, 0).status != KERNEL_SURFACE_FAILED || page->table_count != expected_tables) return test_failure(__LINE__);
    unsigned char invalid_image[sizeof(struct kmodule_image)+sizeof(struct kmodule_export)]={0};
    struct kmodule_image *bad=(void *)invalid_image;
    bad->magic=KMODULE_MAGIC; bad->format=999; bad->header_bytes=sizeof(*bad);
    size_t original_count=page->count, original_reservations=pages->reservations.count;
    if (kernel_module_load(pages,invalid_image,sizeof(invalid_image),"invalid") ||
        page->count!=original_count || pages->reservations.count!=original_reservations) return test_failure(__LINE__);
    /* Live reservation ledger covers every bootstrap-owned usable page. */
    const struct kernel_hub_table *ledger=kernel_hub_find_table(page,"memory.reservations");
    if (!ledger || ledger->address!=(uintptr_t)&pages->reservations) return test_failure(__LINE__);
    size_t reservations=0;
    for (const struct boot_reservation_chunk *chunk=pages->reservations.first;chunk;chunk=chunk->next)
        for (size_t i=0;i<chunk->count;++i) {
            const struct boot_reservation *r=&chunk->entries[i]; ++reservations;
            bool within=false;
            for (size_t j=0;j<pages->map->count;++j) {
                const struct boot_memory_entry *m=pages->map->entries[j];
                if (m->type==0 && r->base>=m->base && r->base+r->bytes<=m->base+m->length) within=true;
            }
            if (!within) return test_failure(__LINE__);
        }
    if (reservations!=pages->reservations.count) return test_failure(__LINE__);
    /* Growth through the mediated mem calls, beyond the old 64 KiB backing. */
    r=CALL(zero,&heap,1,131072);
    if (r.status || !r.value) return test_failure(__LINE__);
    unsigned char *large=(void *)r.value;
    for (size_t i=0;i<131072;++i) { if (large[i]) return test_failure(__LINE__); large[i]=0x5a; }
    r=CALL(resize,&heap,large,262144);
    if (r.status || !r.value) return test_failure(__LINE__);
    large=(void *)r.value;
    for (size_t i=0;i<131072;++i) if (large[i]!=0x5a) return test_failure(__LINE__);
    if (CALL(release,&heap,large,0).status) return test_failure(__LINE__);
    /* An extra module added only to the test image: generic/versioned exports. */
    kernel_surface_call arguments=find(page,"probe.arguments"), add=find(page,"probe.add");
    if (!arguments || !add || arguments(1,2,3,4,5,6).value!=183 || CALL(add,17,25,0).value!=42) return test_failure(__LINE__);
    bool version=false;
    for (size_t i=0;i<page->count;++i)
        if (equal(entries[i].name,"probe.arguments")) version=entries[i].major==2 && entries[i].minor==3;
    if (!version || kernel_surface_find_version(page,"probe.arguments",1,0) ||
        !kernel_surface_find_version(page,"probe.arguments",2,3) ||
        kernel_surface_find_version(page,"probe.arguments",2,4)) return test_failure(__LINE__);
    kernel_surface_call bind=find(page,"inthandle.bind"), check=find(page,"inthandle.check");
    if (!bind || !check || CALL(bind,14,"probe.interrupt",42).status!=KERNEL_SURFACE_FAILED ||
        CALL(bind,128,"probe.interrupt",42).status) return test_failure(__LINE__);
    uint64_t returned;
    __asm__ volatile("int $0x80" : "=a"(returned) : "a"(0) : "memory","cc");
    if (returned!=UINT64_C(0x123456789abcdef0) || CALL(check,128,0,0).value!=1 ||
        CALL(bind,128,0,0).status) return test_failure(__LINE__);
    extern bool kernel_mmio_test(const struct kernel_surfaces *);
    extern bool kernel_clock_test(const struct kernel_surfaces *);
    extern bool kernel_events_test(const struct kernel_surfaces *);
    extern bool kernel_pages_test(const struct kernel_surfaces *,struct boot_pages *);
    if (!kernel_pages_test(page,pages)) return test_failure(__LINE__);
    extern bool kernel_irq_test(const struct kernel_surfaces *);
    extern bool kernel_threads_test(const struct kernel_surfaces *);
    extern bool kernel_nvme_test(const struct kernel_surfaces *);
    extern bool kernel_xhci_test(const struct kernel_surfaces *);
    extern bool kernel_acpi_test(const struct kernel_surfaces *);
    extern bool kernel_usb_test(const struct kernel_surfaces *);
    return kernel_acpi_test(page) && kernel_xhci_test(page) && kernel_usb_test(page) && kernel_scheduler_test(page) && kernel_mmio_test(page) && kernel_clock_test(page) && kernel_events_test(page) && kernel_threads_test(page) && kernel_irq_test(page) && kernel_nvme_test(page);
}
