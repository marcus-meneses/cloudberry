#include "modules.h"
#include "cpu.h"
#include "../boot/limine.h"
#include "../kmodules/shared/hwenum.h"
#include "../kmodules/shared/config.h"
#define HUB_BYTES KERNEL_HUB_BYTES
#define STUB_BYTES KERNEL_MEDIATION_STUB_BYTES
static struct kernel_surfaces *hub_writable;
static struct boot_pages *hub_pages;
struct method_metadata {kernel_surface_call implementation;uint64_t mode;};
struct hub_range {uintptr_t address;uint64_t mapped;uint32_t kind;int executable;uint64_t pending;unsigned phase;};
static struct hub_range method_range={.address=KERNEL_METHODS_ADDRESS,.kind=BOOT_RES_HUB},
    table_range={.address=KERNEL_TABLES_ADDRESS,.kind=BOOT_RES_HUB},
    owner_range={.address=KERNEL_OWNERS_ADDRESS,.kind=BOOT_RES_HUB},
    metadata_range={.address=KERNEL_METADATA_ADDRESS,.kind=BOOT_RES_HUB},
    stub_range={.address=KERNEL_MEDIATION_ADDRESS,.kind=BOOT_RES_MEDIATION,.executable=1};
static unsigned hub_writer;
static int writer_begin(void){return !__atomic_exchange_n(&hub_writer,1,__ATOMIC_ACQUIRE);}
static void writer_end(void){__atomic_store_n(&hub_writer,0,__ATOMIC_RELEASE);}
static void *writable(const struct hub_range *r){return (void *)(r->address+KERNEL_HUB_ALIAS_DELTA);}
static struct kernel_hub_owner *owner_write(uint64_t id){return id<__atomic_load_n(&hub_writable->owner_count,__ATOMIC_ACQUIRE)?&((struct kernel_hub_owner *)writable(&owner_range))[id]:NULL;}
/* The writer token serializes construction. IRQ-off ledger/map work stays one
 * page at a time; bulk zeroing and SMP invalidation run outside that lock. */
static int grow(struct hub_range *r,uint64_t bytes){
    if(bytes>KERNEL_HUB_WINDOW_BYTES)return 0;
    while(r->mapped<bytes){
        uintptr_t address=r->address+r->mapped;uint64_t flags;
        if(!r->pending){flags=kernel_cpu_enter();r->pending=boot_pages_claim(hub_pages,1,r->kind,address);kernel_cpu_leave(flags);if(!r->pending)return 0;boot_pages_zero(hub_pages,r->pending,1);}
        flags=kernel_cpu_enter();int ok=1;
        if(!r->phase){ok=boot_map_page(hub_pages,address,r->pending,false,r->executable);if(ok)r->phase=1;}
        if(ok)ok=boot_map_page(hub_pages,address+KERNEL_HUB_ALIAS_DELTA,r->pending,true,false);
        kernel_cpu_leave(flags);if(!ok)return 0;
        kernel_cpu_flush();r->mapped+=BOOT_PAGE_SIZE;r->pending=0;r->phase=0;
        __atomic_add_fetch(&hub_writable->mapped_bytes,BOOT_PAGE_SIZE,__ATOMIC_RELAXED);
    }
    return 1;
}
static int ensure_methods(uint64_t count){
    if(count>UINT32_MAX || !grow(&method_range,count*sizeof(struct kernel_surface)) || !grow(&metadata_range,count*sizeof(struct method_metadata)) || !grow(&stub_range,count*STUB_BYTES))return 0;
    uint64_t n=method_range.mapped/sizeof(struct kernel_surface),m=metadata_range.mapped/sizeof(struct method_metadata),t=stub_range.mapped/STUB_BYTES;
    if(n>m){n=m;}if(n>t){n=t;}
    __atomic_store_n(&hub_writable->method_capacity,n,__ATOMIC_RELEASE);
    __atomic_store_n(&hub_writable->stub_bytes,stub_range.mapped,__ATOMIC_RELEASE);return 1;
}
static int method_exists(const char *name){const struct kernel_surface *e=kernel_surface_entries(hub_writable);for(uint32_t i=0;i<kernel_surface_count(hub_writable);++i)if(kernel_hub_name_equal(e[i].name,name))return 1;return 0;}
static int owner_accepts(const struct kernel_hub_owner *o){
    uint32_t status=__atomic_load_n(&o->state,__ATOMIC_SEQ_CST);
    return status==HUB_OWNER_LIVE || (status==HUB_OWNER_DRAINING && (__atomic_load_n(&o->pins,__ATOMIC_SEQ_CST) || __atomic_load_n(&o->active,__ATOMIC_SEQ_CST)));
}
static uintptr_t next_module = KERNEL_MODULES_ADDRESS;
static struct hwenum_boot hardware_boot;
extern void kernel_module_gate(void);

struct kernel_surface_result kernel_module_invoke(MODULE_ARGS, uintptr_t index) {
    if (!hub_writable || index >= kernel_surface_count(hub_writable)) return module_result(0, 0);
    const struct kernel_surface *entry=&kernel_surface_entries(hub_writable)[index];
    struct kernel_hub_owner *owner=owner_write(entry->owner);if(!owner)return module_result(0,0);
    int tracked=!(owner->flags&HUB_OWNER_BOOT_PINNED);
    if(tracked){
        if(!owner_accepts(owner))return module_result(0,0);
        uint64_t previous=__atomic_load_n(&owner->active,__ATOMIC_SEQ_CST);
        do{if(previous==UINT64_MAX)return module_result(0,0);}while(!__atomic_compare_exchange_n(&owner->active,&previous,previous+1,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));
        uint32_t status=__atomic_load_n(&owner->state,__ATOMIC_SEQ_CST);
        if(status!=HUB_OWNER_LIVE && !(status==HUB_OWNER_DRAINING && (previous || __atomic_load_n(&owner->pins,__ATOMIC_SEQ_CST)))){__atomic_sub_fetch(&owner->active,1,__ATOMIC_SEQ_CST);return module_result(0,0);}
    }
    const struct method_metadata *meta=&((const struct method_metadata *)writable(&metadata_range))[index];
    unsigned mode=meta->mode;uint64_t flags=0,inner_flags=0;
    if(mode==1 || mode==3)flags=kernel_cpu_enter_tag(index+1);
    if(mode==2)flags=kernel_cpu_enter_domain(1,index+1);
    if(mode==3)inner_flags=kernel_cpu_enter_domain(1,index+1);
    struct kernel_surface_result r=meta->implementation(a,b,c,d,e,f);
    if(mode==3)kernel_cpu_leave_domain(1,inner_flags);
    if(mode==2)kernel_cpu_leave_domain(1,flags);
    if(mode==1 || mode==3)kernel_cpu_leave(flags);
    if(tracked)__atomic_sub_fetch(&owner->active,1,__ATOMIC_SEQ_CST);
    return r;
}
static int valid_name(const char name[32]) {
    size_t n = 0;
    while (n < 32 && name[n]) {
        char c = name[n++];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) return 0;
    }
    return n > 0 && n < 32;
}
static int prefix(const char *name,const char *start) {
    while(*start) {if(*name++!=*start++)return 0;}return 1;
}
static void write_surface(size_t index,const char *name,kernel_surface_call call,uint16_t major,uint16_t minor,int mediated,uint64_t owner) {
    struct method_metadata *meta=&((struct method_metadata *)writable(&metadata_range))[index];
    /* Short mutable mechanisms share one recursive CPU lock initially. Execution
     * engines and IRQ callbacks have separate synchronization and must stay out. */
    meta->mode=!mediated || (prefix(name,"mem.") && !kernel_hub_name_equal(name,"mem.stack_free") && !kernel_hub_name_equal(name,"mem.stack_alloc")) || (prefix(name,"clock.") && !kernel_hub_name_equal(name,"clock.sample")) ||
        prefix(name,"pci.") || prefix(name,"mmio.") || prefix(name,"hwenum.") || (prefix(name,"scheduler.") && !kernel_hub_name_equal(name,"scheduler.run")) ||
        (prefix(name,"inthandle.") && !kernel_hub_name_equal(name,"inthandle.dispatch") &&
         !kernel_hub_name_equal(name,"inthandle.cpu_init"));
    if(prefix(name,"clock.") && !kernel_hub_name_equal(name,"clock.sample")) {
        meta->mode=(kernel_hub_name_equal(name,"clock.init") || kernel_hub_name_equal(name,"clock.rtc") || kernel_hub_name_equal(name,"clock.smp_tsc") || kernel_hub_name_equal(name,"clock.refine"))?3:2;
    }
    const char *unlocked[]={"memory.reserve","memory.release","mem.malloc","mem.calloc","mem.realloc","mem.aligned_alloc","mem.free","mem.usable_size","mem.pages_alloc","mem.pages_free","pci.dma_alloc","pci.dma_free","pci.release","hub.publish_table","hub.publish_tables","hub.owner_info","hub.owner_pin","hub.owner_control"};
    for(size_t i=0;i<sizeof(unlocked)/sizeof(unlocked[0]);++i)if(kernel_hub_name_equal(name,unlocked[i]))meta->mode=0;
    if(kernel_hub_name_equal(name,"cpu.control")) meta->mode=0;
    {
        meta->implementation = call;
        unsigned char *stub = (unsigned char *)writable(&stub_range) + index * STUB_BYTES;
        /* mov r10d,index; movabs rax,kernel_module_gate; jmp rax */
        stub[0] = 0x41; stub[1] = 0xba;
        for (unsigned i = 0; i < 4; ++i) stub[2+i] = (unsigned char)(index >> (i*8));
        stub[6] = 0x48; stub[7] = 0xb8;
        uintptr_t target = (uintptr_t)kernel_module_gate;
        for (unsigned i = 0; i < 8; ++i) stub[8+i] = (unsigned char)(target >> (i*8));
        stub[16] = 0xff; stub[17] = 0xe0;
        call = (kernel_surface_call)(uintptr_t)(KERNEL_MEDIATION_ADDRESS + index * STUB_BYTES);
    }
    struct kernel_surface *entry=&((struct kernel_surface *)writable(&method_range))[index];
    *entry=(struct kernel_surface){.call=call,.major=major,.minor=minor,.owner=owner};
    for(size_t i=0;i<31 && name[i];++i)entry->name[i]=name[i];
}
static int add_surface(const char *name,kernel_surface_call call,uint16_t major,uint16_t minor,int mediated){
    if(!valid_name(name) || !call || !major || method_exists(name) || !ensure_methods((uint64_t)hub_writable->count+1))return 0;
    uint32_t index=hub_writable->count;write_surface(index,name,call,major,minor,mediated,0);
    __atomic_add_fetch(&owner_write(0)->methods,1,__ATOMIC_RELAXED);
    __atomic_store_n(&hub_writable->count,index+1,__ATOMIC_RELEASE);return 1;
}
static struct kernel_surface_result owner_info(MODULE_ARGS){
    MODULE_UNUSED();struct kernel_hub_owner *o=hub_writable?owner_write(a):NULL;if(!o || !b)return module_result(0,0);
    struct kernel_hub_owner *out=(void *)b;*out=(struct kernel_hub_owner){.id=o->id,.image=o->image,.bytes=o->bytes,.flags=o->flags};
    for(unsigned i=0;i<32;++i)out->name[i]=o->name[i];
    out->active=__atomic_load_n(&o->active,__ATOMIC_SEQ_CST);out->pins=__atomic_load_n(&o->pins,__ATOMIC_SEQ_CST);
    out->methods=__atomic_load_n(&o->methods,__ATOMIC_ACQUIRE);out->tables=__atomic_load_n(&o->tables,__ATOMIC_ACQUIRE);out->state=__atomic_load_n(&o->state,__ATOMIC_SEQ_CST);return module_result(1,1);
}
static struct kernel_surface_result owner_pin(MODULE_ARGS){
    MODULE_UNUSED();struct kernel_hub_owner *o=hub_writable?owner_write(a):NULL;if(!o || b>1)return module_result(0,0);
    if(o->flags&HUB_OWNER_BOOT_PINNED)return module_result(1,1);
    if(b){
        if(__atomic_load_n(&o->state,__ATOMIC_SEQ_CST)!=HUB_OWNER_LIVE)return module_result(0,0);
        uint64_t n=__atomic_load_n(&o->pins,__ATOMIC_SEQ_CST);
        do{if(n==UINT64_MAX)return module_result(0,0);}while(!__atomic_compare_exchange_n(&o->pins,&n,n+1,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));
        if(__atomic_load_n(&o->state,__ATOMIC_SEQ_CST)!=HUB_OWNER_LIVE){__atomic_sub_fetch(&o->pins,1,__ATOMIC_SEQ_CST);return module_result(0,0);}
    }else{uint64_t n=__atomic_load_n(&o->pins,__ATOMIC_SEQ_CST);do{if(!n)return module_result(0,0);}while(!__atomic_compare_exchange_n(&o->pins,&n,n-1,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));}
    return module_result(1,1);
}
static struct kernel_surface_result owner_control(MODULE_ARGS){
    MODULE_UNUSED();if(!hub_writable || !writer_begin())return module_result(0,0);
    struct kernel_hub_owner *o=owner_write(a);int ok=0;
    if(o && !(o->flags&HUB_OWNER_BOOT_PINNED) && !__atomic_load_n(&o->tables,__ATOMIC_ACQUIRE)){
        uint32_t status=__atomic_load_n(&o->state,__ATOMIC_SEQ_CST);
        if(b==HUB_OWNER_DRAIN && (status==HUB_OWNER_LIVE || status==HUB_OWNER_DRAINING)){__atomic_store_n(&o->state,HUB_OWNER_DRAINING,__ATOMIC_SEQ_CST);ok=1;}
        if(b==HUB_OWNER_RESUME && status==HUB_OWNER_DRAINING){__atomic_store_n(&o->state,HUB_OWNER_LIVE,__ATOMIC_SEQ_CST);ok=1;}
        if(b==HUB_OWNER_RETIRE && status==HUB_OWNER_DRAINING && !__atomic_load_n(&o->active,__ATOMIC_SEQ_CST) && !__atomic_load_n(&o->pins,__ATOMIC_SEQ_CST)){__atomic_store_n(&o->state,HUB_OWNER_RETIRED,__ATOMIC_SEQ_CST);ok=1;}
    }
    writer_end();return module_result(ok,ok);
}
static void debug(const char *message) {
    while (*message) __asm__ volatile("outb %0,$0xe9" :: "a"((uint8_t)*message++));
}
static void debug_hex(uint64_t value, unsigned width) {
    static const char digits[] = "0123456789abcdef";
    char text[17];
    text[width] = 0;
    for (unsigned i = 0; i < width; ++i) {
        text[width - i - 1] = digits[value & 15]; value >>= 4;
    }
    debug(text);
}
static void debug_address(uintptr_t address) {
    debug("0x"); debug_hex(address, 16);
}
static struct kernel_surface_result dump_hub(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=hub_writable;
    if (!hub) return module_result(0,0);
    const struct kernel_surface *entries = kernel_surface_entries(hub);
    for (uint32_t i = 0; i < hub->count; ++i) {
        debug(entries[i].name);
        debug(" -> ");
        debug_address((uintptr_t)entries[i].call);
        debug("\n");
    }
    const struct kernel_hub_table *tables = kernel_hub_tables(hub);
    for (uint32_t i = 0; i < hub->table_count; ++i) {
        debug(tables[i].name); debug(" -> "); debug_address(tables[i].address); debug("\n");
    }
    return module_result(hub->count,1);
}
/* Logical publication is all-or-nothing. A failed growth may retain unused
 * mapped capacity, but no method/table/owner becomes partially visible. */
static struct kernel_surface_result publish_tables(MODULE_ARGS){
    MODULE_UNUSED();const struct kernel_hub_table *input=(const void *)a;
    if(!hub_writable || !input || !b || b>UINT32_MAX || !writer_begin())return module_result(0,0);
    int ok=0;uint32_t count=kernel_hub_table_count(hub_writable);
    if(b>UINT32_MAX-count)goto done;
    for(size_t i=0;i<b;++i){const struct kernel_hub_table *t=&input[i];struct kernel_hub_owner *o=owner_write(t->owner);
        if(!valid_name(t->name) || !t->address || !t->bytes || t->bytes>UINTPTR_MAX-t->address || !t->entry_bytes || t->entries_offset>t->bytes || t->count>(t->bytes-t->entries_offset)/t->entry_bytes || kernel_hub_find_table(hub_writable,t->name) || !o || __atomic_load_n(&o->state,__ATOMIC_SEQ_CST)!=HUB_OWNER_LIVE)goto done;
        for(size_t j=0;j<i;++j)if(kernel_hub_name_equal(t->name,input[j].name))goto done;
    }
    if(!grow(&table_range,((uint64_t)count+b)*sizeof(*input)))goto done;
    struct kernel_hub_table *tables=writable(&table_range);
    for(size_t i=0;i<b;++i){tables[count+i]=input[i];__atomic_add_fetch(&owner_write(input[i].owner)->tables,1,__ATOMIC_RELEASE);}
    __atomic_store_n(&hub_writable->table_capacity,table_range.mapped/sizeof(*input),__ATOMIC_RELEASE);
    __atomic_store_n(&hub_writable->table_count,count+b,__ATOMIC_RELEASE);ok=1;
done:writer_end();return module_result(ok?b:0,ok);
}
static struct kernel_surface_result publish_table(MODULE_ARGS){
    MODULE_UNUSED();struct kernel_surface_result r=publish_tables(a,1,0,0,0,0);return r.status?r:module_result(((const struct kernel_hub_table *)a)->address,1);
}
static struct kernel_surface_result physical_read(MODULE_ARGS) {
    MODULE_UNUSED(); int ok = hub_pages && boot_physical_read(hub_pages, a, (void *)b, c);
    return module_result(ok ? c : 0, ok);
}
static struct kernel_surface_result map_mmio(MODULE_ARGS) {
    MODULE_UNUSED(); void *mapped=hub_pages?boot_mmio_map(hub_pages,a,b):NULL;
    if(mapped)kernel_cpu_flush();
    return module_result((uintptr_t)mapped,mapped!=NULL);
}
/* Firmware adapters remain small kernel mechanisms; AML policy is in acpi. */
static struct kernel_surface_result firmware_map(MODULE_ARGS) {
    MODULE_UNUSED();void *mapped=hub_pages?boot_firmware_map(hub_pages,a,b):NULL;
    if(mapped)kernel_cpu_flush();
    return module_result((uintptr_t)mapped,mapped!=NULL);
}
static int port_allowed(uintptr_t port,uintptr_t width) {
    if((width!=1 && width!=2 && width!=4) || port>65536-width)return 0;
    const unsigned ranges[][2]={{0x20,0x22},{0x40,0x44},{0x60,0x61},{0x64,0x65},{0x70,0x72},{0xa0,0xa2},{0xe9,0xea},{0xcf8,0xd00}};
    for(size_t i=0;i<sizeof(ranges)/sizeof(ranges[0]);++i)if(port<ranges[i][1] && ranges[i][0]<port+width)return 0;
    return 1;
}
static struct kernel_surface_result port_read(MODULE_ARGS) {
    MODULE_UNUSED();if(!port_allowed(a,b))return module_result(0,0);uint32_t value=0;
    if(b==1) {uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"((uint16_t)a));value=v;}
    else if(b==2) {uint16_t v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"((uint16_t)a));value=v;}
    else __asm__ volatile("inl %1,%0":"=a"(value):"Nd"((uint16_t)a));
    return module_result(value,1);
}
static struct kernel_surface_result port_write(MODULE_ARGS) {
    MODULE_UNUSED();if(!port_allowed(a,b))return module_result(0,0);
    if(b==1)__asm__ volatile("outb %0,%1"::"a"((uint8_t)c),"Nd"((uint16_t)a));
    else if(b==2)__asm__ volatile("outw %0,%1"::"a"((uint16_t)c),"Nd"((uint16_t)a));
    else __asm__ volatile("outl %0,%1"::"a"((uint32_t)c),"Nd"((uint16_t)a));
    return module_result(1,1);
}
static struct kernel_surface_result log_text(MODULE_ARGS) {
    MODULE_UNUSED();if(!a && b)return module_result(0,0);const char *s=(const void *)a;
    for(size_t i=0;i<b;++i)__asm__ volatile("outb %0,$0xe9"::"a"(s[i]));
    return module_result(b,1);
}
static struct kernel_surface_result reserve_memory(MODULE_ARGS) {
    MODULE_UNUSED();
    if(!a || a>SIZE_MAX-4095 || b<BOOT_RES_HEAP || b>BOOT_RES_STACK)return module_result(0,0);
    size_t count=(a+4095)/4096;uint64_t flags=kernel_cpu_enter();
    uint64_t physical=boot_pages_claim(hub_pages,count,BOOT_RES_SCRUB,c);kernel_cpu_leave(flags);
    if(!physical)return module_result(0,0);
    /* SCRUB remains reserved and cannot be mapped/released while sanitization
     * runs with the caller's original interrupt policy. Publish only afterwards. */
    boot_pages_zero(hub_pages,physical,count);flags=kernel_cpu_enter();
    int ok=boot_pages_retype(hub_pages,physical,count,BOOT_RES_SCRUB,b,c);kernel_cpu_leave(flags);
    if(ok && d)*(uint64_t *)d=physical;
    return module_result(ok?(uintptr_t)boot_physical_pointer(hub_pages,physical):0,ok);
}
static struct kernel_surface_result release_memory(MODULE_ARGS) {
    MODULE_UNUSED();uint64_t flags=kernel_cpu_enter();int ok=boot_pages_retire(hub_pages,a,b,c,d);kernel_cpu_leave(flags);
    if(!ok)return module_result(0,0);
    boot_pages_zero(hub_pages,a,b);flags=kernel_cpu_enter();ok=boot_pages_finish(hub_pages,a,b,d);kernel_cpu_leave(flags);return module_result(ok,ok);
}
static struct kernel_surface_result pin_memory(MODULE_ARGS) {
    MODULE_UNUSED();int ok=d<=1 && boot_pages_pin(hub_pages,a,b,c,d);return module_result(ok,ok);
}
static struct kernel_surface_result map_owned(MODULE_ARGS) {
    MODULE_UNUSED();int ok=boot_owned_map(hub_pages,a,b,c);if(ok) kernel_cpu_flush();return module_result(ok,ok);
}
static struct kernel_surface_result unmap_owned(MODULE_ARGS) {
    MODULE_UNUSED();int ok=boot_owned_unmap(hub_pages,a,b,c);if(ok) kernel_cpu_flush();return module_result(ok,ok);
}
static struct kernel_surface_result map_batch(MODULE_ARGS) {
    MODULE_UNUSED();const struct boot_owned_mapping *items=(const void *)a;
    if(!items || !b || b>BOOT_UNMAP_BATCH)return module_result(0,0);
    for(size_t i=0;i<b;++i)for(size_t j=0;j<i;++j)if(items[i].address==items[j].address)return module_result(0,0);
    size_t mapped=0;
    for(;mapped<b;++mapped)if(!boot_owned_map(hub_pages,items[mapped].address,items[mapped].physical,items[mapped].owner))break;
    if(mapped)kernel_cpu_flush();
    return module_result(mapped,mapped==b);
}
static struct kernel_surface_result unmap_batch(MODULE_ARGS) {
    MODULE_UNUSED();int ok=boot_owned_unmap_batch(hub_pages,(const void *)a,b);
    if(ok)kernel_cpu_flush();
    return module_result(ok,ok);
}
const struct kernel_surfaces *kernel_surfaces_publish(struct boot_pages *pages) {
    if (hub_writable) return NULL;
    uint64_t physical = boot_pages_reserve(pages, HUB_BYTES / BOOT_PAGE_SIZE, BOOT_RES_HUB, KERNEL_SURFACES_ADDRESS);
    if(!physical || !boot_map_page(pages,KERNEL_SURFACES_ADDRESS,physical,false,false))return NULL;
    hub_pages=pages;hub_writable=boot_physical_pointer(pages,physical);
    *hub_writable=(struct kernel_surfaces){.magic=KERNEL_SURFACES_MAGIC,.major=KERNEL_HUB_ABI,.page_bytes=HUB_BYTES,.header_bytes=sizeof(*hub_writable),.entry_bytes=sizeof(struct kernel_surface),.physical_address=physical,
        .table_entry_bytes=sizeof(struct kernel_hub_table),.methods_address=KERNEL_METHODS_ADDRESS,.tables_address=KERNEL_TABLES_ADDRESS,.owners_address=KERNEL_OWNERS_ADDRESS,.owner_entry_bytes=sizeof(struct kernel_hub_owner),.mapped_bytes=HUB_BYTES};
    if(!grow(&owner_range,sizeof(struct kernel_hub_owner)))return NULL;
    struct kernel_hub_owner *kernel=writable(&owner_range);*kernel=(struct kernel_hub_owner){.name="kernel",.id=0,.state=HUB_OWNER_LIVE,.flags=HUB_OWNER_BOOT_PINNED};
    hub_writable->owner_capacity=owner_range.mapped/sizeof(*kernel);__atomic_store_n(&hub_writable->owner_count,1,__ATOMIC_RELEASE);
    if(!add_surface("hub.owner_info",owner_info,1,0,0) || !add_surface("hub.owner_pin",owner_pin,1,0,0) || !add_surface("hub.owner_control",owner_control,1,0,0))return NULL;
    const char publish_name[32] = "hub.publish_table", read_name[32] = "hardware.read", reserve_name[32] = "memory.reserve", map_name[32] = "hardware.map_mmio", dump_name[32] = "hub.dump";
    if (!add_surface("hub.publish_tables",publish_tables,1,0,0) || !add_surface(publish_name, publish_table, 1, 0, 0) || !add_surface(read_name, physical_read, 1, 0, 0) ||
        !add_surface(reserve_name, reserve_memory, 1, 1, 0) || !add_surface(map_name, map_mmio, 1, 0, 0) || !add_surface(dump_name, dump_hub, 1, 0, 0)) return NULL;
    const char release_name[32]="memory.release",map_owned_name[32]="memory.map_owned",unmap_owned_name[32]="memory.unmap_owned";
    if (!add_surface(release_name,release_memory,1,0,0) || !add_surface(map_owned_name,map_owned,1,0,0) ||
        !add_surface(unmap_owned_name,unmap_owned,1,0,0)) return NULL;
    const char pin_name[32]="memory.pin";
    if(!add_surface(pin_name,pin_memory,1,0,0))return NULL;
    const char map_batch_name[32]="memory.map_batch";
    if(!add_surface(map_batch_name,map_batch,1,0,0))return NULL;
    const char batch_name[32]="memory.unmap_batch";
    if(!add_surface(batch_name,unmap_batch,1,0,0))return NULL;
    if(!add_surface("hardware.firmware_map",firmware_map,1,0,0) || !add_surface("hardware.port_read",port_read,1,0,0) ||
       !add_surface("hardware.port_write",port_write,1,0,0) || !add_surface("hardware.log",log_text,1,0,0))return NULL;
    const char cpu_name[32]="cpu.control";
    if(!add_surface(cpu_name,kernel_cpu_control,1,0,0)) return NULL;
    struct kernel_hub_table ledger = { .name = "memory.reservations", .address = (uintptr_t)&pages->reservations,
        .bytes = sizeof(pages->reservations), .count = 1, .entry_bytes = sizeof(pages->reservations) };
    if (publish_table((uintptr_t)&ledger, 0, 0, 0, 0, 0).status) return NULL;
    return (const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
}
static bool load_owned(struct boot_pages *pages,const void *image,size_t bytes,const char *name,uint32_t flags) {
    if (!hub_writable || !image || bytes < sizeof(struct kmodule_image) || flags&~HUB_OWNER_BOOT_PINNED) return false;
    const struct kmodule_image *h = image;
    if (h->magic != KMODULE_MAGIC || h->format != KMODULE_FORMAT || h->header_bytes != sizeof(*h) ||
        !valid_name(h->name) || !kernel_hub_name_equal(h->name, name) || h->image_bytes != bytes ||
        h->memory_bytes < bytes || !h->memory_bytes || h->memory_bytes % 4096 ||
        h->writable_offset % 4096 || h->writable_offset < sizeof(*h) || h->writable_offset > bytes ||
        h->export_bytes != sizeof(struct kmodule_export) || !h->exports_count ||
        h->exports_offset < sizeof(*h) || h->exports_offset % 8 || h->exports_offset > h->writable_offset ||
        h->exports_count > (h->writable_offset - h->exports_offset) / sizeof(struct kmodule_export) ||
        h->exports_count>UINT32_MAX-kernel_surface_count(hub_writable) || next_module > KERNEL_SURFACES_ADDRESS ||
        h->memory_bytes > KERNEL_SURFACES_ADDRESS - next_module ||
        h->memory_bytes > KERNEL_SURFACES_ADDRESS - next_module - 4096) return false;
    const struct kmodule_export *exports = (const void *)((const unsigned char *)image + h->exports_offset);
    uint64_t code_start = h->exports_offset + h->exports_count * sizeof(*exports);
    for (size_t i = 0; i < h->exports_count; ++i) {
        if (!valid_name(exports[i].name) || !exports[i].major || exports[i].reserved ||
            exports[i].offset < code_start || exports[i].offset >= h->writable_offset ||
            method_exists(exports[i].name)) return false;
        for (size_t j = 0; j < i; ++j)
            if (kernel_hub_name_equal(exports[i].name, exports[j].name)) return false;
    }
    for(uint32_t i=0;i<hub_writable->owner_count;++i)if(kernel_hub_name_equal(owner_write(i)->name,name))return false;
    uint32_t owner=hub_writable->owner_count,first=kernel_surface_count(hub_writable);
    if(owner==UINT32_MAX || !grow(&owner_range,((uint64_t)owner+1)*sizeof(struct kernel_hub_owner)) || !ensure_methods((uint64_t)first+h->exports_count))return false;
    uint64_t irqflags=kernel_cpu_enter();uint64_t physical=boot_pages_claim(pages,h->memory_bytes/4096,BOOT_RES_MODULE,next_module);kernel_cpu_leave(irqflags);
    if(!physical)return false;
    uintptr_t address=next_module;next_module+=h->memory_bytes+4096;
    boot_pages_zero(pages,physical,h->memory_bytes/4096);
    unsigned char *destination=boot_physical_pointer(pages,physical);const unsigned char *source=image;
    for(size_t i=0;i<bytes;++i)destination[i]=source[i];
    for(size_t i=0;i<h->memory_bytes;i+=4096){int writable_page=i>=h->writable_offset;irqflags=kernel_cpu_enter();int ok=boot_map_page(pages,address+i,physical+i,writable_page,!writable_page);kernel_cpu_leave(irqflags);if(!ok)return false;}
    kernel_cpu_flush();
    struct kernel_hub_owner *record=&((struct kernel_hub_owner *)writable(&owner_range))[owner];
    *record=(struct kernel_hub_owner){.id=owner,.image=address,.bytes=h->memory_bytes,.state=HUB_OWNER_LIVE,.flags=flags,.methods=h->exports_count};
    for(unsigned i=0;i<31 && name[i];++i)record->name[i]=name[i];
    for(size_t i=0;i<h->exports_count;++i)write_surface(first+i,exports[i].name,(kernel_surface_call)(address+exports[i].offset),exports[i].major,exports[i].minor,1,owner);
    uint32_t serial=0;__asm__ volatile("cpuid":"+a"(serial)::"rbx","rcx","rdx","memory");
    /* Serialize remote instruction fetch after writing the new stub bytes,
     * before publishing their callable addresses to other CPUs. */
    kernel_cpu_flush();
    __atomic_store_n(&hub_writable->owner_capacity,owner_range.mapped/sizeof(*record),__ATOMIC_RELEASE);
    __atomic_store_n(&hub_writable->owner_count,owner+1,__ATOMIC_RELEASE);
    __atomic_store_n(&hub_writable->count,first+h->exports_count,__ATOMIC_RELEASE);return true;
}
bool kernel_module_load_owned(struct boot_pages *pages,const void *image,size_t bytes,const char *name,uint32_t flags){
    if(!writer_begin())return false;
    bool ok=load_owned(pages,image,bytes,name,flags);writer_end();return ok;
}
bool kernel_module_load(struct boot_pages *pages,const void *image,size_t bytes,const char *name){return kernel_module_load_owned(pages,image,bytes,name,HUB_OWNER_BOOT_PINNED);}
/* Boot supplies both variants; only selected images are executable mappings.
 * CPU vendor and ACPI IOMMU provider are independent selection dimensions. */
static const struct module_file *boot_file(const char *tag) {
    const struct module_file *chosen=NULL;
    for(uint64_t i=0;i<module_request.response->count;++i){const struct module_file *file=module_request.response->files[i];
        if(file && file->cmdline && kernel_hub_name_equal(file->cmdline,tag)){if(chosen)return NULL;chosen=file;}
    }return chosen;
}
static bool load_boot_image(struct boot_pages *pages,const char *tag,const char *contract) {
    const struct module_file *chosen=boot_file(tag);
    return chosen && kernel_module_load(pages,chosen->address,chosen->size,contract);
}
static bool load_variant(struct boot_pages *pages,const char *tag,const char *contract) {
    if(!load_boot_image(pages,tag,contract))return false;
    debug("Cloudberry backend: ");debug(tag);debug("\n");return true;
}
bool kernel_modules_load(struct boot_pages *pages) {
    if(!module_request.response)return false;
    const struct kernel_surfaces *hub=(const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    if(!load_boot_image(pages,"inthandle","inthandle"))return false;
    kernel_surface_call init=kernel_surface_find_version(hub,"inthandle.init",1,0);
    if(!init || init(0,0,0,0,0,0).status)return false;
    debug("Cloudberry v2: inthandle ready\n");
    /* Cover inherited firmware/bootloader pixels as soon as fault handling is
     * established. Splash needs only Limine's existing framebuffer mapping. */
    if(!load_boot_image(pages,"splash","splash"))return false;
    kernel_surface_call splash=kernel_surface_find_version(hub,"splash.show",1,0);
    if(!splash || splash((uintptr_t)framebuffer_request.response,0,0,0,0,0).status) {
        debug("Cloudberry v2: splash failed\n");return false;
    }
    uint32_t a,b,c,d;__asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0),"c"(0));
    const char *tag=b==0x756e6547 && d==0x49656e69 && c==0x6c65746e?"cpu-intel":b==0x68747541 && d==0x69746e65 && c==0x444d4163?"cpu-amd":NULL;
    if(!tag || !load_variant(pages,tag,"cpu-backend") || !kernel_cpu_backend(hub))return false;
    for(uint64_t i=0;i<module_request.response->count;++i) {
        const struct module_file *file=module_request.response->files[i];
        if(!file || !file->cmdline)return false;
        if(kernel_hub_name_equal(file->cmdline,"cloudberry-config") || kernel_hub_name_equal(file->cmdline,"inthandle") || kernel_hub_name_equal(file->cmdline,"splash") ||
            kernel_hub_name_equal(file->cmdline,"cpu-intel") || kernel_hub_name_equal(file->cmdline,"cpu-amd") ||
            kernel_hub_name_equal(file->cmdline,"iommu-intel") || kernel_hub_name_equal(file->cmdline,"iommu-amd"))continue;
        if(!kernel_module_load(pages,file->address,file->size,file->cmdline)) {debug("Cloudberry module failed: ");debug(file->cmdline);debug("\n");return false;}
    }
    return true;
}
static bool load_iommu_backend(struct boot_pages *pages,const struct kernel_surfaces *hub) {
    const struct kernel_hub_table *ref=kernel_hub_find_table(hub,HWENUM_TABLE_NAME);
    if(!ref)return false;
    const struct hwenum_table *hw=(const void *)ref->address;const char *tag=NULL;
    for(size_t i=0;i<hw->count;++i)if(hw->entries[i].kind==HWENUM_ACPI_TABLE) {
        const char *sig=hw->entries[i].data.acpi.signature;
        const char *found=sig[0]=='D' && sig[1]=='M' && sig[2]=='A' && sig[3]=='R'?"iommu-intel":sig[0]=='I' && sig[1]=='V' && sig[2]=='R' && sig[3]=='S'?"iommu-amd":NULL;
        if(found) {if(tag)return false;tag=found;}
    }
    return !tag || load_variant(pages,tag,"iommu-backend");
}
static bool startup_failure(const char *stage,const char *reason) {
    debug("Cloudberry startup: ");debug(stage);debug(" ");debug(reason);debug("\n");return false;
}
#define START(stage,call) do {if ((call).status) return startup_failure(stage,"failed");} while (0)
bool kernel_modules_start(struct boot_pages *pages, uint64_t rsdp_physical) {
    const struct kernel_surfaces *hub=(const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    const char *names[]={"mem.configure","mem.context","mem.init","mem.pages_init","scheduler.init",
        "hwenum.enumerate","mmio.init","clock.init","apic.init","timer.init","event.init","smp.prepare","threads.init","smp.start","irq.init","pci.init","nvme.init","iommu.init","xhci.init","acpi.init","platform.init","usb.init","usb_hub.init","usb_storage.init","partition.init","ext4_fs.init","sata.init"};
    kernel_surface_call calls[sizeof(names)/sizeof(names[0])];
    for (size_t i=0;i<sizeof(names)/sizeof(names[0]);++i) {
        calls[i]=kernel_surface_find_version(hub,names[i],1,0);
        if (!calls[i]) return startup_failure(names[i],"missing");
    }
    START("mem.configure",calls[0]((uintptr_t)hub,0,0,0,0,0));
    struct kernel_surface_result size=calls[1](0,0,0,0,0,0);
    if (size.status || !size.value) return startup_failure("mem.context","failed");
    const struct kmodule_context_info *info=(const void *)size.value;
    if (!info->bytes || info->bytes>SIZE_MAX-4095 || !info->alignment ||
        (info->alignment & (info->alignment-1)) || info->alignment>4096) return startup_failure("mem.context","invalid descriptor");
    hub_writable->mem_context_bytes=info->bytes;hub_writable->mem_context_alignment=info->alignment;
    uint64_t context=boot_pages_reserve(pages,(info->bytes+4095)/4096,BOOT_RES_CONTEXT,0);
    if (!context) return startup_failure("memory.context","exhausted");
    uintptr_t heap=(uintptr_t)boot_physical_pointer(pages,context);
    uint64_t arena=boot_pages_reserve(pages,1,BOOT_RES_HEAP,heap);
    if (!arena) return startup_failure("memory.heap","exhausted");
    START("mem.init",calls[2](heap,(uintptr_t)boot_physical_pointer(pages,arena),4096,0,0,0));
    const struct module_file *config_file=boot_file("cloudberry-config");
    kernel_surface_call config_init=kernel_surface_find_version(hub,"config.init",1,0);
    if(!config_file || !config_file->address || !config_file->size)return startup_failure("cloudberry.cnf","missing or duplicate data module");
    if(!config_init)return startup_failure("config.init","missing");
    struct kernel_surface_result configured=config_init((uintptr_t)hub,heap,(uintptr_t)config_file->address,config_file->size,0,0);
    if(configured.status){
        struct config_info detail={0};kernel_surface_call config_info=kernel_surface_find_version(hub,"config.info",1,0);
        if(config_info)config_info((uintptr_t)&detail,0,0,0,0,0);
        debug("Cloudberry config error=0x");debug_hex(configured.value,8);debug(" line=0x");debug_hex(detail.line,16);debug("\n");
        return startup_failure("config.init","failed");
    }
    START("mem.pages_init",calls[3]((uintptr_t)hub,heap,0,0,0,0));
    START("scheduler.init",calls[4]((uintptr_t)hub,heap,0,0,0,0));
    hardware_boot.rsdp_physical=rsdp_physical;hardware_boot.memory=pages->map;
    START("hwenum.enumerate",calls[5](heap,(uintptr_t)&hardware_boot,0,0,0,0));
    if(!load_iommu_backend(pages,hub))return startup_failure("iommu backend","selection failed");
    START("mmio.init",calls[6]((uintptr_t)hub,heap,0,0,0,0));
    START("clock.init",calls[7]((uintptr_t)hub,0,0,0,0,0));
    START("apic.init",calls[8]((uintptr_t)hub,0,0,0,0,0));
    kernel_surface_call xstate=kernel_surface_find_version(hub,"xstate.cpu_init",1,0);
    if(!xstate)return startup_failure("xstate.cpu_init","missing");
    START("xstate.cpu_init",xstate(0,0,0,0,0,0));
    if(!mp_request.response) return startup_failure("smp.prepare","missing Limine MP response");
    START("smp.prepare",calls[11]((uintptr_t)hub,heap,(uintptr_t)mp_request.response,0,0,0));
    START("timer.init",calls[9]((uintptr_t)hub,heap,mp_request.response->cpu_count,0,0,0));
    START("threads.init",calls[12]((uintptr_t)hub,heap,mp_request.response->cpu_count,0,0,0));
    START("event.init",calls[10]((uintptr_t)hub,0,0,0,0,0));
    START("smp.start",calls[13](0,0,0,0,0,0));
    START("irq.init",calls[14]((uintptr_t)hub,heap,0,0,0,0));
    START("iommu.init",calls[17]((uintptr_t)hub,heap,0,0,0,0));
    START("pci.init",calls[15]((uintptr_t)hub,heap,0,0,0,0));
    START("acpi.init",calls[19]((uintptr_t)hub,heap,rsdp_physical,0,0,0));
    START("platform.init",calls[20]((uintptr_t)hub,heap,0,0,0,0));
    START("nvme.init",calls[16]((uintptr_t)hub,0,0,0,0,0));
    START("xhci.init",calls[18]((uintptr_t)hub,heap,0,0,0,0));
    START("usb.init",calls[21]((uintptr_t)hub,heap,0,0,0,0));
    START("usb_hub.init",calls[22]((uintptr_t)hub,heap,0,0,0,0));
    START("usb_storage.init",calls[23]((uintptr_t)hub,heap,0,0,0,0));
    START("partition.init",calls[24]((uintptr_t)hub,heap,0,0,0,0));
    START("ext4_fs.init",calls[25]((uintptr_t)hub,heap,0,0,0,0));
    START("sata.init",calls[26]((uintptr_t)hub,0,0,0,0,0));
    return true;
}
