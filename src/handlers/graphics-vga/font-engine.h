#ifndef CLOUDBERRY_FONT_ENGINE_H
#define CLOUDBERRY_FONT_ENGINE_H
#include "raster.h"
#include "../../interfaces/graphics.h"
int font_engine_open(const void *,size_t);
void font_engine_close(void);
cb_status_t font_engine_info(uint32_t pixels,struct cb_graphics_font *);
cb_status_t font_engine_draw(struct gfx_surface *,const struct cb_glyph *,uint32_t pixels);
/* The same rendered coverage cells support host tests/OCR; no target pointers
 * or FreeType structs are exposed through the public graphics ABI. */
cb_status_t font_engine_alpha(uint32_t codepoint,uint32_t pixels,const unsigned char **,uint32_t *,uint32_t *);
#endif
