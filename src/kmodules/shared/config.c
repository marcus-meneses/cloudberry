#include "config.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
#define REQUIRED 1u
#define DEFAULT 2u
/* Inline arrays keep this PIC image free of absolute pointer initializers. */
static const struct schema {char key[25],fallback[32];unsigned type,flags;uint64_t min,max;} schemas[]={
    {.key="version",.type=CONFIG_UINT,.flags=REQUIRED,.min=1,.max=1},
    {.key="filesystem.writable",.fallback="false",.type=CONFIG_BOOL,.flags=DEFAULT},
    {.key="boot.show_inventory",.fallback="true",.type=CONFIG_BOOL,.flags=DEFAULT},
    {.key="drivers.modules",.fallback="",.type=CONFIG_STRING,.flags=DEFAULT},
    {.key="drivers.directory",.fallback="/boot/kmodules/drivers",.type=CONFIG_STRING,.flags=DEFAULT},
    {.key="storage.timeout_ms",.fallback="1000",.type=CONFIG_UINT,.flags=DEFAULT,.min=1,.max=UINT32_MAX},
    {.key="boot.providers",.fallback="keyboard",.type=CONFIG_STRING,.flags=DEFAULT},
    {.key="drivers.partition_guid",.type=CONFIG_GUID}
};
#define ITEMS (sizeof(schemas)/sizeof(schemas[0]))
struct item {struct config_value descriptor;union {uint64_t integer;uint8_t boolean;struct config_guid guid;} value;char *string;unsigned seen;};
static struct config_info info;
static struct item *owned;
static size_t length(const char *p){size_t n=0;while(p[n])++n;return n;}
static int same(const char *p,size_t n,const char *text){size_t i=0;while(i<n && text[i] && p[i]==text[i])++i;return i==n && !text[i];}
static int space(unsigned char c){return c==' ' || c=='\t';}
static int integer(const char *p,size_t n,uint64_t *out){
    if(!n)return 0;
    uint64_t value=0;
    for(size_t i=0;i<n;++i){unsigned digit=(unsigned char)p[i]-'0';if(digit>9 || value>(UINT64_MAX-digit)/10)return 0;value=value*10+digit;}
    *out=value;return 1;
}
static int hex(unsigned char c){return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:-1;}
static int guid(const char *p,size_t n,struct config_guid *out){
    if(n!=36 || p[8]!='-' || p[13]!='-' || p[18]!='-' || p[23]!='-')return 0;
    uint8_t bytes[16];unsigned count=0;
    for(size_t i=0;i<n;){if(i==8 || i==13 || i==18 || i==23){++i;continue;}int a=hex(p[i]),b=hex(p[i+1]);if(a<0 || b<0)return 0;bytes[count++]=(a<<4)|b;i+=2;}
    out->a=((uint32_t)bytes[0]<<24)|((uint32_t)bytes[1]<<16)|((uint32_t)bytes[2]<<8)|bytes[3];
    out->b=((uint16_t)bytes[4]<<8)|bytes[5];out->c=((uint16_t)bytes[6]<<8)|bytes[7];
    for(unsigned i=0;i<8;++i)out->d[i]=bytes[i+8];
    return 1;
}
static int utf8(const char *p,size_t n){
    for(size_t i=0;i<n;){unsigned a=(unsigned char)p[i++];if(a<0x80){if(a<0x20 || a==0x7f)return 0;continue;}
        unsigned count,min,value;
        if(a>=0xc2 && a<=0xdf){count=1;min=0x80;value=a&31;}
        else if(a>=0xe0 && a<=0xef){count=2;min=0x800;value=a&15;}
        else if(a>=0xf0 && a<=0xf4){count=3;min=0x10000;value=a&7;}else return 0;
        if(count>n-i)return 0;
        while(count--){unsigned b=(unsigned char)p[i++];if((b&0xc0)!=0x80)return 0;value=(value<<6)|(b&63);}
        if(value<min || value>0x10ffff || (value>=0xd800 && value<=0xdfff))return 0;
    }return 1;
}
static unsigned value(struct item *item,const struct schema *schema,const char *p,size_t n,kernel_surface_call alloc,uintptr_t heap){
    struct config_value *v=&item->descriptor;*v=(struct config_value){.major=1,.type=schema->type};
    if(schema->type==CONFIG_STRING){
        if(!utf8(p,n) || n==SIZE_MAX)return CONFIG_VALUE;
        struct kernel_surface_result r=CALL(alloc,heap,1,n+1);if(r.status)return CONFIG_MEMORY;
        item->string=(void *)r.value;for(size_t i=0;i<n;++i)item->string[i]=p[i];
        v->data=item->string;v->bytes=n+1;
    }else if(schema->type==CONFIG_BOOL){
        if(!same(p,n,"true") && !same(p,n,"false"))return CONFIG_VALUE;
        item->value.boolean=same(p,n,"true");v->data=&item->value.boolean;v->bytes=1;
    }else if(schema->type==CONFIG_UINT){
        if(!integer(p,n,&item->value.integer) || item->value.integer<schema->min || item->value.integer>schema->max)return CONFIG_VALUE;
        v->data=&item->value.integer;v->bytes=8;
    }else {
        if(!guid(p,n,&item->value.guid))return CONFIG_VALUE;
        v->data=&item->value.guid;v->bytes=sizeof(struct config_guid);
    }return CONFIG_OK;
}
struct kernel_surface_result config_surface_init(MODULE_ARGS){
    MODULE_UNUSED();if(info.ready)return module_result(CONFIG_ALREADY_READY,0);
    info=(struct config_info){0};const struct kernel_surfaces *hub=(void *)a;const char *text=(void *)c;
    unsigned error=CONFIG_ARGUMENT;struct item *items=NULL;struct kernel_hub_table *tables=NULL;size_t count=0;
    kernel_surface_call alloc=NULL,free=NULL,publish=NULL;
    if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC || !b || (!text && d) || (d && c>UINTPTR_MAX-d))goto failed;
    kernel_surface_call cpu=kernel_surface_find_version(hub,"cpu.control",1,0);
    alloc=kernel_surface_find_version(hub,"mem.calloc",1,0);free=kernel_surface_find_version(hub,"mem.free",1,0);publish=kernel_surface_find_version(hub,"hub.publish_tables",1,0);
    if(!cpu || !alloc || !free || !publish || CALL(cpu,CPU_CURRENT,0,0).value || CALL(cpu,CPU_DEPTH,0,0).value)goto failed;
    struct kernel_surface_result r=CALL(alloc,b,ITEMS,sizeof(*items));if(r.status){error=CONFIG_MEMORY;goto failed;}items=(void *)r.value;
    size_t pos=0;uint64_t line=0;
    while(pos<d){size_t start=pos;while(pos<d && text[pos]!='\n'){if(!text[pos]){error=CONFIG_SYNTAX;info.line=line+1;goto failed;}++pos;}size_t end=pos;if(pos<d)++pos;++line;
        if(end>start && text[end-1]=='\r')--end;
        while(start<end && space(text[start]))++start;
        while(end>start && space(text[end-1]))--end;
        if(start==end || text[start]=='#')continue;
        info.line=line;size_t equal=start;while(equal<end && text[equal]!='=')++equal;
        if(equal==end){error=CONFIG_SYNTAX;goto failed;}
        size_t key_end=equal;while(key_end>start && space(text[key_end-1]))--key_end;
        size_t index=0;while(index<ITEMS && !same(text+start,key_end-start,schemas[index].key))++index;
        if(index==ITEMS){error=CONFIG_UNKNOWN;goto failed;}if(items[index].seen){error=CONFIG_DUPLICATE;goto failed;}
        size_t begin=equal+1;while(begin<end && space(text[begin]))++begin;
        error=value(&items[index],&schemas[index],text+begin,end-begin,alloc,b);if(error)goto failed;items[index].seen=1;
    }
    info.line=0;
    for(size_t i=0;i<ITEMS;++i){if(!items[i].seen){if(schemas[i].flags&REQUIRED){error=CONFIG_REQUIRED;goto failed;}
        if(!(schemas[i].flags&DEFAULT))continue;
        error=value(&items[i],&schemas[i],schemas[i].fallback,length(schemas[i].fallback),alloc,b);if(error)goto failed;}
        ++count;
    }
    r=CALL(alloc,b,count,sizeof(*tables));if(r.status){error=CONFIG_MEMORY;goto failed;}tables=(void *)r.value;
    for(size_t i=0,j=0;i<ITEMS;++i)if(items[i].descriptor.data){
        struct kernel_hub_table *t=&tables[j++];const char *prefix="config.";unsigned k=0;
        while(prefix[k]){t->name[k]=prefix[k];++k;}for(unsigned n=0;schemas[i].key[n];++n)t->name[k++]=schemas[i].key[n];
        t->owner=kernel_surface_owner(hub,"config.init");t->address=(uintptr_t)&items[i].descriptor;t->bytes=sizeof(struct config_value);t->entry_bytes=sizeof(struct config_value);t->count=1;
    }
    error=CONFIG_PUBLICATION;if(CALL(publish,tables,count,0).status)goto failed;
    CALL(free,b,tables,0);owned=items;(void)owned;info=(struct config_info){.count=count,.ready=1};return module_result(count,1);
failed:
    if(tables)CALL(free,b,tables,0);
    if(items){for(size_t i=0;i<ITEMS;++i)if(items[i].string)CALL(free,b,items[i].string,0);CALL(free,b,items,0);}
    info.error=error;return module_result(error,0);
}
struct kernel_surface_result config_surface_info(MODULE_ARGS){MODULE_UNUSED();if(!a)return module_result(0,0);*(struct config_info *)a=info;return module_result(1,1);}
