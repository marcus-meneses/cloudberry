#include "../kmodules/shared/usb-storage.h"
#include "../kmodules/shared/partition.h"
#include "../kmodules/shared/ext4-fs.h"
#include "../kmodules/shared/config.h"
#include "../kmodules/shared/threads.h"
#include "probe.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void print(const char *s){while(*s)__asm__ volatile("outb %0,$0xe9"::"a"(*s++));}
struct kernel_surface_result probe_storage_call(MODULE_ARGS){
    MODULE_UNUSED();struct probe_usb *job=(void *)a;const struct kernel_surfaces *hub=job->hub;unsigned line=0;
    kernel_surface_call start=kernel_surface_find(hub,"usb.start"),stop=kernel_surface_find(hub,"usb.stop"),info=kernel_surface_find(hub,"usb.info"),watch=kernel_surface_find(hub,"usb.watch"),thread=kernel_surface_find(hub,"threads.control"),clock=kernel_surface_find(hub,"clock.now_ns"),cpu=kernel_surface_find(hub,"cpu.control");
    kernel_surface_call open=kernel_surface_find(hub,"usb_storage.open"),close=kernel_surface_find(hub,"usb_storage.close"),select=kernel_surface_find(hub,"partition.find"),unselect=kernel_surface_find(hub,"partition.close"),mount=kernel_surface_find(hub,"ext4_fs.mount"),unmount=kernel_surface_find(hub,"ext4_fs.unmount"),stat=kernel_surface_find(hub,"ext4_fs.stat"),read=kernel_surface_find(hub,"ext4_fs.read"),list=kernel_surface_find(hub,"ext4_fs.list");
#define CHECK(x) do{if(!(x)){line=__LINE__;goto failed;}}while(0)
    CHECK(start && stop && info && watch && thread && clock && cpu && open && close && select && unselect && mount && unmount && stat && read && list);
    if(job->hotplug==2)CHECK(!CALL(kernel_surface_find(hub,"usb_hub.start"),0,0,0).status);
    unsigned expected=job->hotplug==2?5:3;
    CHECK(!CALL(watch,1,0,0).status && !CALL(start,0,0,0).status && !CALL(kernel_surface_find(hub,"usb_storage.start"),0,0,0).status);uint64_t deadline=CALL(clock,0,0,0).value+10000000000;struct usb_info status;
    do{CHECK(!CALL(info,&status,0,0).status && !status.failed && CALL(clock,0,0,0).value<deadline);if(status.configured<expected)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.configured<expected);
    const struct kernel_hub_table *usb=kernel_hub_find_table(hub,USB_TABLE_NAME),*guid=kernel_hub_find_table(hub,"config.drivers.partition_guid"),*directory=kernel_hub_find_table(hub,"config.drivers.directory");CHECK(usb && guid && directory);
    struct usb_ref refs[5];unsigned count=0;uint64_t flags=CALL(cpu,1,0,0).value;for(const struct usb_device *p=((const struct usb_table *)usb->address)->first;p && count<expected;p=p->next)refs[count++]=(struct usb_ref){p->identity.id,p->identity.generation};CALL(cpu,2,flags,0);
    kernel_surface_call device=kernel_surface_find(hub,"usb.device"),interface=kernel_surface_find(hub,"usb.interface");CHECK(device && interface);struct usb_storage_open request={0};
    for(unsigned n=0;n<count;++n){struct usb_device dev;CHECK(!CALL(device,&refs[n],&dev,0).status);if(dev.hub || dev.class_code==9)continue;for(unsigned i=0;i<dev.interfaces;++i){struct usb_interface intf;CHECK(!CALL(interface,&refs[n],i,&intf).status);if(intf.class_code==8 && intf.subclass==6 && intf.protocol==0x50){CHECK(!request.device.id);request.device=refs[n];request.interface=intf.number;}}}
    CHECK(request.device.id);struct block_device disk,part;struct kernel_surface_result opened=CALL(open,&request,&disk,sizeof(disk));
    if(opened.status){print("FAIL storage open error ");char digit='0'+opened.value;__asm__ volatile("outb %0,$0xe9"::"a"(digit));print("\n");}CHECK(!opened.status && block_valid(&disk));
    struct block_device refused;CHECK(CALL(open,&request,&refused,sizeof(refused)).status);
    const struct config_value *g=(void *)guid->address,*dir=(void *)directory->address;CHECK(g->type==CONFIG_GUID && g->bytes==16 && dir->type==CONFIG_STRING && dir->bytes<400);
    CHECK(!select((uintptr_t)&disk,(uintptr_t)g->data,(uintptr_t)&part,sizeof(part),0,0).status && part.blocks*part.block_bytes>=512*1024*1024);
    CHECK(CALL(close,disk.cookie,0,0).status);struct ext4_info fs;CHECK(!CALL(mount,&part,&fs,0).status);CHECK(CALL(unselect,part.cookie,0,0).status);
    struct ext4_stat st;CHECK(!CALL(stat,fs.cookie,dir->data,&st).status && (st.mode&0xf000)==0x4000);
    char path[512];unsigned len=0;const char *base=dir->data;while(base[len]){path[len]=base[len];++len;}const char *suffix="/probe.bin";while(*suffix)path[len++]=*suffix++;path[len]=0;
    CHECK(!CALL(stat,fs.cookie,path,&st).status && st.bytes==16384);unsigned char bytes[8192];
    CHECK(!read(fs.cookie,(uintptr_t)path,123,(uintptr_t)bytes,sizeof(bytes),0).status);for(unsigned n=0;n<sizeof(bytes);++n)CHECK(bytes[n]==(uint8_t)((n+123)*37+11));
    CHECK(read(fs.cookie,(uintptr_t)path,16384,(uintptr_t)bytes,1,0).value==0);CHECK(CALL(stat,fs.cookie,"/missing",&st).status);
    uint64_t cursor=0;struct ext4_dirent entry;unsigned found=0;do{struct kernel_surface_result r=list(fs.cookie,(uintptr_t)dir->data,(uintptr_t)&cursor,(uintptr_t)&entry,0,0);CHECK(!r.status);if(!r.value)break;if(entry.name_bytes==9 && entry.name[0]=='p')++found;}while(1);CHECK(found==1);
    if(job->write){
        kernel_surface_call write=kernel_surface_find(hub,"ext4_fs.write"),mutate=kernel_surface_find(hub,"ext4_fs.mutate"),sync=kernel_surface_find(hub,"ext4_fs.sync");CHECK(write && mutate && sync && block_writable(&disk) && block_writable(&part));
        CHECK(write(fs.cookie,(uintptr_t)path,0,(uintptr_t)"NO",2,0).status);
        CHECK(!CALL(unmount,fs.cookie,0,0).status && !CALL(mount,&part,&fs,EXT4_MOUNT_WRITE).status && !fs.readonly);
        len=0;while(base[len]){path[len]=base[len];++len;}suffix="/journal-test.bin";while(*suffix)path[len++]=*suffix++;path[len]=0;
        CHECK(!mutate(fs.cookie,EXT4_MUTATE_CREATE,(uintptr_t)path,0,0,0).status);
        for(unsigned n=0;n<sizeof(bytes);++n)bytes[n]=n*13+3;
        struct kernel_surface_result written=write(fs.cookie,(uintptr_t)path,0,(uintptr_t)bytes,sizeof(bytes),0);CHECK(!written.status && written.value==sizeof(bytes));
        CHECK(!write(fs.cookie,(uintptr_t)path,0,(uintptr_t)"TAIL",4,EXT4_WRITE_APPEND).status);
        CHECK(!CALL(stat,fs.cookie,path,&st).status && st.bytes==sizeof(bytes)+4);
        CHECK(!mutate(fs.cookie,EXT4_MUTATE_TRUNCATE,(uintptr_t)path,137,0,0).status && !mutate(fs.cookie,EXT4_MUTATE_TRUNCATE,(uintptr_t)path,8193,0,0).status);
        CHECK(!read(fs.cookie,(uintptr_t)path,0,(uintptr_t)bytes,sizeof(bytes),0).status);for(unsigned n=0;n<sizeof(bytes);++n)CHECK(bytes[n]==(n<137?(uint8_t)(n*13+3):0));
        CHECK(!mutate(fs.cookie,EXT4_MUTATE_REMOVE,(uintptr_t)path,0,0,0).status);
        CHECK(!write(fs.cookie,(uintptr_t)path,0,(uintptr_t)"DURABLE",7,EXT4_WRITE_CREATE|EXT4_WRITE_EXCLUSIVE).status && !CALL(sync,fs.cookie,0,0).status);
        CHECK(!CALL(unmount,fs.cookie,0,0).status && !CALL(mount,&part,&fs,0).status && fs.readonly);
        struct kernel_surface_result persisted=read(fs.cookie,(uintptr_t)path,0,(uintptr_t)bytes,sizeof(bytes),0);CHECK(!persisted.status && persisted.value==7 && bytes[0]=='D' && bytes[6]=='E');
        print("PASS writable USB/ext4: write/flush, create/grow/append/truncate/delete, journal sync and read-only remount\n");
    }
    if(job->hotplug){
        CHECK(!CALL(part.control,part.cookie,BLOCK_RETAIN,0).status);
        if(job->write)CHECK(!CALL(unmount,fs.cookie,0,0).status && !CALL(mount,&part,&fs,EXT4_MOUNT_WRITE).status);
        print("WAIT STORAGE REMOVE\n");deadline=CALL(clock,0,0,0).value+5000000000;
        do{CHECK(!CALL(info,&status,0,0).status && CALL(clock,0,0,0).value<deadline);if(status.configured!=2)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.configured!=2);
        for(unsigned n=0;n<sizeof(bytes);++n)bytes[n]=0xcc;
        CHECK(read(fs.cookie,(uintptr_t)path,0,(uintptr_t)bytes,sizeof(bytes),0).status && bytes[0]==0xcc);
        CHECK(CALL(open,&request,&refused,sizeof(refused)).status);
    }
    if(job->hotplug){
        kernel_surface_call fsinfo=kernel_surface_find(hub,"ext4_fs.info"),storage_info=kernel_surface_find(hub,"usb_storage.info");struct ext4_info fi;struct usb_storage_record sr;
        deadline=CALL(clock,0,0,0).value+5000000000;
        while(!CALL(fsinfo,fs.cookie,&fi,0).status){CHECK(CALL(clock,0,0,0).value<deadline);CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,CALL(clock,0,0,0).value+10000000,0).status);}
        CHECK(part.read(part.cookie,0,1,(uintptr_t)bytes,sizeof(bytes),0).status && CALL(part.control,part.cookie,BLOCK_RETAIN,0).status);
        CHECK(!CALL(part.control,part.cookie,BLOCK_RELEASE,0).status);
        while(!CALL(storage_info,disk.cookie,&sr,0).status){CHECK(CALL(clock,0,0,0).value<deadline);CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,CALL(clock,0,0,0).value+10000000,0).status);}
        CHECK(CALL(unmount,fs.cookie,0,0).status && CALL(unselect,part.cookie,0,0).status);
        print("PASS disconnected mount/slice/storage reclaimed; stale references fail\n");
    }else CHECK(!CALL(unmount,fs.cookie,0,0).status && !CALL(unselect,part.cookie,0,0).status && !CALL(close,disk.cookie,0,0).status);
    CHECK(disk.read(disk.cookie,0,1,(uintptr_t)bytes,sizeof(bytes),0).status);
    if(job->hotplug){
        print("WAIT STORAGE ADD\n");deadline=CALL(clock,0,0,0).value+5000000000;
        do{CHECK(!CALL(info,&status,0,0).status && CALL(clock,0,0,0).value<deadline);if(status.configured!=expected)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.configured!=expected);
        struct usb_ref old=request.device;count=0;flags=CALL(cpu,1,0,0).value;for(const struct usb_device *p=((const struct usb_table *)usb->address)->first;p && count<expected;p=p->next)if(p->state==USB_CONFIGURED)refs[count++]=(struct usb_ref){p->identity.id,p->identity.generation};CALL(cpu,2,flags,0);
        for(unsigned n=0;n<count;++n){struct usb_device dev;CHECK(!CALL(device,&refs[n],&dev,0).status);if(dev.hub || dev.class_code==9)continue;struct usb_interface intf;CHECK(!CALL(interface,&refs[n],0,&intf).status);if(intf.class_code==8){request.device=refs[n];request.interface=intf.number;}}
        CHECK(request.device.id!=old.id || request.device.generation!=old.generation);
    }
    opened=CALL(open,&request,&disk,sizeof(disk));
    if(opened.status){print("FAIL storage reopen error ");char digit='0'+opened.value;__asm__ volatile("outb %0,$0xe9"::"a"(digit));print("\n");}CHECK(!opened.status);
    if(job->hotplug){uint64_t old=fs.cookie;CHECK(!select((uintptr_t)&disk,(uintptr_t)g->data,(uintptr_t)&part,sizeof(part),0,0).status && !CALL(mount,&part,&fs,job->write?EXT4_MOUNT_WRITE:0).status && fs.cookie!=old);CHECK(!CALL(stat,fs.cookie,path,&st).status);CHECK(!CALL(unmount,fs.cookie,0,0).status && !CALL(unselect,part.cookie,0,0).status);}
    CHECK(!CALL(close,disk.cookie,0,0).status);
    CHECK(!CALL(stop,0,0,0).status);deadline=CALL(clock,0,0,0).value+5000000000;
    do{CHECK(!CALL(info,&status,0,0).status && CALL(clock,0,0,0).value<deadline);if(status.running)CHECK(!CALL(thread,THREAD_BLOCK_UNTIL,deadline,0).status);}while(status.running);
    CHECK(!status.failed && !CALL(stop,0,0,0).status && !CALL(watch,0,0,0).status);job->done=1;
    print("PASS USB storage/ext4: real BOT/SCSI block reads, configured GPT identity, checksummed ext4 stat/list/read, EOF, lifetime pins, stale cookies and reopen\n");return module_result(1,1);
failed:job->errors=line;job->done=1;return module_result(line,0);
#undef CHECK
}
