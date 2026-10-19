#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../handlers/graphics/raster.h"
#include "../handlers/graphics/glyphs.h"
#include "../apps/terminal/parser.h"
int main(void){assert(gfx_rgb(0x80ff0000)==0x00800000 && gfx_rgb(0x0000ff00)==0);uint32_t guarded[66*66];for(size_t i=0;i<66*66;++i)guarded[i]=0x12345678;struct gfx_surface s;gfx_init(&s,guarded+67,64,64);for(size_t i=0;i<64*64;++i)s.pixels[i]=0;gfx_line(&s,(struct gfx_point){-32768,-32768},(struct gfx_point){32767,32767},0xffffffff);assert(s.dirty && s.damage.x==0 && s.damage.y==0 && s.damage.width==64 && s.damage.height==64);assert(guarded[66]==0x12345678 && guarded[67+64*64]==0x12345678);uint64_t version=s.generation;gfx_reset_damage(&s);gfx_line(&s,(struct gfx_point){0,0},(struct gfx_point){63,63},0xffffffff);assert(!s.dirty && s.generation==version);
    gfx_clip(&s,(struct gfx_rect){10,10,4,4});gfx_rect(&s,(struct gfx_rect){0,0,64,64},0xff00ff00,1);assert(s.damage.x==10 && s.damage.y==10 && s.damage.width==4 && s.damage.height==4);gfx_clip(&s,(struct gfx_rect){0,0,64,64});int32_t matrix[6]={131072,0,0,131072,65536,65536};assert(gfx_transform(&s,matrix));uint32_t image[4]={0xffff0000,0xff00ff00,0xff0000ff,0xffffffff};gfx_bitmap(&s,(struct gfx_point){10,10},image,2,2,2,255);assert(s.pixels[21*64+21]==0xffff0000 && s.pixels[22*64+22]==0xffff0000 && s.pixels[24*64+24]==0xffffffff);int32_t identity[6]={65536,0,0,65536,0,0};gfx_transform(&s,identity);s.mode=GFX_SOURCE_OVER;s.pixels[0]=0xff0000ff;gfx_pixel(&s,0,0,0x80ff0000);assert(s.pixels[0]==0xff80007f);gfx_arc(&s,(struct gfx_rect){5,5,30,20},0,360*65536,0xffffffff,0);struct gfx_point curve[4]={{0,0},{10,60},{50,0},{63,63}};gfx_bezier(&s,curve,0xffffffff);
    uint32_t hole_pixels[64*64]={0};struct gfx_surface holes;gfx_init(&holes,hole_pixels,64,64);struct gfx_point polygon[]={{1,1},{50,1},{50,50},{1,50},{INT32_MIN,0},{10,10},{40,10},{40,40},{10,40}};gfx_polygon(&holes,polygon,9,0xffffffff,1);assert(hole_pixels[5*64+5]==0xffffffff && !hole_pixels[20*64+20]);holes.mode=GFX_SOURCE_OVER;gfx_arc(&holes,(struct gfx_rect){10,10,30,30},0,360*65536,0x80ff0000,1);assert(hole_pixels[25*64+25]==0x80ff0000);uint64_t old=holes.generation;gfx_pixel(&holes,25,25,0);assert(holes.generation==old);for(size_t i=0;i<67;++i)assert(guarded[i]==0x12345678);for(size_t i=67+4096;i<66*66;++i)assert(guarded[i]==0x12345678);
    uint32_t batch_pixels[32*32]={0},single_pixels[32*32]={0};struct gfx_surface batch,single;gfx_init(&batch,batch_pixels,32,32);gfx_init(&single,single_pixels,32,32);
    struct cb_glyph glyphs[3]={{0,0,'A',0xffffff,0,0},{8,0,'B',0xffffff,0,0},{0,0,' ',0,0x220000,0}};
    glyphs[2].reserved=1;assert(gfx_glyphs(&batch,(void *)glyphs,sizeof(glyphs))==CB_BAD_REQUEST && !batch.dirty);for(unsigned i=0;i<32*32;++i)assert(!batch_pixels[i]);glyphs[2].reserved=0;
    assert(gfx_glyphs(&batch,(void *)glyphs,0)==CB_BAD_REQUEST && gfx_glyphs(&batch,(void *)glyphs,sizeof(glyphs)-1)==CB_BAD_REQUEST);
    unsigned char unaligned[sizeof(glyphs)+1];memcpy(unaligned+1,glyphs,sizeof(glyphs));assert(gfx_glyphs(&batch,unaligned+1,sizeof(glyphs))==CB_OK);
    for(unsigned i=0;i<3;++i){assert(gfx_glyphs(&single,(void *)(glyphs+i),sizeof(*glyphs))==CB_OK);}
    assert(!memcmp(batch_pixels,single_pixels,sizeof(batch_pixels)) && batch_pixels[0]==0xff220000 && batch.damage.width==16 && batch.damage.height==16);
    /* Compare overlapping copies with the general snapshot path in every
     * direction, including clipping and unchanged-pixel damage accounting. */
    struct gfx_point destinations[]={{0,0},{0,2},{2,0},{-2,-2},{0,0}};
    struct gfx_rect sources[]={{0,2,30,30},{0,0,30,30},{0,0,30,30},{0,0,30,30},{0,0,32,32}};
    for(unsigned trial=0;trial<5;++trial){uint32_t saved[32*32];for(unsigned i=0;i<32*32;++i)batch_pixels[i]=single_pixels[i]=0xff000000|i;gfx_init(&batch,batch_pixels,32,32);gfx_init(&single,single_pixels,32,32);gfx_clip(&batch,(struct gfx_rect){1,1,30,30});gfx_clip(&single,(struct gfx_rect){1,1,30,30});struct gfx_rect from=sources[trial];for(int y=0;y<from.height;++y)memcpy(saved+y*from.width,single_pixels+(from.y+y)*32+from.x,from.width*4);gfx_bitmap(&single,destinations[trial],saved,from.width,from.height,from.width,255);assert(gfx_self_blit(&batch,destinations[trial],from,255) && !memcmp(batch_pixels,single_pixels,sizeof(batch_pixels)) && batch.generation==single.generation && batch.dirty==single.dirty && !memcmp(&batch.damage,&single.damage,sizeof(batch.damage)));}
    assert(!gfx_self_blit(&batch,(struct gfx_point){0,0},(struct gfx_rect){0,0,32,32},128));batch.mode=GFX_SOURCE_OVER;assert(!gfx_self_blit(&batch,(struct gfx_point){0,0},(struct gfx_rect){0,0,32,32},255));
    /* Identity rectangle fast paths must match the polygon reference exactly,
     * including clips, preexisting damage, alpha and generation saturation. */
    for(unsigned trial=0;trial<80;++trial){
        for(unsigned i=0;i<32*32;++i)batch_pixels[i]=single_pixels[i]=0xff000000|((i+trial)%13);
        gfx_init(&batch,batch_pixels,32,32);gfx_init(&single,single_pixels,32,32);
        struct gfx_rect clip={2,3,27,25},r={(int)(trial%11)-4,(int)(trial%9)-3,(int)(trial%35),(int)(trial%31)};
        gfx_clip(&batch,clip);gfx_clip(&single,clip);batch.mode=single.mode=trial%2;
        uint32_t color=batch.mode?0xff112233:0x80112233;
        batch.generation=single.generation=trial&1?UINT64_MAX-5:0;
        gfx_damage(&batch,(struct gfx_rect){1,1,1,1});gfx_damage(&single,(struct gfx_rect){1,1,1,1});
        gfx_rect(&batch,r,color,1);struct gfx_point polygon[4]={{r.x,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height},{r.x,r.y+r.height}};
        if(r.width>0 && r.height>0)gfx_polygon(&single,polygon,4,color,1);
        assert(!memcmp(batch_pixels,single_pixels,sizeof(batch_pixels)) && batch.generation==single.generation && !memcmp(&batch.damage,&single.damage,sizeof(batch.damage)));
    }
    assert(gfx_self_blit(&batch,(struct gfx_point){30000,0},(struct gfx_rect){0,0,4,4},255)==(batch.mode==GFX_SOURCE));
    /* Uniform presentation uses the final RGB values, including transparent
     * black, and never accepts out-of-bounds or mixed-color regions. */
    uint32_t rgb=0xdeadbeef;for(unsigned i=0;i<32*32;++i)batch_pixels[i]=i&1?0xff000000:0;
    assert(gfx_uniform_rgb(&batch,(struct gfx_rect){0,0,32,32},&rgb) && rgb==0);
    batch_pixels[31*32+31]=0xffffffff;
    assert(!gfx_uniform_rgb(&batch,(struct gfx_rect){0,0,32,32},&rgb));
    assert(gfx_uniform_rgb(&batch,(struct gfx_rect){0,0,31,32},&rgb) && rgb==0);
    assert(!gfx_uniform_rgb(&batch,(struct gfx_rect){-1,0,32,32},&rgb));
    assert(!gfx_uniform_rgb(&batch,(struct gfx_rect){0,0,33,32},&rgb));
    assert(!gfx_uniform_rgb(&batch,(struct gfx_rect){0,0,0,32},&rgb));
    char storage[256];struct term_token tokens[128];size_t n;assert(term_tokens("echo 'hello |> world' |> cat => /a",storage,tokens,128,&n) && n==6 && !strcmp(tokens[1].text,"hello |> world") && tokens[2].kind==TERM_PIPE && tokens[4].kind==TERM_RECREATE);assert(!term_tokens("echo 'unterminated",storage,tokens,128,&n));assert(term_tokens("echo \"\" ->file",storage,tokens,128,&n) && n==4 && !*tokens[1].text && tokens[2].kind==TERM_APPEND);puts("PASS raster/parser: clipping, guards, unchanged-pixel damage, transformed bitmaps, alpha, curves and quoted routing");}
