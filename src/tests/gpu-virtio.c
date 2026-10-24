/* Production driver with a deterministic PCI/DMA/IRQ device model. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t model_read32(uintptr_t);
static void model_write16(uintptr_t,uint16_t);
#define GPU_VIRTIO_READ32(p) model_read32(p)
#define GPU_VIRTIO_WRITE16(p,v) model_write16(p,v)
#ifdef CB_GPU_TEST_PROFILE
static void gpu_profile_model(unsigned,unsigned long,unsigned);
#define GPU_PROFILE_ISSUE(type,start,blocks) gpu_profile_model(type,start,blocks)
#endif
#include "../kmodules/shared/gpu-virtio.c"
static _Alignas(16) unsigned char registers[16384];
static struct controller model_controller;static struct gpu_virtio_info model_info;
static struct vg_upload last_upload;
static uint64_t model_time;static unsigned owner_pins,model_iommu,model_access=1;static unsigned pending,timeout_mode,bad_fence,bad_used,fail_dma,reject_create,master,owned,wakes,waits,virgl=1;
struct dma_model {struct dma_model *next;struct mem_pages pages;};static struct dma_model *dma_list;static uint64_t dma_serial;static unsigned dma_live;
#ifdef CB_GPU_TEST_PROFILE
static unsigned traced_commands,traced_blocks;
static void gpu_profile_model(unsigned type,unsigned long start,unsigned blocks){assert(type>=0x100 && type<=0x207 && start<=model_time);++traced_commands;traced_blocks+=blocks;}
#endif
static uint32_t model_read32(uintptr_t p){uintptr_t base=(uintptr_t)registers;if(p==base+4)return *(uint32_t *)registers?(1u|(model_access?2u:0u)):virgl;return *(volatile uint32_t *)p;}
static void model_write16(uintptr_t p,uint16_t v){*(volatile uint16_t *)p=v;if(p==(uintptr_t)registers+4096){assert(v==0);pending=1;}}
static struct kernel_surface_result cpu_model(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result clock_model(MODULE_ARGS){MODULE_UNUSED();return module_result(model_time,1);}
static void device_complete(void){
 assert(pending&&master&&owned);pending=0;struct controller *s=&model_controller;struct vg_header *q=(void *)(s->dma.address+REQUEST_OFFSET),*r=(void *)(s->dma.address+RESPONSE_OFFSET);*r=(struct vg_header){.type=VG_OK,.flags=1,.fence=q->fence+(bad_fence?1:0)};uint32_t bytes=sizeof(*r);
 if(q->type==VG_DISPLAY_INFO){struct vg_display *d=(void *)r;d->h.type=VG_OK_DISPLAY;d->modes[0].enabled=1;d->modes[0].rect=(struct vg_rect){0,0,1280,800};bytes=sizeof(*d);}
 else if(q->type==VG_CAPSET_INFO){struct vg_cap_info *c=(void *)r;c->h.type=VG_OK_CAPSET_INFO;c->id=1;c->version=1;c->bytes=64;bytes=sizeof(*c);}
 else if(q->type==VG_CAPSET){r->type=VG_OK_CAPSET;memset((unsigned char *)r+sizeof(*r),0x5a,64);bytes+=64;}
 else if(q->type==VG_TRANSFER_FROM_3D){
  const struct vg_upload *read=(const void *)((const unsigned char *)q+sizeof(*q));struct resource *resource_record=resource(s,read->resource);assert(resource_record&&resource_record->attached&&read->width==resource_record->public.width&&read->height==resource_record->public.height&&read->stride==read->width*4&&read->layer_stride==resource_record->public.bytes);
  memset((void *)resource_record->public.pixels,0x5a,resource_record->public.bytes);
 }
 else if(q->type==VG_TRANSFER_TO_3D){last_upload=*(const struct vg_upload *)((const unsigned char *)q+sizeof(*q));struct resource *p=resource(s,last_upload.resource);assert(p && last_upload.stride==p->public.width*4 && last_upload.layer_stride==p->public.bytes && last_upload.offset==((uint64_t)last_upload.y*p->public.width+last_upload.x)*4);}
 else if(q->type==VG_CREATE_2D&&reject_create)r->type=VG_ERROR+1;
 else assert(q->type==VG_CREATE_2D||q->type==VG_ATTACH_BACKING||q->type==VG_TRANSFER_2D||q->type==VG_SCANOUT||q->type==VG_FLUSH||q->type==VG_DETACH_BACKING||q->type==VG_UNREF||q->type==VG_CREATE_3D||q->type==VG_CTX_ATTACH||q->type==VG_CTX_DETACH||q->type==VG_TRANSFER_TO_3D||q->type==VG_CTX_CREATE||q->type==VG_CTX_DESTROY||q->type==VG_SUBMIT_3D);
 s->queue.used->ring[s->queue.consumed%s->queue.depth]=(struct virtq_used_entry){bad_used?1:0,bytes};s->queue.used->index=s->queue.consumed+1;
 struct irq_notification n={.id=s->route.id,.owner=s->owner,.count=1};assert(!gpu_virtio_interrupt((uintptr_t)&n,(uintptr_t)s,0,0,0,0).status);
}
static struct kernel_surface_result thread_model(MODULE_ARGS){MODULE_UNUSED();if(a==THREAD_CURRENT)return module_result(55,1);if(a==THREAD_WAKE){assert(b==55);++wakes;return module_result(1,1);}assert(a==THREAD_BLOCK_UNTIL);++waits;if(timeout_mode)model_time=b;else device_complete();return module_result(1,1);}
static struct kernel_surface_result claim_model(MODULE_ARGS){MODULE_UNUSED();assert(a==7&&!owned);owned=1;struct pci_device *p=(void *)c;*p=(struct pci_device){.hardware_index=a,.owner=b,.msix_count=1};p->bars[4]=(struct pci_bar){.physical=0x100000,.bytes=sizeof(registers),.address=(uintptr_t)registers};return module_result(1,1);}
static struct kernel_surface_result master_model(MODULE_ARGS){MODULE_UNUSED();assert(a==7&&owned&&c<=1);master=c;return module_result(1,1);}
static struct kernel_surface_result release_model(MODULE_ARGS){MODULE_UNUSED();assert(a==7&&owned&&!master&&!dma_list&&c==1);owned=0;return module_result(1,1);}
static struct kernel_surface_result dma_alloc_model(MODULE_ARGS){MODULE_UNUSED();assert(a==7&&owned&&c&&d);struct dma_model *m=calloc(1,sizeof(*m));assert(m);void *p=aligned_alloc(4096,c*4096);assert(p);memset(p,0,c*4096);m->pages=(struct mem_pages){.id=++dma_serial,.physical=(uintptr_t)p,.count=c,.address=(uintptr_t)p,.owner=b};m->next=dma_list;dma_list=m;++dma_live;*(struct mem_pages *)d=m->pages;return module_result(m->pages.id,!fail_dma);}
static struct kernel_surface_result dma_free_model(MODULE_ARGS){MODULE_UNUSED();assert(a==7&&owned&&!master);struct dma_model **m=&dma_list;while(*m&&(*m)->pages.id!=c)m=&(*m)->next;assert(*m);struct dma_model *old=*m;*m=old->next;free((void *)old->pages.address);free(old);--dma_live;return module_result(1,1);}
static struct kernel_surface_result alloc_model(MODULE_ARGS){MODULE_UNUSED();void *p=calloc(b,c);return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result drop_model(MODULE_ARGS){MODULE_UNUSED();free((void *)b);return module_result(1,1);}
static struct kernel_surface_result pin_model(MODULE_ARGS){MODULE_UNUSED();assert(a==42);if(b)++owner_pins;else {assert(owner_pins);--owner_pins;}return module_result(1,1);}
static struct kernel_surface_result iommu_model(MODULE_ARGS){MODULE_UNUSED();*(struct iommu_info *)a=(struct iommu_info){.backend=model_iommu};return module_result(1,1);}
static struct kernel_surface_result good_model(MODULE_ARGS){MODULE_UNUSED();return module_result(1,1);}
static struct kernel_surface_result route_model(MODULE_ARGS){MODULE_UNUSED();struct irq_request *p=(void *)a;assert(p->callback&&p->context==(uintptr_t)&model_controller);*(struct irq_route *)b=(struct irq_route){.id=1,.owner=p->owner,.kind=IRQ_MSI};return module_result(1,1);}
static struct kernel_surfaces model_hub;static struct kernel_surface entries[]={{.name="gpu_virtio.interrupt",.call=gpu_virtio_interrupt,.major=1}};
static void initialize(void){assert(!dma_list&&!owned&&!dma_live&&!owner_pins);state=(__typeof__(state)){0};model_controller=(struct controller){0};model_info=(struct gpu_virtio_info){.hardware_index=7,.state=GPU_DISCOVERED};model_controller.info=&model_info;model_controller.layout=(struct virtio_layout){.common={.bar=4,.length=56,.present=1},.notify={.bar=4,.offset=4096,.length=4096,.present=1},.isr={.bar=4,.offset=8192,.length=1,.present=1},.device={.bar=4,.offset=12288,.length=16,.present=1},.notify_multiplier=4};memset(registers,0,sizeof(registers));*(uint16_t *)(registers+18)=2;*(uint16_t *)(registers+24)=8;*(uint32_t *)(registers+12296)=1;*(uint32_t *)(registers+12300)=virgl?1:0;
 model_hub=(struct kernel_surfaces){.count=1,.methods_address=(uintptr_t)entries};state.hub=&model_hub;state.heap=1;state.module_owner=42;state.pin=pin_model;state.controllers=&model_controller;state.table=(struct gpu_virtio_table){.count=1,.devices=&model_info};state.cpu=cpu_model;state.irq_context=cpu_model;state.clock=clock_model;state.thread=thread_model;state.claim=claim_model;state.master=master_model;state.release=release_model;state.dma_alloc=dma_alloc_model;state.dma_free=dma_free_model;state.alloc=alloc_model;state.drop=drop_model;state.msix=state.irq_enable=state.irq_release=state.dma_ready=good_model;state.iommu_info=iommu_model;state.irq_route=route_model;model_time=0;model_iommu=0;model_access=1;pending=timeout_mode=bad_fence=bad_used=fail_dma=reject_create=wakes=waits=0;
}
static uint64_t open_gpu(void){uint64_t previous_interrupts=model_info.interrupts;struct gpu_virtio_info info;struct kernel_surface_result r=gpu_virtio_open(7,(uintptr_t)&info,0,0,0,0);assert(!r.status&&info.state==GPU_ONLINE&&info.accepted&(UINT64_C(1)<<32)&&info.interrupts==previous_interrupts+1);return r.value;}
int main(void){
 initialize();uint64_t cookie=open_gpu();assert(dma_live==1&&wakes==1&&waits==1);
 struct gpu_virtio_resource resource;assert(!gpu_virtio_create(cookie,32,24,(uintptr_t)&resource,0,0).status&&resource.bytes==32*24*4&&dma_live==2);memset((void *)resource.pixels,0x7f,resource.bytes);
 assert(!gpu_virtio_readback(cookie,resource.id,0,0,0,0).status&&((unsigned char *)resource.pixels)[0]==0x5a);
 struct gpu_virtio_present present={.cookie=cookie,.resource=resource.id,.width=32,.height=24};assert(!gpu_virtio_present((uintptr_t)&present,0,0,0,0,0).status);unsigned before=model_info.commands;present.width=33;assert(gpu_virtio_present((uintptr_t)&present,0,0,0,0,0).status&&model_info.commands==before);
 assert(!gpu_virtio_destroy(cookie,resource.id,0,0,0,0).status&&dma_live==1&&!model_info.resources);
 struct gpu_virtio_capset cap={0};assert(!gpu_virtio_capset(cookie,(uintptr_t)&cap,0,0,0,0).status&&cap.bytes==64);unsigned char data[64];assert(!gpu_virtio_capset(cookie,(uintptr_t)&cap,(uintptr_t)data,sizeof(data),0,0).status&&data[0]==0x5a);
 struct kernel_surface_result context=gpu_virtio_context(cookie,0,0,0,0,0);assert(!context.status&&context.value);uint32_t nop=0;assert(!gpu_virtio_submit(cookie,context.value,(uintptr_t)&nop,4,0,0).status);assert(!gpu_virtio_texture(cookie,16,16,2,(uintptr_t)&resource,0).status&&dma_live==2);
 assert(gpu_virtio_upload(cookie,context.value,resource.id,0,0,0).status);assert(!gpu_virtio_attach(cookie,context.value,resource.id,1,0,0).status);assert(!gpu_virtio_upload(cookie,context.value,resource.id,0,0,0).status);
 assert(gpu_virtio_readback(cookie,resource.id,context.value+1,0,0,0).status);assert(!gpu_virtio_readback(cookie,resource.id,context.value,0,0,0).status&&((unsigned char *)resource.pixels)[0]==0x5a);
 assert(!gpu_virtio_context(cookie,context.value,0,0,0,0).status&&!model_info.contexts&&!model_controller.attachments);assert(!gpu_virtio_destroy(cookie,resource.id,0,0,0,0).status&&dma_live==1);
 assert(!gpu_virtio_texture(cookie,16,16,1,(uintptr_t)&resource,1).status&&!resource.pixels&&!resource.bytes&&dma_live==1);assert(gpu_virtio_readback(cookie,resource.id,0,0,0,0).status);assert(!gpu_virtio_destroy(cookie,resource.id,0,0,0,0).status);
 reject_create=1;assert(gpu_virtio_create(cookie,16,16,(uintptr_t)&resource,0,0).status&&dma_live==1&&!model_info.resources&&model_info.state==GPU_ONLINE);reject_create=0;
 assert(!gpu_virtio_close(cookie,0,0,0,0,0).status&&!dma_live&&!owned);assert(gpu_virtio_create(cookie,16,16,(uintptr_t)&resource,0,0).status);
 uint64_t reopened=open_gpu();assert(reopened!=cookie&&owner_pins==1);assert(!gpu_virtio_close(reopened,0,0,0,0,0).status&&!owner_pins);
 for(unsigned mode=0;mode<3;mode++){initialize();cookie=open_gpu();timeout_mode=mode==0;bad_fence=mode==1;bad_used=mode==2;assert(gpu_virtio_display(cookie,(uintptr_t)&(struct vg_display){0},sizeof(struct vg_display),0,0,0).status&&model_info.state==GPU_FAILED&&dma_live==1&&owned);assert(!gpu_virtio_close(cookie,0,0,0,0,0).status&&!dma_live&&!owned);}
 initialize();fail_dma=1;struct gpu_virtio_info info;assert(gpu_virtio_open(7,(uintptr_t)&info,0,0,0,0).status&&!owned&&!dma_live);
 initialize();model_iommu=IOMMU_INTEL;model_access=0;assert(gpu_virtio_open(7,(uintptr_t)&info,0,0,0,0).status&&info.error==GPU_FEATURES&&!dma_live&&!owned&&!owner_pins);
 initialize();model_iommu=IOMMU_AMD;cookie=open_gpu();assert(!gpu_virtio_close(cookie,0,0,0,0,0).status&&!owner_pins);
 virgl=0;initialize();cookie=open_gpu();assert(gpu_virtio_context(cookie,0,0,0,0,0).status&&!model_info.virgl);assert(gpu_virtio_readback(cookie,1,0,0,0,0).status);assert(!gpu_virtio_close(cookie,0,0,0,0,0).status);
#ifdef CB_GPU_TEST_PROFILE
 assert(traced_commands>0 && traced_blocks>0);printf("PASS GPU fence trace hook: %u successful commands, %u blocked waits\n",traced_commands,traced_blocks);
#endif
 puts("PASS virtio-gpu driver: IRQ wake/fences, resources/readback, capsets, contexts/submit, invalid rectangles, rollback, stale cookies, timeout/bad completion DMA retention and reset cleanup");
 return 0;
}
