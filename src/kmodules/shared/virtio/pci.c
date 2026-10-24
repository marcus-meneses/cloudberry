#include "pci.h"
static uint32_t le32(const unsigned char *p){
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
int virtio_pci_layout(const unsigned char c[256],struct virtio_layout *out){
    *out=(struct virtio_layout){
        0
    };
    if(!(c[6]&16))return 0;
    unsigned at=c[0x34];
    uint64_t seen=0;
    while(at){
        if(at<0x40||at>0xfc||(at&3))return 0;
        uint64_t bit=UINT64_C(1)<<(at/4);
        if(seen&bit)return 0;
        seen|=bit;
        if(c[at]==9){
            unsigned length=c[at+2],kind=c[at+3];
            if(length<4||length>256-at)return 0;
            struct virtio_region *region=kind==1?&out->common:kind==2?&out->notify:kind==3?&out->isr:kind==4?&out->device:NULL;
            if(region){
                if(region->present||length<(kind==2?20u:16u)||c[at+4]>=6)return 0;
                *region=(struct virtio_region){
                    .offset=le32(c+at+8),.length=le32(c+at+12),.bar=c[at+4],.present=1
                };
                if(!region->length||region->offset>UINT32_MAX-region->length)return 0;
                if(kind==1&&((region->offset&3)||region->length<56))return 0;
                if(kind==2){
                    if(region->offset&1)return 0;
                    out->notify_multiplier=le32(c+at+16);
                }
                if(kind==4&&((region->offset&3)||region->length<16))return 0;
            }
        }
        at=c[at+1];
    }
    return out->common.present&&out->notify.present&&out->isr.present&&out->device.present;
}
int virtio_region_fits(const struct virtio_region *r,uint64_t size){
    return r->present&&r->bar<6&&r->offset<=size&&r->length<=size-r->offset;
}
int virtio_notify_offset(const struct virtio_layout *l,uint16_t queue_offset,uint64_t *out){
    uint64_t offset=(uint64_t)queue_offset*l->notify_multiplier;
    if(offset&1||offset>l->notify.length||2>l->notify.length-offset)return 0;
    *out=offset;
    return 1;
}
int virtq_setup(struct virtq *q,void *memory,size_t n,uint16_t depth){
    if(!q||!memory||((uintptr_t)memory&15)||n<1024||depth<2||depth>VIRTQ_MAX_DEPTH||(depth&(depth-1)))return 0;
    unsigned char *p=memory;
    for(size_t i=0;i<1024;i++)p[i]=0;
    *q=(struct virtq){
        .desc=(void *)p,.avail=(void *)(p+256),.used=(void *)(p+512),.depth=depth
    };
    return 1;
}
int virtq_publish(struct virtq *q,uint64_t request,size_t bytes,uint64_t response,size_t capacity){
    if(!q||q->failed||q->pending||!bytes||bytes>UINT32_MAX||capacity<24||capacity>UINT32_MAX||request>UINT64_MAX-bytes||response>UINT64_MAX-capacity)return 0;
    q->desc[0]=(struct virtq_descriptor){
        request,bytes,1,1
    };
    q->desc[1]=(struct virtq_descriptor){
        response,capacity,2,0
    };
    q->response_capacity=capacity;
    q->pending=1;
    q->avail->ring[q->produced%q->depth]=0;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    q->avail->index=++q->produced;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return 1;
}
int virtq_complete(struct virtq *q,uint32_t *bytes){
    if(!q||q->failed)return -1;
    uint16_t index=q->used->index;
    if(index==q->consumed)return 0;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if(!q->pending||(uint16_t)(index-q->consumed)!=1)goto bad;
    struct virtq_used_entry e=q->used->ring[q->consumed%q->depth];
    if(e.id||e.bytes<24||e.bytes>q->response_capacity)goto bad;
    ++q->consumed;
    q->pending=0;
    *bytes=e.bytes;
    return 1;
    bad:q->failed=1;
    return -1;
}
