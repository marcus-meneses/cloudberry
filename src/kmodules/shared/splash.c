#include "splash.h"
int splash_validate(const struct framebuffer_response *response) {
    if (!response || !response->count || !response->frames) return 0;
    const struct framebuffer *fb=response->frames[0];
    if (!fb || !fb->address || ((uintptr_t)fb->address&3) || fb->model!=1 || fb->bpp!=32 ||
        !fb->width || !fb->height || fb->width>UINT64_MAX/4 || fb->pitch<fb->width*4 || (fb->pitch&3) ||
        fb->height>UINTPTR_MAX/fb->pitch || fb->height*fb->pitch>UINTPTR_MAX-(uintptr_t)fb->address ||
        fb->red_size!=8 || fb->green_size!=8 || fb->blue_size!=8 ||
        fb->red_shift>24 || fb->green_shift>24 || fb->blue_shift>24) return 0;
    uint32_t red=255u<<fb->red_shift,green=255u<<fb->green_shift,blue=255u<<fb->blue_shift;
    if ((red&green) || (red&blue) || (green&blue)) return 0;
    return 1;
}
int splash_render(const struct framebuffer_response *response,const uint32_t *image,size_t bytes) {
    if(!splash_validate(response) || !image || bytes<8)return 0;
    const struct framebuffer *fb=response->frames[0];
    uint64_t sw=image[0],sh=image[1];
    if (!sw || !sh || sw>(SIZE_MAX-8)/3/sh || 8+sw*sh*3>bytes) return 0;
    uint64_t max_width=fb->width*4/5,max_height=(fb->height/5)*4+(fb->height%5)*4/5;
    if (!max_width) max_width=1;
    if (!max_height) max_height=1;
    uint64_t width=sw<max_width?sw:max_width,height=width*sh/sw;
    if (!height) height=1;
    if (height>max_height) {height=max_height;width=height*sw/sh;if (!width) width=1;}
    const uint64_t left=(fb->width-width)/2,top=(fb->height-height)/2;
    const uint8_t *pixels=(const void *)(image+2);
    for (uint64_t y=0;y<fb->height;++y) {
        volatile uint32_t *row=(void *)((uintptr_t)fb->address+y*fb->pitch);
        for (uint64_t x=0;x<fb->width;++x) row[x]=0;
    }
    for (uint64_t y=0;y<height;++y) {
        volatile uint32_t *row=(void *)((uintptr_t)fb->address+(top+y)*fb->pitch);
        for (uint64_t x=0;x<width;++x) {
            const uint8_t *rgb=pixels+((y*sh/height)*sw+x*sw/width)*3;
            row[left+x]=((uint32_t)rgb[0]<<fb->red_shift)|((uint32_t)rgb[1]<<fb->green_shift)|((uint32_t)rgb[2]<<fb->blue_shift);
        }
    }
    return 1;
}
