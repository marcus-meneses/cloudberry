#include "report.h"
static void clear(void *p,size_t n){for(size_t i=0;i<n;++i)((unsigned char *)p)[i]=0;
}
static int visit(void *ctx,const struct hid_report_item *item){
    struct keyboard_descriptor *d=ctx;
    if(item->type || !(item->classes&HID_KEYBOARD) || (item->flags&1))return 1;
    if(item->size>32 || item->minimum<0 || item->maximum<item->minimum || item->maximum>(INT64_C(1)<<item->size)-1)return 0;
    int relevant=0;
    for(uint32_t i=0;i<item->count;++i)if(hid_report_usage(item,i)>>16==7){relevant=1;
        break;
    }
    if(!relevant)return 1;
    if(item->usage_count>(SIZE_MAX-sizeof(struct keyboard_report_field))/sizeof(uint32_t))return 0;
    struct keyboard_report_field *f=d->alloc(d->context,sizeof(*f)+item->usage_count*sizeof(uint32_t));
    if(!f)return 0;
    f->item=*item;
    for(size_t i=0;i<item->usage_count;++i)f->usages[i]=item->usages[i];
    f->item.usages=f->usages;
    f->next=d->fields;
    d->fields=f;
    return 1;
}
int keyboard_descriptor_parse(struct keyboard_descriptor *d,const void *data,size_t bytes,hid_report_alloc alloc,hid_report_free release,void *context){
    clear(d,sizeof(*d));
    d->alloc=alloc;
    d->free=release;
    d->context=context;
    if(!hid_report_parse(&d->info,data,bytes,alloc,release,context,visit,d) || !d->fields){keyboard_descriptor_free(d);
        return 0;
    }
    return 1;
}
void keyboard_descriptor_free(struct keyboard_descriptor *d){if(d->free)while(d->fields){struct keyboard_report_field *next=d->fields->next;
        d->free(d->context,d->fields);
        d->fields=next;
    }
    clear(d,sizeof(*d));
}
void keyboard_descriptor_reset(struct keyboard_descriptor *d){clear(d->pressed,sizeof(d->pressed));
}
int keyboard_descriptor_decode(struct keyboard_descriptor *d,const void *data,size_t bytes,uint8_t pressed[256]){
    int classes=hid_report_route(&d->info,data,bytes);
    if(classes<0)return -1;
    if(!(classes&HID_KEYBOARD))return 0;
    const unsigned char *p=data;
    unsigned id=d->info.numbered?*p++:0;
    uint8_t bits[32]={0};
    int used=0;
    for(struct keyboard_report_field *f=d->fields;f;f=f->next){const struct hid_report_item *item=&f->item;
        if(item->id!=id)continue;
        used=1;
        for(uint32_t i=0;i<item->count;++i){uint32_t value=hid_report_bits(p,item->offset+i*item->size,item->size);
            if(value<(uint64_t)item->minimum || value>(uint64_t)item->maximum){if(item->flags&64)continue;
                return -1;
            }
            uint32_t usage;
            if(item->flags&2){if(!value)continue;
                usage=hid_report_usage(item,i);
            }
            else {
                uint32_t index=value-(uint32_t)item->minimum;
                if(index<item->usage_count)usage=item->usages[index];
                else if(item->usage_min && index-item->usage_count<=(uint64_t)item->usage_max-item->usage_min)usage=item->usage_min+(uint32_t)(index-item->usage_count);
                else {if(item->flags&64)continue;return -1;}
            }
            if(usage>>16!=7)continue;
            unsigned key=usage&65535;
            if(key>=1 && key<=3)return -1;
            if(key>=4 && key<256)bits[key/8]|=1u<<(key%8);
        }
    }
    if(!used)return 0;
    for(unsigned i=0;i<32;++i)d->pressed[id][i]=bits[i];
    uint8_t combined[32]={0};
    for(unsigned report=0;report<256;++report)if(d->info.classes[0][report]&HID_KEYBOARD)
        for(unsigned i=0;i<32;++i)combined[i]|=d->pressed[report][i];
    for(unsigned key=0;key<256;++key)pressed[key]=!!(combined[key/8]&(1u<<(key%8)));
    return 1;
}
