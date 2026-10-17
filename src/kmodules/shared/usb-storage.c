#include "usb-storage.h"
#include "config.h"
#include "threads.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
struct storage {struct usb_storage_record record;struct storage *next;uint64_t owner,refs;uint32_t tag;unsigned input,output,busy;unsigned char buffer[4096];};
static struct {const struct kernel_surfaces *hub;uintptr_t heap;uint64_t serial;struct storage *first;struct usb_storage_table table;kernel_surface_call cpu,thread,irq,allocate,free,device,interface,endpoint,claim,release,transfer,control,clock;uint64_t timeout;} state;
static void copy(void *out,const void *in,size_t n){unsigned char *d=out;const unsigned char *s=in;while(n--)*d++=*s++;}
static uint32_t le32(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint32_t be32(const unsigned char *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
}
static void putle(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=n>>(i*8);}
static void putbe(unsigned char *p,uint64_t n,unsigned bytes){while(bytes){p[--bytes]=n;n>>=8;}}
static int foreground(void){uint64_t flags;__asm__ volatile("pushfq;pop %0":"=r"(flags));return (flags&512) && state.hub && !CALL(state.cpu,CPU_CURRENT,0,0).value && !CALL(state.cpu,CPU_DEPTH,0,0).value && !CALL(state.irq,0,0,0).value && CALL(state.thread,THREAD_CURRENT,0,0).value;
}
static uint64_t lock(void){return CALL(state.cpu,CPU_ENTER,0,0).value;
}
static void unlock(uint64_t f){CALL(state.cpu,CPU_LEAVE,f,0);}
static struct storage *find(uint64_t cookie){for(struct storage *s=state.first;s;s=s->next)if(s->record.block.cookie==cookie)return s;
    return NULL;
}
static int alive(struct storage *s){struct usb_device d;return !CALL(state.device,&s->record.device,&d,0).status && d.state==USB_CONFIGURED;
}
static uint64_t deadline(void){struct kernel_surface_result r=CALL(state.clock,0,0,0);return r.status || r.value>UINT64_MAX-state.timeout?0:r.value+state.timeout;
}
static int transfer(struct storage *s,unsigned endpoint,void *buffer,unsigned bytes,uint64_t end,unsigned *actual) {
    struct usb_io io={.device=s->record.device,.owner=s->owner,.interface=s->record.interface,.endpoint=endpoint,.bytes=bytes,.buffer=buffer,.deadline_ns=end};
    struct host_transfer_result result={0};struct kernel_surface_result r=CALL(state.transfer,&io,&result,0);
    *actual=result.bytes;return !r.status && result.state==HOST_COMPLETE && result.bytes<=bytes;
}
/* 0=success, 1=SCSI command failure, -1=unsafe transport/protocol state. */
static int command(struct storage *s,const unsigned char *cdb,unsigned length,void *buffer,unsigned bytes,unsigned *actual,int input) {
    unsigned char cbw[31]={0},csw[13]={0};unsigned done;uint64_t end=deadline();if(!end || !length || length>16 || bytes>4096)return -1;
    if(++s->tag==0)++s->tag;
    putle(cbw,0x43425355);putle(cbw+4,s->tag);putle(cbw+8,bytes);cbw[12]=input?0x80:0;cbw[13]=s->record.lun;cbw[14]=length;copy(cbw+15,cdb,length);++s->record.commands;
    if(!transfer(s,s->output,cbw,31,end,&done) || done!=31)return -1;
    *actual=0;
    if(bytes && !transfer(s,input?s->input:s->output,buffer,bytes,end,actual))return -1;
    if(!transfer(s,s->input,csw,13,end,&done) || done!=13 || le32(csw)!=0x53425355 || le32(csw+4)!=s->tag || le32(csw+8)>bytes || csw[12]>1)return -1;
    if(csw[12])return 1;
    if(le32(csw+8)!=bytes-*actual)return -1;
    return 0;
}
static int scsi_io(struct storage *s,const unsigned char *cdb,unsigned length,void *buffer,unsigned bytes,unsigned *actual,int input) {
    int status=command(s,cdb,length,buffer,bytes,actual,input);
    if(status<0){s->record.failed=1;s->record.error=alive(s)?STORAGE_TRANSPORT:STORAGE_DISCONNECTED;return 0;
    }
    if(status) {
        unsigned char sense[18]={0},request[6]={3,0,0,0,18,0};unsigned n=0;
        s->record.sense_key=s->record.asc=s->record.ascq=0;
        int sensed=command(s,request,6,sense,18,&n,1);
        unsigned format=sense[0]&127;
        if(!sensed && n>=14 && (format==0x70 || format==0x71)){s->record.sense_key=sense[2]&15;s->record.asc=sense[12];s->record.ascq=sense[13];}
        else if(!sensed && n>=8 && (format==0x72 || format==0x73)){s->record.sense_key=sense[1]&15;s->record.asc=sense[2];s->record.ascq=sense[3];}
        else s->record.failed=1;
        if(s->record.sense_key==6)s->record.failed=1; /* A published medium identity cannot silently change. */
        s->record.error=STORAGE_COMMAND;return 0;
    }
    s->record.error=STORAGE_OK;return 1;
}
static int scsi(struct storage *s,const unsigned char *cdb,unsigned length,void *buffer,unsigned bytes,unsigned *actual){return scsi_io(s,cdb,length,buffer,bytes,actual,1);}
struct kernel_surface_result usb_storage_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(void *)a;if(state.hub)return module_result(1,state.hub==hub && state.heap==b);
    if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC || !b)return module_result(0,0);
#define GET(field,name) state.field=kernel_surface_find_version(hub,name,1,0);if(!state.field)return module_result(0,0)
    GET(cpu,"cpu.control");GET(thread,"threads.control");GET(irq,"irq.context");GET(allocate,"mem.calloc");GET(free,"mem.free");GET(device,"usb.device");GET(interface,"usb.interface");GET(endpoint,"usb.endpoint");GET(claim,"usb.claim");GET(release,"usb.release");GET(transfer,"usb.transfer");GET(control,"usb.control");GET(clock,"clock.now_ns");
#undef GET
    if(CALL(state.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);
    state.hub=hub;state.heap=b;state.timeout=1000000000;
    const struct kernel_hub_table *t=kernel_hub_find_table(hub,"config.storage.timeout_ms");
    if(t){/* The typed config is owned and immutable. */
        const struct config_value *v=(const void *)t->address;
        if(v->type==CONFIG_UINT && v->bytes==8)state.timeout=*(const uint64_t *)v->data*1000000;
    }
    struct kernel_hub_table table={.name="usb_storage.devices",.address=(uintptr_t)&state.table,.bytes=sizeof(state.table),.entry_bytes=sizeof(state.table)};
    kernel_surface_call publish=kernel_surface_find_version(hub,"hub.publish_table",1,0);
    if(!publish || CALL(publish,&table,0,0).status){state.hub=NULL;return module_result(0,0);
    }return module_result(1,1);
}
struct kernel_surface_result usb_storage_surface_open(MODULE_ARGS) {
    MODULE_UNUSED();const struct usb_storage_open *r=(void *)a;struct block_device *out=(void *)b;
    if(!block_capacity(c))return module_result(STORAGE_ARGUMENT,0);
    if(out)block_zero(out,c);
    if(!foreground() || !r || !out || r->lun>15 || state.serial==UINT64_MAX)return module_result(STORAGE_ARGUMENT,0);
    struct usb_device dev;struct usb_interface intf;unsigned found=0;
    if(CALL(state.device,&r->device,&dev,0).status || dev.state!=USB_CONFIGURED)return module_result(STORAGE_DISCONNECTED,0);
    for(unsigned i=0;i<dev.interfaces;++i)if(!CALL(state.interface,&r->device,i,&intf).status && intf.number==r->interface && !intf.alternate){found=1;break;}
    if(!found || intf.class_code!=8 || intf.subclass!=6 || intf.protocol!=0x50)return module_result(STORAGE_UNSUPPORTED,0);
    if(intf.owner)return module_result(STORAGE_BUSY,0);
    struct kernel_surface_result allocated=CALL(state.allocate,state.heap,1,sizeof(struct storage));if(allocated.status)return module_result(STORAGE_MEMORY,0);
    struct storage *s=(void *)allocated.value;s->record.device=r->device;s->record.interface=r->interface;s->record.lun=r->lun;
    uint64_t serial_flags=lock();
    if(state.serial==UINT64_MAX){unlock(serial_flags);CALL(state.free,state.heap,s,0);return module_result(STORAGE_MEMORY,0);}
    s->record.block.cookie=++state.serial;unlock(serial_flags);s->owner=(uintptr_t)s;
    for(unsigned ep=1;ep<256;++ep){struct usb_endpoint endpoint;
        if(!state.endpoint((uintptr_t)&r->device,r->interface,ep,(uintptr_t)&endpoint,0,0).status && endpoint.transfer_type==2 && endpoint.supported) {
            unsigned *slot=(ep&128)?&s->input:&s->output;if(*slot)goto unsupported;*slot=ep;
        }
    }
    if(!s->input || !s->output)goto unsupported;
    if(CALL(state.claim,&r->device,r->interface,s->owner).status)goto unsupported;
    /* Get Max LUN is optional: a STALL means LUN zero. Other failures are fatal. */
    unsigned char maxlun=0;struct usb_io io={.device=r->device,.owner=s->owner,.interface=r->interface,.bytes=1,.buffer=&maxlun,.setup={0xa1,0xfe,0,0,r->interface,0,1,0}};struct host_transfer_result result={0};
    struct kernel_surface_result get=CALL(state.control,&io,&result,0);
    if((get.status && result.completion_code!=6) || (!get.status && (result.bytes!=1 || maxlun>15)) || r->lun>maxlun)goto fail;
    unsigned char inquiry[36],cdb[16]={0x12,0,0,0,36,0};unsigned n;
    if(!scsi(s,cdb,6,inquiry,36,&n) || n<36 || (inquiry[0]&0xe0) || ((inquiry[0]&31)!=0 && (inquiry[0]&31)!=5))goto fail;
    s->record.peripheral=inquiry[0]&31;
    /* Newly attached SCSI media can report UNIT ATTENTION. Consume it only
     * during initial discovery, before publishing geometry or file references. */
    int ready=0;unsigned char tur[6]={0};
    for(unsigned attempt=0;attempt<3;++attempt){
        if(scsi(s,tur,6,NULL,0,&n)){ready=1;break;}
        if(s->record.error!=STORAGE_COMMAND || s->record.sense_key!=6)break;
        s->record.failed=0;
    }
    if(!ready)goto fail;
    for(unsigned i=0;i<16;++i)cdb[i]=0;
    cdb[0]=0x25;
    if(!scsi(s,cdb,10,s->buffer,8,&n) || n!=8)goto fail;
    uint64_t last=be32(s->buffer);unsigned block=be32(s->buffer+4);
    if(last==UINT32_MAX){for(unsigned i=0;i<16;++i)cdb[i]=0;
    cdb[0]=0x9e;cdb[1]=0x10;cdb[13]=32;
        if(!scsi(s,cdb,16,s->buffer,32,&n) || n!=32)goto fail;
        last=(uint64_t)be32(s->buffer)<<32|be32(s->buffer+4);block=be32(s->buffer+8);}
    unsigned char mode[4]={0},sense_mode[6]={0x1a,8,0x3f,0,4,0};int writable=0;
    if(s->record.peripheral==0){
        if(scsi(s,sense_mode,6,mode,4,&n) && n==4)writable=!(mode[2]&128);
        else if(s->record.failed)goto fail;
    }
    if(writable){unsigned char sync[10]={0x35};if(!scsi_io(s,sync,10,NULL,0,&n,0)){if(s->record.failed)goto fail;writable=0;}}
    s->record.block=(struct block_device){.major=1,.minor=1,.block_bytes=block,.max_transfer_bytes=4096,.flags=writable?0:BLOCK_READ_ONLY,.blocks=last+1,.cookie=s->record.block.cookie,.read=kernel_surface_find_version(state.hub,"usb_storage.read",1,0),.control=kernel_surface_find_version(state.hub,"usb_storage.control",1,0),.write=writable?kernel_surface_find_version(state.hub,"usb_storage.write",1,0):NULL,.flush=writable?kernel_surface_find_version(state.hub,"usb_storage.flush",1,0):NULL};
    s->record.block.root_read=s->record.block.read;s->record.block.root_cookie=s->record.block.cookie;
    if(!block_valid(&s->record.block) || last==UINT64_MAX)goto fail;
    uint64_t flags=lock();s->next=state.first;s->record.next=state.table.first;state.first=s;state.table.first=&s->record;++state.table.count;unlock(flags);block_export(out,&s->record.block,c);return module_result(s->record.block.cookie,1);
unsupported:
    CALL(state.free,state.heap,s,0);return module_result(STORAGE_UNSUPPORTED,0);
fail: {
    unsigned error=s->record.error?s->record.error:STORAGE_PROTOCOL;
    if(CALL(state.release,&r->device,r->interface,s->owner).status && alive(s)){
        /* Another interface can temporarily own the USB execution slot. Keep
         * the failed claim reachable for explicit retryable close, never leak it. */
        s->record.failed=1;s->record.error=error;
        uint64_t flags=lock();s->next=state.first;s->record.next=state.table.first;state.first=s;state.table.first=&s->record;++state.table.count;unlock(flags);
        block_export(out,&s->record.block,c);return module_result(error,0);
    }
    CALL(state.free,state.heap,s,0);return module_result(error,0);
}
}
struct kernel_surface_result usb_storage_surface_read(MODULE_ARGS) {
    MODULE_UNUSED();if(!foreground())return module_result(0,0);
    uint64_t flags=lock();struct storage *s=find(a);
    if(!s || s->busy || s->record.failed || b>s->record.block.blocks || c>s->record.block.blocks-b || c>4096/s->record.block.block_bytes || e<c*s->record.block.block_bytes || (c && !d)){unlock(flags);return module_result(0,0);
    }s->busy=1;unlock(flags);
    unsigned bytes=c*s->record.block.block_bytes,n=0;int ok=alive(s);unsigned char cdb[16]={0};unsigned length;
    if(b<=UINT32_MAX && c<=65535 && (!c || c-1<=UINT32_MAX-b)){cdb[0]=0x28;putbe(cdb+2,b,4);putbe(cdb+7,c,2);length=10;}
    else {cdb[0]=0x88;putbe(cdb+2,b,8);putbe(cdb+10,c,4);length=16;}
    if(c && ok)ok=scsi(s,cdb,length,s->buffer,bytes,&n) && n==bytes;
    if(ok && c){copy((void *)d,s->buffer,bytes);++s->record.reads;}else if(!ok && !alive(s)){s->record.failed=1;s->record.error=STORAGE_DISCONNECTED;}
    flags=lock();s->busy=0;unlock(flags);return module_result(ok?bytes:0,ok);
}
struct kernel_surface_result usb_storage_surface_write(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground())return module_result(0,0);
    uint64_t flags=lock();struct storage *s=find(a);
    if(!s || s->busy || s->record.failed || !block_writable(&s->record.block) || b>s->record.block.blocks || c>s->record.block.blocks-b || c>4096/s->record.block.block_bytes || e<c*s->record.block.block_bytes || (c && !d)){unlock(flags);return module_result(0,0);}
    s->busy=1;unlock(flags);unsigned bytes=c*s->record.block.block_bytes,n=0;int ok=alive(s);unsigned char cdb[16]={0};unsigned length;
    if(b<=UINT32_MAX && c<=65535 && (!c || c-1<=UINT32_MAX-b)){cdb[0]=0x2a;putbe(cdb+2,b,4);putbe(cdb+7,c,2);length=10;}
    else {cdb[0]=0x8a;putbe(cdb+2,b,8);putbe(cdb+10,c,4);length=16;}
    if(c && ok){copy(s->buffer,(void *)d,bytes);ok=scsi_io(s,cdb,length,s->buffer,bytes,&n,0) && n==bytes;}
    if(!ok){s->record.failed=1;if(!alive(s))s->record.error=STORAGE_DISCONNECTED;}
    flags=lock();s->busy=0;unlock(flags);return module_result(ok?bytes:0,ok);
}
struct kernel_surface_result usb_storage_surface_flush(MODULE_ARGS){
    MODULE_UNUSED();if(!foreground())return module_result(0,0);
    uint64_t flags=lock();struct storage *s=find(a);if(!s || s->busy || s->record.failed || !block_writable(&s->record.block)){unlock(flags);return module_result(0,0);}
    s->busy=1;unlock(flags);unsigned char cdb[10]={0x35};unsigned n;int ok=alive(s) && scsi_io(s,cdb,10,NULL,0,&n,0);
    if(!ok)s->record.failed=1;
    flags=lock();s->busy=0;unlock(flags);return module_result(1,ok);
}
struct kernel_surface_result usb_storage_surface_control(MODULE_ARGS) {
    MODULE_UNUSED();if(!foreground())return module_result(0,0);
    uint64_t flags=lock();struct storage *s=find(a);int ok=s && (b==BLOCK_RETAIN?(!s->busy && !s->record.failed && s->refs<UINT64_MAX):(b==BLOCK_RELEASE && s->refs));if(ok){if(b==BLOCK_RETAIN)++s->refs;else --s->refs;}unlock(flags);return module_result(1,ok);
}
struct kernel_surface_result usb_storage_surface_info(MODULE_ARGS) {
    MODULE_UNUSED();if(!state.hub || !b || CALL(state.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);
    uint64_t flags=lock();struct storage *s=find(a);if(s)*(struct usb_storage_record *)b=s->record;unlock(flags);return module_result(1,s!=NULL);
}
struct kernel_surface_result usb_storage_surface_close(MODULE_ARGS) {
    MODULE_UNUSED();if(!foreground())return module_result(0,0);
    uint64_t flags=lock();struct storage *s=find(a);if(!s || s->busy || s->refs){unlock(flags);return module_result(0,0);
    }s->busy=1;unlock(flags);
    int ok=!CALL(state.release,&s->record.device,s->record.interface,s->owner).status;
    if(!ok){struct usb_device d;ok=CALL(state.device,&s->record.device,&d,0).status || d.state==USB_DETACHED || d.state==USB_FAILED;}
    flags=lock();if(!ok){s->busy=0;unlock(flags);return module_result(0,0);
    }
    struct storage **p=&state.first;struct usb_storage_record **q=&state.table.first;while(*p!=s)p=&(*p)->next;while(*q!=&s->record)q=&(*q)->next;*p=s->next;*q=s->record.next;--state.table.count;unlock(flags);CALL(state.free,state.heap,s,0);return module_result(1,1);
}
