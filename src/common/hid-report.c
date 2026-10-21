#include "hid-report.h"
struct globals {uint32_t page,size,count,id;
    int64_t minimum,maximum,physical_minimum,physical_maximum;
};
uint32_t hid_report_usage(const struct hid_report_item *f,uint32_t i){if(i<f->usage_count)return f->usages[i];
    i-=(uint32_t)f->usage_count;
    if(f->usage_min && f->usage_max>=f->usage_min){uint32_t n=f->usage_max-f->usage_min;
        return f->usage_min+(i<n?i:n);
    }
    return f->usage_count?f->usages[f->usage_count-1]:0;
}
uint32_t hid_report_bits(const unsigned char *p,uint32_t at,uint32_t bits){uint32_t v=0;
    for(uint32_t i=0;i<bits;++i)if(p[(at+i)/8]&(1u<<((at+i)%8)))v|=UINT32_C(1)<<i;
    return v;
}
int hid_report_parse(struct hid_report_info *out,const void *data,size_t bytes,hid_report_alloc alloc,hid_report_free release,void *allocator,hid_report_visit visit,void *context){
    if(!out || !data || !alloc || !release || !bytes || bytes>HID_REPORT_LIMIT)return 0;
    unsigned char *zero=(void *)out;
    for(size_t i=0;i<sizeof(*out);++i)zero[i]=0;
    struct nesting {uint32_t classes,collection;};
    struct globals *stack=alloc(allocator,bytes*sizeof(*stack));
    struct nesting *collections=alloc(allocator,bytes*sizeof(*collections));
    uint32_t *usages=alloc(allocator,bytes*sizeof(*usages));
    size_t depth=0,saved=0,used=0;
    uint32_t classes=0,umin=0,umax=0,collection=0,serial=0;
    struct globals g={0};
    const unsigned char *p=data;
    int ok=0;
    if(!stack || !collections || !usages)goto done;
    for(size_t at=0;at<bytes;){unsigned prefix=p[at++];
        if(prefix==0xfe)goto done;
        unsigned size=(prefix&3)==3?4:prefix&3,type=(prefix>>2)&3,tag=prefix>>4;
        if(size>bytes-at)goto done;
        uint32_t v=0;
        for(unsigned i=0;i<size;++i)v|=(uint32_t)p[at+i]<<(i*8);
        at+=size;
        int64_t signed_v=size && (v&(UINT32_C(1)<<(size*8-1)))?(int64_t)v-(INT64_C(1)<<(size*8)):v;
        if(type==1){switch(tag){case 0:if(v>65535)goto done;
                g.page=v;
                break;
                case 1:g.minimum=signed_v;
                break;
                case 2:g.maximum=g.minimum<0?signed_v:v;
                break;
                case 3:g.physical_minimum=signed_v;break;
                case 4:g.physical_maximum=g.physical_minimum<0?signed_v:v;break;
                case 5:case 6:break;
                case 7:g.size=v;
                break;
                case 8:if(!v || v>255)goto done;
                g.id=v;
                out->numbered=1;
                break;
                case 9:g.count=v;
                break;
                case 10:stack[saved++]=g;
                break;
                case 11:if(!saved)goto done;
                g=stack[--saved];
                break;
                default:goto done;
            }
            continue;
        }
        if(type==2){uint32_t u=size==4?v:(g.page<<16)|v;
            switch(tag){case 0:usages[used++]=u;
                break;
                case 1:umin=u;
                break;
                case 2:umax=u;
                break;
                case 3:case 4:case 5:case 7:case 8:case 9:break;
                default:goto done;
            }
            continue;
        }
        if(type!=0 || (umin && (umax<umin || (umin>>16)!=(umax>>16))))goto done;
        struct hid_report_item item={.id=g.id,.size=g.size,.count=g.count,.flags=v,.classes=classes,.minimum=g.minimum,.maximum=g.maximum,.physical_minimum=g.physical_minimum,.physical_maximum=g.physical_maximum,.collection=collection,.usages=usages,.usage_count=used,.usage_min=umin,.usage_max=umax};
        if(tag==10){collections[depth++]=(struct nesting){classes,collection};
            item.type=3;item.parent=collection;item.collection=++serial;item.collection_type=v;collection=serial;
            if(v==1){uint32_t u=hid_report_usage(&item,0);
                if(u==0x10006 || u==0x10007)classes=HID_KEYBOARD;
                else if(u==0x10002)classes=HID_MOUSE;
            }
            item.classes=classes;if(visit && !visit(context,&item))goto done;
        }
        else if(tag==12){if(!depth)goto done;
            --depth;classes=collections[depth].classes;collection=collections[depth].collection;
        }
        else if(tag==8 || tag==9 || tag==11){unsigned kind=tag==8?0:tag==9?1:2;
            uint32_t *bits=&out->bits[kind][g.id];
            if(!g.size || !g.count || g.size>HID_REPORT_LIMIT*8u || g.count>HID_REPORT_LIMIT*8u/g.size || *bits>HID_REPORT_LIMIT*8u-g.size*g.count)goto done;
            item.type=kind;
            item.offset=*bits;
            *bits+=g.size*g.count;
            if(!(v&1))out->classes[kind][g.id]|=classes;
            if(visit && !visit(context,&item))goto done;
        }
        else goto done;
        used=0;
        umin=umax=0;
    }
    if(depth || saved || (out->numbered && (out->bits[0][0] || out->bits[1][0] || out->bits[2][0])))goto done;
    for(unsigned id=0;id<256;++id)for(unsigned type=0;type<3;++type){uint32_t n=(out->bits[type][id]+7)/8+(out->numbered?1:0);
        if(n>HID_REPORT_LIMIT)goto done;
        if(type==0 && out->bits[0][id] && n>out->input_bytes)out->input_bytes=n;
    }
    ok=out->input_bytes!=0;
    done:if(stack)release(allocator,stack);
    if(collections)release(allocator,collections);
    if(usages)release(allocator,usages);
    return ok;
}
int hid_report_route(const struct hid_report_info *info,const void *data,size_t bytes){if(!data || !bytes)return -1;
    unsigned id=info->numbered?*(const unsigned char *)data:0;
    size_t size=(info->bits[0][id]+7)/8+(info->numbered?1:0);
    if(!info->bits[0][id] || bytes<size || bytes>info->input_bytes)return -1;
    return info->classes[0][id];
}
