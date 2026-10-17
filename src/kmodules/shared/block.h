#ifndef CLOUDBERRY_BLOCK_H
#define CLOUDBERRY_BLOCK_H
#include "module.h"
/* Trusted kernel contract, version 1.1. Logical sectors, never byte addresses.
 * read(cookie,lba,count,destination,capacity,0) returns completed bytes, including
 * a partial prefix on failure. Zero-count reads succeed. No retained buffers.
 * control(cookie,BLOCK_RETAIN/RELEASE,0...) pins descriptor lifetime, not device
 * presence. Owner close refuses retained/in-flight instances. Detached identities
 * fail; replacement media never inherits a cookie. Call from a BSP thread, IF=1.
 * Providers take an output capacity when returning the ABI1.1 tail; zero
 * capacity requests the original 48-byte read-only descriptor.
 * READ_ONLY means no write surface; max_transfer_bytes is a per-call ceiling. */
/* write has read's arguments, with a source buffer. flush(cookie,0...) is a
 * durability/order barrier; successful write alone need not be persistent. */
enum block_control {BLOCK_RETAIN=1,BLOCK_RELEASE};
#define BLOCK_READ_ONLY 1u
struct block_device {
    uint16_t major,minor;uint32_t block_bytes,max_transfer_bytes,flags;
    uint64_t blocks,cookie;
    kernel_surface_call read,control;
    /* ABI 1.1 tail: inspect only when minor>=1. */
    kernel_surface_call write,flush;
    /* Stable root identity/range for overlap checks through partition views. */
    kernel_surface_call root_read;uint64_t root_cookie,first_block;
};
static inline int block_valid(const struct block_device *b) {
    return b && b->major==1 && b->read && b->control && b->cookie && b->blocks &&
        b->block_bytes>=512 && b->block_bytes<=4096 && !(b->block_bytes&(b->block_bytes-1)) &&
        b->max_transfer_bytes>=b->block_bytes && !(b->max_transfer_bytes%b->block_bytes) &&
        b->blocks<=UINT64_MAX/b->block_bytes;
}
static inline int block_writable(const struct block_device *b){return block_valid(b) && b->minor>=1 && !(b->flags&BLOCK_READ_ONLY) && b->write && b->flush;}
static inline void block_copy(struct block_device *out,const struct block_device *in){
    *out=(struct block_device){.major=in->major,.minor=in->minor,.block_bytes=in->block_bytes,.max_transfer_bytes=in->max_transfer_bytes,.flags=in->flags,.blocks=in->blocks,.cookie=in->cookie,.read=in->read,.control=in->control};
    out->root_read=in->read;out->root_cookie=in->cookie;
    if(in->minor>=1){out->write=in->write;out->flush=in->flush;out->root_read=in->root_read?in->root_read:in->read;out->root_cookie=in->root_read?in->root_cookie:in->cookie;out->first_block=in->first_block;}
}
#define BLOCK_V1_BYTES offsetof(struct block_device,write)
static inline int block_capacity(size_t bytes){return !bytes || bytes>=sizeof(struct block_device);}
static inline void block_zero(void *out,size_t capacity){unsigned char *p=out;size_t bytes=capacity?sizeof(struct block_device):BLOCK_V1_BYTES;while(bytes--)*p++=0;}
static inline void block_export(void *out,const struct block_device *source,size_t capacity){
    unsigned char *d=out;const unsigned char *s=(const void *)source;size_t n=capacity?sizeof(*source):BLOCK_V1_BYTES;while(n--)*d++=*s++;
    if(!capacity){((struct block_device *)out)->minor=0;((struct block_device *)out)->flags|=BLOCK_READ_ONLY;}
}
#endif
