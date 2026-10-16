#include "xstate.h"
static struct xstate_info layout;
static unsigned ready;
static void cpuid(uint32_t leaf,uint32_t sub,uint32_t *a,uint32_t *b,uint32_t *c,uint32_t *d) {
    __asm__ volatile("cpuid":"=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d):"a"(leaf),"c"(sub):"memory");
}
struct kernel_surface_result xstate_cpu_init(MODULE_ARGS) {
    MODULE_UNUSED();uint32_t aa,bb,cc,dd,max;cpuid(0,0,&max,&bb,&cc,&dd);cpuid(1,0,&aa,&bb,&cc,&dd);
    if((dd&((1u<<0)|(1u<<24)|(1u<<25)|(1u<<26)))!=((1u<<0)|(1u<<24)|(1u<<25)|(1u<<26)))return module_result(0,0);
    int xsave=max>=0xd && (cc&(1u<<26)),avx=cc&(1u<<28);
    uint64_t cr0,cr4;__asm__ volatile("mov %%cr0,%0;mov %%cr4,%1":"=r"(cr0),"=r"(cr4));
    cr0=(cr0|34u)&~UINT64_C(12);cr4|=(1u<<9)|(1u<<10);
    if(xsave)cr4|=1u<<18;else cr4&=~(UINT64_C(1)<<18);
    __asm__ volatile("mov %0,%%cr0;mov %1,%%cr4"::"r"(cr0),"r"(cr4):"memory");
    struct xstate_info out={.mask=3,.bytes=512,.alignment=64,.mode=XSTATE_FXSAVE};
    if(xsave) {
        cpuid(0xd,0,&aa,&bb,&cc,&dd);if((aa&3)!=3)return module_result(0,0);
        if(avx && (aa&4))out.mask|=4;
        __asm__ volatile("xsetbv"::"c"(0),"a"((uint32_t)out.mask),"d"(0):"memory");
        cpuid(0xd,0,&aa,&bb,&cc,&dd);if(bb<576 || bb>UINT32_MAX-63)return module_result(0,0);
        out.bytes=(bb+63)&~63u;out.mode=XSTATE_XSAVE;
        if(out.mask&4) {cpuid(0xd,2,&aa,&bb,&cc,&dd);out.avx_offset=bb;out.avx_bytes=aa;if(bb<576 || aa!=256 || bb>out.bytes || aa>out.bytes-bb)return module_result(0,0);}
    }
    if(__atomic_load_n(&ready,__ATOMIC_ACQUIRE)) {
        if(layout.mask!=out.mask || layout.bytes!=out.bytes || layout.mode!=out.mode || layout.avx_offset!=out.avx_offset || layout.avx_bytes!=out.avx_bytes)return module_result(0,0);
    } else {layout=out;__atomic_store_n(&ready,1,__ATOMIC_RELEASE);}
    __asm__ volatile("fninit":::"memory");uint32_t mxcsr=0x1f80;__asm__ volatile("ldmxcsr %0"::"m"(mxcsr):"memory");return module_result(out.bytes,1);
}
static int valid(uintptr_t buffer,size_t bytes) {return ready && buffer && !(buffer&63) && bytes>=layout.bytes;}
struct kernel_surface_result xstate_info(MODULE_ARGS) {MODULE_UNUSED();if(!ready || !a)return module_result(0,0);*(struct xstate_info *)a=layout;
    return module_result(1,1);}
struct kernel_surface_result xstate_init(MODULE_ARGS) {
    MODULE_UNUSED();if(!valid(a,b))return module_result(0,0);
    unsigned char *out=(void *)a;for(size_t i=0;i<layout.bytes;++i)out[i]=0;
    /* Standard initial FXSAVE image; XSAVE's zero XSTATE_BV initializes all
     * requested components without inheriting the creator's registers. */
    *(uint16_t *)out=0x37f;*(uint32_t *)(out+24)=0x1f80;*(uint32_t *)(out+28)=0xffbf;
    return module_result(1,1);
}
struct kernel_surface_result xstate_save(MODULE_ARGS) {
    MODULE_UNUSED();if(!valid(a,b))return module_result(0,0);
    if(layout.mode==XSTATE_XSAVE)__asm__ volatile("xsave64 (%0)"::"r"(a),"a"((uint32_t)layout.mask),"d"(0):"memory");
    else __asm__ volatile("fxsave64 (%0)"::"r"(a):"memory");
    return module_result(1,1);
}
struct kernel_surface_result xstate_restore(MODULE_ARGS) {
    MODULE_UNUSED();if(!valid(a,b))return module_result(0,0);
    if(layout.mode==XSTATE_XSAVE)__asm__ volatile("xrstor64 (%0)"::"r"(a),"a"((uint32_t)layout.mask),"d"(0):"memory");
    else __asm__ volatile("fxrstor64 (%0)"::"r"(a):"memory");
    return module_result(1,1);
}
