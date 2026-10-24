/* Explicit source-over. Older VirGL hosts decode the BLIT alpha flag without
 * actually blending on their framebuffer-copy path. These small TGSI shaders
 * use vertex ID, texel fetch and ordinary blend state; no vertex DMA is needed. */
static uint32_t ratio_bits(int64_t value,uint32_t denominator){
    if(!value)return 0;
    uint32_t sign=value<0?UINT32_C(0x80000000):0;uint64_t n=value<0?-value:value,den=denominator;int exponent=0;
    while(n<den){n<<=1;--exponent;}while(n>=den*2){den<<=1;++exponent;}
    uint64_t mantissa=((n<<23)+den/2)/den;if(mantissa==(1u<<24)){mantissa>>=1;++exponent;}
    return sign|((uint32_t)(127+exponent)<<23)|((uint32_t)mantissa&0x7fffff);
}
static int shader(struct cb_virgl *s,uint32_t id,uint32_t type,const char *text,size_t bytes){
    size_t n=(bytes+3)/4;uint32_t *p=reserve(s,n+6);if(!p)return 0;
    p[0]=1u|(4u<<8)|((uint32_t)(n+5)<<16);p[1]=id;p[2]=type;p[3]=bytes;p[4]=512;
    unsigned char *out=(void *)(p+6);for(size_t i=0;i<bytes;++i)out[i]=(unsigned char)text[i];return 1;
}
static int bind_object(struct cb_virgl *s,unsigned type,unsigned id){uint32_t *p=reserve(s,2);if(!p)return 0;p[0]=2|(type<<8)|(1u<<16);p[1]=id;return 1;}
static int destroy_object(struct cb_virgl *s,unsigned type,unsigned id){uint32_t *p=reserve(s,2);if(!p)return 0;p[0]=3|(type<<8)|(1u<<16);p[1]=id;return 1;}
int cb_virgl_alpha_init(struct cb_virgl *s){
    if(!s)return 0;
    size_t before=s->count;
    static const char vs[]="VERT\nDCL SV[0], VERTEXID\nDCL OUT[0], POSITION\nDCL CONST[0..1]\nDCL TEMP[0]\nIMM[0] INT32 {1, 0, 0, 0}\nIMM[1] FLT32 {0.0, 0.0, 0.0, 0.0}\n0: MOV TEMP[0], IMM[1]\n1: AND TEMP[0].x, SV[0].xxxx, IMM[0].xxxx\n2: USHR TEMP[0].y, SV[0].xxxx, IMM[0].xxxx\n3: U2F TEMP[0].xy, TEMP[0]\n4: MAD OUT[0], TEMP[0], CONST[0], CONST[1]\n5: END\n";
    static const char fs[]="FRAG\nDCL IN[0], POSITION, LINEAR\nDCL OUT[0], COLOR\nDCL SAMP[0]\nDCL SVIEW[0], 2D, FLOAT\nDCL CONST[0]\nDCL TEMP[0]\n0: ADD TEMP[0], IN[0], CONST[0]\n1: F2I TEMP[0], TEMP[0]\n2: TXF OUT[0], TEMP[0], SAMP[0], 2D\n3: END\n";
    if(!shader(s,3,0,vs,sizeof(vs)) || !shader(s,4,1,fs,sizeof(fs)))goto fail;
    uint32_t *p=reserve(s,12);if(!p)goto fail;p[0]=1|(1u<<8)|(11u<<16);p[1]=5;p[4]=1|(3u<<4)|(19u<<9)|(1u<<17)|(19u<<22)|(15u<<27);
    p=reserve(s,10);if(!p)goto fail;p[0]=1|(2u<<8)|(9u<<16);p[1]=6;p[2]=2u|(1u<<29);p[3]=p[6]=0x3f800000;
    p=reserve(s,6);if(!p)goto fail;p[0]=1|(3u<<8)|(5u<<16);p[1]=7;
    p=reserve(s,10);if(!p)goto fail;p[0]=1|(7u<<8)|(9u<<16);p[1]=8;
    p=reserve(s,2);if(!p)goto fail;p[0]=1|(5u<<8)|(1u<<16);p[1]=10;
    return 1;
fail:s->count=before;return 0;
}
int cb_virgl_alpha(struct cb_virgl *s,uint32_t dst,uint32_t width,uint32_t height,struct cb_virgl_box d,uint32_t src,struct cb_virgl_box b){
    if(!s || !width || !height || !dst || !src || dst==src || !box_valid(d) || !box_valid(b) || d.width!=b.width || d.height!=b.height || d.x>width || d.y>height || d.width>width-d.x || d.height>height-d.y)return 0;
    size_t before=s->count;
    if(!cb_virgl_surface(s,2,dst,2) || !cb_virgl_framebuffer(s,2) || !bind_object(s,1,5) || !bind_object(s,2,6) || !bind_object(s,3,7) || !bind_object(s,5,10))goto fail;
    for(unsigned type=0;type<2;++type){uint32_t *p=reserve(s,3);if(!p)goto fail;p[0]=31|(2u<<16);p[1]=3+type;p[2]=type;}
    uint32_t *p=reserve(s,8);if(!p)goto fail;p[0]=4|(7u<<16);p[2]=p[5]=ratio_bits(width,2);p[3]=p[6]=ratio_bits(height,2);p[4]=0x3f800000;
    p=reserve(s,11);if(!p)goto fail;p[0]=12|(10u<<16);p[3]=ratio_bits((uint64_t)d.width*2,width);p[4]=ratio_bits((uint64_t)d.height*2,height);p[7]=ratio_bits((int64_t)d.x*2-width,width);p[8]=ratio_bits((int64_t)d.y*2-height,height);p[10]=0x3f800000;
    p=reserve(s,7);if(!p)goto fail;p[0]=12|(6u<<16);p[1]=1;p[3]=ratio_bits((int64_t)b.x-d.x,1);p[4]=ratio_bits((int64_t)b.y-d.y,1);p[6]=0xbf800000;
    p=reserve(s,7);if(!p)goto fail;p[0]=1|(6u<<8)|(6u<<16);p[1]=9;p[2]=src;p[3]=1;p[6]=0x688;
    p=reserve(s,4);if(!p)goto fail;p[0]=10|(3u<<16);p[1]=1;p[3]=9;
    p=reserve(s,4);if(!p)goto fail;p[0]=18|(3u<<16);p[1]=1;p[3]=8;
    p=reserve(s,13);if(!p)goto fail;p[0]=8|(12u<<16);p[2]=4;p[3]=5;p[5]=1;p[11]=3;
    p=reserve(s,4);if(!p)goto fail;p[0]=10|(3u<<16);p[1]=1;
    if(!destroy_object(s,6,9) || !cb_virgl_framebuffer(s,1) || !destroy_object(s,8,2))goto fail;
    return 1;
fail:s->count=before;return 0;
}
