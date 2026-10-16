#include "iommu-internal.h"
#include "../../kernel/cpu.h"
#include "threads.h"
#include "mmio.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
struct iommu_state iommu_state;
static struct iommu_backend_env backend_environment;
static void record_fault(struct iommu_unit *,uint32_t,uint64_t,uint32_t);
#define state iommu_state
static int bsp(void) {return state.cpu && !CALL(state.cpu,CPU_CURRENT,0,0).value;}
static int enter(void) {
    uint64_t flags;__asm__ volatile("pushfq;pop %0":"=r"(flags));
    return bsp() && (flags&512) && !CALL(state.cpu,CPU_DEPTH,0,0).value && !__atomic_exchange_n(&state.busy,1,__ATOMIC_ACQUIRE);
}
static struct kernel_surface_result done(uintptr_t value,int ok) {__atomic_store_n(&state.busy,0,__ATOMIC_RELEASE);return module_result(value,ok);}
uint64_t iommu_now(void) {struct kernel_surface_result r=CALL(state.clock,0,0,0);return r.status?UINT64_MAX:r.value;}
static int pause_wait(void) {
    if(CALL(state.control,THREAD_CURRENT,0,0).value)return !CALL(state.control,THREAD_YIELD,0,0).status;
    __asm__ volatile("pause");return 1;
}
int iommu_wait64(uintptr_t at,uint64_t mask,uint64_t value) {
    uint64_t start=iommu_now();if(start==UINT64_MAX)return 0;
    for(unsigned i=0;i<2000000;++i) {
        if((iommu_r64(at)&mask)==value)return 1;
        uint64_t now=iommu_now();if(now==UINT64_MAX || now<start || now-start>UINT64_C(1000000000) || !pause_wait())return 0;
    }return 0;
}
int iommu_wait32(uintptr_t at,uint32_t mask,uint32_t value) {
    uint64_t start=iommu_now();if(start==UINT64_MAX)return 0;
    for(unsigned i=0;i<2000000;++i) {
        if((iommu_r32(at)&mask)==value)return 1;
        uint64_t now=iommu_now();if(now==UINT64_MAX || now<start || now-start>UINT64_C(1000000000) || !pause_wait())return 0;
    }return 0;
}
void iommu_publish(struct iommu_unit *unit,uintptr_t address,size_t bytes) {
    if(!unit->coherent && state.cache_line) {
        uintptr_t start=address&~(uintptr_t)(state.cache_line-1),end=address+bytes;
        for(uintptr_t at=start;at<end;at+=state.cache_line)__asm__ volatile("clflush (%0)"::"r"(at):"memory");
    }
    __asm__ volatile("mfence":::"memory");
}
int iommu_alloc_pages(size_t count,struct mem_pages *out) {return !CALL(state.pages,count,IOMMU_OWNER,out).status;}
static void *allocate(size_t n,size_t size) {struct kernel_surface_result r=CALL(state.alloc,state.heap,n,size);return r.status?NULL:(void *)r.value;}
static int read_bytes(uint64_t at,void *out,size_t bytes) {struct kernel_surface_result r=CALL(state.read,at,out,bytes);return !r.status && r.value==bytes;}
static uint16_t requester(const struct hwenum_descriptor *h) {return ((uint16_t)h->data.pci.bus<<8)|(h->data.pci.slot<<3)|h->data.pci.function;}
struct kernel_surface_result iommu_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)a;
    if(state.hub)return module_result(1,state.hub==hub && state.heap==b);
    if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC || !b)return module_result(0,0);
    uint32_t ca,cb,cc,cd;__asm__ volatile("cpuid":"=a"(ca),"=b"(cb),"=c"(cc),"=d"(cd):"a"(1),"c"(0));
    unsigned line=((cb>>8)&255)*8;if((cd&(1u<<19)) && line && !(line&(line-1)))state.cache_line=line;
    state.cpu=kernel_surface_find(hub,"cpu.control");if(!bsp())return module_result(0,0);
    state.alloc=kernel_surface_find(hub,"mem.calloc");state.free=kernel_surface_find(hub,"mem.free");state.pages=kernel_surface_find(hub,"mem.pages_alloc");state.release=kernel_surface_find(hub,"mem.pages_free");state.read=kernel_surface_find(hub,"hardware.read");state.map_mmio=kernel_surface_find(hub,"hardware.map_mmio");state.pin=kernel_surface_find(hub,"memory.pin");state.clock=kernel_surface_find(hub,"clock.now_ns");state.control=kernel_surface_find(hub,"threads.control");
    const struct kernel_hub_table *hw=kernel_hub_find_table(hub,HWENUM_TABLE_NAME);
    if(!hw || !state.alloc || !state.free || !state.pages || !state.release || !state.read || !state.map_mmio || !state.pin || !state.clock || !state.control)return module_result(0,0);
    state.hw=(const void *)hw->address;state.heap=b;state.domains=allocate(state.hw->count,sizeof(struct iommu_domain));state.reserved=allocate(state.hw->count,1);
    if(!state.domains || !state.reserved)return module_result(0,0);
    for(size_t i=0;i<state.hw->count;++i) {const struct hwenum_descriptor *h=&state.hw->entries[i];if(h->kind!=HWENUM_ACPI_TABLE)continue;
        int kind=h->data.acpi.signature[0]=='D' && h->data.acpi.signature[1]=='M' && h->data.acpi.signature[2]=='A' && h->data.acpi.signature[3]=='R'?IOMMU_INTEL:
            h->data.acpi.signature[0]=='I' && h->data.acpi.signature[1]=='V' && h->data.acpi.signature[2]=='R' && h->data.acpi.signature[3]=='S'?IOMMU_AMD:0;
        if(!kind)continue;
        if(state.firmware || h->length<48 || h->length>SIZE_MAX) {state.unsupported=1;break;}
        state.info.backend=kind;state.firmware_bytes=h->length;state.firmware=allocate(h->length,1);
        if(!state.firmware || !read_bytes(h->address,state.firmware,h->length))return module_result(0,0);
    }
    if(state.info.backend && !state.unsupported) {
        state.backend_init=kernel_surface_find_version(hub,"iommu.backend.init",1,0);
        state.backend_start=kernel_surface_find_version(hub,"iommu.backend.start",1,0);
        state.backend_context=kernel_surface_find_version(hub,"iommu.backend.context",1,0);
        state.backend_invalidate=kernel_surface_find_version(hub,"iommu.backend.invalidate",1,0);
        state.backend_discover=kernel_surface_find_version(hub,"iommu.backend.discover",1,0);
        state.backend_route=kernel_surface_find_version(hub,"iommu.backend.route",1,0);
        state.backend_collect=kernel_surface_find_version(hub,"iommu.backend.collect",1,0);
        if(!state.backend_discover || !state.backend_route || !state.backend_collect || !state.backend_init || !state.backend_start || !state.backend_context || !state.backend_invalidate)return module_result(0,0);
        backend_environment=(struct iommu_backend_env){.abi=1,.backend=state.info.backend,.cache_line=state.cache_line,.last_error=&state.info.last_error,
            .alloc_pages=iommu_alloc_pages,.wait32=iommu_wait32,.wait64=iommu_wait64,.publish=iommu_publish,.frontend=&state,
            .allocate=allocate,.read_bytes=read_bytes,.record_fault=record_fault};
        struct kernel_surface_result selected=CALL(state.backend_init,&backend_environment,0,0);
        if(selected.status || selected.value!=state.info.backend)return module_result(0,0);
        state.units=allocate(state.firmware_bytes/16+1,sizeof(struct iommu_unit));
        if(!state.units)return module_result(0,0);
        state.unsupported=CALL(state.backend_discover,0,0,0).status!=0;
    }
    state.info.units=state.unit_count;state.info.failed=state.unsupported;state.hub=hub;return module_result(1,1);
}
static struct iommu_unit *select_unit(const struct hwenum_descriptor *h) {
    struct kernel_surface_result r=CALL(state.backend_route,h,0,0);return r.status?NULL:(void *)r.value;
}
/* ExitBootServices ends firmware ownership, but disabling PCI bus mastering
 * alone does not reset engines or prove posted writes have drained. Require
 * readback and drain PCIe non-posted transactions here; the device owner must
 * still reset/quiesce its controller before attaching/reusing DMA storage. */
static int pending_transactions(uintptr_t config) {
    if(!(*(volatile uint16_t *)(config+6)&16))return 0;
    unsigned offset=*(volatile uint8_t *)(config+0x34);unsigned char seen[256]={0};
    for(unsigned count=0;offset && count<48;++count) {
        if(offset<0x40 || offset>0xfc || (offset&3) || seen[offset])return -1;
        seen[offset]=1;unsigned id=*(volatile uint8_t *)(config+offset);
        if(id==0x10) {if(offset>0xf4)return -1;return !!(*(volatile uint16_t *)(config+offset+0xa)&32);}
        offset=*(volatile uint8_t *)(config+offset+1);
    }return offset?-1:0;
}
static int quiesce(void) {
    if(state.quiesced)return 1;
    for(size_t i=0;i<state.hw->count;++i) {const struct hwenum_descriptor *h=&state.hw->entries[i];
        if(h->kind!=HWENUM_PCI || (h->data.pci.header_type&127)!=0)continue;
        struct kernel_surface_result mapped=CALL(state.map_mmio,h->address,4096,0);if(mapped.status)return 0;
        volatile uint16_t *command=(void *)(mapped.value+4);*command=*command&~4u;__asm__ volatile("mfence":::"memory");
        if(*command&4)return 0;
        uint64_t begin=iommu_now();if(begin==UINT64_MAX)return 0;
        unsigned spins;for(spins=0;spins<2000000;++spins) {int pending=pending_transactions(mapped.value);if(pending<0)return 0;if(!pending)break;
            uint64_t now=iommu_now();if(now==UINT64_MAX || now<begin || now-begin>UINT64_C(1000000000) || !pause_wait())return 0;
        }
        if(spins==2000000)return 0;
    }
    state.quiesced=1;return 1;
}
static struct iommu_node *new_node(struct iommu_domain *domain) {
    struct iommu_node *n=allocate(1,sizeof(*n));if(!n)return NULL;
    if(!iommu_alloc_pages(1,&n->page)) {CALL(state.free,state.heap,n,0);return NULL;}
    if(domain->unit)iommu_publish(domain->unit,n->page.address,4096);
    n->next=domain->nodes;domain->nodes=n;return n;
}
static struct iommu_node *node_at(struct iommu_domain *domain,uint64_t physical) {for(struct iommu_node *n=domain->nodes;n;n=n->next)if(n->page.physical==physical)return n;return NULL;}
static uint64_t *leaf(struct iommu_domain *domain,uint64_t address,int create) {
    struct iommu_node *n=domain->root;if(!n || !domain->unit || address>=(UINT64_C(1)<<domain->unit->address_bits))return NULL;
    for(unsigned level=domain->unit->levels;level>1;--level) {
        uint64_t *entry=&((uint64_t *)n->page.address)[(address>>(12+(level-1)*9))&511];
        if(!(*entry&1)) {
            if(!create)return NULL;
            struct iommu_node *next=new_node(domain);if(!next)return NULL;
            *entry=next->page.physical|(state.leaf_flags|(state.level_shift?((uint64_t)(level-1)<<state.level_shift):0));iommu_publish(domain->unit,(uintptr_t)entry,8);
        }
        n=node_at(domain,*entry&IOMMU_ADDRESS_MASK);if(!n)return NULL;
    }
    return &((uint64_t *)n->page.address)[(address>>12)&511];
}
int iommu_invalidate(struct iommu_unit *u,uint16_t rid,uint16_t domain) {
    int ok=!CALL(state.backend_invalidate,u,rid,domain).status;
    ++state.info.invalidations;if(!ok)state.info.failed=1;return ok;
}
static void collect_faults(void);
struct kernel_surface_result iommu_surface_attach(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.hub || !enter())return module_result(0,0);
    if(a>=state.hw->count || !b || state.info.failed || state.hw->entries[a].kind!=HWENUM_PCI || state.reserved[a])return done(0,0);
    struct iommu_domain *domain=&state.domains[a];if(domain->owner)return done(1,domain->owner==b);
    if(!state.info.backend) {domain->owner=b;++state.info.attached;return done(1,1);}
    struct iommu_unit *u=select_unit(&state.hw->entries[a]);if(!u) {state.info.last_error=2;return done(0,0);}
    if(!u->active) {
        if(!quiesce()) {state.info.last_error=3;return done(0,0);}
        if(!u->registers) {struct kernel_surface_result m=CALL(state.map_mmio,u->physical,state.register_bytes,0);if(m.status) {state.info.last_error=4;return done(0,0);}u->registers=m.value;}
        if(CALL(state.backend_start,u,0,0).status) {state.info.failed=1;return done(0,0);}
        ++state.info.active;
    }
    domain->unit=u;domain->requester=requester(&state.hw->entries[a]);domain->domain=domain->requester;
    if(!domain->root)domain->root=new_node(domain);
    if(!domain->root)return done(0,0);
    int ok=!state.backend_context((uintptr_t)u,domain->requester,domain->domain,domain->root->page.physical,0,0).status;
    if(!ok) {state.info.failed=1;return done(0,0);}
    collect_faults();domain->faulted=0;domain->owner=b;++state.info.attached;return done(1,1);
}
static struct iommu_domain *owned(size_t index,uint64_t owner) {return index<state.hw->count && owner && state.domains[index].owner==owner?&state.domains[index]:NULL;}
struct kernel_surface_result iommu_surface_map(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.hub || !enter())return module_result(0,0);
    struct iommu_domain *domain=owned(a,b);const struct mem_pages *p=(const void *)c;
    if(!domain || !p || !p->id || p->owner!=b || !p->count || p->count>SIZE_MAX/4096 || (p->physical&4095) || p->physical>UINT64_MAX-p->count*4096 || (state.info.backend && (p->physical>=(UINT64_C(1)<<domain->unit->address_bits) || p->count*4096>(UINT64_C(1)<<domain->unit->address_bits)-p->physical)) || state.info.failed)return done(0,0);
    for(struct iommu_mapping *m=domain->mappings;m;m=m->next)if(m->pages.id==p->id || (p->physical<m->pages.physical+m->pages.count*4096 && m->pages.physical<p->physical+p->count*4096))return done(0,0);
    struct iommu_mapping *m=allocate(1,sizeof(*m));if(!m)return done(0,0);
    if(state.pin(p->physical,p->count,b,1,0,0).status) {CALL(state.free,state.heap,m,0);return done(0,0);}
    m->pages=*p;m->next=domain->mappings;domain->mappings=m;
    if(!state.info.backend) {m->installed=p->count;state.info.mapped_pages+=p->count;return done(1,1);}
    for(;m->installed<p->count;++m->installed) {
        uint64_t address=p->physical+m->installed*4096;uint64_t *entry=leaf(domain,address,1);if(!entry || *entry)break;
        *entry=address|state.leaf_flags;iommu_publish(domain->unit,(uintptr_t)entry,8);
    }
    __asm__ volatile("mfence":::"memory");state.info.mapped_pages+=m->installed;
    if(m->installed==p->count && iommu_invalidate(domain->unit,domain->requester,domain->domain))return done(1,1);
    /* Partial construction or uncertain invalidation retains the pin/record.
     * PCI must retain the allocation and attempt an explicit unmap, never free. */
    return done(1,0);
}
struct kernel_surface_result iommu_surface_unmap(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.hub || !enter())return module_result(0,0);
    struct iommu_domain *domain=owned(a,b);if(!domain)return done(0,0);
    struct iommu_mapping **at=&domain->mappings;while(*at && (*at)->pages.id!=c)at=&(*at)->next;
    if(!*at || state.info.failed)return done(0,0);
    struct iommu_mapping *m=*at;
    if(state.info.backend) {
        for(size_t i=0;i<m->installed;++i) {uint64_t *entry=leaf(domain,m->pages.physical+i*4096,0);if(!entry)return done(0,0);*entry=0;iommu_publish(domain->unit,(uintptr_t)entry,8);}
        __asm__ volatile("mfence":::"memory");if(!iommu_invalidate(domain->unit,domain->requester,domain->domain))return done(0,0);
    }
    if(state.pin(m->pages.physical,m->pages.count,b,0,0,0).status)return done(0,0);
    state.info.mapped_pages-=m->installed;*at=m->next;CALL(state.free,state.heap,m,0);return done(1,1);
}
struct kernel_surface_result iommu_surface_detach(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.hub || !enter())return module_result(0,0);
    struct iommu_domain *domain=owned(a,b);if(!domain || domain->mappings || c!=1 || state.info.failed)return done(0,0);
    if(state.info.backend) {
        int ok=!state.backend_context((uintptr_t)domain->unit,domain->requester,domain->domain,0,0,0).status;
        if(!ok) {state.info.failed=1;return done(0,0);}
    }
    domain->owner=0;--state.info.attached;return done(1,1);
}
static void record_fault(struct iommu_unit *unit,uint32_t rid,uint64_t address,uint32_t reason) {
    state.info.last_fault_requester=rid;state.info.last_fault_address=address;state.info.fault_reason=reason;++state.info.faults;
    if(state.domains)for(size_t i=0;i<state.hw->count;++i) {
        struct iommu_domain *domain=&state.domains[i];
        if(domain->owner && domain->unit==unit && domain->requester==rid)domain->faulted=1;
    }
}
static void collect_faults(void) {
    if(state.info.backend && state.backend_collect && CALL(state.backend_collect,0,0,0).status)state.info.failed=1;
}
struct kernel_surface_result iommu_surface_info(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.hub || !bsp() || !a)return module_result(0,0);
    uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;collect_faults();*(struct iommu_info *)a=state.info;CALL(state.cpu,CPU_LEAVE,flags,0);return module_result(1,1);
}
struct kernel_surface_result iommu_surface_lookup(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.hub || !enter())return module_result(0,0);
    struct iommu_domain *domain=owned(a,b);if(!domain)return done(0,0);
    uint64_t page=c&~UINT64_C(4095);
    if(!state.info.backend) {
        for(struct iommu_mapping *m=domain->mappings;m;m=m->next)if(c>=m->pages.physical && c-m->pages.physical<m->pages.count*4096)return done(c,1);
        return done(0,0);
    }
    uint64_t *entry=leaf(domain,page,0);return done(entry && (*entry&1)?(*entry&IOMMU_ADDRESS_MASK)|(c&4095):0,entry && (*entry&1));
}

struct kernel_surface_result iommu_surface_ready(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.hub || !bsp() || state.info.failed)return module_result(0,0);
    uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;collect_faults();struct iommu_domain *domain=owned(a,b);int ok=domain && !domain->faulted;CALL(state.cpu,CPU_LEAVE,flags,0);return module_result(ok,ok);
}

struct kernel_surface_result iommu_surface_platform(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.hub || !a || !bsp())return module_result(0,0);
    struct iommu_platform out={.abi=1,.backend=state.info.backend,.firmware_supported=!state.unsupported,.unit_count=state.unit_count,
        .busmaster_disabled=state.quiesced,.controller_quiescence_required=1,.interrupt_remapping=0,.firmware_takeover_verified=0};
    for(size_t i=0;i<state.unit_count;++i)if(state.units[i].active) {
        uint32_t bits=state.units[i].address_bits;if(!out.minimum_iova_bits || bits<out.minimum_iova_bits)out.minimum_iova_bits=bits;
    }
    *(struct iommu_platform *)a=out;return module_result(1,1);
}
