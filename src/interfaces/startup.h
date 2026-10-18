#ifndef CLOUDBERRY_INTERFACE_STARTUP_H
#define CLOUDBERRY_INTERFACE_STARTUP_H
#include "handle.h"
/* Readonly copied startup data. Entries are prefix, name bytes, file bytes.
 * No kernel addresses, capabilities or implicit runtime resource grants. */
struct cb_startup {struct cb_version version;uint64_t bytes,files;};
struct cb_startup_file {uint64_t name_bytes,data_bytes;};
static inline void cb_startup_copy(void *dst,const void *src,size_t n){unsigned char *d=(unsigned char *)dst;const unsigned char *s=(const unsigned char *)src;for(size_t i=0;i<n;++i)d[i]=s[i];}
static inline int cb_startup_find(const void *packet,size_t bytes,const char *name,const void **data,size_t *data_bytes){
    if(!packet || !name || bytes<sizeof(struct cb_startup))return 0;
    struct cb_startup h;cb_startup_copy(&h,packet,sizeof(h));
    if(h.version.bytes!=sizeof(h) || h.version.major!=1 || h.version.minor || h.bytes!=bytes || h.files>(bytes-sizeof(h))/sizeof(struct cb_startup_file))return 0;
    size_t wanted=0;while(name[wanted])++wanted;size_t at=sizeof(h);const unsigned char *p=(const unsigned char *)packet;const void *found=NULL;size_t found_bytes=0;
    for(uint64_t i=0;i<h.files;++i){struct cb_startup_file f;if(at>bytes || sizeof(f)>bytes-at)return 0;cb_startup_copy(&f,p+at,sizeof(f));at+=sizeof(f);
        if(!f.name_bytes || f.name_bytes>bytes-at || f.data_bytes>bytes-at-f.name_bytes)return 0;
        int same=f.name_bytes==wanted;for(size_t k=0;same && k<wanted;++k)if(p[at+k]!=(unsigned char)name[k])same=0;
        if(same){if(found)return 0;found=p+at+f.name_bytes;found_bytes=f.data_bytes;}
        at+=f.name_bytes+f.data_bytes;
    }
    if(at!=bytes || !found)return 0;
    if(data)*data=found;
    if(data_bytes)*data_bytes=found_bytes;
    return 1;
}
#endif
