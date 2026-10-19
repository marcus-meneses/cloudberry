#ifndef CLOUDBERRY_INTERFACE_DISPLAY_H
#define CLOUDBERRY_INTERFACE_DISPLAY_H
#include "handle.h"
#define CB_DISPLAY_NAME "/@display"
#define CB_DISPLAY_INTERFACE_ID {{0x43,0x42,0x44,0x49,0x53,0x31,0x46,0x3b,0x91,0xe1,0x40,0xa,0x1c,0x87,0xc0,0x7}}
enum {CB_DISPLAY_INFO=1,CB_DISPLAY_FILL,CB_DISPLAY_BLIT,CB_DISPLAY_COPY};
struct cb_display_info {uint32_t width,height;};
struct cb_rectangle {uint32_t x,y,width,height,color,reserved;};
/* COPY: overlap-safe copy within the display, coordinates only; no pointers. */
struct cb_display_copy {uint32_t x,y,source_x,source_y,width,height;};
/* BLIT: rectangle then width*height pixels, 0x00RRGGBB. No raw addresses. */
#endif
