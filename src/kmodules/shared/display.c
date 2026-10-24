#include "io.h"
#include "../../boot/limine.h"
#include "../../interfaces/display.h"
#include "mem-pages.h"
#include "display-boot.h"
static struct framebuffer screen;
static uint64_t boot_frames;
static uint64_t owner;static const struct kernel_surfaces *display_hub;
/* Resident I/O-backend state, never exposed through a handle. The shadow is
 * allocated lazily; a full initial paint avoids reading slow framebuffer RAM. */
static struct mem_pages shadow_pages;
static uint32_t *shadow;
static unsigned shadow_valid;
#ifndef CB_DISPLAY_READ_PIXEL
#define CB_DISPLAY_READ_PIXEL(p) (*(p))
#endif
static int allocate_shadow(void){
    if(shadow)return 1;
    if(!display_hub || !screen.width || !screen.height || screen.height>SIZE_MAX/screen.width/4)return 0;
    size_t bytes=(size_t)screen.width*screen.height*4;if(bytes>SIZE_MAX-4095)return 0;
    kernel_surface_call allocate=kernel_surface_find(display_hub,"mem.pages_alloc");
    if(!allocate || allocate((bytes+4095)/4096,0,(uintptr_t)&shadow_pages,0,0,0).status)return 0;
    shadow=(void *)shadow_pages.address;return shadow!=NULL;
}
static int synchronize_shadow(void){
    if(!allocate_shadow())return 0;
    if(shadow_valid)return 1;
    for(uint32_t y=0;y<screen.height;++y){const volatile uint32_t *row=(const void *)((uintptr_t)screen.address+y*screen.pitch);for(uint32_t x=0;x<screen.width;++x)shadow[(size_t)y*screen.width+x]=CB_DISPLAY_READ_PIXEL(row+x);}
    shadow_valid=1;return 1;
}
int display_init(const struct framebuffer_response *r){
    boot_frames=0;
    if(!r || !r->count || !r->frames || !r->frames[0])return 0;
    screen=*r->frames[0];if(screen.red_shift>24 || screen.green_shift>24 || screen.blue_shift>24)return 0;uint32_t red=255u<<screen.red_shift,green=255u<<screen.green_shift,blue=255u<<screen.blue_shift;if((red&green) || (red&blue) || (green&blue))return 0;int valid=screen.address && screen.width && screen.height && screen.width<=UINT32_MAX && screen.height<=UINT32_MAX && screen.bpp==32 && screen.model==1 && screen.red_size==8 && screen.green_size==8 && screen.blue_size==8 && screen.red_shift<=24 && screen.green_shift<=24 && screen.blue_shift<=24 && screen.width<=UINT64_MAX/4 && screen.pitch>=screen.width*4 && screen.height<=UINTPTR_MAX/screen.pitch && screen.height*screen.pitch<=UINTPTR_MAX-(uintptr_t)screen.address;boot_frames=valid?r->count:0;return valid;
}
struct kernel_surface_result display_surface_bind(MODULE_ARGS){MODULE_UNUSED();if(!a)return module_result(0,0);const char *name=(const void *)a,*expected="display";while(*name && *name==*expected){++name;++expected;}if(!*name && !*expected)owner=b;return module_result(1,1);}
static void copy(void *dst,const void *src,size_t n){unsigned char *d=dst;const unsigned char *s=src;while(n--)*d++=*s++;}
int display_invoke(struct io_invocation *r){
    if((r->operation<CB_IO_DISPLAY_INFO || r->operation>CB_IO_DISPLAY_BLIT) && r->operation!=CB_IO_DISPLAY_COPY)return 0;
    r->status=CB_DENIED;if(!owner || owner!=r->process)return 1;r->status=CB_BAD_REQUEST;
    if(r->operation==CB_IO_DISPLAY_INFO){if(!r->bytes && r->capacity>=sizeof(struct cb_display_info)){struct cb_display_info info={screen.width,screen.height};copy(r->output,&info,sizeof(info));r->actual=sizeof(info);r->status=CB_OK;}return 1;}
    if(r->operation==CB_IO_DISPLAY_COPY){
        struct cb_display_copy box;if(r->bytes!=sizeof(box))return 1;copy(&box,r->input,sizeof(box));
        if(box.x>screen.width || box.y>screen.height || box.source_x>screen.width || box.source_y>screen.height || box.width>screen.width-box.x || box.width>screen.width-box.source_x || box.height>screen.height-box.y || box.height>screen.height-box.source_y)return 1;
        if(!box.width || !box.height){r->status=CB_OK;return 1;}
        if(!synchronize_shadow()){r->status=CB_UNAVAILABLE;return 1;}
        int reverse_y=box.y>box.source_y,reverse_x=box.y==box.source_y && box.x>box.source_x;
        for(uint32_t i=0;i<box.height;++i){uint32_t y=reverse_y?box.height-1-i:i;
            uint32_t *out=shadow+(size_t)(box.y+y)*screen.width+box.x;
            const uint32_t *in=shadow+(size_t)(box.source_y+y)*screen.width+box.source_x;
            uint32_t first=box.width,last=0;
            for(uint32_t j=0;j<box.width;++j){uint32_t x=reverse_x?box.width-1-j:j,pixel=in[x];if(out[x]!=pixel){out[x]=pixel;if(x<first)first=x;if(x>last)last=x;}}
            if(first<box.width){volatile uint32_t *row=(void *)((uintptr_t)screen.address+(box.y+y)*screen.pitch+(uint64_t)box.x*4);for(uint32_t x=first;x<=last;++x)row[x]=out[x];}
        }r->status=CB_OK;return 1;
    }
    struct cb_rectangle box;if(r->bytes<sizeof(box))return 1;copy(&box,r->input,sizeof(box));
    if(box.reserved || box.x>screen.width || box.y>screen.height || box.width>screen.width-box.x || box.height>screen.height-box.y || box.color>0xffffff)return 1;
    uint64_t pixels=(uint64_t)box.width*box.height;if(pixels>SIZE_MAX/4)return 1;
    if(r->bytes!=sizeof(box)+(r->operation==CB_IO_DISPLAY_BLIT?pixels*4:0))return 1;
    (void)allocate_shadow();
    const unsigned char *data=(const unsigned char *)r->input+sizeof(box);
    if(r->operation==CB_IO_DISPLAY_FILL){
        uint32_t rgb=box.color,packed=((rgb>>16&255)<<screen.red_shift)|((rgb>>8&255)<<screen.green_shift)|((rgb&255)<<screen.blue_shift);
        for(uint32_t y=0;y<box.height;++y){volatile uint32_t *row=(void *)((uintptr_t)screen.address+(box.y+y)*screen.pitch);for(uint32_t x=0;x<box.width;++x){if(shadow)shadow[(size_t)(box.y+y)*screen.width+box.x+x]=packed;row[box.x+x]=packed;}}
        if(shadow && !box.x && !box.y && box.width==screen.width && box.height==screen.height)shadow_valid=1;
        r->status=CB_OK;return 1;
    }
    for(uint32_t y=0;y<box.height;++y){volatile uint32_t *row=(void *)((uintptr_t)screen.address+(box.y+y)*screen.pitch);for(uint32_t x=0;x<box.width;++x){uint32_t rgb=box.color;if(r->operation==CB_IO_DISPLAY_BLIT)copy(&rgb,data+((size_t)y*box.width+x)*4,4);uint32_t packed=((rgb>>16&255)<<screen.red_shift)|((rgb>>8&255)<<screen.green_shift)|((rgb&255)<<screen.blue_shift);if(shadow)shadow[(size_t)(box.y+y)*screen.width+box.x+x]=packed;row[box.x+x]=packed;}}
    if(shadow && !box.x && !box.y && box.width==screen.width && box.height==screen.height)shadow_valid=1;
    r->status=CB_OK;return 1;
}


struct kernel_surface_result display_surface_init(MODULE_ARGS){MODULE_UNUSED();if(display_hub)return module_result(1,display_hub==(const void *)a);if(!a || !c)return module_result(0,0);if(!display_init((const void *)c))return module_result(0,0);display_hub=(const void *)a;return module_result(1,1);}
struct kernel_surface_result display_surface_invoke(MODULE_ARGS){MODULE_UNUSED();if(!a)return module_result(0,0);return module_result(1,display_invoke((void *)a));}

struct kernel_surface_result display_surface_boot_info(MODULE_ARGS){
    MODULE_UNUSED();if(!boot_frames || !a || b!=sizeof(struct display_boot_info) || c || d || e || f)return module_result(0,0);
    *(struct display_boot_info *)a=(struct display_boot_info){boot_frames,screen.width,screen.height,screen.pitch};return module_result(boot_frames,1);
}
