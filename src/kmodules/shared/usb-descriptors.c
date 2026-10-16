#include "usb-descriptors.h"
static unsigned word(const unsigned char *p) {return p[0]|((unsigned)p[1]<<8);}
unsigned usb_ep0_packet(unsigned speed,unsigned encoded) {
    if(speed==USB_LOW)return encoded==8?8:0;
    if(speed==USB_HIGH)return encoded==64?64:0;
    if(speed==USB_SUPER)return encoded==9?512:0;
    return speed==USB_FULL && (encoded==8 || encoded==16 || encoded==32 || encoded==64)?encoded:0;
}
unsigned usb_port_speed(unsigned protocol,unsigned id,const uint32_t *psi,unsigned count) {
    if(!count) {
        if(protocol==3)return id==4 || id==5?USB_SUPER:0;
        if(protocol==2)return id==1?USB_FULL:id==2?USB_LOW:id==3?USB_HIGH:0;
        return 0;
    }
    uint64_t bitrate=0;
    for(unsigned i=0;i<count;++i)if((psi[i]&15)==id) {
        uint64_t rate=psi[i]>>16;unsigned exponent=(psi[i]>>4)&3;
        while(exponent--)rate*=1000;
        if(bitrate && bitrate!=rate)return 0;
        bitrate=rate;
    }
    if(protocol==3)return bitrate>=UINT64_C(5000000000)?USB_SUPER:0;
    return protocol==2?(bitrate==1500000?USB_LOW:bitrate==12000000?USB_FULL:bitrate==480000000?USB_HIGH:0):0;
}
static int normalize(struct usb_endpoint *ep,const unsigned char *raw,unsigned speed) {
    ep->address=raw[2];ep->transfer_type=raw[3]&3;unsigned packet=word(raw+4);
    if(!(ep->address&15) || (ep->address&~143u) || !packet || (packet&0xe000))return 0;
    ep->max_packet=packet&2047;ep->supported=ep->transfer_type==2 || ep->transfer_type==3;
    ep->host_type=ep->transfer_type==2?HOST_BULK:ep->transfer_type==3?HOST_INTERRUPT:0;
    if(!ep->max_packet || ep->max_packet>1024 || !ep->transfer_type)return 0;
    if(ep->transfer_type==1){ep->supported=0;return 1;} /* Preserve unsupported isochronous description. */
    if(ep->transfer_type==2) {
        if(packet&~2047u)return 0;
        if(speed==USB_LOW)return 0;
        if(speed==USB_FULL && ep->max_packet!=8 && ep->max_packet!=16 && ep->max_packet!=32 && ep->max_packet!=64)return 0;
        if(speed==USB_HIGH && ep->max_packet!=512)return 0;
        return speed!=USB_SUPER || ep->max_packet==1024;
    }
    if(!raw[6])return 0;
    if(speed==USB_HIGH || speed==USB_SUPER) {
        if(raw[6]>16)return 0;
        ep->interval=raw[6]-1;
        if(speed==USB_HIGH) {
            unsigned transactions=(packet>>11)&3;if(transactions==3)return 0;
            ep->max_burst=transactions;ep->max_esit=ep->max_packet*(transactions+1);
        } else if(packet&~2047u)return 0;
    } else {
        if((packet&~2047u) || ep->max_packet>(speed==USB_LOW?8:64))return 0;
        unsigned microframes=raw[6]*8;while(microframes>>=1)++ep->interval;
        ep->max_esit=ep->max_packet;
    }
    return 1;
}
void usb_free_configuration(const struct usb_allocator *a,struct usb_configuration *cfg) {
    if(cfg->interfaces)a->release(a->context,cfg->interfaces);
    if(cfg->endpoints)a->release(a->context,cfg->endpoints);
    if(cfg->configured)a->release(a->context,cfg->configured);
    *cfg=(struct usb_configuration){0};
}
int usb_parse_configuration(unsigned char *raw,size_t bytes,unsigned speed,const struct usb_allocator *a,struct usb_configuration *out) {
    if(!raw || !out || !a || !a->allocate || !a->release || bytes<9 || raw[0]!=9 || raw[1]!=2 || word(raw+2)!=bytes || !raw[4] || !raw[5] || !(raw[7]&128) || (raw[7]&31) || speed<USB_LOW || speed>USB_SUPER)return 0;
    size_t interfaces=0,endpoints=0;
    for(size_t offset=9;offset<bytes;) {
        if(bytes-offset<2 || raw[offset]<2 || raw[offset]>bytes-offset)return 0;
        if(raw[offset+1]==4){if(raw[offset]<9)return 0;++interfaces;}
        if(raw[offset+1]==5){if(raw[offset]<7)return 0;++endpoints;}
        offset+=raw[offset];
    }
    if(!interfaces || interfaces>SIZE_MAX/sizeof(struct usb_parsed_interface) || endpoints>SIZE_MAX/sizeof(struct usb_endpoint))return 0;
    struct usb_configuration cfg={.raw=raw,.bytes=bytes,.value=raw[5],.attributes=raw[7],.power=raw[8],.distinct_interfaces=raw[4]};
    cfg.interfaces=a->allocate(a->context,interfaces*sizeof(*cfg.interfaces));
    cfg.endpoints=a->allocate(a->context,(endpoints?endpoints:1)*sizeof(*cfg.endpoints));cfg.configured=a->allocate(a->context,endpoints?endpoints:1);
    if(!cfg.interfaces || !cfg.endpoints || !cfg.configured){usb_free_configuration(a,&cfg);return -1;}
    for(size_t i=0;i<interfaces;++i)cfg.interfaces[i]=(struct usb_parsed_interface){0};
    for(size_t i=0;i<endpoints;++i){cfg.endpoints[i]=(struct usb_endpoint){0};cfg.configured[i]=0;}
    unsigned char distinct[256]={0};unsigned unique=0;size_t current=SIZE_MAX,pending=SIZE_MAX;uint32_t active_endpoints=0;
    for(size_t offset=9;offset<bytes;offset+=raw[offset]) {
        const unsigned char *r=raw+offset;
        if(pending!=SIZE_MAX && r[1]!=48)goto fail; /* SS companion must immediately follow its endpoint. */
        if(r[1]==4) {
            if(current!=SIZE_MAX && cfg.endpoint_count-cfg.interfaces[current].first!=cfg.interfaces[current].info.endpoints)goto fail;
            for(size_t i=0;i<cfg.interface_count;++i)if(cfg.interfaces[i].info.number==r[2] && cfg.interfaces[i].info.alternate==r[3])goto fail;
            current=cfg.interface_count++;cfg.interfaces[current]=(struct usb_parsed_interface){.info={.number=r[2],.alternate=r[3],.class_code=r[5],.subclass=r[6],.protocol=r[7],.string_index=r[8],.endpoints=r[4]},.first=cfg.endpoint_count};
            if(!r[3]){if(distinct[r[2]])goto fail;distinct[r[2]]=1;++unique;}
        } else if(r[1]==5) {
            if(current==SIZE_MAX || cfg.endpoint_count-cfg.interfaces[current].first>=cfg.interfaces[current].info.endpoints)goto fail;
            struct usb_endpoint *ep=&cfg.endpoints[cfg.endpoint_count];if(!normalize(ep,r,speed))goto fail;
            unsigned bit=(ep->address&15)*2+!!(ep->address&128);
            for(size_t i=cfg.interfaces[current].first;i<cfg.endpoint_count;++i)if(cfg.endpoints[i].address==ep->address)goto fail;
            if(!cfg.interfaces[current].info.alternate){if(active_endpoints&(1u<<bit))goto fail;active_endpoints|=1u<<bit;}
            if(speed==USB_SUPER)pending=cfg.endpoint_count;
            ++cfg.endpoint_count;
        } else if(r[1]==48) {
            if(speed!=USB_SUPER || pending==SIZE_MAX || r[0]!=6 || r[2]>15)goto fail;
            struct usb_endpoint *ep=&cfg.endpoints[pending];ep->max_burst=r[2];ep->max_esit=word(r+4);
            if(ep->transfer_type==2){if(r[3]&~31u || ep->max_esit)goto fail;if(r[3]&31)ep->supported=0;}
            if(ep->transfer_type==3 && (r[3] || !ep->max_esit || ep->max_esit>ep->max_packet*(ep->max_burst+1)))goto fail;
            pending=SIZE_MAX;
        } else if(r[1]==2 || (r[1]==11 && r[0]<8))goto fail;
    }
    if(pending!=SIZE_MAX || current==SIZE_MAX || cfg.endpoint_count-cfg.interfaces[current].first!=cfg.interfaces[current].info.endpoints || unique!=raw[4])goto fail;
    for(size_t i=0;i<cfg.interface_count;++i)if(!distinct[cfg.interfaces[i].info.number])goto fail;
    *out=cfg;return 1;
fail:usb_free_configuration(a,&cfg);return 0;
}
