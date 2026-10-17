#include "../kmodules/shared/nvme.h"
#include "../kmodules/shared/sata.h"
#include "../kmodules/shared/partition.h"
#include "../kmodules/shared/ext4-fs.h"
#include "../kmodules/shared/config.h"
#include "../kmodules/shared/hwenum.h"
#include "../kmodules/shared/threads.h"
#include "probe.h"
#include "../kmodules/shared/irq.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void print(const char *p){while(*p)__asm__ volatile("outb %0,$0xe9"::"a"(*p++));}
/* Controller/namespace/port lifetime acceptance on disposable blank media. */
static struct kernel_surface_result multi(const struct kernel_surfaces *hub,const struct hwenum_table *hw,unsigned backend){
    unsigned line=0;uint64_t indices[2],count=0;struct block_device disks[3]={0};struct nvme_info ni;
#define VERIFY(x) do{if(!(x)){line=__LINE__;goto failed;}}while(0)
    for(size_t i=0;i<hw->count;++i)if(hw->entries[i].kind==HWENUM_PCI && hw->entries[i].data.pci.class_code==1 && hw->entries[i].data.pci.subclass==(backend==1?8:6)){VERIFY(count<2);indices[count++]=i;}
    VERIFY(count==2);
    kernel_surface_call open=kernel_surface_find(hub,backend==1?"nvme.open":"sata.open"),close=kernel_surface_find(hub,backend==1?"nvme.close":"sata.close"),block=kernel_surface_find(hub,"nvme.block"),thread=kernel_surface_find(hub,"threads.control");
    for(unsigned i=0;i<3;++i){uint64_t index=indices[i==2];if(backend==1){struct kernel_surface_result r=CALL(open,index,&ni,i==1?2:1);VERIFY(!r.status);VERIFY(!CALL(block,&disks[i],sizeof(disks[i]),r.value).status);}else{struct sata_open req={.hardware_index=index,.port=i==1};struct kernel_surface_result r=CALL(open,&req,&disks[i],sizeof(disks[i]));if(r.status){line=1000+r.value;goto failed;}}VERIFY(block_valid(&disks[i]));}
    /* The singleton compatibility selector must reject ambiguity. */
    if(backend==1)VERIFY(CALL(block,&ni,sizeof(ni),0).status);
    unsigned char data[4096];
    for(unsigned i=0;i<3;++i){for(unsigned j=0;j<sizeof(data);++j)data[j]=(i+1)*37+j;VERIFY(!disks[i].write(disks[i].cookie,128,4096/disks[i].block_bytes,(uintptr_t)data,sizeof(data),0).status);VERIFY(!CALL(disks[i].flush,disks[i].cookie,0,0).status);}
    for(unsigned i=0;i<3;++i){VERIFY(!disks[i].read(disks[i].cookie,128,4096/disks[i].block_bytes,(uintptr_t)data,sizeof(data),0).status);for(unsigned j=0;j<sizeof(data);++j)VERIFY(data[j]==(uint8_t)((i+1)*37+j));}
    VERIFY(!CALL(disks[0].control,disks[0].cookie,BLOCK_RETAIN,0).status && CALL(close,disks[0].cookie,0,0).status && !CALL(disks[0].control,disks[0].cookie,BLOCK_RELEASE,0).status);
    VERIFY(!CALL(close,disks[0].cookie,0,0).status);
    VERIFY(disks[0].read(disks[0].cookie,128,1,(uintptr_t)data,sizeof(data),0).status);
    VERIFY(!disks[1].read(disks[1].cookie,128,1,(uintptr_t)data,sizeof(data),0).status);
    /* Reopening the closed namespace/port must not reset its live sibling. */
    uint64_t stale=disks[0].cookie;
    if(backend==1){struct kernel_surface_result r=CALL(open,indices[0],&ni,1);VERIFY(!r.status && !CALL(block,&disks[0],sizeof(disks[0]),r.value).status);}else{struct sata_open req={.hardware_index=indices[0]};VERIFY(!CALL(open,&req,&disks[0],sizeof(disks[0])).status);}
    VERIFY(disks[0].cookie!=stale && disks[0].read(stale,0,1,(uintptr_t)data,sizeof(data),0).status);
    VERIFY(!disks[1].read(disks[1].cookie,128,1,(uintptr_t)data,sizeof(data),0).status);
    struct irq_info irq;uint64_t route;
    if(backend==1){VERIFY(!CALL(kernel_surface_find(hub,"nvme.info"),&ni,disks[0].cookie,0).status);route=ni.route_id;}else{struct sata_info si;VERIFY(!CALL(kernel_surface_find(hub,"sata.info"),disks[0].cookie,&si,0).status);route=si.route_id;}
    VERIFY(!CALL(kernel_surface_find(hub,"irq.info"),route,&irq,0).status && !CALL(kernel_surface_find(hub,"irq.enable"),route,irq.route.owner,0).status);
    VERIFY(disks[0].read(disks[0].cookie,128,1,(uintptr_t)data,sizeof(data),0).status);
    /* A shared NVMe controller fails all namespaces. AHCI can stop one engine.
     * Its shared route is deliberately masked here, so don't submit siblings. */
    if(backend==1)VERIFY(disks[1].read(disks[1].cookie,128,1,(uintptr_t)data,sizeof(data),0).status);
    VERIFY(!disks[2].read(disks[2].cookie,128,1,(uintptr_t)data,sizeof(data),0).status);
    for(unsigned i=0;i<3;++i){unsigned retry=0;while(CALL(close,disks[i].cookie,0,0).status && retry++<128)VERIFY(!CALL(thread,THREAD_YIELD,0,0).status);VERIFY(retry<=128);}
    print(backend==1?"PASS multiple NVMe controllers/namespaces, isolation, pins, stale cookies, failure boundary\n":"PASS multiple AHCI controllers/ports, sibling-preserving open/close, pins, stale cookies, failure isolation\n");return module_result(1,1);
failed:return module_result(line,0);
#undef VERIFY
}

struct kernel_surface_result probe_block_call(MODULE_ARGS){
    MODULE_UNUSED();struct probe_usb *job=(void *)a;const struct kernel_surfaces *hub=job->hub;unsigned line=0;
#define CHECK(x) do{if(!(x)){line=__LINE__;goto failed;}}while(0)
    const struct kernel_hub_table *hwref=kernel_hub_find_table(hub,HWENUM_TABLE_NAME),*guid=kernel_hub_find_table(hub,"config.drivers.partition_guid");CHECK(hwref && guid);
    const struct hwenum_table *hw=(const void *)hwref->address;const struct config_value *g=(const void *)guid->address;CHECK(g->type==CONFIG_GUID && g->bytes==16);
    uint64_t index=hw->count;for(size_t i=0;i<hw->count;++i){const struct hwenum_descriptor *h=&hw->entries[i];if(h->kind==HWENUM_PCI && h->data.pci.class_code==1 && h->data.pci.subclass==(job->write==1?8:6)){if(index!=hw->count){struct kernel_surface_result result=multi(hub,hw,job->write);job->done=1;job->errors=result.status?result.value:0;return result;}index=i;}}
    CHECK(index<hw->count);
    kernel_surface_call close=kernel_surface_find(hub,job->write==1?"nvme.close":"sata.close"),open=kernel_surface_find(hub,job->write==1?"nvme.open":"sata.open"),select=kernel_surface_find(hub,"partition.find"),unselect=kernel_surface_find(hub,"partition.close"),mount=kernel_surface_find(hub,"ext4_fs.mount"),unmount=kernel_surface_find(hub,"ext4_fs.unmount"),read=kernel_surface_find(hub,"ext4_fs.read"),write=kernel_surface_find(hub,"ext4_fs.write"),stat=kernel_surface_find(hub,"ext4_fs.stat"),mutate=kernel_surface_find(hub,"ext4_fs.mutate");CHECK(close && open && select && unselect && mount && unmount && read && write && stat && mutate);
    struct block_device disk,part;struct nvme_info ni={0};struct sata_info si={0};struct sata_open request={.hardware_index=index};
    struct kernel_surface_result opened;
    if(job->write==1){CHECK(CALL(open,index,&ni,UINT32_MAX).status);opened=CALL(open,index,&ni,0);if(opened.status){line=1000+opened.value;goto failed;}CHECK(!CALL(kernel_surface_find(hub,"nvme.block"),&disk,sizeof(disk),0).status);}
    else {opened=CALL(open,&request,&disk,sizeof(disk));if(opened.status){line=2000+opened.value;goto failed;}}
    CHECK(block_valid(&disk) && block_writable(&disk));uint64_t old_cookie=disk.cookie;unsigned char buffer[4096];
    CHECK(!disk.read(disk.cookie,disk.blocks,0,0,0,0).status);
    CHECK(disk.read(disk.cookie,disk.blocks,1,(uintptr_t)buffer,sizeof(buffer),0).status);
    CHECK(disk.write(disk.cookie,disk.blocks,1,(uintptr_t)buffer,sizeof(buffer),0).status);
    CHECK(disk.read(disk.cookie,0,1,(uintptr_t)buffer,1,0).status);
    CHECK(disk.read(disk.cookie,0,4097/disk.block_bytes+1,(uintptr_t)buffer,sizeof(buffer),0).status);
    CHECK(!select((uintptr_t)&disk,(uintptr_t)g->data,(uintptr_t)&part,sizeof(part),0,0).status);
    CHECK(CALL(close,job->write==1?0:disk.cookie,0,0).status);
    CHECK(part.write(part.cookie,part.blocks,1,(uintptr_t)buffer,sizeof(buffer),0).status);
    struct ext4_info fs;CHECK(!CALL(mount,&part,&fs,EXT4_MOUNT_WRITE).status && !fs.readonly);CHECK(CALL(unselect,part.cookie,0,0).status);
    struct kernel_surface_result r=read(fs.cookie,(uintptr_t)"/boot/system/drivers/probe.bin",123,(uintptr_t)buffer,sizeof(buffer),0);CHECK(!r.status && r.value==sizeof(buffer));for(unsigned i=0;i<sizeof(buffer);++i)CHECK(buffer[i]==(uint8_t)((i+123)*37+11));
    const char *path="/boot/system/drivers/backend-write.bin";
    for(unsigned i=0;i<sizeof(buffer);++i)buffer[i]=i*13+7;
    CHECK(!write(fs.cookie,(uintptr_t)path,0,(uintptr_t)buffer,sizeof(buffer),EXT4_WRITE_CREATE|EXT4_WRITE_EXCLUSIVE).status);
    CHECK(!write(fs.cookie,(uintptr_t)path,UINT64_MAX,(uintptr_t)"TAIL",4,EXT4_WRITE_APPEND).status);
    struct ext4_stat st;CHECK(!CALL(stat,fs.cookie,path,&st).status && st.bytes==4100);
    CHECK(!CALL(unmount,fs.cookie,0,0).status && !CALL(mount,&part,&fs,0).status && fs.readonly);
    r=read(fs.cookie,(uintptr_t)path,4096,(uintptr_t)buffer,sizeof(buffer),0);CHECK(!r.status && r.value==4 && buffer[0]=='T' && buffer[3]=='L');
    CHECK(!CALL(unmount,fs.cookie,0,0).status && !CALL(mount,&part,&fs,EXT4_MOUNT_WRITE).status);
    CHECK(!mutate(fs.cookie,EXT4_MUTATE_RENAME,(uintptr_t)path,0,(uintptr_t)"/boot/system/drivers/renamed.bin",0).status);
    CHECK(!mutate(fs.cookie,EXT4_MUTATE_REMOVE,(uintptr_t)"/boot/system/drivers/renamed.bin",0,0,0).status);
    CHECK(!CALL(unmount,fs.cookie,0,0).status && !CALL(unselect,part.cookie,0,0).status);
    uint64_t route=0;struct irq_info irq;
    if(job->write==1){CHECK(!CALL(kernel_surface_find(hub,"nvme.info"),&ni,0,0).status && ni.interrupts);route=ni.route_id;}
    else{CHECK(!CALL(kernel_surface_find(hub,"sata.info"),disk.cookie,&si,0).status && si.interrupts && si.writes && si.flushes);route=si.route_id;}
    CHECK(!CALL(kernel_surface_find(hub,"irq.info"),route,&irq,0).status && irq.delivered && irq.drained);
    CHECK(!CALL(disk.control,disk.cookie,BLOCK_RETAIN,0).status && CALL(close,job->write==1?0:disk.cookie,0,0).status && !CALL(disk.control,disk.cookie,BLOCK_RELEASE,0).status);
    /* Suppress notification delivery: deadline poisons the owner, cleanup and
     * reopen must not revive the old cookie. No write is retried. */
    CHECK(!CALL(kernel_surface_find(hub,"irq.enable"),route,irq.route.owner,0).status);
    CHECK(disk.read(disk.cookie,0,1,(uintptr_t)buffer,sizeof(buffer),0).status);
    CHECK(disk.flush(disk.cookie,0,0,0,0,0).status);
    CHECK(!CALL(close,job->write==1?0:disk.cookie,0,0).status);
    CHECK(disk.read(old_cookie,0,1,(uintptr_t)buffer,sizeof(buffer),0).status);
    if(job->write==1){CHECK(!CALL(open,index,&ni,1).status && !CALL(kernel_surface_find(hub,"nvme.block"),&disk,sizeof(disk),0).status);}
    else CHECK(!CALL(open,&request,&disk,sizeof(disk)).status);
    CHECK(disk.cookie!=old_cookie && disk.read(old_cookie,0,1,(uintptr_t)buffer,sizeof(buffer),0).status && !disk.read(disk.cookie,0,1,(uintptr_t)buffer,sizeof(buffer),0).status);
    CHECK(!CALL(close,job->write==1?0:disk.cookie,0,0).status);
    if(job->write==1){
        uint64_t cleanup=0;CHECK(open(index,(uintptr_t)&ni,2,(uintptr_t)&cleanup,0,0).status);
        kernel_surface_call inspect=kernel_surface_find(hub,"nvme.info"),yield=kernel_surface_find(hub,"threads.control");
        unsigned draining=0;
        while(cleanup && !CALL(inspect,&ni,cleanup,0).status){if(!CALL(close,cleanup,0,0).status)break;CHECK(draining++<128 && !CALL(yield,THREAD_YIELD,0,0).status);}

        struct kernel_surface_result reopen=CALL(open,index,&ni,1);
        if(reopen.status){line=3000+reopen.value;goto failed;}
        CHECK(ni.namespace_id==1);
        /* Admin queue setup can leave a deferred message in flight. Close
         * deliberately retains ownership until that callback has drained. */
        kernel_surface_call thread=kernel_surface_find(hub,"threads.control");CHECK(thread);
        unsigned retry=0;
        while(CALL(close,0,0,0).status && retry++<128)CHECK(!CALL(thread,THREAD_YIELD,0,0).status);
        CHECK(retry<=128);
    }
    print(job->write==1?"PASS block NVMe: boot media, GPT GUID, ext4 journal writes/remount, MSI-X, bounds/pins, timeout/reset/stale identity\n":"PASS block SATA: boot media, GPT GUID, ext4 journal writes/remount, MSI, bounds/pins, timeout/reset/stale identity\n");
    job->done=1;return module_result(1,1);
failed:job->done=1;job->errors=line;return module_result(line,0);
#undef CHECK
}
