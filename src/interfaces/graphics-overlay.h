#ifndef CLOUDBERRY_GRAPHICS_OVERLAY_H
#define CLOUDBERRY_GRAPHICS_OVERLAY_H
#include "handle.h"
/* Reserved to trusted boot providers by installation admission. Composition
 * uses the graphics backing store; overlays never overwrite underlying pixels. */
#define CB_GRAPHICS_OVERLAY_NAME "/@graphics-overlay"
#define CB_GRAPHICS_OVERLAY_INTERFACE_ID {{0x43,0x42,0x47,0x4f,0x56,0x31,0x46,0x3b,0x91,0xe1,0x40,0x0a,0x1c,0x87,0xc0,0x18}}
enum {CB_OVERLAY_INFO=1,CB_OVERLAY_SET,CB_OVERLAY_MOVE,CB_OVERLAY_HIDE};
struct cb_overlay_set {int32_t x,y;uint32_t width,height;}; /* then ARGB pixels */
struct cb_overlay_move {int32_t x,y;};
#endif
