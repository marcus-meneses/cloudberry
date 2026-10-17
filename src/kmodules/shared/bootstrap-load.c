#include "bootstrap-load.h"
#include "config.h"
#include "hwenum.h"
#include "block-util.h"
#include "threads.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static struct {const struct kernel_surfaces *hub;uintptr_t heap;struct driver_table table;kernel_surface_call alloc,free,read,stat,load,cpu,thread,irq;} state;
struct manifest {char init[32],start[32];struct module_requirement *requires;uint64_t count,capacity;uint32_t error,version,named,match,pci_class;};
static size_t length(const char *p){size_t n=0;while(p[n])++n;return n;}
static void *allocate(size_t bytes){struct kernel_surface_result r=CALL(state.alloc,state.heap,1,bytes);return r.status?NULL:(void *)r.value;}
static void release(void *p){if(p)CALL(state.free,state.heap,p,0);}
static int same(const char *a,const char *b){return kernel_hub_name_equal(a,b);}
static int name(char *out,const char *p,int method){size_t n=length(p);if(!n || n>=32)return 0;for(size_t i=0;i<n;++i){char c=p[i];if(!((c>='a' && c<='z') || (i && c>='0' && c<='9') || (i && (c=='_' || c=='-' || (method && c=='.')))))return 0;out[i]=c;}out[n]=0;return 1;}
static const struct config_value *config(const char *key){const struct kernel_hub_table *t=kernel_hub_find_table(state.hub,key);const struct config_value *v=t?(const void *)t->address:NULL;return v && v->major==1 && v->type==CONFIG_STRING && v->data && v->bytes && ((const char *)v->data)[v->bytes-1]==0?v:NULL;}
static char *path(const char *directory,const char *base,const char *extension){size_t d=length(directory),b=length(base),e=length(extension);if(!d || directory[0]!='/' || b>SIZE_MAX-d-2 || e>SIZE_MAX-d-b-2)return NULL;char *out=allocate(d+b+e+2);if(!out)return NULL;disk_copy(out,directory,d);out[d++]='/';disk_copy(out+d,base,b);disk_copy(out+d+b,extension,e+1);return out;}
static void *file(uint64_t volume,const char *path,size_t *bytes){struct ext4_stat st;if(CALL(state.stat,volume,path,&st).status || (st.mode&0xf000)!=0x8000 || !st.bytes || st.bytes>=SIZE_MAX)return NULL;void *buffer=allocate(st.bytes+1);if(!buffer)return NULL;struct kernel_surface_result r=state.read(volume,(uintptr_t)path,0,(uintptr_t)buffer,st.bytes,0);if(r.status || r.value!=st.bytes){release(buffer);return NULL;}*bytes=st.bytes;return buffer;}
static char *trim(char *p){while(*p==' ' || *p=='\t')++p;size_t n=length(p);while(n && (p[n-1]==' ' || p[n-1]=='\t' || p[n-1]=='\r'))p[--n]=0;return p;}
static int decimal(const char *text,uint16_t *out){if(!*text)return 0;unsigned value=0;for(;*text;++text){unsigned d=(unsigned char)*text-'0';if(d>9 || value>(65535-d)/10)return 0;value=value*10+d;}*out=value;return 1;}
static int requirement(struct manifest *m,char *text){char *at=text;while(*at && *at!='@')++at;if(!*at)return 0;*at++=0;char *dot=at;while(*dot && *dot!='.')++dot;if(!*dot)return 0;*dot++=0;struct module_requirement r={0};if(!name(r.name,text,1) || !decimal(at,&r.major) || !r.major || !decimal(dot,&r.minor) || m->count>=SIZE_MAX/sizeof(r))return 0;
    for(uint64_t i=0;i<m->count;++i)if(same(m->requires[i].name,r.name))return 0;
    if(m->count==m->capacity){uint64_t capacity=m->capacity?m->capacity*2:4;if(capacity<m->capacity || capacity>SIZE_MAX/sizeof(r)){m->error=DRIVER_MEMORY;return 0;}struct module_requirement *next=allocate(capacity*sizeof(r));if(!next){m->error=DRIVER_MEMORY;return 0;}if(m->count)disk_copy(next,m->requires,m->count*sizeof(r));release(m->requires);m->requires=next;m->capacity=capacity;}m->requires[m->count++]=r;return 1;}
static int parse(struct manifest *m,char *text,size_t bytes,const char *module){
    for(size_t i=0;i<bytes;++i)if(!text[i])return 0;
    size_t offset=0;unsigned init=0,start=0;
    while(offset<bytes){char *line=text+offset;while(offset<bytes && text[offset]!='\n')++offset;if(offset<bytes)text[offset++]=0;line=trim(line);if(!*line || *line=='#')continue;char *value=line;while(*value && *value!='=')++value;if(!*value)return 0;*value++=0;value=trim(value);line=trim(line);
        if(same(line,"version")){if(m->version || !same(value,"1"))return 0;m->version=1;}
        else if(same(line,"module")){if(m->named || !same(value,module))return 0;m->named=1;}
        else if(same(line,"init")){if(init++ || !name(m->init,value,1))return 0;}
        else if(same(line,"start")){if(start++ || !name(m->start,value,1))return 0;}
        else if(same(line,"requires")){if(!requirement(m,value))return 0;}
        else if(same(line,"match")){if(m->match || !same(value,"always"))return 0;m->match=1;}
        else if(same(line,"pci.class")){if(m->match || length(value)!=6)return 0;uint32_t code=0;for(unsigned i=0;i<6;++i){unsigned c=value[i],d=c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:16;if(d>15)return 0;code=(code<<4)|d;}m->pci_class=code;m->match=2;}
        else return 0;
    }return m->version && m->named && init && m->match;
}
static uint64_t *devices(const struct manifest *m,uint64_t *count){*count=0;if(m->match==1)return NULL;const struct kernel_hub_table *t=kernel_hub_find_table(state.hub,HWENUM_TABLE_NAME);if(!t)return NULL;const struct hwenum_table *hw=(const void *)t->address;
    for(uint64_t i=0;i<hw->count;++i){const struct hwenum_descriptor *h=&hw->entries[i];if(h->kind==HWENUM_PCI && (((uint32_t)h->data.pci.class_code<<16)|((uint32_t)h->data.pci.subclass<<8)|h->data.pci.programming_interface)==m->pci_class)++*count;}
    if(!*count || *count>SIZE_MAX/sizeof(uint64_t))return NULL;
    uint64_t *indices=allocate(*count*sizeof(uint64_t));if(!indices)return NULL;uint64_t used=0;
    for(uint64_t i=0;i<hw->count;++i){const struct hwenum_descriptor *h=&hw->entries[i];if(h->kind==HWENUM_PCI && (((uint32_t)h->data.pci.class_code<<16)|((uint32_t)h->data.pci.subclass<<8)|h->data.pci.programming_interface)==m->pci_class)indices[used++]=i;}return indices;
}
static int selected(struct driver_record *r,const char *directory,uint64_t volume){
    struct manifest manifest={0};size_t bytes=0;char *metadata=path(directory,r->name,".driver"),*image_path=NULL;void *text=NULL,*image=NULL;uint64_t *indices=NULL;unsigned error=DRIVER_FILE;
    if(!metadata || !(text=file(volume,metadata,&bytes)))goto done;
    error=DRIVER_MANIFEST;if(!parse(&manifest,text,bytes,r->name)){if(manifest.error)error=manifest.error;goto done;}
    indices=devices(&manifest,&r->devices);if(manifest.match==2 && r->devices && !indices){error=DRIVER_MEMORY;goto done;}if(manifest.match==2 && !r->devices){r->state=DRIVER_SKIPPED;error=0;goto done;}
    error=DRIVER_FILE;image_path=path(directory,r->name,".kmod");if(!image_path || !(image=file(volume,image_path,&bytes)))goto done;
    struct module_load_request request={.abi=1,.image=image,.bytes=bytes,.requirements=manifest.requires,.requirement_count=manifest.count,.devices=indices,.device_count=r->devices};disk_copy(request.name,r->name,32);disk_copy(request.init,manifest.init,32);disk_copy(request.start,manifest.start,32);
    r->result.owner=UINT64_MAX;error=CALL(state.load,&request,&r->result,0).status?DRIVER_LOAD:DRIVER_OK;r->state=error?DRIVER_FAILED:DRIVER_LIVE;
done:r->error=error;if(error)r->state=DRIVER_FAILED;release(metadata);release(image_path);release(text);release(image);release(indices);release(manifest.requires);return !error;
}
int bootstrap_loader_init(const struct kernel_surfaces *hub,uintptr_t heap){
    state.hub=hub;state.heap=heap;
#define BIND(field,key) state.field=kernel_surface_find(hub,key);if(!state.field)return 0
    BIND(alloc,"mem.calloc");BIND(free,"mem.free");BIND(read,"ext4_fs.read");BIND(stat,"ext4_fs.stat");BIND(load,"kernel.module_load");BIND(cpu,"cpu.control");BIND(thread,"threads.control");BIND(irq,"irq.context");
#undef BIND
    struct kernel_hub_table table={.name="bootstrap.drivers",.address=(uintptr_t)&state.table,.bytes=sizeof(state.table),.count=1,.entry_bytes=sizeof(state.table),.owner=kernel_surface_owner(hub,"bootstrap.init")};kernel_surface_call publish=kernel_surface_find(hub,"hub.publish_table");return publish && !CALL(publish,&table,0,0).status;
}
struct kernel_surface_result bootstrap_surface_load(MODULE_ARGS){
    MODULE_UNUSED();uint64_t interrupts;__asm__ volatile("pushfq;pop %0":"=r"(interrupts));if(!state.hub || !(interrupts&512) || CALL(state.cpu,CPU_CURRENT,0,0).value || CALL(state.cpu,CPU_DEPTH,0,0).value || CALL(state.irq,0,0,0).value || !CALL(state.thread,THREAD_CURRENT,0,0).value || state.table.started)return module_result(0,0);
    state.table.started=1;const struct config_value *modules=config("config.drivers.modules"),*directory=config("config.drivers.directory");unsigned error=DRIVER_CONFIG;struct bootstrap_info info;kernel_surface_call snapshot=kernel_surface_find(state.hub,"bootstrap.info");
    if(!modules || !directory || !snapshot || CALL(snapshot,&info,0,0).status || info.state!=BOOTSTRAP_READY)goto done;
    const char *cursor=modules->data;error=0;struct driver_record **tail=&state.table.first;
    /* Validate the complete opt-in list before any selected image can execute. */
    while(*cursor){const char *first=cursor;while(*cursor && *cursor!=',')++cursor;size_t n=cursor-first;if(!n || n>=32){error=DRIVER_CONFIG;break;}char text[32]={0};disk_copy(text,first,n);char checked[32]={0};if(!name(checked,text,0)){error=DRIVER_CONFIG;break;}
        for(struct driver_record *p=state.table.first;p;p=p->next)if(same(p->name,checked)){error=DRIVER_CONFIG;break;}
        if(error)break;
        struct driver_record *r=allocate(sizeof(*r));if(!r){error=DRIVER_MEMORY;break;}disk_copy(r->name,checked,32);r->state=DRIVER_SELECTED;r->result.owner=UINT64_MAX;
        uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;*tail=r;tail=&r->next;++state.table.count;CALL(state.cpu,CPU_LEAVE,flags,0);
        if(*cursor){++cursor;if(!*cursor){error=DRIVER_CONFIG;break;}}
    }
    if(!error)for(struct driver_record *r=state.table.first;r;r=r->next)if(!selected(r,directory->data,info.filesystem.cookie)){error=r->error;break;}

done:state.table.error=error;state.table.finished=1;
    kernel_surface_call log=kernel_surface_find(state.hub,"hardware.log");const char *message=error?"bootstrap: selected driver load failed; inspect bootstrap.drivers\n":"bootstrap: selected future drivers ready\n";if(log)CALL(log,message,length(message),0);return module_result(error,!error);
}
struct kernel_surface_result bootstrap_surface_driver_info(MODULE_ARGS){MODULE_UNUSED();if(!state.hub || !a || CALL(state.cpu,CPU_CURRENT,0,0).value)return module_result(0,0);uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;*(struct driver_table *)a=state.table;CALL(state.cpu,CPU_LEAVE,flags,0);return module_result(1,1);}
