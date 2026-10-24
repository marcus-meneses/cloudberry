#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/virtio/pci.h"
#include "../kmodules/shared/virtio/gpu-wire.h"
static void u32(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(i*8);}
static void capability(unsigned char *c,unsigned at,unsigned next,unsigned type,uint32_t offset,uint32_t length){c[at]=9;c[at+1]=next;c[at+2]=type==2?20:16;c[at+3]=type;c[at+4]=4;u32(c+at+8,offset);u32(c+at+12,length);}
int main(void){
 unsigned char c[256]={0},copy[256];struct virtio_layout layout;c[6]=16;c[0x34]=0x40;capability(c,0x40,0x50,1,0,56);capability(c,0x50,0x68,2,4096,4096);u32(c+0x60,4);capability(c,0x68,0x78,3,8192,1);capability(c,0x78,0,4,12288,16);
 assert(virtio_pci_layout(c,&layout)&&virtio_region_fits(&layout.device,16384)&&!virtio_region_fits(&layout.device,12288));uint64_t offset;assert(virtio_notify_offset(&layout,1023,&offset)&&offset==4092&&!virtio_notify_offset(&layout,1024,&offset));
 for(unsigned mode=0;mode<8;mode++){memcpy(copy,c,256);if(mode==0)copy[0x41]=0x40;if(mode==1)copy[0x41]=0x51;if(mode==2)copy[0x42]=255;if(mode==3)copy[0x44]=6;if(mode==4)copy[0x53]=1;if(mode==5)u32(copy+0x4c,55);if(mode==6)copy[0x68+2]=4;if(mode==7)u32(copy+0x80,UINT32_MAX-2);assert(!virtio_pci_layout(copy,&layout));}
 _Alignas(16) unsigned char memory[4096];struct virtq q;assert(!virtq_setup(&q,memory+1,4095,8)&&!virtq_setup(&q,memory,4096,3)&&virtq_setup(&q,memory,4096,8));uint32_t bytes;
 for(unsigned i=0;i<70000;i++){assert(virtq_publish(&q,0x1000,40,0x2000,128));assert(!virtq_publish(&q,0x1000,40,0x2000,128));assert(q.desc[0].next==1&&q.desc[0].flags==1&&q.desc[1].flags==2&&q.avail->ring[(uint16_t)(q.produced-1)%q.depth]==0);assert(!virtq_complete(&q,&bytes));q.used->ring[q.consumed%q.depth]=(struct virtq_used_entry){0,24};q.used->index=q.consumed+1;assert(virtq_complete(&q,&bytes)==1&&bytes==24&&!q.pending);}
 for(unsigned mode=0;mode<5;mode++){assert(virtq_setup(&q,memory,4096,8)&&virtq_publish(&q,0x1000,40,0x2000,128));q.used->ring[0]=(struct virtq_used_entry){mode==0?1:0,mode==1?129:mode==2?23:24};q.used->index=mode==3?2:1;if(mode==4)q.pending=0;assert(virtq_complete(&q,&bytes)==-1&&q.failed&&!virtq_publish(&q,0x1000,40,0x2000,128));}
 struct vg_header h={VG_OK,1,123,0,0};assert(vg_reply_valid(&h,sizeof(h),123,VG_OK));assert(!vg_reply_valid(&h,sizeof(h)-1,123,VG_OK)&&!vg_reply_valid(&h,sizeof(h),124,VG_OK));h.flags=0;assert(!vg_reply_valid(&h,sizeof(h),123,VG_OK));
 puts("PASS virtio: capability bounds/cycles, BAR/notify validation, split queue wrap, malformed completions, one in-flight chain and GPU fence validation");
}
