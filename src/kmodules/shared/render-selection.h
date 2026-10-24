#ifndef CLOUDBERRY_RENDER_SELECTION_H
#define CLOUDBERRY_RENDER_SELECTION_H
#include "hwenum.h"
#include "render-role.h"
/* Trusted driver contract. PROBE must not claim/reconfigure a GPU; a feature
 * query selector may be saved/restored within one bounded critical section.
 * A ready driver supplies both renderer and display facilities. RELEASE
 * is idempotent and restores the firmware scanout before VGA fallback. */
#define RENDER_PROBE_SURFACE "gpu.opengl.probe"
#define RENDER_RELEASE_SURFACE "gpu.opengl.release"
enum {RENDER_VGA=1,RENDER_OPENGL};
enum {RENDER_PROBING=1,RENDER_ACTIVE,RENDER_FAILED};
enum {RENDER_NO_GPU=1,RENDER_NO_DRIVER,RENDER_NO_PROVIDERS,RENDER_PROBE_FAILED,RENDER_INCOMPLETE,RENDER_START_FAILED,RENDER_CLEANUP_FAILED,RENDER_ACCELERATED};
enum {RENDER_GPU_PRESENT=1,RENDER_GPU_GRAPHICS=2,RENDER_GPU_FONT=4,RENDER_GPU_OVERLAY=8,RENDER_GPU_COMPLETE=15};
struct render_probe {uint32_t bytes;uint16_t major,minor;uint64_t hardware_index,capabilities;uint16_t vendor,device;uint32_t reserved;};
struct render_selection {uint32_t bytes;uint16_t major,minor;uint32_t state,backend,reason,reserved;uint64_t devices,candidates,probes,hardware_index;uint16_t vendor,device;uint32_t class_code;};
static inline int render_inventory_valid(const struct hwenum_table *table,size_t bytes){return table&&bytes>=sizeof(*table)&&table->magic==HWENUM_TABLE_MAGIC&&table->major==1&&table->header_bytes==sizeof(*table)&&table->entry_bytes==sizeof(struct hwenum_descriptor)&&table->count<=table->capacity&&table->count<=(bytes-sizeof(*table))/sizeof(struct hwenum_descriptor);}
/* Returns the selected pair, without loading providers or changing hardware. */
static inline unsigned render_select(const struct hwenum_table *table,size_t bytes,kernel_surface_call probe,int release_available,int providers_available,struct render_selection *out){
    *out=(struct render_selection){.bytes=sizeof(*out),.major=1,.state=RENDER_PROBING,.backend=RENDER_VGA,.reason=RENDER_NO_GPU,.hardware_index=UINT64_MAX};
    if(!render_inventory_valid(table,bytes))return RENDER_VGA;
    for(uint64_t i=0;i<table->count;i++){
        const struct hwenum_descriptor *d=table->entries+i;if(d->kind!=HWENUM_PCI||d->data.pci.class_code!=3)continue;++out->devices;
        if(out->hardware_index==UINT64_MAX){out->hardware_index=i;out->vendor=d->data.pci.vendor;out->device=d->data.pci.device;out->class_code=3u<<16|(uint32_t)d->data.pci.subclass<<8|d->data.pci.programming_interface;}
        /* All PCI display subclasses can be probed. A vendor/device name alone
         * never establishes rendering capability (including virtio-gpu). */
        if(!probe||!release_available){out->reason=RENDER_NO_DRIVER;continue;}
        struct render_probe p={.bytes=sizeof(p),.major=1,.hardware_index=i,.vendor=d->data.pci.vendor,.device=d->data.pci.device};++out->probes;
        struct kernel_surface_result r=probe((uintptr_t)&p,0,0,0,0,0);if(r.status){out->reason=RENDER_PROBE_FAILED;continue;}
        if(p.bytes!=sizeof(p)||p.major!=1||p.minor||p.reserved||p.hardware_index!=i||p.vendor!=d->data.pci.vendor||p.device!=d->data.pci.device||(p.capabilities&RENDER_GPU_COMPLETE)!=RENDER_GPU_COMPLETE){out->reason=RENDER_INCOMPLETE;continue;}
        ++out->candidates;if(!providers_available){out->reason=RENDER_NO_PROVIDERS;continue;}
        out->backend=RENDER_OPENGL;out->reason=RENDER_ACCELERATED;out->hardware_index=i;out->vendor=p.vendor;out->device=p.device;out->class_code=3u<<16|(uint32_t)d->data.pci.subclass<<8|d->data.pci.programming_interface;return RENDER_OPENGL;
    }return RENDER_VGA;
}
#endif
