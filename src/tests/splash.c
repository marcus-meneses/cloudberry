#include <assert.h>
#include <stdio.h>
#include "../kmodules/shared/splash.h"
static const struct {uint32_t width,height;uint8_t pixels[12];} image={2,2,{255,0,0,0,255,0,0,0,255,255,255,255}};
int main(void) {
    uint32_t storage[50];for (unsigned i=0;i<50;++i) storage[i]=0xdeadbeef;
    struct framebuffer fb={.address=storage+1,.width=6,.height=6,.pitch=32,.bpp=32,.model=1,
        .red_size=8,.green_size=8,.blue_size=8,.red_shift=16,.green_shift=8,.blue_shift=0};
    struct framebuffer *frames[]={&fb};struct framebuffer_response response={.count=1,.frames=frames};
    assert(splash_render(&response,(const void *)&image,sizeof(image)));
    for (unsigned y=0;y<6;++y) for (unsigned x=0;x<8;++x) {
        uint32_t expected=x>=6?0xdeadbeef:0;
        if (y==2 && x==2) expected=0xff0000;
        if (y==2 && x==3) expected=0x00ff00;
        if (y==3 && x==2) expected=0x0000ff;
        if (y==3 && x==3) expected=0xffffff;
        assert(storage[1+y*8+x]==expected);
    }
    assert(storage[0]==0xdeadbeef && storage[49]==0xdeadbeef);
    assert(!splash_render(NULL,(const void *)&image,sizeof(image)));
    assert(!splash_render(&response,(const void *)&image,sizeof(image)-1));
    fb.red_shift=8;assert(!splash_render(&response,(const void *)&image,sizeof(image)));fb.red_shift=16;
    fb.height=UINT64_MAX;assert(!splash_render(&response,(const void *)&image,sizeof(image)));fb.height=6;
    fb.pitch=31;assert(!splash_render(&response,(const void *)&image,sizeof(image)));fb.pitch=32;
    fb.bpp=24;assert(!splash_render(&response,(const void *)&image,sizeof(image)));fb.bpp=32;
    fb.address=NULL;assert(!splash_render(&response,(const void *)&image,sizeof(image)));fb.address=storage+1;
    fb.width=1;fb.height=1;fb.pitch=4;fb.red_shift=0;fb.blue_shift=16;
    assert(splash_render(&response,(const void *)&image,sizeof(image)) && storage[1]==255);
    assert(storage[0]==0xdeadbeef && storage[2]==0);
    puts("PASS: splash centering, RGB channels, padding/guard preservation, tiny framebuffer and invalid format/size rejection");
    return 0;
}
