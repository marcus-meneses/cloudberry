#include "../shared/iommu-internal.h"
static const struct iommu_backend_env *environment;
/* AMD-Vi device-table and v1 page-table backend, command-ring invalidation. */
int iommu_amd_start(struct iommu_unit *u) {
    uintptr_t r=u->registers;u->levels=4;u->address_bits=48;u->coherent=1;
    if(iommu_r64(r+0x18)&((UINT64_C(1)<<0)|(UINT64_C(1)<<2)|(UINT64_C(1)<<12)))return 0;
    if(!environment->alloc_pages(512,&u->table) || !environment->alloc_pages(1,&u->commands) || !environment->alloc_pages(1,&u->events) || !environment->alloc_pages(1,&u->completion))return 0;
    /* Invalid V=0 DTEs can bypass translation; valid entries with no DMA
     * permissions block unowned requesters. Keep compatible MSI pass-through. */
    uint64_t *dte=(void *)u->table.address;
    for(size_t i=0;i<65536;++i) {dte[i*4]=3;dte[i*4+2]=0;}
    iommu_w64(r,u->table.physical|511);iommu_w64(r+8,u->commands.physical|(UINT64_C(8)<<56));
    iommu_w64(r+16,u->events.physical|(UINT64_C(8)<<56));
    iommu_w64(r+0x2000,0);iommu_w64(r+0x2008,0);iommu_w64(r+0x2010,0);iommu_w64(r+0x2018,0);
    /* Coherent device-table fetches are required for our WB tables. Preserve
     * firmware policy/reserved fields; do not erase platform-specific controls. */
    uint64_t control=iommu_r64(r+0x18);
    control&=~((UINT64_C(1)<<3)|(UINT64_C(1)<<4)); /* No unowned fault/completion IRQ. */
    iommu_w64(r+0x18,control|1|(UINT64_C(1)<<12)|4|(UINT64_C(1)<<10));
    if(!environment->wait64(r+0x2020,UINT64_C(1)<<4,UINT64_C(1)<<4))return 0;
    u->active=1;return 1;
}
static int command(struct iommu_unit *u,uint64_t low,uint64_t high) {
    unsigned next=(u->tail+16)&4095;
    if(next==(iommu_r64(u->registers+0x2000)&4095))return 0;
    uint64_t *slot=(void *)(u->commands.address+u->tail);slot[0]=low;slot[1]=high;
    u->tail=next;__asm__ volatile("mfence":::"memory");iommu_w64(u->registers+0x2008,next);return 1;
}
int iommu_amd_invalidate(struct iommu_unit *u,uint16_t requester,uint16_t domain) {
    if(u->sequence==UINT64_MAX)return 0;
    uint64_t sequence=++u->sequence;*(volatile uint64_t *)u->completion.address=0;
    if(!command(u,requester|(UINT64_C(2)<<60),0) || !command(u,((uint64_t)domain<<32)|(UINT64_C(3)<<60),UINT64_C(0x7ffffffffffff000)|3) ||
       !command(u,u->completion.physical|1|(UINT64_C(1)<<60),sequence))return 0;
    return environment->wait64(u->completion.address,UINT64_MAX,sequence);
}
int iommu_amd_context(struct iommu_unit *u,uint16_t requester,uint16_t domain,uint64_t root) {
    uint64_t *dte=(void *)(u->table.address+(size_t)requester*32);
    dte[0]=3;__asm__ volatile("mfence":::"memory");dte[1]=domain;dte[2]=0;dte[3]=0;
    if(root)dte[0]=root|3|((uint64_t)u->levels<<9)|(UINT64_C(3)<<61);
    __asm__ volatile("mfence":::"memory");return iommu_amd_invalidate(u,requester,domain);
}

#ifndef IOMMU_HOST_TEST
struct kernel_surface_result iommu_backend_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct iommu_backend_env *env=(const void *)a;
    if(!env || env->abi!=1 || env->backend!=IOMMU_AMD || !env->last_error || !env->alloc_pages || !env->wait32 || !env->wait64 || !env->publish)return module_result(0,0);
    if(environment && environment!=env)return module_result(0,0);
    if(!env->frontend || !env->allocate || !env->read_bytes || !env->record_fault)return module_result(0,0);
    environment=env;env->frontend->leaf_flags=1|(UINT64_C(3)<<61);
    env->frontend->level_shift=9;env->frontend->register_bytes=16384;
    return module_result(IOMMU_AMD,1);
}
struct kernel_surface_result iommu_backend_start(MODULE_ARGS) {MODULE_UNUSED();return module_result(1,environment && a && iommu_amd_start((void *)a));}
struct kernel_surface_result iommu_backend_context(MODULE_ARGS) {MODULE_UNUSED();return module_result(1,environment && a && iommu_amd_context((void *)a,b,c,d));}
struct kernel_surface_result iommu_backend_invalidate(MODULE_ARGS) {MODULE_UNUSED();return module_result(1,environment && a && iommu_amd_invalidate((void *)a,b,c));}

#endif

#define state (*environment->frontend)
static uint16_t u16(const unsigned char *p) {return p[0]|((uint16_t)p[1]<<8);}
static uint32_t u32(const unsigned char *p) {return u16(p)|((uint32_t)u16(p+2)<<16);}
static uint64_t u64(const unsigned char *p) {return u32(p)|((uint64_t)u32(p+4)<<32);}
static int assign_range(unsigned first,unsigned last,unsigned unit,int reject) {
    if(first>last || last>65535 || unit>65534)return 0;
    for(unsigned i=first;i<=last;++i) {
        uint16_t selected=reject?UINT16_MAX:(uint16_t)(unit+1);
        if(state.routing[i] && state.routing[i]!=selected)return 0;
        state.routing[i]=selected;
    }return 1;
}
static int parse_amd(void) {
    const unsigned char *f=state.firmware;size_t length=state.firmware_bytes;
    for(size_t at=48;at<length;) {
        if(length-at<4)return 0;
        unsigned kind=f[at],bytes=u16(f+at+2);if(bytes<4 || bytes>length-at)return 0;
        if(kind==0x10 || kind==0x11) {
            size_t fixed=kind==0x10?24:40;if(bytes<fixed || u16(f+at+16))return 0;
            uint64_t physical=u64(f+at+8);size_t unit=state.unit_count;
            for(size_t i=0;i<state.unit_count;++i)if(state.units[i].physical==physical)unit=i;
            if(!physical || (physical&4095))return 0;
            if(unit==state.unit_count) {state.units[unit].physical=physical;++state.unit_count;}
            unsigned range=0;int in_range=0,reject_range=0;
            for(size_t entry=fixed;entry<bytes;) {
                unsigned type=f[at+entry];size_t size=4u<<(type>>6);if(size>bytes-entry)return 0;
                unsigned id=u16(f+at+entry+1);
                if(type==1) {if(!assign_range(0,65535,unit,0))return 0;}
                else if(type==2 || type==0x46) {if(!assign_range(id,id,unit,type==0x46))return 0;}
                else if(type==3 || type==0x43 || type==0x47) {if(in_range)return 0;range=id;in_range=1;reject_range=type!=3;}
                else if(type==4) {if(!in_range || !assign_range(range,id,unit,reject_range))return 0;in_range=0;}
                else if(type==0x42) {if(!assign_range(id,id,unit,1))return 0;}
                else if(type!=0 && type!=0x48)return 0; /* Special interrupt source, no PCI DMA. */
                entry+=size;
            }
            if(in_range)return 0;
        } else if(kind>=0x20 && kind<=0x22) {
            /* Reserved/exclusion requirements need a separate ownership policy. */
            return 0;
        } else return 0;
        at+=bytes;
    }return state.unit_count!=0;
}
static struct iommu_unit *backend_route(const struct hwenum_descriptor *h) {
    unsigned rid=((unsigned)h->data.pci.bus<<8)|(h->data.pci.slot<<3)|h->data.pci.function;
    unsigned route=state.routing[rid];return h->data.pci.segment || !route || route==UINT16_MAX?NULL:&state.units[route-1];
}
static void backend_collect(void) {
    for(size_t i=0;i<state.unit_count;++i) {struct iommu_unit *u=&state.units[i];if(!u->active)continue;
            if(iommu_r64(u->registers+0x2020)&1) {state.info.failed=1;state.info.last_error=202;}
            unsigned tail=iommu_r64(u->registers+0x2018)&4095;
            for(unsigned n=0;n<256 && u->event_head!=tail;++n) {
                volatile uint64_t *event=(void *)(u->events.address+u->event_head);uint64_t low=event[0];
                environment->record_fault(u,low&65535,event[1],(low>>60)&15);
                u->event_head=(u->event_head+16)&4095;
            }
            iommu_w64(u->registers+0x2010,u->event_head);
    }
}
#ifndef IOMMU_HOST_TEST
struct kernel_surface_result iommu_backend_discover(MODULE_ARGS) {
    MODULE_UNUSED();if(!environment)return module_result(0,0);
    state.routing=environment->allocate(65536,sizeof(uint16_t));if(!state.routing)return module_result(0,0);
    return module_result(1,parse_amd());
}
struct kernel_surface_result iommu_backend_route(MODULE_ARGS) {
    MODULE_UNUSED();if(!environment || !a)return module_result(0,0);
    struct iommu_unit *unit=backend_route((const void *)a);return module_result((uintptr_t)unit,unit!=NULL);
}
struct kernel_surface_result iommu_backend_collect(MODULE_ARGS) {MODULE_UNUSED();if(!environment)return module_result(0,0);backend_collect();return module_result(1,1);}
#endif
#undef state
