#ifndef CLOUDBERRY_GL_DISPLAY_RENDER_H
#define CLOUDBERRY_GL_DISPLAY_RENDER_H
#include "../../interfaces/gpu.h"
#include "../../interfaces/display.h"
/* Adapter copies requests/results through /@io; no kernel addresses. Successful
 * replies must have exactly output_bytes bytes. Resource IDs stay display-owned. */
typedef cb_status_t (*cb_gl_io)(void *,const struct cb_io_gpu_request *,const void *,size_t,void *,size_t);
enum {CB_GL_TRANSFER_BYTES=12288};
struct cb_gl_texture;
typedef void *(*cb_gl_allocate)(void *,size_t);
typedef void (*cb_gl_release)(void *,void *);
struct cb_gl_display {
    cb_gl_io io;void *context;uint64_t cookie;
    uint32_t width,height,scanout,ctx,screen,scratch,solid,stage,stage_width,stage_height,frame;
    cb_status_t failure;unsigned dirty,composing,alpha_ready;
    cb_gl_allocate allocate;cb_gl_release release;void *heap;
    struct cb_gl_texture *textures;uint64_t surface_serial,compose_owner;
};
/* width=height=0 queries scanout geometry; scanout=UINT32_MAX selects the
 * first enabled scanout in that mode. Explicit sizes support isolated tests. */
cb_status_t cb_gl_display_open(struct cb_gl_display *,cb_gl_io,void *,uint64_t hardware,uint32_t width,uint32_t height,uint32_t scanout);
cb_status_t cb_gl_display_close(struct cb_gl_display *);
void cb_gl_display_heap(struct cb_gl_display *,cb_gl_allocate,cb_gl_release,void *);
cb_status_t cb_gl_surface_create(struct cb_gl_display *,uint64_t owner,uint32_t width,uint32_t height,uint64_t *);
cb_status_t cb_gl_surface_release(struct cb_gl_display *,uint64_t owner,uint64_t surface);
cb_status_t cb_gl_surface_owner_exit(struct cb_gl_display *,uint64_t owner);
cb_status_t cb_gl_surface_write(struct cb_gl_display *,uint64_t owner,const void *,size_t);
cb_status_t cb_gl_surface_patch(struct cb_gl_display *,uint64_t owner,const void *,size_t);
cb_status_t cb_gl_surface_compose(struct cb_gl_display *,uint64_t owner,const void *,size_t);
cb_status_t cb_gl_display_fill(struct cb_gl_display *,const struct cb_rectangle *);
cb_status_t cb_gl_display_copy(struct cb_gl_display *,const struct cb_display_copy *);
cb_status_t cb_gl_display_blit(struct cb_gl_display *,const struct cb_rectangle *,const void *,size_t);
cb_status_t cb_gl_display_present(struct cb_gl_display *);
/* Diagnostics only: read back a counted portion of the GPU target. */
cb_status_t cb_gl_display_batch(struct cb_gl_display *,const void *,size_t);
cb_status_t cb_gl_display_read(struct cb_gl_display *,uint64_t offset,void *,size_t);
int cb_gl_display_rectangle(const struct cb_gl_display *,const struct cb_rectangle *);
int cb_gl_display_copy_valid(const struct cb_gl_display *,const struct cb_display_copy *);
#endif
