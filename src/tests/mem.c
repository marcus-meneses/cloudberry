#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/mem.h"

static _Alignas(4096) unsigned char arena[1024 * 1024];
static struct mem_heap heap;
static unsigned random_state = 0x41c0ffee;
static unsigned next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static void check_bytes(const unsigned char *p, size_t n, unsigned char value) {
    for (size_t i = 0; i < n; ++i) assert(p[i] == value);
}
int main(void) {
    assert(!mem_init(NULL, arena, sizeof arena));
    assert(!mem_init(&heap, NULL, sizeof arena));
    assert(!mem_malloc(&heap, 10));
    assert(!mem_init(&heap, arena, 50));
    assert(!mem_init(&heap, (void *)(UINTPTR_MAX - 31), 64));
    assert(mem_init(&heap, arena + 1, sizeof arena - 2));
    void *p = mem_malloc(&heap, 1);
    assert(p && (uintptr_t)p % MEM_ALIGNMENT == 0);
    mem_free(&heap, p);
    assert(mem_init(&heap, arena, 4096));
    unsigned char *region_a = mem_malloc(&heap,4032);
    assert(region_a); memset(region_a,0x42,4032);
    assert(!mem_add_region(&heap,arena,4096));
    assert(mem_add_region(&heap,arena+4096,4096));
    void *region_b = mem_malloc(&heap,4032); assert(region_b);
    assert(!mem_malloc(&heap,1));
    mem_free(&heap,region_b);
    assert(mem_add_region(&heap,arena+16384,16384));
    unsigned char *region_c=mem_realloc(&heap,region_a,8000);
    assert(region_c); check_bytes(region_c,4032,0x42); mem_free(&heap,region_c);
    assert(mem_init(&heap, arena, sizeof arena));
    p = mem_malloc(&heap, 1);
    size_t maximum = mem_usable_size(p);
    mem_free(&heap, p);
    p = mem_malloc(&heap, sizeof arena - 64);
    assert(p && mem_usable_size(p) == sizeof arena - 64);
    maximum = mem_usable_size(p);
    assert(!mem_malloc(&heap, 1));
    mem_free(&heap, p);
    assert(!mem_malloc(&heap, 0));
    assert(!mem_malloc(&heap, SIZE_MAX));
    assert(!mem_calloc(&heap, SIZE_MAX, 2));
    assert(!mem_calloc(&heap, 0, 20));
    assert(!mem_aligned_alloc(&heap, 0, 32));
    assert(!mem_aligned_alloc(&heap, 24, 48));
    assert(!mem_aligned_alloc(&heap, 64, 65));
    assert(!mem_aligned_alloc(&heap, (size_t)1 << 63, (size_t)1 << 63));
    assert(mem_usable_size(NULL) == 0);
    mem_free(&heap, NULL);

    /* All free orders, splitting and both-direction coalescing. */
    for (unsigned order = 0; order < 6; ++order) {
        static const unsigned orders[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
        void *blocks[3];
        for (unsigned i = 0; i < 3; ++i) { blocks[i] = mem_malloc(&heap, 128); assert(blocks[i]); }
        for (unsigned i = 0; i < 3; ++i) mem_free(&heap, blocks[orders[order][i]]);
        p = mem_malloc(&heap, maximum); assert(p); mem_free(&heap, p);
    }
    for (size_t alignment = 1; alignment <= 65536; alignment *= 2) {
        p = mem_aligned_alloc(&heap, alignment, alignment * 2);
        assert(p && (uintptr_t)p % alignment == 0 && (uintptr_t)p % MEM_ALIGNMENT == 0);
        memset(p, 0x77, alignment * 2);
        mem_free(&heap, p);
    }
    unsigned char *a = mem_malloc(&heap, 128), *b = mem_malloc(&heap, 512);
    assert(a && b); memset(a, 0x55, 128);
    mem_free(&heap, b);
    assert(mem_realloc(&heap, a, 400) == a); check_bytes(a, 128, 0x55);
    assert(mem_realloc(&heap, a, 32) == a); check_bytes(a, 32, 0x55);
    assert(!mem_realloc(&heap, a, SIZE_MAX)); check_bytes(a, 32, 0x55);
    assert(!mem_realloc(&heap, a, sizeof arena)); check_bytes(a, 32, 0x55);
    mem_free(&heap, a);
    a = mem_malloc(&heap, 128); b = mem_malloc(&heap, 128);
    assert(a && b); memset(a, 0x31, 128);
    unsigned char *moved = mem_realloc(&heap, a, 8192);
    assert(moved && moved != a); check_bytes(moved, 128, 0x31);
    mem_free(&heap, b); mem_free(&heap, moved);
    p = mem_realloc(&heap, NULL, 100); assert(p);
    assert(!mem_realloc(&heap, p, 0));
    p = mem_calloc(&heap, 100, 33); assert(p); check_bytes(p, 3300, 0); mem_free(&heap, p);

    /* Fragmentation/realloc stress: verify payloads and live region separation. */
    struct slot { unsigned char *p; size_t size; unsigned char value; } slots[128] = {0};
    for (unsigned step = 0; step < 20000; ++step) {
        unsigned index = next_random() % 128;
        struct slot *s = &slots[index];
        if (s->p) check_bytes(s->p, s->size, s->value);
        unsigned action = next_random() % 4;
        if (s->p && action == 0) {
            mem_free(&heap, s->p); s->p = NULL;
        } else {
            size_t size = next_random() % 12000 + 1;
            unsigned char *q;
            if (s->p) {
                q = mem_realloc(&heap, s->p, size);
                if (!q) { check_bytes(s->p, s->size, s->value); continue; }
                check_bytes(q, size < s->size ? size : s->size, s->value);
            } else if (action == 1) {
                q = mem_calloc(&heap, size, 1);
                if (q) check_bytes(q, size, 0);
            } else if (action == 2) {
                size_t alignment = (size_t)16 << (next_random() % 9);
                size = (size + alignment - 1) & ~(alignment - 1);
                q = mem_aligned_alloc(&heap, alignment, size);
                if (q) assert((uintptr_t)q % alignment == 0);
            } else q = mem_malloc(&heap, size);
            if (!q) continue;
            assert((uintptr_t)q % MEM_ALIGNMENT == 0 && mem_usable_size(q) >= size);
            for (unsigned j = 0; j < 128; ++j) if (j != index && slots[j].p) {
                uintptr_t start = (uintptr_t)q, other = (uintptr_t)slots[j].p;
                assert(start + size <= other || other + slots[j].size <= start);
            }
            s->p = q; s->size = size; s->value = (unsigned char)next_random();
            memset(s->p, s->value, s->size);
        }
    }
    for (unsigned i = 0; i < 128; ++i) if (slots[i].p) {
        check_bytes(slots[i].p, slots[i].size, slots[i].value);
        mem_free(&heap, slots[i].p);
    }
    p = mem_malloc(&heap, maximum); assert(p); mem_free(&heap, p);
    puts("PASS: mem alignment, overflow, exhaustion, coalescing, calloc and realloc; 20,000 mixed operations");
    return 0;
}
