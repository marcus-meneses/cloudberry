/* A composite device's two interrupt endpoints and EP0 must coexist. */
#include "../kmodules/shared/usb.c"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static unsigned live,nested;static uint32_t *stop_after_submit;
static struct device fixture;
static struct usb_host host;
static struct {struct host_transfer_request request;struct host_transfer_result result;} jobs[3];
static unsigned char first[4],second[4],control_bytes[4];
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result clock_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(100,1);}
static struct kernel_surface_result thread_stub(MODULE_ARGS){MODULE_UNUSED();assert(a!=THREAD_BLOCK);return module_result(1,1);}
static struct kernel_surface_result alloc_stub(MODULE_ARGS){MODULE_UNUSED();void *p=calloc(b,c);if(p)++live;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result free_stub(MODULE_ARGS){MODULE_UNUSED();assert(live);--live;free((void *)b);return module_result(1,1);}
static struct kernel_surface_result endpoint_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(1,1);}
static struct kernel_surface_result submit_stub(MODULE_ARGS){MODULE_UNUSED();struct host_transfer_request *r=(void *)b;unsigned i=r->endpoint==129?1:r->endpoint==130?2:0;assert(!jobs[i].result.id);jobs[i].request=*r;jobs[i].result=(struct host_transfer_result){i+1,HOST_COMPLETE,0,r->bytes,0};if(stop_after_submit)*stop_after_submit=1;return module_result(i+1,1);}
static struct kernel_surface_result result_stub(MODULE_ARGS){MODULE_UNUSED();unsigned i=b-1;assert(i<3 && jobs[i].result.id==b);if(d){memset(jobs[i].request.buffer,0x10+i,jobs[i].request.bytes);}*(struct host_transfer_result *)c=jobs[i].result;if(d)jobs[i].result.id=0;return module_result(1,1);}
static struct kernel_surface_result wait_stub(MODULE_ARGS){MODULE_UNUSED();if(b==2 && !nested){nested=1;struct usb_ref ref={fixture.record.identity.id,fixture.record.identity.generation};struct usb_interface description;assert(!usb_surface_interface((uintptr_t)&ref,1,(uintptr_t)&description,0,0,0).status && description.owner==22);
        struct usb_io io={.device=ref,.owner=22,.interface=1,.endpoint=130,.buffer=second,.bytes=sizeof(second)};struct host_transfer_result result;assert(!usb_surface_transfer((uintptr_t)&io,(uintptr_t)&result,0,0,0,0).status);
        io=(struct usb_io){.device=ref,.owner=11,.interface=0,.buffer=control_bytes,.bytes=sizeof(control_bytes),.setup={0x81,6,0,0x22,0,0,4,0}};assert(!usb_surface_control((uintptr_t)&io,(uintptr_t)&result,0,0,0,0).status);assert(fixture.data_active==1 && !fixture.busy);
    }else if(b==3)assert(fixture.data_active==2);
    *(struct host_transfer_result *)c=jobs[b-1].result;return module_result(1,1);}
static struct kernel_surface_result cancel_stub(MODULE_ARGS){MODULE_UNUSED();unsigned i=c==129?1:c==130?2:0;jobs[i].result.state=HOST_CANCELLED;return module_result(1,1);}
static struct kernel_surface_result remove_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(1,1);}
int main(void){struct kernel_surfaces hub={0};state.hub=&hub;state.heap=1;state.cpu=cpu_stub;state.thread=thread_stub;state.irq_context=cpu_stub;state.clock=clock_stub;state.alloc=alloc_stub;state.free=free_stub;state.endpoint=endpoint_stub;state.transfer=submit_stub;state.result=result_stub;state.wait=wait_stub;state.cancel=cancel_stub;state.remove=remove_stub;
    static struct usb_parsed_interface interfaces[2];static struct usb_endpoint endpoints[2];static unsigned char configured[2];static struct usb_configuration config;
    interfaces[0].info=(struct usb_interface){.number=0,.endpoints=1,.class_code=3};interfaces[1].info=(struct usb_interface){.number=1,.endpoints=1,.class_code=3};interfaces[1].first=1;endpoints[0]=(struct usb_endpoint){.address=129,.supported=1,.max_packet=8,.transfer_type=3};endpoints[1]=endpoints[0];endpoints[1].address=130;config.interfaces=interfaces;config.interface_count=2;config.endpoints=endpoints;config.configured=configured;
    fixture.host=&host;fixture.record.identity.id=DEVICE_USB_ID(1);fixture.record.identity.generation=1;fixture.record.state=USB_CONFIGURED;fixture.configs=&config;fixture.config_count=1;fixture.token=1;fixture.buffer=allocate(NULL,4096);state.devices=&fixture;host.id=7;struct usb_ref ref={fixture.record.identity.id,1};assert(!usb_surface_claim((uintptr_t)&ref,0,11,0,0,0).status && !usb_surface_claim((uintptr_t)&ref,1,22,0,0,0).status);
    struct usb_io io={.device=ref,.owner=11,.interface=0,.endpoint=129,.buffer=first,.bytes=sizeof(first)};struct host_transfer_result result;assert(!usb_surface_transfer((uintptr_t)&io,(uintptr_t)&result,0,0,0,0).status && nested && !fixture.data_active);
    for(unsigned i=0;i<4;++i)assert(first[i]==0x11 && second[i]==0x12 && control_bytes[i]==0x10);
    assert(jobs[0].request.buffer!=jobs[1].request.buffer && jobs[1].request.buffer!=jobs[2].request.buffer && jobs[0].request.buffer!=jobs[2].request.buffer);
    uint32_t cancelled=1;struct usb_io_cancellable stopping={.base=io,.cancelled=&cancelled};stopping.base.reserved=USB_IO_CANCELLABLE;unsigned char preserved=first[0];assert(usb_surface_transfer((uintptr_t)&stopping,(uintptr_t)&result,0,0,0,0).status && !fixture.data_active && first[0]==preserved);
    cancelled=0;stop_after_submit=&cancelled;assert(usb_surface_transfer((uintptr_t)&stopping,(uintptr_t)&result,0,0,0,0).status && result.state==HOST_CANCELLED && !fixture.data_active && first[0]==preserved);stop_after_submit=NULL;
    assert(cleanup(&fixture));release(NULL,fixture.buffer);assert(!live);puts("PASS USB composite endpoints: overlapping interrupt receives and EP0, independent bounce buffers, scoped claims and cleanup");return 0;}
