#include "../kmodules/shared/hid-reports.c"
#include "hid-fixtures.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static long live,budget=-1;
static unsigned channels,thread_id,fail_watch,claims,releases,cancels,posts[2],removed[2],hold,closed_during_post,feed,mode;
static uintptr_t owner;
static uint64_t caller=7,subscribers[2],connections[2];
static const unsigned char *report_descriptor=hid_combined;static size_t report_descriptor_bytes=sizeof(hid_combined);
static struct {struct kernel_surfaces hub;struct kernel_surface methods[2];struct kernel_hub_table table;} fixture;
static struct usb_table table;static struct usb_device device;
static cb_status_t invoke(unsigned,const void *,size_t,void *,size_t,size_t *);
static struct kernel_surface_result mem_stub(MODULE_ARGS){MODULE_UNUSED();if(!budget)return module_result(0,0);if(budget>0)--budget;void *p=calloc(b,c);if(p)++live;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result free_stub(MODULE_ARGS){MODULE_UNUSED();assert(live>0);--live;free((void *)b);return module_result(1,1);}
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result process_stub(MODULE_ARGS){MODULE_UNUSED();*(struct process_info *)b=(struct process_info){.id=a};return module_result(1,1);}
static struct kernel_surface_result channel_stub(MODULE_ARGS){MODULE_UNUSED();assert((a==7 || a==8) && b>>63 && c==sizeof(struct cb_io_hid_event)+CB_HID_LIMIT);++channels;return module_result(1,1);}
static struct kernel_surface_result unsubscribe_stub(MODULE_ARGS){MODULE_UNUSED();assert(channels);--channels;return module_result(1,1);}
static struct kernel_surface_result create_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(++thread_id,1);}
static struct kernel_surface_result threads_stub(MODULE_ARGS){
    MODULE_UNUSED();if(a==THREAD_CURRENT)return module_result(1,1);
    if(a==THREAD_BLOCK){for(struct hid_link *l=hid.links;l;l=l->next)if(l->watcher && !l->ready){l->ready=fail_watch?2:1;return module_result(1,1);}
        assert(hold && posts[0] && posts[1]);struct hid_link *l=hfind(7,connections[0]);assert(l && l->waiting);l->waiting=0;hold=0;}
    if(a==THREAD_INFO)*(struct thread_info *)c=(struct thread_info){.state=THREAD_EXITED};
    return module_result(1,1);
}
static struct kernel_surface_result device_stub(MODULE_ARGS){MODULE_UNUSED();const struct usb_ref *ref=(void *)a;if(ref->id!=device.identity.id || ref->generation!=device.identity.generation)return module_result(0,0);*(struct usb_device *)b=device;return module_result(1,1);}
static struct kernel_surface_result interface_stub(MODULE_ARGS){MODULE_UNUSED();if(b>1)return module_result(0,0);*(struct usb_interface *)c=(struct usb_interface){.number=b,.class_code=3,.subclass=1,.owner=b?777:owner};return module_result(1,1);}
static struct kernel_surface_result endpoint_stub(MODULE_ARGS){MODULE_UNUSED();if(c!=129)return module_result(0,0);*(struct usb_endpoint *)d=(struct usb_endpoint){.address=129,.transfer_type=3,.supported=1,.max_packet=8};return module_result(1,1);}
static struct kernel_surface_result config_stub(MODULE_ARGS){MODULE_UNUSED();unsigned char config[]={9,2,34,0,1,1,0,0x80,50,9,4,0,0,1,3,1,1,0,9,0x21,0x11,1,0,1,0x22,0,0,7,5,129,3,8,0,10};config[25]=report_descriptor_bytes;config[26]=report_descriptor_bytes>>8;assert(d>=sizeof(config));memcpy((void *)c,config,sizeof(config));*(size_t *)e=sizeof(config);return module_result(1,1);}
static struct kernel_surface_result claim_stub(MODULE_ARGS){MODULE_UNUSED();if(owner)return module_result(0,0);owner=c;++claims;return module_result(1,1);}
static struct kernel_surface_result release_stub(MODULE_ARGS){MODULE_UNUSED();assert(owner==c);owner=0;++releases;return module_result(1,1);}
static struct kernel_surface_result control_stub(MODULE_ARGS){
    MODULE_UNUSED();struct usb_io *r=(void *)a;assert(owner==r->owner && r->interface==0);
    if(r->setup[1]==6){assert(r->setup[0]==0x81 && r->setup[3]==0x22);memcpy(r->buffer,report_descriptor,report_descriptor_bytes);}
    else if(r->setup[1]==1){assert(r->setup[0]==0xa1 && r->setup[3]==3 && r->bytes==2);memcpy(r->buffer,(unsigned char[]){2,0xa0},2);}
    else if(r->setup[1]==9){assert(r->setup[0]==0x21 && r->setup[3]==3 && ((unsigned char *)r->buffer)[0]==2);}
    else assert(r->setup[1]==0x0a || (r->setup[1]==0x0b && r->setup[2]==1));
    *(struct host_transfer_result *)b=(struct host_transfer_result){.state=HOST_COMPLETE,.bytes=r->bytes};return module_result(1,1);
}
static struct kernel_surface_result cancel_stub(MODULE_ARGS){MODULE_UNUSED();++cancels;return module_result(1,1);}
static struct kernel_surface_result transfer_stub(MODULE_ARGS){
    MODULE_UNUSED();struct usb_io_cancellable *r=(void *)a;assert(r->base.owner==owner && r->cancelled && r->base.reserved==USB_IO_CANCELLABLE);
    static const unsigned char key[]={1,0,0,4,0,0,0,0,0},mouse[]={2,1,12,0xf9},shared[]={1,12,0xf9};const unsigned char *data=NULL;size_t bytes=0;
    if(mode==0 && feed<2){data=feed?mouse:key;bytes=feed?sizeof(mouse):sizeof(key);}else if(mode==1 && !feed){data=shared;bytes=sizeof(shared);}
    ++feed;if(data){assert(r->base.bytes>=bytes);memcpy(r->base.buffer,data,bytes);*(struct host_transfer_result *)b=(struct host_transfer_result){.state=HOST_COMPLETE,.bytes=bytes};return module_result(1,1);}
    device.state=USB_DETACHED;owner=0;*(struct host_transfer_result *)b=(struct host_transfer_result){.state=HOST_FAILED};return module_result(0,0);
}
static struct kernel_surface_result post_stub(MODULE_ARGS){
    MODULE_UNUSED();assert(a==7 || a==8);unsigned index=a-7;const struct cb_io_hid_event *event=(void *)c;assert(b==connections[index] && event->connection==connections[index] && event->subscription==subscribers[index] && d==sizeof(*event)+event->bytes);
    if(event->kind==CB_IO_REPORT){++posts[index];if(mode==0)assert(event->bytes && ((unsigned char *)(event+1))[0]==(index?2:1));if(index && hold)assert(posts[0]);}
    else {assert(event->kind==CB_IO_DEVICE_REMOVED);++removed[index];}
    if(closed_during_post && !index && event->kind==CB_IO_REPORT){assert(hclose(hfind(7,b),0)==CB_BUSY);closed_during_post=0;return module_result(1,1);}
    if(!index && hold && event->kind==CB_IO_REPORT)return module_result(1,1);
    uint64_t previous=caller;caller=a;struct cb_io_subscription ack={b};assert(invoke(CB_IO_HID_ACK,&ack,sizeof(ack),NULL,0,NULL)==CB_OK);caller=previous;return module_result(1,1);
}
static cb_status_t invoke(unsigned op,const void *p,size_t bytes,void *out,size_t capacity,size_t *actual){struct io_invocation r={.process=caller,.operation=op,.input=p,.bytes=bytes,.output=out,.capacity=capacity};assert(hid_reports_invoke(&r));if(actual)*actual=r.actual;return r.status;}
static void close_id(uint64_t pid,uint64_t id){caller=pid;struct cb_io_subscription close={id};assert(invoke(CB_IO_HID_CLOSE,&close,sizeof(close),NULL,0,NULL)==CB_OK);}
static void subscribe(unsigned i){caller=7+i;assert(invoke(CB_IO_HID_SUBSCRIBE,NULL,0,&subscribers[i],8,NULL)==CB_OK);}
static void open_both(void){for(unsigned i=0;i<2;++i){caller=7+i;struct cb_io_hid_open request={subscribers[i],11,device.identity.generation,0,0};struct cb_io_hid_opened opened;assert(invoke(CB_IO_HID_OPEN,&request,sizeof(request),&opened,sizeof(opened),NULL)==CB_OK);connections[i]=opened.connection;}assert(hfind(7,connections[0])->source==hfind(8,connections[1])->source);}
static void start(unsigned i){caller=7+i;struct hid_source *s=hfind(caller,connections[i])->source;struct cb_io_hid_capture capture={connections[i],s->report_bytes,0};assert(invoke(CB_IO_HID_START,&capture,sizeof(capture),NULL,0,NULL)==CB_OK);}
int main(void){
    fixture.hub.header_bytes=offsetof(__typeof__(fixture),methods);fixture.hub.count=2;fixture.hub.tables_offset=offsetof(__typeof__(fixture),table);fixture.hub.table_count=1;strcpy(fixture.methods[0].name,"hid.report_worker");fixture.methods[0].call=hid_surface_report_worker;strcpy(fixture.methods[1].name,"hid.capture_worker");fixture.methods[1].call=hid_surface_capture_worker;strcpy(fixture.table.name,USB_TABLE_NAME);fixture.table.address=(uintptr_t)&table;table.first=&device;
    device.identity.id=11;device.identity.generation=1;device.state=USB_CONFIGURED;device.interfaces=2;
    hid.hub=&fixture.hub;hid.heap=1;hid.alloc=mem_stub;hid.free=free_stub;hid.cpu=cpu_stub;hid.threads=threads_stub;hid.create=create_stub;hid.clock=cpu_stub;hid.channel=channel_stub;hid.unsubscribe=unsubscribe_stub;hid.post=post_stub;hid.process_info=process_stub;hid.device=device_stub;hid.interface=interface_stub;hid.endpoint=endpoint_stub;hid.configuration=config_stub;hid.claim=claim_stub;hid.release=release_stub;hid.control=control_stub;hid.transfer=transfer_stub;hid.cancel=cancel_stub;hid_reports_bind("keyboard",7);hid_reports_bind("mouse",8);
    caller=9;struct io_invocation denied={.process=caller,.operation=CB_IO_HID_SUBSCRIBE};assert(hid_reports_invoke(&denied) && denied.status==CB_DENIED);caller=7;
    fail_watch=1;uint64_t unused;assert(invoke(CB_IO_HID_SUBSCRIBE,NULL,0,&unused,8,NULL)==CB_RESOURCE_FAILURE && !live && !channels);fail_watch=0;subscribe(0);subscribe(1);
    caller=7;struct cb_io_hid_list list={.subscription=subscribers[0]};struct cb_io_hid_device item;size_t actual;assert(invoke(CB_IO_HID_LIST,&list,sizeof(list),&item,sizeof(item),&actual)==CB_OK && actual==sizeof(item) && item.interface==0);list.device=item.device;list.ordinal=item.ordinal;assert(invoke(CB_IO_HID_LIST,&list,sizeof(list),&item,sizeof(item),&actual)==CB_OK && !actual);
    struct cb_io_hid_open request={subscribers[0],11,1,0,0};struct cb_io_hid_opened opened;
    for(long n=0;n<9;++n){long before=live;budget=n;cb_status_t status=invoke(CB_IO_HID_OPEN,&request,sizeof(request),&opened,sizeof(opened),NULL);budget=-1;if(!status)close_id(7,opened.connection);else assert(status==CB_RESOURCE_FAILURE || status==CB_UNAVAILABLE);assert(live==before && channels==2 && !owner && !hid.sources);}
    unsigned old_claims=claims;open_both();assert(claims==old_claims+1 && channels==4);struct hid_source *source=hid.sources;
    caller=8;list=(struct cb_io_hid_list){.subscription=subscribers[1]};assert(invoke(CB_IO_HID_LIST,&list,sizeof(list),&item,sizeof(item),&actual)==CB_OK && actual);char descriptor[sizeof(hid_combined)];struct cb_io_hid_data data={connections[1],0,sizeof(descriptor)};assert(invoke(CB_IO_HID_DESCRIPTOR,&data,sizeof(data),descriptor,sizeof(descriptor),&actual)==CB_OK && actual==sizeof(descriptor) && !memcmp(descriptor,hid_combined,sizeof(descriptor)));data.offset=UINT32_MAX;assert(invoke(CB_IO_HID_DESCRIPTOR,&data,sizeof(data),descriptor,sizeof(descriptor),NULL)==CB_BAD_REQUEST);
    start(0);struct cb_io_hid_feature feature={connections[1],2,2};unsigned char bytes[sizeof(feature)+2];caller=8;memcpy(bytes,&feature,sizeof(feature));assert(invoke(CB_IO_HID_GET_FEATURE,&feature,sizeof(feature),bytes+sizeof(feature),2,&actual)==CB_OK);assert(invoke(CB_IO_HID_SET_FEATURE,bytes,sizeof(bytes),NULL,0,NULL)==CB_OK);start(1);assert(invoke(CB_IO_HID_GET_FEATURE,&feature,sizeof(feature),bytes,2,NULL)==CB_BAD_REQUEST);
    assert(!hid_surface_capture_worker((uintptr_t)source,0,0,0,0,0).status && posts[0]==1 && posts[1]==1 && removed[0]==1 && removed[1]==1);close_id(7,connections[0]);assert(hid.sources && !cancels);close_id(8,connections[1]);assert(!hid.sources && channels==2);
    device.identity.generation=2;device.state=USB_CONFIGURED;report_descriptor=hid_shared_report;report_descriptor_bytes=sizeof(hid_shared_report);mode=1;feed=0;posts[0]=posts[1]=removed[0]=removed[1]=0;open_both();start(0);start(1);hold=1;assert(!hid_surface_capture_worker((uintptr_t)hid.sources,0,0,0,0,0).status && posts[0]==1 && posts[1]==1 && !hold);close_id(7,connections[0]);close_id(8,connections[1]);
    device.identity.generation=3;device.state=USB_CONFIGURED;feed=0;open_both();start(0);start(1);closed_during_post=1;posts[0]=posts[1]=0;assert(!hid_surface_capture_worker((uintptr_t)hid.sources,0,0,0,0,0).status && posts[1]==1 && !closed_during_post);close_id(7,connections[0]);assert(hid.sources);close_id(8,connections[1]);
    close_id(7,subscribers[0]);close_id(8,subscribers[1]);assert(!hid.links && !hid.sources && !owner && !channels && !live);
    puts("PASS shared HID IO: one claim, report routing/fanout, ACK isolation, sibling close, disconnect/reconnect, startup/feature scope and OOM cleanup");return 0;
}
