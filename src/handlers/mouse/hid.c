#include "hid.h"
#include "../../common/hid-report.h"
#include "../../interfaces/mouse.h"
#define MH_BITS (4096u*8u)
static void zero(void *p,size_t n){for(size_t i=0;i<n;++i)((unsigned char *)p)[i]=0;}
static void copy(void *d,const void *s,size_t n){for(size_t i=0;i<n;++i)((unsigned char *)d)[i]=((const unsigned char *)s)[i];}
static int grow(struct mouse_hid *m,void **pointer,size_t *capacity,size_t wanted,size_t size){
    if(wanted<=*capacity)return 1;
    size_t n=*capacity?*capacity*2:16;if(n<wanted)n=wanted;if(n>SIZE_MAX/size)return 0;void *p=m->alloc(m->context,n*size);if(!p)return 0;zero(p,n*size);if(*pointer){copy(p,*pointer,*capacity*size);m->free(m->context,*pointer);}*pointer=p;*capacity=n;return 1;
}
void mouse_hid_free(struct mouse_hid *m){if(m->free){for(size_t i=0;i<m->count;++i)if(m->fields[i].usages)m->free(m->context,m->fields[i].usages);if(m->fields)m->free(m->context,m->fields);if(m->collections)m->free(m->context,m->collections);}zero(m,sizeof(*m));}
static int add_field(struct mouse_hid *m,struct mouse_hid_field f){if(!grow(m,(void **)&m->fields,&m->capacity,m->count+1,sizeof(f)))return 0;m->fields[m->count++]=f;return 1;}
static int mouse_item(void *ctx,const struct hid_report_item *item){
    struct mouse_hid *m=ctx;
    if(item->type==3){if(!grow(m,(void **)&m->collections,&m->collections_capacity,(size_t)item->collection+1,sizeof(*m->collections)))return 0;m->collections[item->collection].parent=item->parent;m->collections_count=(size_t)item->collection+1;return 1;}
    if(!(item->classes&HID_MOUSE) || (item->flags&1) || (item->type!=0 && item->type!=2))return 1;
    if(item->minimum>item->maximum)return 0;
    if(!(item->flags&2)){
        uint32_t first=hid_report_usage(item,0);
        if(item->type==0 && first>>16==9){if(item->size>32)return 0;
            struct mouse_hid_field f={.offset=item->offset,.size=item->size,.count=item->count,.kind=MH_BUTTON_ARRAY,.collection=item->collection,.report_id=item->id,.minimum=item->minimum,.maximum=item->maximum,.usage_min=item->usage_min,.usage_max=item->usage_max,.usage_count=item->usage_count,.resolution=1};
            if(item->usage_count){f.usages=m->alloc(m->context,item->usage_count*sizeof(uint32_t));if(!f.usages)return 0;copy(f.usages,item->usages,item->usage_count*sizeof(uint32_t));}
            for(unsigned b=1;b<=64;++b){uint32_t u=0x90000|b;for(size_t i=0;i<item->usage_count;++i)if(item->usages[i]==u)f.mask|=UINT64_C(1)<<(b-1);if(item->usage_min<=u && item->usage_max>=u)f.mask|=UINT64_C(1)<<(b-1);}
            if(!add_field(m,f)){if(f.usages)m->free(m->context,f.usages);return 0;}
        }
        return 1;
    }
    for(uint32_t i=0;i<item->count;++i){uint32_t u=hid_report_usage(item,i),kind=0;
        if(item->type==2 && u==0x10048)kind=MH_MULTIPLIER;
        else if(item->type==0){if(u==0x10030)kind=MH_X;else if(u==0x10031)kind=MH_Y;else if(u==0x10038)kind=MH_WHEEL;else if(u==0xc0238)kind=MH_PAN;else if(u>>16==9 && (u&65535)>=1 && (u&65535)<=64)kind=MH_BUTTON;}
        if(!kind)continue;
        if(item->size>32 || item->minimum<-(INT64_C(1)<<(item->size-1)) || item->maximum>(item->minimum<0?(INT64_C(1)<<(item->size-1))-1:(INT64_C(1)<<item->size)-1))return 0;
        if(kind==MH_X || kind==MH_Y)m->flags|=(item->flags&4)?CB_MOUSE_RELATIVE:CB_MOUSE_ABSOLUTE;
        struct mouse_hid_field f={.offset=item->offset+i*item->size,.size=item->size,.count=1,.kind=kind,.usage=u&65535,.collection=item->collection,.report_id=item->id,.feature=item->type==2,.relative=(item->flags&4)!=0,.minimum=item->minimum,.maximum=item->maximum,.physical_minimum=item->physical_minimum,.physical_maximum=item->physical_maximum,.resolution=1};if(kind==MH_BUTTON)f.mask=UINT64_C(1)<<(f.usage-1);if(!add_field(m,f))return 0;
    }
    return 1;
}
int mouse_hid_parse(struct mouse_hid *m,const void *data,size_t bytes,void *(*alloc)(void *,size_t),void (*release)(void *,void *),void *context){
    zero(m,sizeof(*m));if(!alloc || !release)return 0;m->alloc=alloc;m->free=release;m->context=context;
    struct hid_report_info info;if(!hid_report_parse(&info,data,bytes,alloc,release,context,mouse_item,m)){mouse_hid_free(m);return 0;}
    copy(m->bits,info.bits,sizeof(m->bits));m->numbered=info.numbered;
    for(size_t i=0;i<m->count;++i)if(m->fields[i].kind==MH_X || m->fields[i].kind==MH_Y)return 1;
    mouse_hid_free(m);return 0;
}
static uint32_t extract(const unsigned char *p,uint32_t offset,uint32_t bits){uint32_t value=0;for(uint32_t b=0;b<bits;++b)if(p[(offset+b)/8]&(1u<<((offset+b)%8)))value|=UINT32_C(1)<<b;return value;}
static void insert(unsigned char *p,uint32_t offset,uint32_t bits,uint32_t value){for(uint32_t b=0;b<bits;++b){unsigned char mask=1u<<((offset+b)%8);if(value&(UINT32_C(1)<<b))p[(offset+b)/8]|=mask;else p[(offset+b)/8]&=(unsigned char)~mask;}}
static int64_t field_value(const struct mouse_hid_field *f,uint32_t value){if(f->minimum>=0)return value;uint32_t sign=UINT32_C(1)<<(f->size-1);return (int64_t)(value^sign)-sign;}
static int64_t saturate(int64_t a,int64_t b){if(b>0 && a>INT64_MAX-b)return INT64_MAX;if(b<0 && a<INT64_MIN-b)return INT64_MIN;return a+b;}
int mouse_hid_decode(struct mouse_hid *m,const void *data,size_t bytes,struct mouse_hid_input *out){
    const unsigned char *p=data;unsigned id=0;if(!data || !out || !bytes)return -1;if(m->numbered){id=*p++;--bytes;}if(!m->bits[0][id] || bytes<(m->bits[0][id]+7)/8)return -1;
    struct mouse_hid_input r={.x=m->x,.y=m->y,.buttons=m->buttons,.flags=m->flags,.resolution_x=1,.resolution_y=1};uint64_t clear=0;int used=0;
    for(size_t i=0;i<m->count;++i)if(!m->fields[i].feature && m->fields[i].report_id==id)clear|=m->fields[i].mask;
    r.buttons&=~clear;
    for(size_t i=0;i<m->count;++i){const struct mouse_hid_field *f=m->fields+i;if(f->feature || f->report_id!=id)continue;used=1;
        if(f->kind==MH_BUTTON_ARRAY){for(uint32_t j=0;j<f->count;++j){int64_t value=field_value(f,extract(p,f->offset+j*f->size,f->size));if(value<f->minimum || value>f->maximum)continue;uint64_t index=(uint64_t)(value-f->minimum);uint32_t usage=index<f->usage_count?f->usages[index]:f->usage_min && index<f->usage_count+(uint64_t)f->usage_max-f->usage_min+1?f->usage_min+(uint32_t)(index-f->usage_count):0;if(usage>>16==9 && (usage&65535)>=1 && (usage&65535)<=64)r.buttons|=UINT64_C(1)<<((usage&65535)-1);}continue;}
        int64_t value=field_value(f,extract(p,f->offset,f->size));if(value<f->minimum || value>f->maximum)return -1;
        if(f->kind==MH_BUTTON){if(value)r.buttons|=f->mask;}
        else if(f->kind==MH_X || f->kind==MH_Y){if(f->relative){r.flags|=CB_MOUSE_RELATIVE;if(f->kind==MH_X)r.dx=saturate(r.dx,value);else r.dy=saturate(r.dy,value);}else {if(f->maximum<=f->minimum)return -1;r.flags|=CB_MOUSE_ABSOLUTE;int64_t normalized=(value-f->minimum)*65535/(f->maximum-f->minimum);if(f->kind==MH_X)r.x=normalized;else r.y=normalized;}}
        else if(f->kind==MH_WHEEL){r.wheel_y=saturate(r.wheel_y,value);r.resolution_y=f->resolution;}
        else if(f->kind==MH_PAN){r.wheel_x=saturate(r.wheel_x,value);r.resolution_x=f->resolution;}
    }
    if(!used)return 0;
    if(r.flags&CB_MOUSE_RELATIVE){r.x=saturate(r.x,r.dx);r.y=saturate(r.y,r.dy);}r.changed=m->buttons^r.buttons;m->buttons=r.buttons;m->x=r.x;m->y=r.y;m->flags=r.flags;*out=r;return 1;
}
size_t mouse_hid_feature_bytes(const struct mouse_hid *m,unsigned id){if(id>255 || !m->bits[2][id])return 0;for(size_t i=0;i<m->count;++i)if(m->fields[i].kind==MH_MULTIPLIER && m->fields[i].report_id==id)return (m->bits[2][id]+7)/8+(m->numbered?1:0);return 0;}
int mouse_hid_feature_prepare(const struct mouse_hid *m,unsigned id,void *data,size_t bytes){size_t needed=mouse_hid_feature_bytes(m,id);unsigned char *p=data;if(!needed || bytes!=needed || (m->numbered && *p++!=id))return 0;int changed=0;
    for(size_t i=0;i<m->count;++i){const struct mouse_hid_field *f=m->fields+i;if(f->kind==MH_MULTIPLIER && f->report_id==id && f->physical_maximum>=1 && f->physical_maximum<=UINT32_MAX){insert(p,f->offset,f->size,(uint32_t)f->maximum);changed=1;}}return changed;
}
void mouse_hid_feature_apply(struct mouse_hid *m,unsigned id,const void *data,size_t bytes){size_t needed=mouse_hid_feature_bytes(m,id);const unsigned char *p=data;if(!needed || bytes!=needed || (m->numbered && *p++!=id))return;
    for(size_t i=0;i<m->count;++i){struct mouse_hid_field *wheel=m->fields+i;if(wheel->kind!=MH_WHEEL && wheel->kind!=MH_PAN)continue;uint32_t scope=wheel->collection;
        while(scope){int found=0;for(size_t j=0;j<m->count;++j){const struct mouse_hid_field *f=m->fields+j;if(f->kind!=MH_MULTIPLIER || f->report_id!=id || f->collection!=scope || f->maximum<=f->minimum)continue;int64_t value=field_value(f,extract(p,f->offset,f->size));if(value<f->minimum || value>f->maximum)continue;if(f->physical_maximum<f->physical_minimum)continue;uint64_t product=(uint64_t)(value-f->minimum)*(uint64_t)(f->physical_maximum-f->physical_minimum);int64_t scale=f->physical_minimum+(int64_t)(product/(uint64_t)(f->maximum-f->minimum));if(scale>=1 && scale<=UINT32_MAX){wheel->resolution=(uint32_t)scale;found=1;}}
            if(found)break;
            scope=m->collections[scope].parent;
        }
    }
}
