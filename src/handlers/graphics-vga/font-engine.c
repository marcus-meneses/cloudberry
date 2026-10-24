#include "font-engine.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <string.h>
#include <limits.h>
struct cached_glyph {struct cached_glyph *next;uint32_t codepoint,size,width,height;uint64_t stamp;size_t bytes;unsigned char alpha[];};
static FT_Library library;static FT_Face face;
static struct cached_glyph *cache[257];static size_t cache_bytes;static uint64_t stamp;
static uint32_t active_size;
enum {CACHE_BYTES=4*1024*1024,DEFAULT_SIZE=16};
static void evict(void){struct cached_glyph **old=NULL;for(unsigned i=0;i<257;++i)for(struct cached_glyph **p=&cache[i];*p;p=&(*p)->next)if(!old || (*p)->stamp<(*old)->stamp)old=p;if(old){struct cached_glyph *p=*old;*old=p->next;cache_bytes-=p->bytes;free(p);}}
void font_engine_close(void){while(cache_bytes)evict();if(face)FT_Done_Face(face);if(library)FT_Done_FreeType(library);face=NULL;library=NULL;active_size=0;stamp=0;}
int font_engine_open(const void *data,size_t bytes){
    font_engine_close();if(!data || bytes<12 || bytes>LONG_MAX || FT_Init_FreeType(&library))return 0;
    if(FT_New_Memory_Face(library,data,(FT_Long)bytes,0,&face) || !FT_IS_SCALABLE(face) || FT_Select_Charmap(face,FT_ENCODING_UNICODE)){font_engine_close();return 0;}
    return 1;
}
cb_status_t font_engine_info(uint32_t size,struct cb_graphics_font *out){
    if(!face)return CB_UNAVAILABLE;
    if(!size)size=DEFAULT_SIZE;
    if(size<4 || size>128 || !out)return CB_BAD_REQUEST;
    if(size!=active_size){if(FT_Set_Pixel_Sizes(face,0,size))return CB_BAD_REQUEST;active_size=size;}
    *out=(struct cb_graphics_font){.cell_width=(uint32_t)((face->size->metrics.max_advance+63)/64),.cell_height=(uint32_t)((face->size->metrics.height+63)/64),.ascent=(uint32_t)((face->size->metrics.ascender+63)/64),.pixel_size=size};
    uint32_t full_height=(uint32_t)((face->size->metrics.ascender-face->size->metrics.descender+63)/64);if(full_height>out->cell_height)out->cell_height=full_height;
    if(!out->cell_width || !out->cell_height || out->cell_width>256 || out->cell_height>256)return CB_BAD_REQUEST;
    if(out->ascent>out->cell_height)out->ascent=out->cell_height;
    if(face->family_name){size_t n=strlen(face->family_name);if(n>=sizeof(out->family))n=sizeof(out->family)-1;memcpy(out->family,face->family_name,n);}return CB_OK;
}
cb_status_t font_engine_alpha(uint32_t cp,uint32_t size,const unsigned char **alpha,uint32_t *width,uint32_t *height){
    struct cb_graphics_font info;cb_status_t status=font_engine_info(size,&info);if(status)return status;size=info.pixel_size;unsigned bucket=(cp*33u+size)%257;
    for(struct cached_glyph *p=cache[bucket];p;p=p->next)if(p->codepoint==cp && p->size==size){p->stamp=++stamp;*alpha=p->alpha;*width=p->width;*height=p->height;return CB_OK;}
    FT_UInt index=FT_Get_Char_Index(face,cp);if(!index && cp){index=FT_Get_Char_Index(face,0xfffd);if(!index)index=FT_Get_Char_Index(face,'?');}
    if(FT_Load_Glyph(face,index,FT_LOAD_DEFAULT|FT_LOAD_NO_BITMAP) || FT_Render_Glyph(face->glyph,FT_RENDER_MODE_NORMAL))return CB_BAD_REQUEST;
    size_t bytes=sizeof(struct cached_glyph)+(size_t)info.cell_width*info.cell_height;while(cache_bytes+bytes>CACHE_BYTES && cache_bytes)evict();
    struct cached_glyph *p=calloc(1,bytes);if(!p)return CB_RESOURCE_FAILURE;p->bytes=bytes;p->codepoint=cp;p->size=size;p->width=info.cell_width;p->height=info.cell_height;p->stamp=++stamp;
    FT_GlyphSlot glyph=face->glyph;FT_Bitmap *bitmap=&glyph->bitmap;int left=((int)info.cell_width-(int)((glyph->advance.x+63)/64))/2+glyph->bitmap_left,top=(int)info.ascent-glyph->bitmap_top;
    for(unsigned y=0;y<bitmap->rows;++y){int row=top+(int)y;if(row<0 || row>=(int)info.cell_height)continue;const unsigned char *src=bitmap->pitch>=0?bitmap->buffer+(size_t)y*(unsigned)bitmap->pitch:bitmap->buffer+(size_t)(bitmap->rows-1-y)*(unsigned)(-bitmap->pitch);
        for(unsigned x=0;x<bitmap->width;++x){int col=left+(int)x;if(col<0 || col>=(int)info.cell_width)continue;unsigned a=bitmap->pixel_mode==FT_PIXEL_MODE_MONO?((src[x/8]>>(7-x%8))&1)*255:src[x];p->alpha[(size_t)row*info.cell_width+col]=(unsigned char)a;}}
    p->next=cache[bucket];cache[bucket]=p;cache_bytes+=bytes;*alpha=p->alpha;*width=p->width;*height=p->height;return CB_OK;
}
cb_status_t font_engine_draw(struct gfx_surface *s,const struct cb_glyph *g,uint32_t size){
    const unsigned char *alpha;uint32_t width,height;cb_status_t status=font_engine_alpha(g->codepoint,size,&alpha,&width,&height);if(status)return status;
    static uint32_t colors[256],last_fg,last_bg;static int ready;if(!ready || last_fg!=g->foreground || last_bg!=g->background){last_fg=g->foreground;last_bg=g->background;ready=1;for(unsigned a=0;a<256;++a){uint32_t rgb=0xff000000;for(unsigned shift=0;shift<24;shift+=8)rgb|=((((g->foreground>>shift)&255)*a+((g->background>>shift)&255)*(255-a)+127)/255)<<shift;colors[a]=rgb;}}
    int direct=s->matrix[0]==65536 && !s->matrix[1] && !s->matrix[2] && s->matrix[3]==65536 && !s->matrix[4] && !s->matrix[5];uint64_t changed=0;int32_t min_x=INT32_MAX,min_y=INT32_MAX,max_x=0,max_y=0;
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){int32_t dx=(int32_t)g->x+(int32_t)x,dy=(int32_t)g->y+(int32_t)y;uint32_t color=colors[alpha[(size_t)y*width+x]];
        if(!direct){gfx_pixel(s,dx,dy,color);continue;}
        if(dx<0 || dy<0 || dx>=(int32_t)s->width || dy>=(int32_t)s->height || dx<s->clip.x || dy<s->clip.y || dx>=(int64_t)s->clip.x+s->clip.width || dy>=(int64_t)s->clip.y+s->clip.height)continue;
        uint32_t *pixel=s->pixels+(size_t)dy*s->width+dx;if(*pixel==color)continue;*pixel=color;++changed;if(dx<min_x)min_x=dx;if(dy<min_y)min_y=dy;if(dx>max_x)max_x=dx;if(dy>max_y)max_y=dy;
    }if(changed){gfx_damage(s,(struct gfx_rect){min_x,min_y,max_x-min_x+1,max_y-min_y+1});s->generation=changed>UINT64_MAX-s->generation?UINT64_MAX:s->generation+changed;}return CB_OK;
}
