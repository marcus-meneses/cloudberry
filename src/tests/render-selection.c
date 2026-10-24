#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/render-selection.h"
static struct {struct hwenum_table h;struct hwenum_descriptor entries[4];} inventory;
static unsigned mode,probed;static uint64_t accept=1;
static struct kernel_surface_result probe_stub(MODULE_ARGS){MODULE_UNUSED();struct render_probe *p=(void *)a;++probed;assert(p->major==1&&p->bytes==sizeof(*p));if(p->hardware_index!=accept||mode==1)return module_result(0,0);p->capabilities=RENDER_GPU_COMPLETE;if(mode==2)p->capabilities&=~RENDER_GPU_FONT;if(mode==3)p->hardware_index++;if(mode==4)p->vendor^=1;if(mode==5)p->bytes--;if(mode==6)p->minor=1;return module_result(1,1);}
int main(void){
 struct render_selection s;inventory.h=(struct hwenum_table){.magic=HWENUM_TABLE_MAGIC,.major=1,.count=3,.capacity=4,.header_bytes=sizeof(inventory.h),.entry_bytes=sizeof(struct hwenum_descriptor)};
 inventory.entries[0].kind=HWENUM_MEMORY;
 inventory.entries[1]=(struct hwenum_descriptor){.kind=HWENUM_PCI,.data.pci={.class_code=3,.vendor=0x1af4,.device=0x1050}};
 inventory.entries[2]=(struct hwenum_descriptor){.kind=HWENUM_PCI,.data.pci={.class_code=3,.vendor=0x1002,.device=0x1234}};
 assert(render_select(NULL,0,probe_stub,1,1,&s)==RENDER_VGA&&s.reason==RENDER_NO_GPU);
 assert(render_select(&inventory.h,sizeof(inventory),NULL,1,1,&s)==RENDER_VGA&&s.reason==RENDER_NO_DRIVER&&s.devices==2);
 assert(render_select(&inventory.h,sizeof(inventory),probe_stub,0,1,&s)==RENDER_VGA&&!s.probes);
 assert(render_select(&inventory.h,sizeof(inventory),probe_stub,1,0,&s)==RENDER_VGA&&s.candidates==1&&s.devices==2);
 assert(render_select(&inventory.h,sizeof(inventory),probe_stub,1,1,&s)==RENDER_OPENGL&&s.hardware_index==1&&s.vendor==0x1af4&&s.device==0x1050&&s.reason==RENDER_ACCELERATED);
 accept=2;assert(render_select(&inventory.h,sizeof(inventory),probe_stub,1,1,&s)==RENDER_OPENGL&&s.hardware_index==2&&s.probes==2);
 accept=1;for(mode=1;mode<=6;mode++)assert(render_select(&inventory.h,sizeof(inventory),probe_stub,1,1,&s)==RENDER_VGA&&!s.candidates);
 mode=0;unsigned before=probed;assert(render_select(&inventory.h,sizeof(inventory.h),probe_stub,1,1,&s)==RENDER_VGA&&probed==before);
 inventory.h.count=5;assert(render_select(&inventory.h,sizeof(inventory),probe_stub,1,1,&s)==RENDER_VGA&&probed==before);
 assert(!strcmp(render_provider_role("display-vga"),"display")&&!strcmp(render_provider_role("graphics-opengl"),"graphics"));assert(!strcmp(render_provider_role("graphics-evil"),"graphics-evil"));
 puts("PASS renderer selection: discovery, complete capability gating, multiple GPUs, missing driver/providers, malformed probes and exact provider-role aliases");
}
