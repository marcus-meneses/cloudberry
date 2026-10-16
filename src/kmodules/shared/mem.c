#include "mem.h"

/* Boundary tags allow constant-time neighbor discovery and coalescing.
 * Free-list links occupy a fixed header, so split blocks remain aligned. */
struct mem_block {
    size_t size_flags;
    size_t previous_size;
    struct mem_block *previous_free, *next_free;
};
#define HEADER_SIZE sizeof(struct mem_block)
#define MIN_BLOCK (HEADER_SIZE + MEM_ALIGNMENT)
#define FREE_FLAG ((size_t)1)
#define SIZE_MASK (~(size_t)(MEM_ALIGNMENT - 1))
_Static_assert(sizeof(size_t) == 8, "mem requires x86-64");
_Static_assert(HEADER_SIZE % MEM_ALIGNMENT == 0, "aligned block header");

static size_t block_size(const struct mem_block *block) {
    return block->size_flags & SIZE_MASK;
}
static bool is_free(const struct mem_block *block) {
    return (block->size_flags & FREE_FLAG) != 0;
}
static struct mem_block *next_block(struct mem_block *block) {
    return (struct mem_block *)((unsigned char *)block + block_size(block));
}
static unsigned bin_index(size_t size) {
    return 63u - (unsigned)__builtin_clzll(size);
}
static void insert_free(struct mem_heap *heap, struct mem_block *block) {
    unsigned index = bin_index(block_size(block));
    block->size_flags |= FREE_FLAG;
    block->previous_free = NULL;
    block->next_free = heap->bins[index];
    if (block->next_free) block->next_free->previous_free = block;
    heap->bins[index] = block;
    heap->nonempty |= UINT64_C(1) << index;
}
static void remove_free(struct mem_heap *heap, struct mem_block *block) {
    unsigned index = bin_index(block_size(block));
    if (block->previous_free) block->previous_free->next_free = block->next_free;
    else heap->bins[index] = block->next_free;
    if (block->next_free) block->next_free->previous_free = block->previous_free;
    if (!heap->bins[index]) heap->nonempty &= ~(UINT64_C(1) << index);
    block->size_flags &= ~FREE_FLAG;
}
static bool allocation_size(size_t bytes, size_t *result) {
    if (!bytes || bytes > SIZE_MAX - HEADER_SIZE - (MEM_ALIGNMENT - 1)) return false;
    *result = (bytes + HEADER_SIZE + MEM_ALIGNMENT - 1) & SIZE_MASK;
    return true;
}

/* Compiler-independent freestanding memory primitives, no libc dependency. */
static void zero_bytes(void *destination, size_t bytes) {
    __asm__ volatile ("cld; rep stosb" : "+D"(destination), "+c"(bytes) : "a"(0) : "memory", "cc");
}
static void copy_bytes(void *destination, const void *source, size_t bytes) {
    __asm__ volatile ("cld; rep movsb" : "+D"(destination), "+S"(source), "+c"(bytes) : : "memory", "cc");
}

bool mem_add_region(struct mem_heap *heap, void *storage, size_t bytes) {
    if (!heap) return false;
    uintptr_t address = (uintptr_t)storage;
    if (!storage || bytes > UINTPTR_MAX - address ||
        address > UINTPTR_MAX - (MEM_ALIGNMENT - 1)) return false;
    uintptr_t begin = (address + MEM_ALIGNMENT - 1) & SIZE_MASK;
    uintptr_t end = (address + bytes) & SIZE_MASK;
    if (end <= begin || end - begin < MIN_BLOCK + HEADER_SIZE) return false;
    for (const struct mem_block *r = heap->regions; r; r = r->next_free) {
        uintptr_t existing_begin = (uintptr_t)r->previous_free;
        uintptr_t existing_end = (uintptr_t)r + HEADER_SIZE;
        if (begin < existing_end && existing_begin < end) return false;
    }
    if (!heap->begin) { heap->begin = begin; heap->end = end; }
    struct mem_block *first = (struct mem_block *)begin;
    first->size_flags = end - begin - HEADER_SIZE;
    first->previous_size = 0;
    struct mem_block *sentinel = next_block(first);
    sentinel->size_flags = 0;
    sentinel->previous_size = block_size(first);
    sentinel->previous_free = first;
    sentinel->next_free = heap->regions;
    heap->regions = sentinel;
    insert_free(heap, first);
    return true;
}

bool mem_init(struct mem_heap *heap, void *storage, size_t bytes) {
    if (!heap) return false;
    zero_bytes(heap, sizeof(*heap));
    return mem_add_region(heap, storage, bytes);
}

/* Compute alignment padding without leaving an unusably small leading block. */
static bool fits(const struct mem_block *block, size_t need, size_t alignment,
                 size_t *padding) {
    uintptr_t payload = (uintptr_t)block + HEADER_SIZE;
    if (payload > UINTPTR_MAX - (alignment - 1)) return false;
    uintptr_t aligned = (payload + alignment - 1) & ~(uintptr_t)(alignment - 1);
    size_t lead = aligned - payload;
    if (lead && lead < MIN_BLOCK) {
        size_t extra = ((MIN_BLOCK - lead + alignment - 1) / alignment) * alignment;
        if (lead > SIZE_MAX - extra) return false;
        lead += extra;
    }
    size_t available = block_size(block);
    if (lead > available || need > available - lead) return false;
    *padding = lead;
    return true;
}

/* Split an allocated block; if its successor is free, merge it into the tail.
 * Used both by allocation and in-place realloc shrinking/growth. */
static void trim_block(struct mem_heap *heap, struct mem_block *block, size_t need) {
    size_t size = block_size(block);
    if (size - need < MIN_BLOCK) return;
    struct mem_block *after = next_block(block);
    size_t remainder = size - need;
    if (is_free(after)) {
        remainder += block_size(after);
        remove_free(heap, after);
    }
    block->size_flags = need;
    struct mem_block *tail = next_block(block);
    tail->size_flags = remainder;
    tail->previous_size = need;
    next_block(tail)->previous_size = remainder;
    insert_free(heap, tail);
}

static void *allocate(struct mem_heap *heap, size_t bytes, size_t alignment) {
    size_t need;
    if (!heap || !heap->begin || !allocation_size(bytes, &need)) return NULL;
    uint64_t candidates = heap->nonempty & (UINT64_MAX << bin_index(need));
    while (candidates) {
        unsigned index = (unsigned)__builtin_ctzll(candidates);
        for (struct mem_block *block = heap->bins[index]; block; block = block->next_free) {
            size_t padding;
            if (!fits(block, need, alignment, &padding)) continue;
            size_t original_size = block_size(block);
            remove_free(heap, block);
            if (padding) {
                struct mem_block *aligned = (struct mem_block *)((unsigned char *)block + padding);
                aligned->size_flags = original_size - padding;
                aligned->previous_size = padding;
                block->size_flags = padding;
                next_block(aligned)->previous_size = block_size(aligned);
                insert_free(heap, block);
                block = aligned;
            }
            trim_block(heap, block, need);
            return (unsigned char *)block + HEADER_SIZE;
        }
        candidates &= candidates - 1;
    }
    return NULL;
}

void *mem_malloc(struct mem_heap *heap, size_t bytes) {
    return allocate(heap, bytes, MEM_ALIGNMENT);
}

void mem_free(struct mem_heap *heap, void *pointer) {
    if (!pointer) return;
    struct mem_block *block = (struct mem_block *)((unsigned char *)pointer - HEADER_SIZE);
    size_t size = block_size(block);
    struct mem_block *after = next_block(block);
    if (is_free(after)) {
        size += block_size(after);
        remove_free(heap, after);
    }
    if (block->previous_size) {
        struct mem_block *before = (struct mem_block *)((unsigned char *)block - block->previous_size);
        if (is_free(before)) {
            size += block_size(before);
            remove_free(heap, before);
            block = before;
        }
    }
    block->size_flags = size;
    next_block(block)->previous_size = size;
    insert_free(heap, block);
}

void *mem_calloc(struct mem_heap *heap, size_t count, size_t bytes) {
    if (bytes && count > SIZE_MAX / bytes) return NULL;
    size_t total = count * bytes;
    void *pointer = mem_malloc(heap, total);
    if (pointer) zero_bytes(pointer, total);
    return pointer;
}

size_t mem_usable_size(const void *pointer) {
    if (!pointer) return 0;
    const struct mem_block *block = (const struct mem_block *)((const unsigned char *)pointer - HEADER_SIZE);
    return block_size(block) - HEADER_SIZE;
}

void *mem_resize_in_place(struct mem_heap *heap,void *pointer,size_t bytes) {
    if(!pointer || !bytes)return NULL;
    size_t need;
    if (!allocation_size(bytes, &need)) return NULL;
    struct mem_block *block = (struct mem_block *)((unsigned char *)pointer - HEADER_SIZE);
    size_t size = block_size(block);
    if (need <= size) {
        trim_block(heap, block, need);
        return pointer;
    }
    struct mem_block *after = next_block(block);
    if (is_free(after) && block_size(after) >= need - size) {
        size += block_size(after);
        remove_free(heap, after);
        block->size_flags = size;
        next_block(block)->previous_size = size;
        trim_block(heap, block, need);
        return pointer;
    }
    return NULL;
}
void *mem_realloc(struct mem_heap *heap, void *pointer, size_t bytes) {
    if (!pointer) return mem_malloc(heap, bytes);
    if (!bytes) { mem_free(heap, pointer); return NULL; }
    void *in_place=mem_resize_in_place(heap,pointer,bytes);if(in_place)return in_place;
    void *replacement = mem_malloc(heap, bytes);
    if (!replacement) return NULL;
    copy_bytes(replacement, pointer, mem_usable_size(pointer));
    mem_free(heap, pointer);
    return replacement;
}

void *mem_aligned_alloc(struct mem_heap *heap, size_t alignment, size_t bytes) {
    if (!alignment || (alignment & (alignment - 1)) || bytes % alignment) return NULL;
    if (alignment < MEM_ALIGNMENT) alignment = MEM_ALIGNMENT;
    return allocate(heap, bytes, alignment);
}
