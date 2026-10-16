#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kmodules/amd/iommu-amd.c"
static void *allocations[4];static unsigned count;
static int pages(size_t n,struct mem_pages *out) {
    void *p=aligned_alloc(4096,n*4096);if(!p)return 0;memset(p,0,n*4096);allocations[count++]=p;
    *out=(struct mem_pages){.address=(uintptr_t)p,.physical=(uintptr_t)p,.count=n,.id=count};return 1;
}
static int wait32(uintptr_t address,uint32_t mask,uint32_t value) {(void)address;(void)mask;(void)value;return 1;}
static int wait64(uintptr_t address,uint64_t mask,uint64_t value) {return (*(uint64_t *)address&mask)==value;}
static void publish(struct iommu_unit *u,uintptr_t address,size_t n) {(void)u;(void)address;(void)n;}
static void *allocate(size_t a,size_t b) {return calloc(a,b);}
static int read_bytes(uint64_t a,void *b,size_t c) {(void)a;(void)b;(void)c;return 0;}
static void record_fault(struct iommu_unit *u,uint32_t r,uint64_t a,uint32_t f) {(void)u;(void)r;(void)a;(void)f;}
int main(void) {
    static _Alignas(4096) uint64_t regs[2048];uint32_t error=0;
    struct iommu_state frontend={0};
    struct iommu_backend_env env={.abi=1,.backend=IOMMU_AMD,.cache_line=64,.last_error=&error,.alloc_pages=pages,.wait32=wait32,.wait64=wait64,.publish=publish,
        .frontend=&frontend,.allocate=allocate,.read_bytes=read_bytes,.record_fault=record_fault};
    assert(!iommu_backend_init((uintptr_t)&env,0,0,0,0,0).status);
    struct iommu_unit unit={.registers=(uintptr_t)regs};regs[0x18/8]=(1u<<11)|(1u<<3)|(1u<<4);regs[0x2020/8]=1u<<4;
    assert(iommu_amd_start(&unit) && count==4 && unit.active && unit.levels==4 && unit.address_bits==48);
    assert(regs[0x18/8]==((1u<<11)|(1u<<10)|(1u<<12)|5));
    uint64_t *dte=(void *)unit.table.address;
    for(unsigned i=0;i<65536;++i)assert(dte[i*4]==3 && !dte[i*4+1] && !dte[i*4+2] && !dte[i*4+3]);
    assert(!iommu_amd_start(&unit) && count==4);
    for(unsigned i=0;i<count;++i)free(allocations[i]);
    puts("PASS AMD backend: coherent table fetch, preserved firmware policy, masked unowned IRQs, deny-default DTEs and active-engine refusal");
    return 0;
}
