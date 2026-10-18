#include <stdint.h>
static volatile uint64_t identity;
__attribute__((noreturn)) void _start(uint64_t argument){
    uint64_t mode=argument>>32;
    if(mode==1)*(volatile uint64_t *)UINT64_C(0xffffffffa1000000)=1;
    if(mode==2)__asm__ volatile("cli");
    if(mode==3){unsigned char code[8]={0xc3};((void (*)(void))code)();}
    if(mode==4)*(volatile unsigned char *)(uintptr_t)_start=0;
    if(mode==5)*(volatile uint64_t *)0x300000=1;
    if(mode==6)__asm__ volatile("xor %%eax,%%eax;mov %%ax,%%gs;pushfq;orq $0x4000,(%%rsp);popfq;movq $-1,%%rax;int $0x80;pushfq;popq %%rax;testq $0x4000,%%rax;jz 1f;ud2;1:":::"rax","memory","cc");
    identity=(uint32_t)argument;
    uint32_t a=1,b,c,d;__asm__ volatile("cpuid":"+a"(a),"=b"(b),"=c"(c),"=d"(d));
    unsigned avx=(c&(1u<<27)) && (c&(1u<<28));if(avx){uint32_t low,high;__asm__ volatile("xgetbv":"=a"(low),"=d"(high):"c"(0));avx=(low&6)==6;}
    uint64_t expected[4]={argument,argument^123,argument^456,argument^789},actual[4];
    if(avx)__asm__ volatile("vmovdqu %0,%%ymm0"::"m"(expected):"memory");else __asm__ volatile("movq %0,%%xmm0"::"r"(argument):"memory");
    uint64_t invalid=UINT64_MAX;__asm__ volatile("int $0x80":"+a"(invalid)::"memory","cc");
    for(volatile uint64_t i=0;i<500000;++i)__asm__ volatile("pause");
    if(avx){__asm__ volatile("vmovdqu %%ymm0,%0":"=m"(actual)::"memory");for(unsigned j=0;j<4;++j)if(actual[j]!=expected[j])__asm__ volatile("ud2");}
    else{__asm__ volatile("movq %%xmm0,%0":"=r"(actual[0])::"memory");if(actual[0]!=argument)__asm__ volatile("ud2");}
    register uint64_t op __asm__("rax")=0;
    __asm__ volatile("int $0x80"::"a"(op),"D"(identity):"memory");
    __builtin_unreachable();
}
