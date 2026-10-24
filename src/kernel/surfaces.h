#ifndef CLOUDBERRY_KERNEL_SURFACES_H
#define CLOUDBERRY_KERNEL_SURFACES_H
#include <stddef.h>
#include <stdint.h>
#define KERNEL_SURFACES_ADDRESS UINT64_C(0xffffffffa1000000)
#define KERNEL_HUB_ABI 3u
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
    uint64_t owner;
};
struct kernel_surfaces {
    uint64_t magic;
    uint16_t major, minor;
    uint32_t page_bytes;
    uint32_t header_bytes, entry_bytes, count, reserved;
    uint64_t physical_address;
    uint64_t mem_context_bytes, mem_context_alignment;
    uint32_t tables_offset, table_entry_bytes, table_count, table_capacity;
    uintptr_t methods_address,tables_address,owners_address;
    uint32_t method_capacity,owner_count,owner_capacity,owner_entry_bytes;
    uint64_t mapped_bytes,stub_bytes;
    uintptr_t method_hash_address; /* ABI 3.1: buckets followed by immutable nodes. */
};
enum kernel_hub_owner_state {HUB_OWNER_LIVE=1,HUB_OWNER_DRAINING,HUB_OWNER_RETIRED,HUB_OWNER_INITIALIZING,HUB_OWNER_FAILED,HUB_OWNER_STARTING};
enum kernel_hub_owner_flags {HUB_OWNER_BOOT_PINNED=1};
enum kernel_hub_owner_op {HUB_OWNER_DRAIN=1,HUB_OWNER_RESUME,HUB_OWNER_RETIRE};
struct kernel_hub_owner {
    char name[32];uint64_t id;uintptr_t image;uint64_t bytes;
    uint64_t active,pins,methods,tables;uint32_t state,flags;
};
struct kernel_hub_table {
    char name[32];
    uintptr_t address;
    uint64_t bytes, count;
    uint32_t entry_bytes, entries_offset;
    uint64_t owner; /* Explicit publisher; zero is the kernel. Boot-lifetime backing. */
};
static inline const struct kernel_surface *kernel_surface_entries(const struct kernel_surfaces *page) {
    return page->methods_address?(const void *)page->methods_address:(const void *)((const unsigned char *)page + page->header_bytes);
}
static inline const struct kernel_hub_table *kernel_hub_tables(const struct kernel_surfaces *hub) {
    return hub->tables_address?(const void *)hub->tables_address:(const void *)((const unsigned char *)hub + hub->tables_offset);
}
static inline uint32_t kernel_surface_count(const struct kernel_surfaces *hub){return __atomic_load_n(&hub->count,__ATOMIC_ACQUIRE);}
static inline uint32_t kernel_hub_table_count(const struct kernel_surfaces *hub){return __atomic_load_n(&hub->table_count,__ATOMIC_ACQUIRE);}
static inline const struct kernel_hub_owner *kernel_hub_owner(const struct kernel_surfaces *hub,uint64_t id){
    return hub->owners_address && id<__atomic_load_n(&hub->owner_count,__ATOMIC_ACQUIRE)?&((const struct kernel_hub_owner *)hub->owners_address)[id]:NULL;
}
static inline int kernel_hub_owner_live(const struct kernel_surfaces *hub,uint64_t id){
    const struct kernel_hub_owner *o=kernel_hub_owner(hub,id);
    return o?__atomic_load_n(&o->state,__ATOMIC_ACQUIRE)==HUB_OWNER_LIVE:!id;
}
static inline int kernel_hub_name_equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
/* Stable FNV-1a 64-bit keys. Full-key conflicts are rejected at publication;
 * bucket collisions are chained. No name comparison is needed for resolution. */
#define KERNEL_METHOD_HASH_BUCKETS 4096u
#define KERNEL_METHOD_HASH_DIRECTORY_BYTES (KERNEL_METHOD_HASH_BUCKETS * sizeof(uint32_t))
struct kernel_method_hash_node {uint64_t key;uint32_t next,reserved;};
static inline uint64_t kernel_surface_key(const char *name){
    uint64_t key=UINT64_C(14695981039346656037);
    if(name)while(*name){key^=(unsigned char)*name++;key*=UINT64_C(1099511628211);}
    return key;
}
/* Returns metadata even for a non-LIVE provider; public find helpers below
 * enforce visibility. One count snapshot hides an entire unpublished batch. */
static inline const struct kernel_surface *kernel_surface_by_key(const struct kernel_surfaces *hub,uint64_t key){
    const struct kernel_surface *entries=kernel_surface_entries(hub);
    uint32_t count=kernel_surface_count(hub);
    if(hub->method_hash_address){
        const uint32_t *buckets=(const void *)hub->method_hash_address;
        const struct kernel_method_hash_node *nodes=(const void *)(hub->method_hash_address+KERNEL_METHOD_HASH_DIRECTORY_BYTES);
        uint32_t at=__atomic_load_n(&buckets[key&(KERNEL_METHOD_HASH_BUCKETS-1)],__ATOMIC_ACQUIRE);
        while(at){uint32_t index=at-1;const struct kernel_method_hash_node *node=&nodes[index];
            if(index<count && node->key==key)return &entries[index];
            at=node->next;
        }
        return NULL;
    }
    /* Inline-array host fixtures without the ABI 3.1 index. Production always
     * provides the index, including during boot and runtime module loading. */
    for(uint32_t i=0;i<count;++i)if(kernel_surface_key(entries[i].name)==key)return &entries[i];
    return NULL;
}
static inline kernel_surface_call kernel_surface_find_key(const struct kernel_surfaces *hub,uint64_t key){
    const struct kernel_surface *entry=kernel_surface_by_key(hub,key);
    return entry && kernel_hub_owner_live(hub,entry->owner)?entry->call:NULL;
}
static inline kernel_surface_call kernel_surface_find_key_version(const struct kernel_surfaces *hub,uint64_t key,uint16_t major,uint16_t minor){
    const struct kernel_surface *entry=kernel_surface_by_key(hub,key);
    return entry && entry->major==major && entry->minor>=minor && kernel_hub_owner_live(hub,entry->owner)?entry->call:NULL;
}
static inline kernel_surface_call kernel_surface_find(const struct kernel_surfaces *hub,const char *name){
    return name?kernel_surface_find_key(hub,kernel_surface_key(name)):NULL;
}
static inline kernel_surface_call kernel_surface_find_version(const struct kernel_surfaces *hub,const char *name,uint16_t major,uint16_t minor){
    return name?kernel_surface_find_key_version(hub,kernel_surface_key(name),major,minor):NULL;
}
static inline const struct kernel_hub_table *kernel_hub_find_table(const struct kernel_surfaces *hub, const char *name) {
    const struct kernel_hub_table *tables = kernel_hub_tables(hub);
    for (uint32_t i = 0; i < kernel_hub_table_count(hub); ++i)
        if (kernel_hub_name_equal(tables[i].name, name)) return &tables[i];
    return NULL;
}
/* Provider identity is metadata, not an authorization boundary in trusted kernel
 * space. Published records/callable stubs remain address-stable until reboot. */
static inline uint64_t kernel_surface_owner(const struct kernel_surfaces *hub,const char *name){
    const struct kernel_surface *e=name?kernel_surface_by_key(hub,kernel_surface_key(name)):NULL;return e?e->owner:0;
}
static inline struct kernel_surface_result kernel_hub_pin_call(const struct kernel_surfaces *hub,kernel_surface_call entry,int acquire){
    const struct kernel_surface *e=kernel_surface_entries(hub);
    for(uint32_t i=0;i<kernel_surface_count(hub);++i)if(e[i].call==entry){
        if(!e[i].owner)return (struct kernel_surface_result){1,0};
        kernel_surface_call pin=kernel_surface_find(hub,"hub.owner_pin");
        return pin?pin(e[i].owner,acquire,0,0,0,0):(struct kernel_surface_result){0,1};
    }
    return (struct kernel_surface_result){0,1};
}
#endif
