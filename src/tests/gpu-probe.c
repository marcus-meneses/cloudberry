/* Real production probe/release on a cold PCI/MMIO model; no graphics host. */
#define main gpu_transport_test_main
#include "gpu-virtio.c"
#undef main
static unsigned char config_bytes[256];
static uint16_t vga[10];static uint64_t pci_owner;static struct display_boot_info boot_info;
static struct {struct hwenum_table h;struct hwenum_descriptor entries[8];} inventory;
static struct kernel_surface_result probe_config(MODULE_ARGS){
    MODULE_UNUSED();struct pci_firmware_access *r=(void *)a;
    assert(r->width==4 && r->offset<=252);memcpy(&r->value,config_bytes+r->offset,4);return module_result(1,1);
}
static struct kernel_surface_result probe_device(MODULE_ARGS){
    MODULE_UNUSED();assert(a==7);*(struct pci_record *)b=(struct pci_record){.hardware_index=7,.identity={.owner=pci_owner}};return module_result(1,1);
}
static struct kernel_surface_result probe_boot_info(MODULE_ARGS){
    MODULE_UNUSED();assert(b==sizeof(boot_info));*(struct display_boot_info *)a=boot_info;return module_result(1,1);
}
static struct kernel_surface_result probe_mapping(MODULE_ARGS){
    MODULE_UNUSED();uintptr_t result=0;
    if(a==UINT64_C(0x80000500) && b==20)result=(uintptr_t)vga;
    if(a==UINT64_C(0x80001000) && b==56)result=(uintptr_t)registers;
    if(a==UINT64_C(0x80003000) && b==16)result=(uintptr_t)registers+12288;
    return module_result(result,result!=0);
}
static void cap(unsigned at,unsigned next,unsigned kind,unsigned offset,unsigned length){
    config_bytes[at]=9;config_bytes[at+1]=next;config_bytes[at+2]=kind==2?20:16;config_bytes[at+3]=kind;config_bytes[at+4]=2;
    memcpy(config_bytes+at+8,&offset,4);memcpy(config_bytes+at+12,&length,4);
}
static struct render_probe request(void){return (struct render_probe){.bytes=sizeof(struct render_probe),.major=1,.hardware_index=7,.vendor=0x1af4,.device=0x1050};}
static void cold(void){
    virgl=1;initialize();pci_owner=0;
    boot_info=(struct display_boot_info){1,640,480,640*4};state.boot_info=probe_boot_info;
    state.config_read=probe_config;state.pci_info=probe_device;state.map_mmio=probe_mapping;
    memset(config_bytes,0,sizeof(config_bytes));uint32_t id=0x10501af4,bar=0xc0000008,control=0x80000000;
    memcpy(config_bytes,&id,4);config_bytes[4]=2;config_bytes[6]=16;config_bytes[0x34]=0x40;
    memcpy(config_bytes+0x10,&bar,4);memcpy(config_bytes+0x18,&control,4);
    cap(0x40,0x50,1,0x1000,56);cap(0x50,0x64,2,0x2000,16);cap(0x64,0x74,3,0x1800,1);cap(0x74,0,4,0x3000,16);
    inventory=(__typeof__(inventory)){.h={.magic=HWENUM_TABLE_MAGIC,.major=1,.count=8,.capacity=8,.header_bytes=sizeof(struct hwenum_table),.entry_bytes=sizeof(struct hwenum_descriptor)}};
    inventory.entries[0]=(struct hwenum_descriptor){.kind=HWENUM_MEMORY,.address=0xc0000000,.length=640*480*4,.data.memory={.type=7}};
    inventory.entries[7]=(struct hwenum_descriptor){.kind=HWENUM_PCI,.data.pci={.vendor=0x1af4,.device=0x1050,.class_code=3}};
    state.hardware=&inventory.h;
    uint16_t mode[]={0xb0c5,640,480,32,0x41,0,640,1024,0,0};memcpy(vga,mode,sizeof(vga));
    *(uint32_t *)registers=3;
}
static struct kernel_surface_result qualify(struct render_probe *p){return gpu_virtio_probe((uintptr_t)p,0,0,0,0,0);}
int main(void){
    cold();struct render_probe p=request();assert(!qualify(&p).status && p.capabilities==RENDER_GPU_COMPLETE && state.selected==&model_controller && model_controller.qualified);
    assert(*(uint32_t *)registers==3 && !registers[20] && !owned && !master && !owner_pins && !dma_live && !model_info.commands && !model_info.interrupts);
    assert(!gpu_virtio_release(7,0,0,0,0,0).status && !gpu_virtio_release(7,0,0,0,0,0).status);
    ++vga[1];assert(gpu_virtio_release(7,0,0,0,0,0).status);--vga[1];
    pci_owner=42;assert(gpu_virtio_release(7,0,0,0,0,0).status);pci_owner=0;
    model_controller.bridge_process=99;assert(gpu_virtio_release(7,0,0,0,0,0).status);model_controller.bridge_process=0;
    registers[20]=4;assert(gpu_virtio_release(7,0,0,0,0,0).status);registers[20]=0;
    cold();virgl=0;p=request();assert(!qualify(&p).status && !p.capabilities && !state.selected && *(uint32_t *)registers==3);
    cold();model_iommu=IOMMU_INTEL;model_access=0;p=request();assert(!qualify(&p).status && !p.capabilities && !dma_live);
    cold();model_iommu=IOMMU_AMD;p=request();assert(!qualify(&p).status && p.capabilities==RENDER_GPU_COMPLETE);
    cold();inventory.entries[0].address+=4096;p=request();assert(qualify(&p).status && !state.selected);
    cold();inventory.entries[1]=inventory.entries[0];p=request();assert(qualify(&p).status);
    cold();boot_info.frames=2;p=request();assert(qualify(&p).status);
    cold();boot_info.pitch+=4;p=request();assert(qualify(&p).status);
    cold();boot_info.width++;p=request();assert(qualify(&p).status);
    cold();vga[3]=16;p=request();assert(qualify(&p).status);
    cold();vga[4]=1;p=request();assert(qualify(&p).status);
    cold();vga[8]=1;p=request();assert(qualify(&p).status);
    cold();registers[20]=4;p=request();assert(qualify(&p).status && *(uint32_t *)registers==3);
    cold();pci_owner=42;p=request();assert(qualify(&p).status && *(uint32_t *)registers==3);
    cold();inventory.entries[7].data.pci.subclass=0x80;p=request();assert(qualify(&p).status);
    cold();p=request();p.bytes--;assert(qualify(&p).status);
    cold();state.management=1;p=request();assert(qualify(&p).status && state.management==1 && !state.selected);state.management=0;
    assert(!qualify(&p).status);state.management=1;assert(gpu_virtio_release(7,0,0,0,0,0).status && state.management==1);state.management=0;
    puts("PASS GPU production policy: feature-bank preservation, firmware ownership/mode, 2D fallback, Intel/AMD DMA requirements and fail-closed release");
    return 0;
}
