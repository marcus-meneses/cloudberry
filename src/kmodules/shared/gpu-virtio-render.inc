/* Boot qualification for VGA-compatible virtio, never generic PCI-ID guessing.
 * QEMU standard-vga: BAR0 is VRAM, BAR2+0x500 is the DISPI register window.
 * See docs/specs/standard-vga.txt and hw/display/virtio-vga.c in QEMU v6.2.0. */
static uint32_t probe_u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t probe_bar(const unsigned char *config,unsigned bar){
    if(bar>=6)return 0;
    for(unsigned i=0;i<bar;++i)if(!(config[0x10+i*4]&1) && (config[0x10+i*4]&6)==4){if(i+1==bar)return 0;++i;}
    uint32_t low=probe_u32(config+0x10+bar*4);unsigned type=(low>>1)&3;
    if(!low || low==UINT32_MAX || (low&1) || (type!=0 && type!=2) || (type==2 && bar==5))return 0;
    return (low&~UINT64_C(15))|(type==2?(uint64_t)probe_u32(config+0x14+bar*4)<<32:0);
}
static uintptr_t probe_map(uint64_t physical,uint64_t offset,size_t bytes){
    if(!physical || offset>UINT64_MAX-physical || bytes>UINT64_MAX-physical-offset)return 0;
    uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;
    struct kernel_surface_result mapped=CALL(state.map_mmio,physical+offset,bytes,0);
    CALL(state.cpu,CPU_LEAVE,flags,0);
    return mapped.status?0:mapped.value;
}
static int probe_boot_vga(struct controller *s,const unsigned char *config){
    struct display_boot_info boot;
    if(CALL(state.boot_info,&boot,sizeof(boot),0).status || boot.frames!=1)return 0;
    const struct hwenum_descriptor *frame=NULL;
    for(uint64_t i=0;i<state.hardware->count;++i){const struct hwenum_descriptor *d=state.hardware->entries+i;
        if(d->kind==HWENUM_MEMORY && d->data.memory.type==7){if(frame)return 0;frame=d;}
    }
    if(!frame || probe_bar(config,0)!=frame->address || !frame->length)return 0;
    uint64_t bar=probe_bar(config,2);s->probe_vga=probe_map(bar,0x500,20);if(!s->probe_vga)return 0;
    uint16_t *v=s->boot_vga;for(unsigned i=0;i<10;++i)v[i]=r16(s->probe_vga+i*2);
    return v[0]>=0xb0c0 && v[0]<=0xb0c5 && v[1] && v[2] && v[3]==32 && v[1]==boot.width && v[2]==boot.height && (uint64_t)v[6]*4==boot.pitch && (v[4]&0x41)==0x41 && !v[5] && v[6]>=v[1] && v[7]>=v[2] && !v[8] && !v[9] && (uint64_t)v[6]*4*v[2]<=frame->length;
}
static struct kernel_surface_result probe_candidate(MODULE_ARGS){
    MODULE_UNUSED();struct render_probe *p=(void *)a;
    if(!foreground() || !p || p->bytes!=sizeof(*p) || p->major!=1 || p->minor || p->reserved || b || c || d || e || f)return module_result(0,0);
    p->capabilities=0;struct controller *s=hardware(p->hardware_index);
    if(!s || !state.hardware || p->hardware_index>=state.hardware->count || s->owned || s->owner_pinned || gpu_bridge_owner(s) || (s->info->state!=GPU_DISCOVERED && s->info->state!=GPU_CLOSED))return module_result(0,0);
    const struct hwenum_descriptor *device=state.hardware->entries+p->hardware_index;
    if(device->kind!=HWENUM_PCI || device->data.pci.vendor!=0x1af4 || device->data.pci.device!=0x1050 || device->data.pci.subclass || device->data.pci.programming_interface || (device->data.pci.header_type&127) || p->vendor!=0x1af4 || p->device!=0x1050)return module_result(0,0);
    s->qualified=0;if(state.selected==s)state.selected=NULL;
    s->info->stage=100;struct pci_record record;unsigned char config[256];struct virtio_layout layout;
    if(CALL(state.pci_info,p->hardware_index,&record,0).status || record.identity.owner || !config_snapshot(s,config) || probe_u32(config)!=UINT32_C(0x10501af4) || !(config[4]&2) || !virtio_pci_layout(config,&layout))return module_result(0,0);
    s->info->stage=101;
    if(!probe_boot_vga(s,config))return module_result(0,0);
    /* Only the qualified VGA variant's BAR2 layout has restoration support. */
    if(layout.common.bar!=2 || layout.device.bar!=2 || layout.common.offset<0x1000 || layout.device.offset<0x1000 || ((uint64_t)layout.common.offset<layout.device.offset+16u && (uint64_t)layout.device.offset<layout.common.offset+56u))return module_result(0,0);
    s->info->stage=102;
    uintptr_t common=probe_map(probe_bar(config,2),layout.common.offset,56),device_config=probe_map(probe_bar(config,2),layout.device.offset,16);
    if(!common || !device_config || r8(common+20))return module_result(0,0);
    uint8_t generation=r8(common+21);uint32_t scanouts=r32(device_config+8),capsets=r32(device_config+12);
    /* These writes select a read-only feature bank, not driver features/status.
     * Save/restore in one bounded critical section: no allocation or waits. */
    uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;
    uint32_t saved=r32(common);w32(common,0);uint64_t offered=r32(common+4);
    w32(common,1);offered|=(uint64_t)r32(common+4)<<32;w32(common,saved);
    int stable=r32(common)==saved && !r8(common+20) && generation==r8(common+21);
    CALL(state.cpu,CPU_LEAVE,flags,0);
    struct iommu_info platform={0};if(CALL(state.iommu_info,&platform,0,0).status)return module_result(0,0);
    s->probe_common=common;s->info->offered=offered;s->info->stage=103;
    if(!stable || !scanouts || scanouts>16 || !capsets || capsets>64 || !(offered&1) || !(offered&(UINT64_C(1)<<32)) || (platform.backend!=IOMMU_NONE && !(offered&(UINT64_C(1)<<33))))return module_result(1,1);
    s->probe_common=common;s->qualified=1;state.selected=s;s->info->stage=104;
    p->capabilities=RENDER_GPU_COMPLETE;return module_result(1,1);
}
static struct kernel_surface_result release_candidate(MODULE_ARGS){
    MODULE_UNUSED();struct controller *s=hardware(a);struct pci_record record;
    if(!foreground() || b || c || d || e || f || !s || !s->qualified || s->owned || s->owner_pinned || gpu_bridge_owner(s) || s->resources || s->contexts || s->attachments || s->dma.id || s->route.id || (s->info->state!=GPU_DISCOVERED && s->info->state!=GPU_CLOSED))return module_result(0,0);
    if(CALL(state.pci_info,a,&record,0).status || record.identity.owner || r8(s->probe_common+20))return module_result(0,0);
    for(unsigned i=0;i<10;++i)if(r16(s->probe_vga+i*2)!=s->boot_vga[i])return module_result(0,0);
    /* Production exit/reset owns cleanup. Never steal a live session or claim
     * success based only on cleared bus mastering. Firmware mode must match. */
    return module_result(1,1);
}

struct kernel_surface_result gpu_virtio_probe(MODULE_ARGS){
    if(!foreground() || __atomic_exchange_n(&state.management,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    struct kernel_surface_result result=probe_candidate(a,b,c,d,e,f);
    __atomic_store_n(&state.management,0,__ATOMIC_RELEASE);return result;
}
struct kernel_surface_result gpu_virtio_release(MODULE_ARGS){
    if(!foreground() || __atomic_exchange_n(&state.management,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    struct kernel_surface_result result=release_candidate(a,b,c,d,e,f);
    __atomic_store_n(&state.management,0,__ATOMIC_RELEASE);return result;
}
