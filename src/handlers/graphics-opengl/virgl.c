#include "virgl.h"
/* Packet layouts: virglrenderer virgl_protocol.h (MIT), upstream blob
 * 98aa431eaf60512fc22ba2a3540ff8c1402078a0. Independent bounded encoder.
 * https://chromium.googlesource.com/chromiumos/third_party/virglrenderer/+/d74a494ac29e003aef9f7044f810f2fdce4508f7/src/virgl_protocol.h
 */
static uint32_t *reserve(struct cb_virgl *s,size_t n){
    if(!s || !s->words || s->count>s->capacity || n>s->capacity-s->count)return NULL;
    uint32_t *out=s->words+s->count;for(size_t i=0;i<n;++i)out[i]=0;s->count+=n;return out;
}
uint32_t cb_virgl_unorm8(unsigned value){
    if(!value)return 0;
    if(value>=255)return UINT32_C(0x3f800000);
    unsigned shift=0;while((value<<shift)<255)++shift;
    uint32_t mantissa=(uint32_t)((((uint64_t)value<<(23+shift))+127)/255);
    return ((127-shift)<<23)|(mantissa&0x7fffff);
}
int cb_virgl_surface(struct cb_virgl *s,uint32_t handle,uint32_t resource,uint32_t format){
    if(!handle || !resource || (format!=1 && format!=2))return 0;
    uint32_t *p=reserve(s,6);if(!p)return 0;p[0]=1u|(8u<<8)|(5u<<16);p[1]=handle;p[2]=resource;p[3]=format;return 1;
}
int cb_virgl_framebuffer(struct cb_virgl *s,uint32_t surface){
    uint32_t *p=reserve(s,surface?4:3);if(!p)return 0;p[0]=5u|((surface?3u:2u)<<16);p[1]=surface?1:0;if(surface)p[3]=surface;return 1;
}
int cb_virgl_clear(struct cb_virgl *s,uint32_t argb){
    uint32_t *p=reserve(s,9);if(!p)return 0;p[0]=7u|(8u<<16);p[1]=4; /* PIPE_CLEAR_COLOR0 */
    p[2]=cb_virgl_unorm8((argb>>16)&255);p[3]=cb_virgl_unorm8((argb>>8)&255);
    p[4]=cb_virgl_unorm8(argb&255);p[5]=cb_virgl_unorm8(argb>>24);return 1;
}
static int box_valid(struct cb_virgl_box b){return b.width && b.height && b.x<=INT32_MAX && b.y<=INT32_MAX && b.width<=(uint32_t)INT32_MAX-b.x && b.height<=(uint32_t)INT32_MAX-b.y;}
int cb_virgl_blit(struct cb_virgl *s,uint32_t dst,uint32_t df,struct cb_virgl_box d,uint32_t src,uint32_t sf,struct cb_virgl_box b,int alpha){
    if(!dst || !src || dst==src || (df!=1 && df!=2) || (sf!=1 && sf!=2) || !box_valid(d) || !box_valid(b) || alpha)return 0;
    uint32_t *p=reserve(s,22);if(!p)return 0;p[0]=16u|(21u<<16);p[1]=15u|((uint32_t)alpha<<12);
    p[4]=dst;p[6]=df;p[7]=d.x;p[8]=d.y;p[10]=d.width;p[11]=d.height;p[12]=1;
    p[13]=src;p[15]=sf;p[16]=b.x;p[17]=b.y;p[19]=b.width;p[20]=b.height;p[21]=1;return 1;
}

#include "virgl-alpha.inc"
