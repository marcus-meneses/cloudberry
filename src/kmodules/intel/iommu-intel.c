#include "../shared/iommu-internal.h"
static const struct iommu_backend_env *environment;
/* Legacy VT-d roots/contexts, three-level second-stage tables. Interrupt
 * remapping, scalable mode, ATS/PASID and firmware-active engines are rejected. */
int iommu_intel_invalidate(struct iommu_unit *u) {
    uintptr_t r=u->registers;
    if(u->capability&(UINT64_C(1)<<4)) {iommu_w32(r+0x18,(u->active?(1u<<31):0)|(1u<<27));if(!environment->wait32(r+0x1c,1u<<27,0))return 0;}
    iommu_w64(r+0x28,(UINT64_C(1)<<63)|(UINT64_C(1)<<61));
    if(!environment->wait64(r+0x28,UINT64_C(1)<<63,0) || ((iommu_r64(r+0x28)>>59)&3)!=1)return 0;
    uint64_t command=(UINT64_C(1)<<63)|(UINT64_C(1)<<60);
    if(u->capability&(UINT64_C(1)<<55))command|=UINT64_C(1)<<49;
    if(u->capability&(UINT64_C(1)<<54))command|=UINT64_C(1)<<48;
    iommu_w64(r+u->iotlb,command);
    return environment->wait64(r+u->iotlb,UINT64_C(1)<<63,0) && ((iommu_r64(r+u->iotlb)>>57)&3)==1;
}
int iommu_intel_start(struct iommu_unit *u) {
    uintptr_t r=u->registers;u->capability=iommu_r64(r+8);u->extended=iommu_r64(r+16);u->coherent=!!(u->extended&1);
    u->iotlb=((u->extended>>8)&1023)*16+8;
    unsigned max_bits=((u->capability>>16)&63)+1;
    u->levels=(u->capability&(UINT64_C(4)<<8)) && max_bits>=48?4:3;
    u->address_bits=12+9*u->levels;
    if(!(u->capability&(UINT64_C(1)<<(u->levels+6)))) {*environment->last_error=101;return 0;}
    if(!(u->extended&1) && !environment->cache_line) {*environment->last_error=102;return 0;}
    if(((u->capability>>16)&63)+1<39 || (iommu_r32(r+0x64)&0x80000001u) || u->iotlb>4088 || (iommu_r32(r+0x1c)&((1u<<31)|(1u<<26)|(1u<<25))) || (iommu_r64(r+0x20)&(UINT64_C(1)<<10))) {*environment->last_error=103;return 0;}
    if(!environment->alloc_pages(1,&u->table))return 0;
    environment->publish(u,u->table.address,4096);
    iommu_w32(r+0x38,1u<<31); /* Faults remain readable, no unowned fault IRQ. */
    iommu_w64(r+0x20,u->table.physical);iommu_w32(r+0x18,1u<<30);
    if(!environment->wait32(r+0x1c,1u<<30,1u<<30))return 0;
    if(u->capability&(UINT64_C(1)<<4)) {iommu_w32(r+0x18,1u<<27);if(!environment->wait32(r+0x1c,1u<<27,0))return 0;}
    if(!iommu_intel_invalidate(u)) {*environment->last_error=104;return 0;}
    iommu_w32(r+0x18,1u<<31);if(!environment->wait32(r+0x1c,1u<<31,1u<<31))return 0;
    u->active=1;return 1;
}
int iommu_intel_context(struct iommu_unit *u,uint16_t requester,uint16_t domain,uint64_t root) {
    unsigned bus=requester>>8,index=requester&255;
    struct mem_pages *context=&u->contexts[bus];
    if(!context->id) {if(!environment->alloc_pages(1,context))return 0;environment->publish(u,context->address,4096);}
    uint64_t *contexts=(void *)context->address,*roots=(void *)u->table.address;
    contexts[index*2]=0;__asm__ volatile("mfence":::"memory");
    contexts[index*2+1]=((uint64_t)domain<<8)|(u->levels-2);
    if(root)contexts[index*2]=root|1;
    environment->publish(u,(uintptr_t)&contexts[index*2],16);
    roots[bus*2]=context->physical|1;environment->publish(u,(uintptr_t)&roots[bus*2],16);
    return iommu_intel_invalidate(u);
}

#ifndef IOMMU_HOST_TEST
struct kernel_surface_result iommu_backend_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct iommu_backend_env *env=(const void *)a;
    if(!env || env->abi!=1 || env->backend!=IOMMU_INTEL || !env->last_error || !env->alloc_pages || !env->wait32 || !env->wait64 || !env->publish)return module_result(0,0);
    if(environment && environment!=env)return module_result(0,0);
    if(!env->frontend || !env->allocate || !env->read_bytes || !env->record_fault)return module_result(0,0);
    environment=env;env->frontend->leaf_flags=3;
    env->frontend->level_shift=0;env->frontend->register_bytes=4096;
    return module_result(IOMMU_INTEL,1);
}
struct kernel_surface_result iommu_backend_start(MODULE_ARGS) {MODULE_UNUSED();return module_result(1,environment && a && iommu_intel_start((void *)a));}
struct kernel_surface_result iommu_backend_context(MODULE_ARGS) {MODULE_UNUSED();return module_result(1,environment && a && iommu_intel_context((void *)a,b,c,d));}
struct kernel_surface_result iommu_backend_invalidate(MODULE_ARGS) {MODULE_UNUSED();return module_result(1,environment && a && iommu_intel_invalidate((void *)a));}

#endif

#define state (*environment->frontend)
static uint16_t u16(const unsigned char *p) {return p[0]|((uint16_t)p[1]<<8);}
static uint32_t u32(const unsigned char *p) {return u16(p)|((uint32_t)u16(p+2)<<16);}
static uint64_t u64(const unsigned char *p) {return u32(p)|((uint64_t)u32(p+4)<<32);}
static const struct hwenum_descriptor *pci_at(uint16_t segment,unsigned bus,unsigned slot,unsigned function) {
    for(size_t i=0;i<state.hw->count;++i) {const struct hwenum_descriptor *h=&state.hw->entries[i];
        if(h->kind==HWENUM_PCI && h->data.pci.segment==segment && h->data.pci.bus==bus && h->data.pci.slot==slot && h->data.pci.function==function)return h;
    }return NULL;
}
static int bridge_range(const struct hwenum_descriptor *bridge,unsigned *secondary,unsigned *last) {
    unsigned char buses[2];
    if(!bridge || (bridge->data.pci.header_type&127)!=1 || bridge->data.pci.class_code!=6 || bridge->data.pci.subclass!=4 || !state.read || !environment->read_bytes(bridge->address+0x19,buses,2))return 0;
    *secondary=buses[0];*last=buses[1];return *secondary && *secondary<=*last && *secondary!=bridge->data.pci.bus;
}
static int scope_match(const unsigned char *p,size_t bytes,const struct hwenum_descriptor *h) {
    for(size_t at=0;at<bytes;) {
        if(bytes-at<6 || p[at+1]<6 || p[at+1]>bytes-at || ((p[at+1]-6)&1))return -1;
        unsigned kind=p[at],length=p[at+1];
        if(kind==1 || kind==2) {
            if(length<8)return -1;
            unsigned bus=p[at+5];unsigned char seen[256]={0};
            for(unsigned path=6;path<length;path+=2) {
                unsigned slot=p[at+path],function=p[at+path+1];if(slot>31 || function>7 || seen[bus])return -1;
                seen[bus]=1;
                if(path+2==length && kind==1) {if(bus==h->data.pci.bus && slot==h->data.pci.slot && function==h->data.pci.function)return 1;break;}
                unsigned secondary,last;
                if(!bridge_range(pci_at(h->data.pci.segment,bus,slot,function),&secondary,&last))return -1;
                if(path+2==length) {if(h->data.pci.bus>=secondary && h->data.pci.bus<=last)return 1;break;}
                bus=secondary;
            }
        }
        at+=length;
    }return 0;
}
static int parse_intel(void) {
    const unsigned char *f=state.firmware;size_t length=state.firmware_bytes;
    for(size_t at=48;at<length;) {
        if(length-at<4)return 0;
        unsigned kind=u16(f+at),bytes=u16(f+at+2);if(bytes<4 || bytes>length-at)return 0;
        if(kind==0) {
            if(bytes<16 || (f[at+4]&~1))return 0;
            struct iommu_unit *u=&state.units[state.unit_count++];u->segment=u16(f+at+6);u->physical=u64(f+at+8);u->include_all=f[at+4]&1;
            u->scopes=state.firmware+at+16;u->scope_bytes=bytes-16;
            if(!u->physical || (u->physical&4095))return 0;
        } else if(kind==1) {
            if(bytes<24 || u64(f+at+8)>u64(f+at+16) || (u64(f+at+8)&4095) || (u64(f+at+16)&4095)!=4095)return 0;
            for(size_t i=0;i<state.hw->count;++i)if(state.hw->entries[i].kind==HWENUM_PCI && state.hw->entries[i].data.pci.segment==u16(f+at+6)) {
                int match=scope_match(f+at+24,bytes-24,&state.hw->entries[i]);if(match<0)return 0;if(match)state.reserved[i]=1;
            }
        }
        at+=bytes;
    }return state.unit_count!=0;
}
static struct iommu_unit *backend_route(const struct hwenum_descriptor *h) {
    struct iommu_unit *selected=NULL,*fallback=NULL;
    for(size_t i=0;i<state.unit_count;++i) {struct iommu_unit *u=&state.units[i];if(u->segment!=h->data.pci.segment)continue;
        int match=scope_match(u->scopes,u->scope_bytes,h);if(match<0)return NULL;
        if(match) {if(selected)return NULL;selected=u;}
        if(u->include_all) {if(fallback)return NULL;fallback=u;}
    }return selected?selected:fallback;
}
static void backend_collect(void) {
    for(size_t i=0;i<state.unit_count;++i) {struct iommu_unit *u=&state.units[i];if(!u->active)continue;
            if(iommu_r32(u->registers+0x34)&1) {state.info.failed=1;state.info.last_error=201;}
            size_t offset=((u->capability>>24)&1023)*16,count=((u->capability>>40)&255)+1;
            for(size_t n=0;n<count && offset+16<=4096;++n,offset+=16) {
                uint64_t high=iommu_r64(u->registers+offset+8);if(!(high&(UINT64_C(1)<<63)))continue;
                environment->record_fault(u,high&65535,iommu_r64(u->registers+offset)&~UINT64_C(4095),(high>>32)&255);
                iommu_w64(u->registers+offset+8,UINT64_C(1)<<63);
            }
    }
}
#ifndef IOMMU_HOST_TEST
struct kernel_surface_result iommu_backend_discover(MODULE_ARGS) {
    MODULE_UNUSED();if(!environment)return module_result(0,0);
    return module_result(1,parse_intel());
}
struct kernel_surface_result iommu_backend_route(MODULE_ARGS) {
    MODULE_UNUSED();if(!environment || !a)return module_result(0,0);
    struct iommu_unit *unit=backend_route((const void *)a);return module_result((uintptr_t)unit,unit!=NULL);
}
struct kernel_surface_result iommu_backend_collect(MODULE_ARGS) {MODULE_UNUSED();if(!environment)return module_result(0,0);backend_collect();return module_result(1,1);}
#endif
#undef state
