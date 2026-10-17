#include "../kmodules/shared/nvme.h"
#include "../kmodules/shared/iommu.h"
#include "../kmodules/shared/pci.h"
#include "../kmodules/shared/hwenum.h"
#include "../kmodules/shared/threads.h"
#include "../kmodules/shared/mmio.h"
#include "../boot/memory.h"
#include "probe.h"
#include <stdbool.h>
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void measure(const char *name,uint64_t value) {
    while(*name)__asm__ volatile("outb %0,$0xe9"::"a"(*name++));
    char digits[24];unsigned n=0;
    do{digits[n++]='0'+value%10;value/=10;}while(value);while(n)__asm__ volatile("outb %0,$0xe9"::"a"(digits[--n]));__asm__ volatile("outb %0,$0xe9"::"a"((unsigned char)'\n'));
}
static bool failure(unsigned line) {
    const char *s="FAIL nvme-boot.c line ";while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));
    char digits[12];unsigned n=0;do{digits[n++]='0'+line%10;line/=10;}while(line);
    while(n)__asm__ volatile("outb %0,$0xe9"::"a"(digits[--n]));
    __asm__ volatile("outb %0,$0xe9"::"a"((unsigned char)'\n'));return false;
}
#define CHECK(x) do{if(!(x))return failure(__LINE__);}while(0)
static kernel_surface_call create,control,step,now,entry;
static bool read_job(struct probe_nvme *job) {
    struct thread_request request={.abi=1,.cpu=0,.stack_bytes=16384,.entry=(uintptr_t)entry,.major=1};request.payload.arguments[0]=(uintptr_t)job;
    struct kernel_surface_result t=CALL(create,&request,0,0);if(t.status)return false;
    uint64_t start=CALL(now,0,0,0).value;
    for(;;) {
        struct thread_info info;if(CALL(control,THREAD_INFO,t.value,&info).status)return false;
        if(info.state==THREAD_EXITED)break;
        if(CALL(step,1,0,0).status || CALL(now,0,0,0).value-start>3000000000)return false;
    }
    return job->done && !CALL(control,THREAD_REAP,t.value,0).status;
}
static void issue_read(uintptr_t registers,uintptr_t sq,unsigned slot,uint64_t physical,uint32_t nsid) {
    volatile uint32_t *command=(void *)(sq+slot*64);for(unsigned i=0;i<16;++i)command[i]=0;
    command[0]=2|((0xfef0u+slot)<<16);command[1]=nsid;command[6]=(uint32_t)physical;command[7]=physical>>32;
    uint64_t cap=*(volatile uint64_t *)registers,stride=UINT64_C(4)<<((cap>>32)&15);
    __asm__ volatile("mfence":::"memory");*(volatile uint32_t *)(registers+0x1000+2*stride)=slot+1;
}
static bool denied_dma(const struct kernel_surfaces *hub,size_t index,const struct nvme_info *info,int *closed) {
    struct pci_record device;CHECK(!CALL(kernel_surface_find(hub,"pci.device"),index,&device,0).status);
    const uint64_t owner=device.identity.owner;CHECK(owner);struct iommu_info before,after;
    kernel_surface_call inspect=kernel_surface_find(hub,"iommu.info"),lookup=kernel_surface_find(hub,"iommu.lookup"),grant=kernel_surface_find(hub,"iommu.map"),revoke=kernel_surface_find(hub,"iommu.unmap"),master=kernel_surface_find(hub,"pci.busmaster");
    CHECK(!CALL(inspect,&before,0,0).status);if(!before.backend)return true;
    CHECK(before.active && before.attached==1 && before.mapped_pages==6 && !before.failed);
    const struct kernel_hub_table *ledger=kernel_hub_find_table(hub,"memory.reservations"),*mm=kernel_hub_find_table(hub,MMIO_TABLE_NAME);CHECK(ledger && mm);
    const struct boot_reservation_table *table=(const void *)ledger->address;uint64_t base=0;
    for(const struct boot_reservation_chunk *chunk=table->first;chunk;chunk=chunk->next)
        for(size_t i=0;i<chunk->count;++i)if(chunk->entries[i].kind==BOOT_RES_PAGES && chunk->entries[i].owner==owner && chunk->entries[i].bytes==6*4096)base=chunk->entries[i].base;
    CHECK(base && CALL(lookup,index,owner,base+7).value==base+7 && CALL(lookup,index,owner+1,base).status);
    kernel_surface_call release=kernel_surface_find(hub,"memory.release");CHECK(release(base,6,BOOT_RES_PAGES,owner,0,0).status);
    struct mem_pages canary;kernel_surface_call allocate=kernel_surface_find(hub,"mem.pages_alloc"),free=kernel_surface_find(hub,"mem.pages_free"),map=kernel_surface_find(hub,"memory.map_owned"),unmap=kernel_surface_find(hub,"memory.unmap_owned");
    CHECK(!CALL(allocate,1,owner,&canary).status && CALL(lookup,index,owner,canary.physical).status);
    CHECK(!CALL(master,index,owner,0).status && !CALL(grant,index,owner,&canary).status && !CALL(master,index,owner,1).status);
    unsigned char *bytes=(void *)canary.address;for(size_t i=0;i<4096;++i)bytes[i]=0xa5;
    uintptr_t sq=MEM_STACK_BASE-32768,cq=sq+4096;
    CHECK(!CALL(map,sq,base+8192,owner).status && !CALL(map,cq,base+12288,owner).status);
    uintptr_t registers=0;const struct mmio_table *regions=(const void *)mm->address;
    for(size_t i=0;i<regions->count;++i)if(regions->entries[i].kind==MMIO_PCI_BAR && regions->entries[i].hardware_index==index && !regions->entries[i].bar)registers=regions->entries[i].virtual_address;
    CHECK(registers);
    /* Prime the hardware IOTLB with a successful DMA to this page. Then revoke
     * that exact grant and repeat, proving stale translations cannot authorize it. */
    issue_read(registers,sq,1,canary.physical,info->namespace_id);
    volatile uint16_t *status=(void *)(cq+16+14);uint64_t start=CALL(now,0,0,0).value;
    while(!(*status&1) && CALL(now,0,0,0).value-start<500000000)__asm__ volatile("pause");
    CHECK((*status&1) && !(*status>>1));for(size_t i=0;i<512;++i)CHECK(bytes[i]==(unsigned char)(i*37+11));
    uint64_t cap=*(volatile uint64_t *)registers,stride=UINT64_C(4)<<((cap>>32)&15);
    *(volatile uint32_t *)(registers+0x1000+3*stride)=2;
    CHECK(!CALL(master,index,owner,0).status && !CALL(revoke,index,owner,canary.id).status && CALL(lookup,index,owner,canary.physical).status);
    for(size_t i=0;i<4096;++i)bytes[i]=0xa5;
    CHECK(!CALL(master,index,owner,1).status);issue_read(registers,sq,2,canary.physical,info->namespace_id);
    status=(void *)(cq+32+14);start=CALL(now,0,0,0).value;
    while(!(*status&1) && CALL(now,0,0,0).value-start<500000000)__asm__ volatile("pause");
    /* Some emulated endpoints report command success despite failed DMA.
     * Check physical isolation rather than trusting that completion status. */
    for(size_t i=0;i<4096;++i)CHECK(bytes[i]==0xa5);
    CHECK(!CALL(inspect,&after,0,0).status);
    if(after.faults==before.faults && after.backend==IOMMU_AMD) {
        const char *note="NOTE AMD-Vi: revoked DMA blocked; missing-PTE fault logging is absent in QEMU 6.2\n";
        while(*note)__asm__ volatile("outb %0,$0xe9"::"a"(*note++));
    } else CHECK(after.faults>before.faults);
    CHECK(!CALL(unmap,sq,base+8192,owner).status && !CALL(unmap,cq,base+12288,owner).status);
    kernel_surface_call drain=kernel_surface_find(hub,"irq.drain"),run=kernel_surface_find(hub,"threads.run");
    unsigned retry=0;while(CALL(drain,1,0,0).status && retry++<64)CHECK(!CALL(run,1,0,0).status);CHECK(retry<=64);
    CHECK(!CALL(kernel_surface_find(hub,"nvme.close"),0,0,0).status);*closed=1;
    CHECK(!CALL(free,canary.id,canary.owner,0).status);
    const char *message="PASS IOMMU: active NVMe DMA, pins/owners, primed IOTLB revocation, denied device write and unchanged canary\n";
    while(*message)__asm__ volatile("outb %0,$0xe9"::"a"(*message++));
    return true;
}
bool kernel_nvme_test(const struct kernel_surfaces *hub) {
    create=kernel_surface_find(hub,"threads.create");control=kernel_surface_find(hub,"threads.control");step=kernel_surface_find(hub,"event.step");now=kernel_surface_find(hub,"clock.now_ns");entry=kernel_surface_find(hub,"probe.nvme");
    kernel_surface_call open=kernel_surface_find(hub,"nvme.open"),info_call=kernel_surface_find(hub,"nvme.info"),close=kernel_surface_find(hub,"nvme.close"),claim=kernel_surface_find(hub,"pci.claim"),release=kernel_surface_find(hub,"pci.release"),enable=kernel_surface_find(hub,"irq.enable");
    CHECK(create && control && step && now && entry && open && close && claim && release && enable);
    const struct kernel_hub_table *ref=kernel_hub_find_table(hub,HWENUM_TABLE_NAME);CHECK(ref);
    const struct hwenum_table *hw=(const void *)ref->address;size_t index=hw->count;
    for(size_t i=0;i<hw->count;++i)if(hw->entries[i].kind==HWENUM_PCI && hw->entries[i].data.pci.class_code==1 && hw->entries[i].data.pci.subclass==8 && hw->entries[i].data.pci.programming_interface==2){index=i;break;}
    CHECK(index<hw->count);struct nvme_info info;
    __asm__ volatile("sti":::"memory");
    CHECK(CALL(open,hw->count,&info,0).status);
    struct kernel_surface_result opened=CALL(open,index,&info,0);if(opened.status) {struct iommu_info detail;CALL(kernel_surface_find(hub,"iommu.info"),&detail,0,0);if(detail.last_error)return failure(2000+detail.last_error);return failure(1000+opened.value);}
    CHECK(info.online && info.block_bytes==512 && info.blocks==2097152 && info.route_id);
    CHECK(CALL(open,index,&info,0).status && CALL(claim,index,17,&(struct pci_device){0}).status && CALL(release,index,17,1).status);
    static unsigned char buffer[8192];
    for(unsigned round=0;round<40;++round) {
        struct probe_nvme job={.lba=round%16,.blocks=16,.buffer=(uintptr_t)buffer};CHECK(read_job(&job) && !job.status && job.value==sizeof(buffer));
        for(unsigned i=0;i<sizeof(buffer);++i)CHECK(buffer[i]==(unsigned char)(((round%16)*512+i)*37+11));
    }
    struct probe_nvme end={.lba=info.blocks-1,.blocks=1,.buffer=(uintptr_t)buffer};CHECK(read_job(&end) && !end.status);
    const char *marker="CLOUDBERRY-NVME-END";for(unsigned i=0;marker[i];++i)CHECK(buffer[i]==(unsigned char)marker[i]);
    struct probe_nvme invalid={.lba=info.blocks,.blocks=1,.buffer=(uintptr_t)buffer};CHECK(read_job(&invalid) && invalid.status);
    CHECK(!CALL(info_call,&info,0,0).status && info.reads>=81 && info.interrupts);
    struct irq_info irq;kernel_surface_call irq_info=kernel_surface_find(hub,"irq.info");CHECK(!CALL(irq_info,info.route_id,&irq,0).status && irq.delivered && irq.drained);
    /* Deliberately suppress completion delivery. The request must time out,
     * disable/reset, refuse reuse and allow a clean close/reopen. */
    CHECK(!CALL(enable,irq.route.id,irq.route.owner,0).status);
    struct probe_nvme timeout={.blocks=1,.buffer=(uintptr_t)buffer};CHECK(read_job(&timeout) && timeout.status);
    CHECK(!CALL(info_call,&info,0,0).status && info.failed && !info.online && info.timeouts==1);
    CHECK(!CALL(close,0,0,0).status && CALL(close,0,0,0).status);
    CHECK(!CALL(open,index,&info,0).status);
    struct iommu_platform platform;CHECK(!CALL(kernel_surface_find(hub,"iommu.platform"),&platform,0,0).status && platform.abi==1);
    measure("MEASURE IOMMU backend: ",platform.backend);measure("MEASURE IOMMU firmware supported: ",platform.firmware_supported);measure("MEASURE IOMMU IOVA bits: ",platform.minimum_iova_bits);measure("MEASURE firmware DMA takeover verified: ",platform.firmware_takeover_verified);

    struct probe_nvme again={.blocks=1,.buffer=(uintptr_t)buffer};CHECK(read_job(&again) && !again.status);
    int closed=0;CHECK(denied_dma(hub,index,&info,&closed));
    if(!closed)CHECK(!CALL(close,0,0,0).status);
    struct iommu_info final;CHECK(!CALL(kernel_surface_find(hub,"iommu.info"),&final,0,0).status && !final.attached && !final.mapped_pages);
    const char *s="PASS NVMe: sized BARs, exclusive PCI/DMA ownership, real MSI-X reads, queue wrap, range rejection, timeout/reset and reopen on read-only disposable media\n";
    while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));
    return true;
}
