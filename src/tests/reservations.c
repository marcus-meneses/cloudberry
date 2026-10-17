#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../boot/memory.h"
static _Alignas(4096) unsigned char storage[4*1024*1024];
int main(void) {
    struct boot_memory_entry first={0x1000,5*4096,0}, second={0x100000,2*1024*1024,0};
    struct boot_memory_entry *entries[]={&first,&second};
    struct boot_memory_map map={0,2,entries};
    struct boot_pages pages;
    memset(storage,0xaa,sizeof(storage));
    assert(boot_pages_init(&pages,&map,(uintptr_t)storage-0x1000));
    assert(boot_pages_reserve(&pages,3,BOOT_RES_MODULE,123)==0x2000);
    assert(boot_pages_reserve(&pages,2,BOOT_RES_HEAP,456)==0x100000);
    assert(boot_pages_reserve(&pages,1,BOOT_RES_OTHER,0)==0x102000);
    /* The continuation is only a search hint: exhaustion above it must still
     * discover smaller holes in an earlier memory-map entry. */
    pages.search_hint=0x300000;
    assert(boot_pages_reserve(&pages,1,BOOT_RES_OTHER,0)==0x5000);
    size_t before=pages.reservations.count;
    assert(!boot_pages_reserve(&pages,SIZE_MAX,BOOT_RES_OTHER,0));
    assert(!boot_pages_reserve(&pages,1000000,BOOT_RES_OTHER,0));
    assert(pages.reservations.count==before);
    for (unsigned i=0;i<300;++i) assert(boot_pages_reserve(&pages,1,BOOT_RES_HEAP,i));
    size_t count=0,chunks=0;
    for (const struct boot_reservation_chunk *c=pages.reservations.first;c;c=c->next) {
        ++chunks;
        assert(c->count<=BOOT_RESERVATIONS_PER_CHUNK);
        for (size_t i=0;i<c->count;++i) {
            const struct boot_reservation *r=&c->entries[i]; ++count;
            assert(r->base%4096==0 && r->bytes%4096==0 && r->bytes);
            for (const struct boot_reservation_chunk *other=pages.reservations.first;other;other=other->next)
                for (size_t j=0;j<other->count;++j) {
                    const struct boot_reservation *s=&other->entries[j];
                    assert(r==s || r->base+r->bytes<=s->base || s->base+s->bytes<=r->base);
                }
        }
    }
    assert(count==pages.reservations.count && chunks>=3);
    assert(first.base==0x1000 && first.length==5*4096 && second.length==2*1024*1024);
    unsigned char *zeroed=boot_physical_pointer(&pages,0x100000);
    for (unsigned i=0;i<8192;++i) assert(!zeroed[i]);
    uint64_t managed=boot_pages_reserve(&pages,2,BOOT_RES_PAGES,42);assert(managed);
    memset(boot_physical_pointer(&pages,managed),0xab,8192);
    assert(!boot_pages_release(&pages,managed,1,BOOT_RES_PAGES,42));
    assert(!boot_pages_release(&pages,managed,2,BOOT_RES_PAGES,43));
    assert(!boot_pages_release(&pages,0x100000,2,BOOT_RES_HEAP,456));
    assert(boot_pages_release(&pages,managed,2,BOOT_RES_PAGES,42));
    for (unsigned i=0;i<8192;++i) assert(!((unsigned char *)boot_physical_pointer(&pages,managed))[i]);
    assert(!boot_pages_release(&pages,managed,2,BOOT_RES_PAGES,42));
    assert(boot_pages_reserve(&pages,2,BOOT_RES_PAGES,43)==managed);
    assert(boot_pages_pin(&pages,managed,2,43,1));
    assert(!boot_pages_release(&pages,managed,2,BOOT_RES_PAGES,43));
    assert(!boot_pages_pin(&pages,managed,2,44,0) && boot_pages_pin(&pages,managed,2,43,0));
    assert(boot_pages_retire(&pages,managed,2,BOOT_RES_PAGES,43));
    assert(!boot_pages_retire(&pages,managed,2,BOOT_RES_PAGES,43) && !boot_pages_pin(&pages,managed,2,43,1));
    assert(boot_pages_reserve(&pages,2,BOOT_RES_PAGES,44)!=managed);
    boot_pages_zero(&pages,managed,2);assert(boot_pages_finish(&pages,managed,2,43));
    assert(boot_pages_reserve(&pages,2,BOOT_RES_PAGES,45)==managed);
    puts("PASS: explicit reservations, metadata growth, gap reuse, failed-allocation preservation and non-overlap");
    return 0;
}
