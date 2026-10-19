#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t samples[24];static size_t count,used;
static uint32_t read32(uintptr_t address){assert(address==(used%3==1?0x1000:0x1004));assert(used<count);return samples[used++];}
#define CB_HPET_READ32(address) read32(address)
#include "../kmodules/shared/hpet-counter.h"
int main(void){
    uint64_t value=123;assert(!hpet_counter_read(0,&value) && value==123);
    count=3;samples[0]=7;samples[1]=42;samples[2]=7;
    assert(hpet_counter_read(0x1000,&value) && value==((UINT64_C(7)<<32)|42) && used==3);
    used=0;count=6;samples[0]=7;samples[1]=UINT32_MAX;samples[2]=8;samples[3]=8;samples[4]=2;samples[5]=8;
    assert(hpet_counter_read(0x1000,&value) && value==((UINT64_C(8)<<32)|2) && used==6);
    used=0;count=6;samples[0]=7;samples[1]=0;samples[2]=8;
    assert(hpet_counter_read(0x1000,&value) && value==((UINT64_C(8)<<32)|2) && used==6);
    used=0;count=6;samples[0]=samples[1]=samples[2]=0;samples[3]=0;samples[4]=100;samples[5]=0;
    assert(hpet_counter_read(0x1000,&value) && value==100 && used==6);
    used=0;count=24;for(unsigned i=0;i<24;++i)samples[i]=0;
    value=123;assert(!hpet_counter_read(0x1000,&value) && value==123 && used==24);
    used=0;count=24;for(unsigned i=0;i<8;++i){samples[i*3]=i;samples[i*3+1]=0;samples[i*3+2]=i+1;}
    value=123;assert(!hpet_counter_read(0x1000,&value) && used==24 && value==123);
    puts("PASS HPET counter: stable halves, rollover before/after low read, bounded instability and unchanged failure result");return 0;
}
