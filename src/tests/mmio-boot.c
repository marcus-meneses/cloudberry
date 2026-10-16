#include "../kmodules/shared/mmio.h"
#include "../kmodules/shared/hwenum.h"
static bool test_failure(unsigned line) {
    const char *message="FAIL mmio-boot.c line ";
    while (*message) __asm__ volatile("outb %0,$0xe9" :: "a"(*message++));
    char digits[10];unsigned n=0;
    do { digits[n++]=(char)('0'+line%10);line/=10; } while (line);
    while (n) __asm__ volatile("outb %0,$0xe9" :: "a"(digits[--n]));
    __asm__ volatile("outb %0,$0xe9" :: "a"((unsigned char)'\n'));
    return false;
}
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
bool kernel_mmio_test(const struct kernel_surfaces *hub) {
    kernel_surface_call map=kernel_surface_find_version(hub,"mmio.map",1,0),
        read=kernel_surface_find_version(hub,"mmio.read",1,0),
        write=kernel_surface_find_version(hub,"mmio.write",1,0),
        physical_read=kernel_surface_find_version(hub,"hardware.read",1,0),
        physical_map=kernel_surface_find_version(hub,"hardware.map_mmio",1,0);
    const struct kernel_hub_table *ref=kernel_hub_find_table(hub,MMIO_TABLE_NAME),
        *hardware=kernel_hub_find_table(hub,HWENUM_TABLE_NAME);
    if (!map || !read || !write || !physical_read || !physical_map || !ref || !hardware) return test_failure(__LINE__);
    const struct mmio_table *table=(const void *)ref->address;
    const struct hwenum_table *hw=(const void *)hardware->address;
    if (table->magic!=MMIO_TABLE_MAGIC || table->count!=ref->count || !table->count) return test_failure(__LINE__);
    for (size_t i=0;i<hw->count;++i) {
        const struct hwenum_descriptor *h=&hw->entries[i];
        if (h->kind==HWENUM_MEMORY && h->data.memory.type==0 && h->address && h->length>=4096 &&
            CALL(physical_map,h->address,4096,0).status!=KERNEL_SURFACE_FAILED) return test_failure(__LINE__);
    }
    for (size_t i=0;i<table->count;++i) if (table->entries[i].kind==MMIO_HPET) {
        const struct mmio_region *region=&table->entries[i];
        uint32_t before=0,after=0;
        /* Exercise RO -> RW mapping upgrade, and reuse after hardware dirties PTE. */
        if (CALL(physical_read,region->physical,&before,4).status || !before ||
            CALL(map,i,0,0).status) return test_failure(__LINE__);
        struct kernel_surface_result capabilities=CALL(read,i,0,8),configuration=CALL(read,i,16,8);
        if (capabilities.status || (uint32_t)capabilities.value!=before || !(capabilities.value>>32) || configuration.status) return test_failure(__LINE__);
        /* Preserve timer configuration: test writable mapping without arming an IRQ. */
        if (write(i,16,8,configuration.value,0,0).status || CALL(read,i,16,8).value!=configuration.value ||
            CALL(physical_read,region->physical,&after,4).status || after!=before ||
            CALL(read,i,1,8).status!=KERNEL_SURFACE_FAILED ||
            CALL(read,i,region->bytes,8).status!=KERNEL_SURFACE_FAILED) return test_failure(__LINE__);
        return true;
    }
    return test_failure(__LINE__);
}
