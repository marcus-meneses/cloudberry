#ifndef CLOUDBERRY_XHCI_PLATFORM_H
#define CLOUDBERRY_XHCI_PLATFORM_H
#include <stdint.h>
#ifdef XHCI_TEST
uint32_t xhci_read32(uintptr_t);
void xhci_write32(uintptr_t,uint32_t);
uint64_t xhci_flags(void);
#else
static inline uint32_t xhci_read32(uintptr_t p) {return *(volatile uint32_t *)p;}
static inline void xhci_write32(uintptr_t p,uint32_t v) {__asm__ volatile("mfence":::"memory");*(volatile uint32_t *)p=v;}
static inline uint64_t xhci_flags(void) {uint64_t f;__asm__ volatile("pushfq;pop %0":"=r"(f));return f;}
#endif
#endif
