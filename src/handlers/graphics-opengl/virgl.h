#ifndef CLOUDBERRY_VIRGL_ENCODER_H
#define CLOUDBERRY_VIRGL_ENCODER_H
#include <stdint.h>
#include <stddef.h>
/* Private pair implementation, not an application API. Stable VirGL commands
 * for level-0 BGRA/BGRX 2D resources. Encoding never uses the FPU. */
struct cb_virgl {uint32_t *words;size_t count,capacity;};
struct cb_virgl_box {uint32_t x,y,width,height;};
uint32_t cb_virgl_unorm8(unsigned);
int cb_virgl_surface(struct cb_virgl *,uint32_t handle,uint32_t resource,uint32_t format);
int cb_virgl_framebuffer(struct cb_virgl *,uint32_t surface);
int cb_virgl_clear(struct cb_virgl *,uint32_t argb);
/* BLIT alpha argument is reserved and must be zero. Source-over uses the
 * explicit alpha shader below, including on hosts that ignore BLIT blending. */
int cb_virgl_blit(struct cb_virgl *,uint32_t dst,uint32_t dst_format,struct cb_virgl_box,
                  uint32_t src,uint32_t src_format,struct cb_virgl_box,int alpha);
int cb_virgl_alpha_init(struct cb_virgl *);
int cb_virgl_alpha(struct cb_virgl *,uint32_t dst,uint32_t width,uint32_t height,struct cb_virgl_box,uint32_t src,struct cb_virgl_box);
#endif
