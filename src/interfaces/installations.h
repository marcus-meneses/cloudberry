#ifndef CLOUDBERRY_INTERFACE_INSTALLATIONS_H
#define CLOUDBERRY_INTERFACE_INSTALLATIONS_H
#include "handle.h"
enum {CB_INSTALL_APPLICATION=1,CB_INSTALL_LAUNCHER=2,CB_INSTALL_MANAGER=4,CB_INSTALL_PROVIDER=8};
struct cb_install_catalog {struct cb_version version;uint64_t bytes,count;};
struct cb_install_record {uint64_t bytes,name_bytes,path_bytes,manifest_bytes;uint8_t installation[16];uint32_t flags,cpu;uint64_t stack_bytes;};
static inline void cb_install_copy(void *d,const void *s,size_t n){unsigned char *a=(unsigned char *)d;const unsigned char *b=(const unsigned char *)s;for(size_t i=0;i<n;++i)a[i]=b[i];}
static inline int cb_install_equal(const void *a,const void *b,size_t n){const unsigned char *x=(const unsigned char *)a,*y=(const unsigned char *)b;for(size_t i=0;i<n;++i)if(x[i]!=y[i])return 0;return 1;}
static inline int cb_install_valid(const void *data,size_t bytes){
    if(!data || bytes<sizeof(struct cb_install_catalog))return 0;
    struct cb_install_catalog h;cb_install_copy(&h,data,sizeof(h));if(h.version.bytes!=sizeof(h) || h.version.major!=1 || h.version.minor || h.bytes!=bytes || !h.count || h.count>(bytes-sizeof(h))/sizeof(struct cb_install_record))return 0;
    const unsigned char *p=(const unsigned char *)data;size_t at=sizeof(h);unsigned managers=0;
    for(uint64_t i=0;i<h.count;++i){struct cb_install_record r;if(sizeof(r)>bytes-at)return 0;cb_install_copy(&r,p+at,sizeof(r));if(r.bytes<sizeof(r) || r.bytes>bytes-at || !r.name_bytes || !r.path_bytes || !r.manifest_bytes || !cb_frame_fits(sizeof(r),r.name_bytes,r.path_bytes,r.bytes) || r.manifest_bytes!=r.bytes-sizeof(r)-r.name_bytes-r.path_bytes || !r.stack_bytes || (r.flags!=CB_INSTALL_APPLICATION && r.flags!=(CB_INSTALL_APPLICATION|CB_INSTALL_LAUNCHER) && r.flags!=CB_INSTALL_MANAGER && r.flags!=CB_INSTALL_PROVIDER))return 0;
        if(r.flags==CB_INSTALL_MANAGER)++managers;
        unsigned any=0;for(unsigned k=0;k<16;++k)any|=r.installation[k];if(!any)return 0;
        const unsigned char *name=p+at+sizeof(r),*path=name+r.name_bytes;
        for(size_t k=0;k<r.name_bytes;++k){unsigned char c=name[k];if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='-'))return 0;}
        if(path[0]!='/')return 0;
        size_t component=1;for(size_t k=1;k<=r.path_bytes;++k){if(k==r.path_bytes || path[k]=='/'){size_t n=k-component;if(!n || (n==1 && path[component]=='.') || (n==2 && path[component]=='.'&&path[component+1]=='.'))return 0;component=k+1;}else if(!path[k])return 0;}
        for(size_t prev=sizeof(h);prev<at;){struct cb_install_record q;cb_install_copy(&q,p+prev,sizeof(q));if(cb_install_equal(q.installation,r.installation,16) || (q.name_bytes==r.name_bytes && cb_install_equal(p+prev+sizeof(q),name,r.name_bytes)))return 0;prev+=q.bytes;}
        at+=r.bytes;
    }return at==bytes && managers==1;
}
/* Use only after full catalog validation. Returned spans remain catalog-owned. */
static inline const unsigned char *cb_install_find(const void *catalog,const void *name,size_t bytes,const uint8_t *identity,struct cb_install_record *out){
    struct cb_install_catalog h;cb_install_copy(&h,catalog,sizeof(h));const unsigned char *p=(const unsigned char *)catalog;size_t at=sizeof(h);
    for(uint64_t i=0;i<h.count;++i){struct cb_install_record r;cb_install_copy(&r,p+at,sizeof(r));const unsigned char *n=p+at+sizeof(r);if(identity?cb_install_equal(identity,r.installation,16):(r.name_bytes==bytes && cb_install_equal(n,name,bytes))){*out=r;return n;}at+=r.bytes;}return NULL;
}
#endif
