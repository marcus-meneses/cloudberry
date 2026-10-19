#ifndef CLOUDBERRY_INTERFACE_GRAPHICS_H
#define CLOUDBERRY_INTERFACE_GRAPHICS_H
#include "handle.h"
#define CB_GRAPHICS_NAME "/@graphics"
#define CB_GRAPHICS_INTERFACE_ID {{0x43,0x42,0x47,0x46,0x58,0x31,0x46,0x3b,0x91,0xe1,0x40,0xa,0x1c,0x87,0xc0,0x8}}
enum {CB_GRAPHICS_INFO=1,CB_GRAPHICS_FILL,CB_GRAPHICS_GLYPH,CB_GRAPHICS_SURFACE_CREATE,CB_GRAPHICS_SURFACE_RELEASE,CB_GRAPHICS_SURFACE_INFO,CB_GRAPHICS_STATE,CB_GRAPHICS_LINE,CB_GRAPHICS_RECTANGLE,CB_GRAPHICS_ELLIPSE,CB_GRAPHICS_ARC,CB_GRAPHICS_BEZIER,CB_GRAPHICS_PATH,CB_GRAPHICS_BITMAP,CB_GRAPHICS_BLIT,CB_GRAPHICS_PRESENT,CB_GRAPHICS_GLYPHS};
/* GLYPHS: nonempty cb_glyph[] payload; validates all entries, draws in order,
 * then presents once. GLYPH retains its single-entry contract. */
struct cb_glyph {uint32_t x,y,codepoint,foreground,background,reserved;};
/* Surface 0 is the display-sized canvas; other IDs belong to their creator.
 * New primitives use ARGB straight alpha; legacy GLYPH/FILL use opaque RGB. */
struct cb_graphics_info {uint32_t width,height;uint64_t surfaces,pixel_bytes;};
struct cb_graphics_create {uint32_t width,height;};
struct cb_graphics_ref {uint64_t surface;};
struct cb_graphics_surface_info {uint64_t surface,generation;uint32_t width,height;int32_t damage_x,damage_y,damage_width,damage_height;uint32_t dirty,reserved;};
struct cb_graphics_state {uint64_t surface;int32_t matrix[6];int32_t clip_x,clip_y,clip_width,clip_height;uint32_t composition,reserved;};
struct cb_graphics_draw {uint64_t surface;uint32_t color,fill;int32_t x[4],y[4];};
/* RECTANGLE/ELLIPSE: x[0],y[0],x[1]=width,y[1]=height.
 * ARC adds x[2]=start angle,y[2]=sweep angle, degrees 16.16.
 * BEZIER uses four control points. PATH payload appends cb_graphics_segment[]. */
enum {CB_PATH_MOVE=1,CB_PATH_LINE,CB_PATH_CUBIC,CB_PATH_CLOSE};
struct cb_graphics_segment {uint32_t command,reserved;int32_t x[3],y[3];};
struct cb_graphics_bitmap {uint64_t surface;int32_t x,y;uint32_t width,height,stride,opacity;};
/* BITMAP inline stride*height uint32_t ARGB pixels. BLIT copies a source region
 * to a destination using its transform/clip/composition; self-blits snapshot. */
struct cb_graphics_blit {uint64_t surface,source;int32_t x,y,source_x,source_y;uint32_t width,height,opacity,reserved;};
/* PRESENT only accepts surface 0, transfers dirty pixels, retains damage on any
 * failed chunk. SURFACE_INFO observes damage without consuming it. */
#endif
