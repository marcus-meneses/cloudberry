#ifndef CLOUDBERRY_VIRTIO_GPU_WIRE_H
#define CLOUDBERRY_VIRTIO_GPU_WIRE_H
#include <stdint.h>
#include <stddef.h>
enum {VG_DISPLAY_INFO=0x100,VG_CREATE_2D,VG_UNREF,VG_SCANOUT,VG_FLUSH,VG_TRANSFER_2D,VG_ATTACH_BACKING,VG_DETACH_BACKING,VG_CAPSET_INFO,VG_CAPSET};
enum {VG_CTX_CREATE=0x200,VG_CTX_DESTROY,VG_CTX_ATTACH,VG_CTX_DETACH,VG_CREATE_3D,VG_TRANSFER_TO_3D,VG_TRANSFER_FROM_3D,VG_SUBMIT_3D};
enum {VG_OK=0x1100,VG_OK_DISPLAY,VG_OK_CAPSET_INFO,VG_OK_CAPSET,VG_ERROR=0x1200};
struct vg_header {uint32_t type,flags;uint64_t fence;uint32_t context,padding;};
struct vg_rect {uint32_t x,y,width,height;};
struct vg_display {struct vg_header h;struct {struct vg_rect rect;uint32_t enabled,flags;} modes[16];};
struct vg_create_2d {uint32_t resource,format,width,height;};
struct vg_ref {uint32_t resource,padding;};
struct vg_scanout {struct vg_rect rect;uint32_t scanout,resource;};
struct vg_flush {struct vg_rect rect;uint32_t resource,padding;};
struct vg_transfer {struct vg_rect rect;uint64_t offset;uint32_t resource,padding;};
struct vg_attach {uint32_t resource,count;uint64_t address;uint32_t length,padding;};
struct vg_cap_request {uint32_t id,version;};
struct vg_cap_info {struct vg_header h;uint32_t id,version,bytes,padding;};
struct vg_context {uint32_t name_bytes,init;char name[64];};
struct vg_texture {uint32_t resource,target,format,bind,width,height,depth,array_size,last_level,samples,flags,padding;};
struct vg_upload {uint32_t x,y,z,width,height,depth;uint64_t offset;uint32_t resource,level,stride,layer_stride;};
_Static_assert(sizeof(struct vg_texture)==48 && sizeof(struct vg_upload)==48,"virtio GPU 3D payloads");
struct vg_submit {uint32_t bytes,padding;};
_Static_assert(sizeof(struct vg_header)==24,"virtio GPU header");
_Static_assert(sizeof(struct vg_display)==408,"virtio GPU display modes");
_Static_assert(sizeof(struct vg_attach)==24,"virtio GPU backing attachment");
static inline int vg_reply_valid(const void *p,size_t n,uint64_t fence,uint32_t expected){if(n<sizeof(struct vg_header))return 0;const struct vg_header *h=p;return h->flags==1&&h->fence==fence&&h->type==expected;}
#endif
