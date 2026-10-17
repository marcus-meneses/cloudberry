/* Production BOT command parser exercised with deterministic USB completions. */
#include "../kmodules/shared/usb-storage.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned phase,mode,opcode,cbw_count,refs,allocations,deny_release;static uint64_t claim_owner;static uint32_t tag,expected;static unsigned input;
static struct kernel_surface_result fake_cpu(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result fake_thread(MODULE_ARGS){MODULE_UNUSED();return module_result(a==THREAD_CURRENT?1:0,1);}
static struct kernel_surface_result fake_clock(MODULE_ARGS){MODULE_UNUSED();return module_result(1,1);}
static struct kernel_surface_result fake_device(MODULE_ARGS){MODULE_UNUSED();*(struct usb_device *)b=(struct usb_device){.state=mode==9?USB_DETACHED:USB_CONFIGURED,.interfaces=1};return module_result(1,1);}
static struct kernel_surface_result fake_transfer(MODULE_ARGS){
    MODULE_UNUSED();struct usb_io *io=(void *)a;unsigned char *p=io->buffer;
    if(!phase){assert(io->endpoint==2 && io->bytes==31 && le32(p)==0x43425355 && (p[12]==128 || p[12]==0) && p[14]<=16);tag=le32(p+4);expected=le32(p+8);opcode=p[15];input=p[12]==128;++cbw_count;phase=expected?1:2;}
    else if(phase==1){assert(io->endpoint==(input?0x81:2) && io->bytes==expected);if(input)memset(p,0x5a,io->bytes);else for(unsigned k=0;k<io->bytes;++k)assert(p[k]==0x69);if(opcode==3){memset(p,0,io->bytes);p[0]=0x70;p[2]=6;p[12]=0x28;if(mode==10){p[0]=0x72;p[1]=5;p[2]=0x21;p[12]=0;}}phase=2;}
    else {assert(io->endpoint==0x81 && io->bytes==13);memset(p,0,13);putle(p,mode==1?0:0x53425355);putle(p+4,tag+(mode==2));putle(p+8,mode==3?expected+1:mode==4?1:0);p[12]=mode==5?2:(mode==6 || mode==10) && opcode!=3?1:0;phase=0;}
    *(struct host_transfer_result *)b=(struct host_transfer_result){.state=mode==8?HOST_DISCONNECTED:HOST_COMPLETE,.bytes=io->bytes-(mode==7 && io->bytes==13)};
    return module_result(io->bytes,mode!=8);
}
static struct kernel_surface_result parent_control(MODULE_ARGS){MODULE_UNUSED();if(b==BLOCK_RETAIN)++refs;else {assert(refs);--refs;}return module_result(1,1);}
static struct kernel_surface_result fake_alloc(MODULE_ARGS){MODULE_UNUSED();void *p=calloc(b,c);if(p)++allocations;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result fake_free(MODULE_ARGS){MODULE_UNUSED();assert(allocations);free((void *)b);--allocations;return module_result(1,1);}
static struct kernel_surface_result fake_interface(MODULE_ARGS){MODULE_UNUSED();*(struct usb_interface *)c=(struct usb_interface){.class_code=8,.subclass=6,.protocol=0x50,.owner=claim_owner};return module_result(1,1);}
static struct kernel_surface_result fake_endpoint(MODULE_ARGS){MODULE_UNUSED();if(c!=2 && c!=0x81)return module_result(0,0);*(struct usb_endpoint *)d=(struct usb_endpoint){.transfer_type=2,.supported=1};return module_result(1,1);}
static struct kernel_surface_result fake_claim(MODULE_ARGS){MODULE_UNUSED();assert(!claim_owner);claim_owner=c;return module_result(1,1);}
static struct kernel_surface_result fake_release(MODULE_ARGS){MODULE_UNUSED();assert(claim_owner==c);if(deny_release)return module_result(0,0);claim_owner=0;return module_result(1,1);}
static struct kernel_surface_result fake_control(MODULE_ARGS){MODULE_UNUSED();struct usb_io *io=(void *)a;*(unsigned char *)io->buffer=0;*(struct host_transfer_result *)b=(struct host_transfer_result){.state=HOST_COMPLETE,.bytes=1};return module_result(1,1);}
int main(void){
    state.hub=(void *)1;state.cpu=fake_cpu;state.thread=fake_thread;state.irq=fake_cpu;state.clock=fake_clock;state.device=fake_device;state.transfer=fake_transfer;state.timeout=100;
    struct storage s={.input=0x81,.output=2,.owner=1};s.record.block=(struct block_device){.major=1,.block_bytes=512,.max_transfer_bytes=4096,.blocks=16,.cookie=1,.read=usb_storage_surface_read,.control=parent_control};
    s.record.device=(struct usb_ref){1,1};state.first=&s;unsigned char cdb[16]={0x88},data[512];unsigned actual;
    for(mode=0;mode<=8;++mode){phase=0;s.record.failed=0;s.record.error=0;memset(data,0xcc,sizeof(data));int ok=scsi(&s,cdb,16,data,512,&actual);
        if(mode==0)assert(ok && actual==512 && !s.record.failed);
        else if(mode==6)assert(!ok && s.record.failed && s.record.error==STORAGE_COMMAND && s.record.sense_key==6 && s.record.asc==0x28);
        else assert(!ok && s.record.failed);
    }
    mode=10;phase=0;s.record.failed=0;assert(!scsi(&s,cdb,16,data,512,&actual) && !s.record.failed && s.record.sense_key==5 && s.record.asc==0x21);
    mode=0;phase=0;s.record.failed=0;memset(data,0xcc,sizeof(data));assert(!usb_storage_surface_read(1,15,1,(uintptr_t)data,512,0).status && data[0]==0x5a);
    unsigned calls=cbw_count;assert(usb_storage_surface_read(1,16,1,(uintptr_t)data,512,0).status);assert(usb_storage_surface_read(1,0,9,(uintptr_t)data,512,0).status);assert(usb_storage_surface_read(1,0,1,(uintptr_t)data,511,0).status);assert(cbw_count==calls);
    mode=1;memset(data,0xcc,sizeof(data));assert(usb_storage_surface_read(1,0,1,(uintptr_t)data,512,0).status && data[0]==0xcc);
    mode=9;s.record.failed=0;assert(usb_storage_surface_read(1,0,1,(uintptr_t)data,512,0).status && s.record.error==STORAGE_DISCONNECTED);
    s.record.failed=0;s.busy=1;assert(usb_storage_surface_control(1,BLOCK_RETAIN,0,0,0,0).status);s.busy=0;
    assert(!usb_storage_surface_control(1,BLOCK_RETAIN,0,0,0,0).status && s.refs==1);assert(usb_storage_surface_close(1,0,0,0,0,0).status);assert(!usb_storage_surface_control(1,BLOCK_RELEASE,0,0,0,0).status && !s.refs);assert(usb_storage_surface_control(1,BLOCK_RELEASE,0,0,0,0).status);
    mode=0;phase=0;s.record.failed=0;s.record.block.minor=1;s.record.block.flags=0;s.record.block.write=usb_storage_surface_write;s.record.block.flush=usb_storage_surface_flush;memset(data,0x69,sizeof(data));
    assert(!usb_storage_surface_write(1,2,1,(uintptr_t)data,512,0).status && opcode==0x2a && data[0]==0x69);
    s.record.block.blocks=UINT64_C(1)<<34;assert(!usb_storage_surface_write(1,UINT64_C(1)<<33,1,(uintptr_t)data,512,0).status && opcode==0x8a);
    assert(!usb_storage_surface_flush(1,0,0,0,0,0).status && opcode==0x35);
    s.record.block.flags=BLOCK_READ_ONLY;calls=cbw_count;assert(usb_storage_surface_write(1,0,1,(uintptr_t)data,512,0).status && usb_storage_surface_flush(1,0,0,0,0,0).status && calls==cbw_count);
    state.first=NULL;state.allocate=fake_alloc;state.free=fake_free;state.interface=fake_interface;state.endpoint=fake_endpoint;state.claim=fake_claim;state.release=fake_release;state.control=fake_control;
    struct usb_storage_open request={.device={1,1}};struct block_device cleanup;mode=0;phase=0;deny_release=1;
    assert(usb_storage_surface_open((uintptr_t)&request,(uintptr_t)&cleanup,0,0,0,0).status && cleanup.cookie && allocations==1 && state.table.count==1 && claim_owner);
    assert(!block_valid(&cleanup));deny_release=0;assert(!usb_storage_surface_close(cleanup.cookie,0,0,0,0,0).status && !allocations && !claim_owner && !state.table.count);
    puts("PASS USB storage: BOT wrappers/tags/residue/phase, short status, disconnect, sense, bounds, unchanged failed-read buffer and lifetime pins");
}
