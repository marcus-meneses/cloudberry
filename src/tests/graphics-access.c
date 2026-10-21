#include <assert.h>
#include <stdio.h>
#define _start graphics_entry
#include "../handlers/graphics/graphics.c"
#undef _start
int main(void){
    uint64_t surface=4;struct cb_graphics_blit blit={.surface=4,.source=0};
    assert(!screen_access(CB_GRAPHICS_INFO,NULL,0));assert(!screen_access(CB_GRAPHICS_SURFACE_CREATE,NULL,0));
    assert(screen_access(CB_GRAPHICS_FILL,NULL,0));assert(screen_access(CB_GRAPHICS_GLYPH,NULL,0));
    assert(screen_access(CB_GRAPHICS_BLIT,(void *)&blit,sizeof(blit)));blit.source=5;assert(!screen_access(CB_GRAPHICS_BLIT,(void *)&blit,sizeof(blit)));
    blit.surface=0;assert(screen_access(CB_GRAPHICS_BLIT,(void *)&blit,sizeof(blit)));
    assert(!screen_access(CB_GRAPHICS_STATE,(void *)&surface,8));surface=0;assert(screen_access(CB_GRAPHICS_STATE,(void *)&surface,8));
    struct screen_context one={.owner=1,.clip={2,3,4,5},.matrix={131072,0,0,131072,7,8},.mode=GFX_SOURCE_OVER};contexts=&one;screen.raster.width=160;screen.raster.height=80;
    select_context(1);assert(screen.raster.clip.x==2 && screen.raster.matrix[0]==131072 && screen.raster.mode==GFX_SOURCE_OVER);
    select_context(2);assert(screen.raster.clip.x==0 && screen.raster.clip.width==160 && screen.raster.matrix[0]==65536 && screen.raster.matrix[4]==0 && screen.raster.mode==GFX_SOURCE);
    select_context(1);assert(screen.raster.clip.x==2 && screen.raster.matrix[4]==7);
    puts("PASS graphics screen authority: direct draws, screen targets and screen readback; owned offscreen work independent");
}
