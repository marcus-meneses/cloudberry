#include <assert.h>
#include <stdio.h>
#include "../kmodules/shared/xstate.c"
static _Alignas(64) unsigned char original[4096],one[4096],two[4096];
static void set(const uint64_t pattern[4],uint64_t fp,uint32_t mx,uint16_t cw) {
    __asm__ volatile("ldmxcsr %0;fldcw %1;fldl %2"::"m"(mx),"m"(cw),"m"(fp):"memory");
    if(layout.mask&4)__asm__ volatile("vmovdqu (%0),%%ymm0;vmovdqu (%0),%%ymm15"::"r"(pattern):"memory");
    else __asm__ volatile("movdqu (%0),%%xmm0;movdqu (%0),%%xmm15"::"r"(pattern):"memory");
}
static int check(const uint64_t expected[4],uint64_t fp,uint32_t mx,uint16_t cw) {
    uint64_t got[4],read_fp;uint32_t read_mx;uint16_t read_cw;
    if(layout.mask&4)__asm__ volatile("vmovdqu %%ymm0,%0":"=m"(got)::"memory");
    else __asm__ volatile("movdqu %%xmm0,%0":"=m"(got)::"memory");
    for(unsigned i=0;i<((layout.mask&4)?4u:2u);++i)if(got[i]!=expected[i])return 0;
    if(layout.mask&4)__asm__ volatile("vmovdqu %%ymm15,%0":"=m"(got)::"memory");
    else __asm__ volatile("movdqu %%xmm15,%0":"=m"(got)::"memory");
    for(unsigned i=0;i<((layout.mask&4)?4u:2u);++i)if(got[i]!=expected[i])return 0;
    __asm__ volatile("stmxcsr %0;fnstcw %1;fstl %2":"=m"(read_mx),"=m"(read_cw),"=m"(read_fp)::"memory");
    return read_mx==mx && read_cw==cw && read_fp==fp;
}
int main(void) {
    uint32_t a,b,c,d;cpuid(1,0,&a,&b,&c,&d);
    layout=(struct xstate_info){.mask=3,.bytes=512,.alignment=64,.mode=XSTATE_FXSAVE};
    if((c&((1u<<26)|(1u<<27)))==((1u<<26)|(1u<<27))) {
        uint32_t low,high;__asm__ volatile("xgetbv":"=a"(low),"=d"(high):"c"(0));(void)high;
        if((low&3)!=3)return 1;
        if((c&(1u<<28)) && (low&4))layout.mask=7;
        layout.mode=XSTATE_XSAVE;layout.bytes=576;
        if(layout.mask&4) {cpuid(0xd,2,&a,&b,&c,&d);if(b>sizeof(original) || a>sizeof(original)-b)return 1;layout.bytes=(a+b+63)&~63u;layout.avx_offset=b;layout.avx_bytes=a;}
    }
    ready=1;assert(!xstate_save((uintptr_t)original,sizeof(original),0,0,0,0).status);
    assert(!xstate_init((uintptr_t)one,sizeof(one),0,0,0,0).status && !xstate_init((uintptr_t)two,sizeof(two),0,0,0,0).status);
    assert(xstate_init((uintptr_t)one+1,sizeof(one),0,0,0,0).status && xstate_init((uintptr_t)one,1,0,0,0,0).status);
    uint64_t aa[4]={1,2,3,4},bb[4]={5,6,7,8};
    assert(!xstate_restore((uintptr_t)one,sizeof(one),0,0,0,0).status);set(aa,UINT64_C(0x3ff0000000000000),0x3f80,0x77f);
    assert(!xstate_save((uintptr_t)one,sizeof(one),0,0,0,0).status);
    assert(!xstate_restore((uintptr_t)two,sizeof(two),0,0,0,0).status);set(bb,UINT64_C(0x4000000000000000),0x5f80,0xb7f);
    assert(!xstate_save((uintptr_t)two,sizeof(two),0,0,0,0).status);
    for(unsigned i=0;i<100;++i) {
        assert(!xstate_restore((uintptr_t)one,sizeof(one),0,0,0,0).status && check(aa,UINT64_C(0x3ff0000000000000),0x3f80,0x77f));
        assert(!xstate_restore((uintptr_t)two,sizeof(two),0,0,0,0).status && check(bb,UINT64_C(0x4000000000000000),0x5f80,0xb7f));
    }
    assert(!xstate_restore((uintptr_t)original,sizeof(original),0,0,0,0).status);
    puts(layout.mask&4?"PASS host xstate: production XSAVE/XRSTOR preserves x87/SSE/AVX and control state":"PASS host xstate: x87/SSE preserved; AVX unavailable on host");
    return 0;
}
