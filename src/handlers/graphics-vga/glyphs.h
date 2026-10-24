#ifndef CLOUDBERRY_GRAPHICS_GLYPHS_H
#define CLOUDBERRY_GRAPHICS_GLYPHS_H
#include "raster.h"
#include "font-engine.h"
#include "../../interfaces/graphics.h"
static inline struct cb_glyph gfx_glyph_at(const unsigned char *p){struct cb_glyph g;unsigned char *out=(void *)&g;for(size_t i=0;i<sizeof(g);++i)out[i]=p[i];return g;}
/* Validate the whole packet before changing pixels. The provider presents the
 * resulting damage once; font interpretation remains in graphics. */
static inline cb_status_t gfx_glyphs(struct gfx_surface *surface,const unsigned char *p,size_t bytes){
    if(!bytes || bytes%sizeof(struct cb_glyph))return CB_BAD_REQUEST;
    for(size_t at=0;at<bytes;at+=sizeof(struct cb_glyph)){struct cb_glyph g=gfx_glyph_at(p+at);
        if(g.codepoint>0x10ffff || (g.codepoint>=0xd800 && g.codepoint<=0xdfff) || g.reserved || g.foreground>0xffffff || g.background>0xffffff || g.x>32759 || g.y>32751)return CB_BAD_REQUEST;
    }
    for(size_t at=0;at<bytes;at+=sizeof(struct cb_glyph)){struct cb_glyph g=gfx_glyph_at(p+at);cb_status_t status=font_engine_draw(surface,&g,0);if(status)return status;}
    return CB_OK;
}
#endif
