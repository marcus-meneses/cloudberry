#ifndef CLOUDBERRY_VIRTIO_PCI_H
#define CLOUDBERRY_VIRTIO_PCI_H
#include <stdint.h>
#include <stddef.h>
struct virtio_region {uint32_t offset,length;uint8_t bar,present;uint16_t reserved;};
struct virtio_layout {struct virtio_region common,notify,isr,device;uint32_t notify_multiplier;};
/* Read-only conventional PCI configuration snapshot. Bounds/cycles, duplicate
 * capabilities and minimum modern interface sizes are checked before MMIO. */
int virtio_pci_layout(const unsigned char config[256],struct virtio_layout *);
int virtio_region_fits(const struct virtio_region *,uint64_t bar_bytes);
int virtio_notify_offset(const struct virtio_layout *,uint16_t queue_offset,uint64_t *);
#define VIRTQ_MAX_DEPTH 8u
struct virtq_descriptor {uint64_t address;uint32_t bytes;uint16_t flags,next;};
struct virtq_used_entry {uint32_t id,bytes;};
struct virtq_avail {uint16_t flags,index,ring[VIRTQ_MAX_DEPTH],used_event;};
struct virtq_used {uint16_t flags,index;struct virtq_used_entry ring[VIRTQ_MAX_DEPTH];uint16_t avail_event;};
struct virtq {volatile struct virtq_descriptor *desc;volatile struct virtq_avail *avail;volatile struct virtq_used *used;uint16_t depth,produced,consumed;uint32_t response_capacity;unsigned pending,failed;};
int virtq_setup(struct virtq *,void *memory,size_t,uint16_t);
int virtq_publish(struct virtq *,uint64_t request,size_t,uint64_t response,size_t);
/* 0 pending, 1 completed, -1 malformed. One two-descriptor chain at a time.
 * Malformed rings are terminal until device reset and queue reinitialization. */
int virtq_complete(struct virtq *,uint32_t *response_bytes);
#endif
