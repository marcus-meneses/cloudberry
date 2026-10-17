#ifndef CLOUDBERRY_BLOCK_UTIL_H
#define CLOUDBERRY_BLOCK_UTIL_H
#include "block.h"
static inline uint16_t disk_u16(const void *data){const uint8_t *p=data;return p[0]|(uint16_t)p[1]<<8;}
static inline uint32_t disk_u32(const void *data){const uint8_t *p=data;return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline uint64_t disk_u64(const void *data){const uint8_t *p=data;return disk_u32(p)|(uint64_t)disk_u32(p+4)<<32;}
static inline void disk_copy(void *to,const void *from,size_t n){uint8_t *d=to;const uint8_t *s=from;while(n--)*d++=*s++;}
static inline int disk_equal(const void *a,const void *b,size_t n){const uint8_t *p=a,*q=b;while(n--)if(*p++!=*q++)return 0;return 1;}
static inline uint32_t disk_crc(uint32_t crc,const void *data,size_t n,uint32_t polynomial){const uint8_t *p=data;while(n--){crc^=*p++;for(unsigned i=0;i<8;++i)crc=(crc>>1)^((0u-(crc&1))&polynomial);}return crc;}
/* scratch holds one logical sector. Byte reads stay inside the descriptor and
 * split at the backend limit. Caller serializes use of its scratch buffer. */
static inline int block_bytes_read(const struct block_device *device,uint64_t offset,void *destination,size_t bytes,void *scratch) {
    uint64_t total=device->blocks*device->block_bytes;if(offset>total || bytes>total-offset || (bytes && !destination))return 0;
    uint8_t *out=destination;unsigned sector=device->block_bytes;
    while(bytes){unsigned within=offset%sector;size_t count;
        if(within || bytes<sector){struct kernel_surface_result r=device->read(device->cookie,offset/sector,1,(uintptr_t)scratch,sector,0);if(r.status || r.value!=sector)return 0;count=sector-within;if(count>bytes)count=bytes;disk_copy(out,(uint8_t *)scratch+within,count);}
        else {count=bytes-bytes%sector;if(count>device->max_transfer_bytes)count=device->max_transfer_bytes;
            struct kernel_surface_result r=device->read(device->cookie,offset/sector,count/sector,(uintptr_t)out,count,0);if(r.status || r.value!=count)return 0;}
        offset+=count;out+=count;bytes-=count;
    }return 1;
}
#endif
