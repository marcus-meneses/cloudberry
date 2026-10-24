#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static unsigned framebuffer_reads;
static uint32_t read_pixel(const volatile uint32_t *p){++framebuffer_reads;return *p;}
#define CB_DISPLAY_READ_PIXEL(p) read_pixel(p)
#include "../kmodules/shared/display.c"
static uint32_t pixels[2+12*6],ram_shadow[8*6];
static void initialize(void){
    shadow=ram_shadow;shadow_valid=0;framebuffer_reads=0;
    for(unsigned i=0;i<2+12*6;++i)pixels[i]=0xdeadbeef;
    for(unsigned y=0;y<6;++y)for(unsigned x=0;x<8;++x)pixels[1+y*12+x]=y*8+x;
    static struct framebuffer frame;frame=(struct framebuffer){.address=pixels+1,.width=8,.height=6,.pitch=48,.bpp=32,.model=1,.red_size=8,.green_size=8,.blue_size=8,.red_shift=0,.green_shift=8,.blue_shift=16};
    struct framebuffer *frames[]={&frame};struct framebuffer_response response={.count=1,.frames=frames};assert(display_init(&response));display_surface_bind((uintptr_t)"display",7,0,0,0,0);
}
static unsigned invoke(struct cb_display_copy *box,uint64_t pid,size_t bytes){struct io_invocation r={.process=pid,.operation=CB_IO_DISPLAY_COPY,.input=box,.bytes=bytes};assert(display_invoke(&r));return r.status;}
int main(void){
    initialize();struct display_boot_info boot;assert(!display_surface_boot_info((uintptr_t)&boot,sizeof(boot),0,0,0,0).status && boot.frames==1 && boot.width==8 && boot.height==6 && boot.pitch==48);assert(display_surface_boot_info((uintptr_t)&boot,sizeof(boot)-1,0,0,0,0).status);
    struct cb_display_copy cases[]={{0,0,0,1,8,5},{0,1,0,0,8,5},{1,0,0,0,7,6},{0,0,1,0,7,6},{1,1,0,0,7,5},{8,6,8,6,0,0}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i){initialize();uint32_t expected[2+12*6],saved[48];memcpy(expected,pixels,sizeof(pixels));struct cb_display_copy box=cases[i];
        for(unsigned y=0;y<box.height;++y)for(unsigned x=0;x<box.width;++x)saved[y*box.width+x]=pixels[1+(box.source_y+y)*12+box.source_x+x];
        for(unsigned y=0;y<box.height;++y)for(unsigned x=0;x<box.width;++x)expected[1+(box.y+y)*12+box.x+x]=saved[y*box.width+x];
        assert(invoke(&box,7,sizeof(box))==CB_OK && !memcmp(pixels,expected,sizeof(pixels)));
    }
    initialize();uint32_t saved[2+12*6];memcpy(saved,pixels,sizeof(pixels));struct cb_display_copy box={0,0,0,1,8,5};
    assert(invoke(&box,6,sizeof(box))==CB_DENIED);assert(invoke(&box,7,sizeof(box)-1)==CB_BAD_REQUEST);
    box.width=UINT32_MAX;assert(invoke(&box,7,sizeof(box))==CB_BAD_REQUEST);box.width=8;box.source_y=2;assert(invoke(&box,7,sizeof(box))==CB_BAD_REQUEST);assert(!memcmp(saved,pixels,sizeof(pixels)));
    struct cb_rectangle fill={1,2,3,2,0x112233,0};struct io_invocation r={.process=7,.operation=CB_IO_DISPLAY_FILL,.input=&fill,.bytes=sizeof(fill)};assert(display_invoke(&r) && r.status==CB_OK);
    for(unsigned y=0;y<6;++y)for(unsigned x=0;x<12;++x)assert(pixels[1+y*12+x]==((x>=1 && x<4 && y>=2 && y<4)?0x332211:saved[1+y*12+x]));
    /* A complete paint seeds the shadow without a framebuffer read. Subsequent
     * overlapping copies read only RAM, including after a canonical RGB blit. */
    initialize();fill=(struct cb_rectangle){0,0,8,6,0x224466,0};r=(struct io_invocation){.process=7,.operation=CB_IO_DISPLAY_FILL,.input=&fill,.bytes=sizeof(fill)};assert(display_invoke(&r) && !r.status && shadow_valid && !framebuffer_reads);
    struct {struct cb_rectangle box;uint32_t pixels[2];} blit={{1,1,2,1,0,0},{0x112233,0xaabbcc}};
    r=(struct io_invocation){.process=7,.operation=CB_IO_DISPLAY_BLIT,.input=&blit,.bytes=sizeof(blit)};assert(display_invoke(&r) && !r.status);
    box=(struct cb_display_copy){1,2,1,1,2,1};assert(!invoke(&box,7,sizeof(box)) && !framebuffer_reads && pixels[1+2*12+1]==0x332211 && pixels[1+2*12+2]==0xccbbaa);
    shadow=NULL;display_hub=NULL;assert(invoke(&box,7,sizeof(box))==CB_UNAVAILABLE);
    assert(pixels[0]==0xdeadbeef && pixels[sizeof(pixels)/4-1]==0xdeadbeef);
    puts("PASS display copy: overlap directions, native format, pitch guards, zero size, malformed payload, bounds and installation owner");return 0;
}
