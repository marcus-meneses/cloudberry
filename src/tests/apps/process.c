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
    for(volatile uint64_t i=0;i<500000;++i)__asm__ volatile("pause");
    register uint64_t op __asm__("rax")=0;
    __asm__ volatile("int $0x80"::"a"(op),"D"(identity):"memory");
    __builtin_unreachable();
}
