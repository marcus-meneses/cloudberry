#ifndef CLOUDBERRY_HPET_COUNTER_H
#define CLOUDBERRY_HPET_COUNTER_H
#include <stdint.h>
/* A live 64-bit HPET counter need not support atomic 64-bit MMIO transactions.
 * Read its high half around the low half, retrying an intervening rollover.
 * No locks, allocation or reliance on the clock being sampled. */
#ifndef CB_HPET_READ32
#define CB_HPET_READ32(address) (*(const volatile uint32_t *)(address))
#endif
static inline int hpet_counter_read(uintptr_t address,uint64_t *out){
    if(!address || !out)return 0;
    for(unsigned attempt=0;attempt<8;++attempt){
        uint32_t before=CB_HPET_READ32(address+4);
        uint32_t low=CB_HPET_READ32(address);
        uint32_t after=CB_HPET_READ32(address+4);
        /* All users sample an enabled, advancing counter. An all-zero
         * sample is not a valid deadline reference; retry it rather than
         * declaring a lock timeout or accepting a backwards timestamp. */
        if(before==after && (after || low)){*out=((uint64_t)after<<32)|low;return 1;}
#ifdef CB_HPET_ZERO
        if(!(before || low || after))CB_HPET_ZERO(address);
#endif
    }
    return 0;
}
#endif
