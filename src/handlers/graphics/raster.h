#ifndef CLOUDBERRY_RASTER_H
#define CLOUDBERRY_RASTER_H
#include <stdint.h>
#include <stddef.h>
struct gfx_point {int32_t x,y;};
struct gfx_rect {int32_t x,y,width,height;};
/* Transform coefficients are signed 16.16, positions integer logical pixels.
 * Clip is in device pixels. All raster writes go through clipping and damage. */
struct gfx_surface {uint32_t *pixels;uint32_t width,height;struct gfx_rect clip,damage;int32_t matrix[6];uint64_t generation;unsigned dirty,mode;};
enum {GFX_SOURCE=0,GFX_SOURCE_OVER=1};
void gfx_init(struct gfx_surface *,uint32_t *,uint32_t,uint32_t);
uint32_t gfx_rgb(uint32_t);
/* Whether the displayed pixels in an in-bounds region have one RGB value. */
int gfx_uniform_rgb(const struct gfx_surface *,struct gfx_rect,uint32_t *);
void gfx_reset_damage(struct gfx_surface *);
void gfx_damage(struct gfx_surface *,struct gfx_rect);
int gfx_fill_rect(struct gfx_surface *,struct gfx_rect,uint32_t);
int gfx_copy_region(const struct gfx_surface *,struct gfx_point,struct gfx_rect,unsigned,struct gfx_point *,struct gfx_rect *);
int gfx_transform(struct gfx_surface *,const int32_t[6]);
void gfx_clip(struct gfx_surface *,struct gfx_rect);
struct gfx_point gfx_map(const struct gfx_surface *,struct gfx_point);
void gfx_pixel(struct gfx_surface *,int32_t,int32_t,uint32_t);
void gfx_line(struct gfx_surface *,struct gfx_point,struct gfx_point,uint32_t);
/* INT32_MIN x separates contours; fill uses the even-odd rule. */
void gfx_polygon(struct gfx_surface *,const struct gfx_point *,size_t,uint32_t,int);
void gfx_rect(struct gfx_surface *,struct gfx_rect,uint32_t,int);
void gfx_arc(struct gfx_surface *,struct gfx_rect,int32_t,int32_t,uint32_t,int);
void gfx_bezier(struct gfx_surface *,const struct gfx_point[4],uint32_t);
/* Source bitmap dimensions and stride are checked by the wire decoder.
 * Source/destination must not alias; provider snapshots self-blits. */
void gfx_bitmap(struct gfx_surface *,struct gfx_point,const uint32_t *,uint32_t,uint32_t,uint32_t,unsigned);
/* Overlap-safe, allocation-free SOURCE copy with identity transform/opacity.
 * Returns 0 when the caller must use its general snapshot/composition path. */
int gfx_self_blit(struct gfx_surface *,struct gfx_point,struct gfx_rect,unsigned);
#endif
