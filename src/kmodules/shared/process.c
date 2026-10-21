#include "process.h"
#include "../../interfaces/sessions.h"
#include "../../interfaces/applications.h"
#include "../../interfaces/system.h"
#include "process-elf.h"
#include "threads.h"
#include "inthandle.h"
#include "mem-pages.h"
#include "mem-spaces.h"
#include "../../kernel/cpu.h"
struct page {struct page *next;struct mem_pages frame;uint64_t address;uint32_t flags,mapped;};
struct allocation {struct allocation *next;uint64_t address,bytes;};
struct process {char *home;uint64_t parent,session,session_parent,session_closed,session_stop_failed;struct cb_credentials credentials;uint64_t *groups;struct process *next;struct allocation *allocations;uint64_t memory_next,stack_bottom,user_pages;char name[256];uint32_t name_bytes,name_truncated;struct process_info info;struct page *pages;struct mem_stack trap;uint64_t entry,sp,argument,saved_rsp;};
static struct {const struct kernel_surfaces *hub;uintptr_t heap;uint64_t serial;struct process *head;
    kernel_surface_call alloc,free,pages_alloc,pages_free,stack_alloc,stack_free,space_create,space_destroy,space_map,create,control,entry_stack,stat,read,cpu;} state;
struct kernel_surface_result process_surface_check(MODULE_ARGS);
extern uint64_t process_enter(uintptr_t,uintptr_t,uint64_t,uint64_t *) __attribute__((visibility("hidden")));
extern void process_return(void) __attribute__((visibility("hidden")));
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static uint64_t lock(void){return CALL(state.cpu,CPU_ENTER,0,0).value;}
static void unlock(uint64_t flags){CALL(state.cpu,CPU_LEAVE,flags,0);}
static void *allocate(size_t bytes){struct kernel_surface_result r=CALL(state.alloc,state.heap,1,bytes);return r.status?NULL:(void *)r.value;}
static void release(void *p){if(p)CALL(state.free,state.heap,p,0);}
static struct process *find(uint64_t id){for(struct process *p=state.head;p;p=p->next)if(p->info.id==id)return p;return NULL;}
static struct process *current(void){uint64_t id=CALL(state.control,THREAD_CURRENT,0,0).value;for(struct process *p=state.head;p;p=p->next)if(p->info.thread==id && id)return p;return NULL;}
static int add_page(struct process *p,uint64_t address,uint32_t flags,const void *source,size_t bytes){
    struct page *page=allocate(sizeof(*page));if(!page)return 0;
    if(CALL(state.pages_alloc,1,p->info.id,&page->frame).status){release(page);return 0;}
    page->address=address;page->flags=flags;
    const unsigned char *s=source;unsigned char *d=(void *)page->frame.address;for(size_t i=0;i<bytes;++i)d[i]=s[i];
    struct kernel_surface_result r=state.space_map(p->info.address_space,p->info.id,address,page->frame.physical,flags,0);
    page->mapped=!r.status;uint64_t flags_saved=lock();page->next=p->pages;p->pages=page;++p->user_pages;unlock(flags_saved);return !r.status;
}
static int cleanup(struct process *p){
    if(p->info.address_space && CALL(state.space_destroy,p->info.address_space,p->info.id,0).status)return 0;
    p->info.address_space=0;
    while(p->pages){struct page *page=p->pages;if(CALL(state.pages_free,page->frame.id,p->info.id,0).status)return 0;uint64_t flags=lock();p->pages=page->next;--p->user_pages;unlock(flags);release(page);}
    if(p->trap.id && CALL(state.stack_free,p->trap.id,p->info.id,0).status)return 0;
    p->trap.id=0;release(p->home);p->home=NULL;release(p->groups);p->groups=NULL;while(p->allocations){struct allocation *record=p->allocations;p->allocations=record->next;release(record);}return 1;
}
static struct kernel_surface_result load_failure(struct process *p){
    if(!cleanup(p)){p->info.state=PROCESS_CLEANUP;uint64_t flags=lock();p->next=state.head;state.head=p;unlock(flags);return module_result(p->info.id,0);}
    release(p);return module_result(0,0);
}
static int elf_load(struct process *p,const unsigned char *image,size_t bytes,uint64_t stack_bytes){
    if(!process_elf_valid(image,bytes))return 0;
    const struct elf_header *h=(const void *)image;
    const struct elf_program *programs=(const void *)(image+h->phoff);
    if(!stack_bytes || stack_bytes>MEM_USER_LIMIT/2 || stack_bytes>UINT64_MAX-4095)return 0;
    for(unsigned i=0;i<h->phnum;++i){const struct elf_program *s=&programs[i];if(s->type!=1)continue;
        for(uint64_t off=0;off<s->memsz;off+=4096){size_t copy=off<s->filesz?(s->filesz-off<4096?s->filesz-off:4096):0;
            if(!add_page(p,s->address+off,((s->flags&2)?MEM_SPACE_WRITE:0)|((s->flags&1)?MEM_SPACE_EXEC:0),image+s->offset+(copy?off:0),copy))return 0;}
    }
    uint64_t top=MEM_USER_LIMIT-65536,rounded=(stack_bytes+4095)&~UINT64_C(4095);
    for(uint64_t off=0;off<rounded;off+=4096)if(!add_page(p,top-rounded+off,MEM_SPACE_WRITE,NULL,0))return 0;
    p->entry=h->entry;p->sp=top-8;p->stack_bottom=top-rounded;return 1;
}
struct kernel_surface_result process_surface_init(MODULE_ARGS){
    MODULE_UNUSED();if(state.hub)return module_result(1,state.hub==(const void *)a && state.heap==b);
    const struct kernel_surfaces *hub=(const void *)a;if(!hub || !b)return module_result(0,0);
#define BIND(field,name) do{state.field=kernel_surface_find(hub,name);if(!state.field)return module_result(0,0);}while(0)
    BIND(alloc,"mem.calloc");BIND(free,"mem.free");BIND(pages_alloc,"mem.pages_alloc");BIND(pages_free,"mem.pages_free");BIND(stack_alloc,"mem.stack_alloc");BIND(stack_free,"mem.stack_free");BIND(space_create,"mem.space_create");BIND(space_destroy,"mem.space_destroy");BIND(space_map,"mem.space_map");BIND(create,"threads.create");BIND(control,"threads.control");BIND(entry_stack,"threads.entry_stack");BIND(stat,"ext4_fs.stat");BIND(read,"ext4_fs.read");BIND(cpu,"cpu.control");
#undef BIND
    kernel_surface_call claim=kernel_surface_find(hub,"inthandle.claim"),fault=kernel_surface_find(hub,"inthandle.user_fault");
    if(!claim || !fault || claim(128,(uintptr_t)"process.gate",0,1,0,0).status || CALL(fault,"process.fault",0,0).status)return module_result(0,0);
    state.heap=b;state.hub=hub;return module_result(1,1);
}
#include "ext4-fs.h"
struct kernel_surface_result process_surface_load(MODULE_ARGS){
    MODULE_UNUSED();const struct process_request *r=(const void *)a;
    if(!state.hub || !r || (r->abi!=1 && r->abi!=2) || !r->path || !r->filesystem)return module_result(0,0);
    struct ext4_stat st;if(CALL(state.stat,r->filesystem,r->path,&st).status || !st.bytes || st.bytes>SIZE_MAX)return module_result(0,0);
    unsigned char *image=allocate(st.bytes);if(!image)return module_result(0,0);
    struct kernel_surface_result got=state.read(r->filesystem,(uintptr_t)r->path,0,(uintptr_t)image,st.bytes,0);
    if(got.status || got.value!=st.bytes){release(image);return module_result(0,0);}
    struct process *p=allocate(sizeof(*p));if(!p){release(image);return module_result(0,0);}
    const char *label=r->path;for(const char *at=r->path;*at;++at)if(*at=='/')label=at+1;
    while(label[p->name_bytes] && p->name_bytes<sizeof(p->name)-1){p->name[p->name_bytes]=label[p->name_bytes];++p->name_bytes;}p->name_truncated=label[p->name_bytes]!=0;
    uint64_t flags=lock();if(state.serial==UINT64_MAX){unlock(flags);release(p);release(image);return module_result(0,0);}p->info.id=++state.serial;p->info.cpu=r->cpu;p->info.state=PROCESS_LOADING;unlock(flags);
    struct mem_space_info space;int ok=!CALL(state.space_create,p->info.id,&space,0).status;if(ok)p->info.address_space=space.id;
    if(ok)ok=elf_load(p,image,st.bytes,r->stack_bytes?r->stack_bytes:65536);
    release(image);
    if(ok)ok=!CALL(state.stack_alloc,65536,p->info.id,&p->trap).status;
    if(!ok)return load_failure(p);
    p->credentials.generation=1; /* Explicit root bootstrap identity. */
    p->argument=r->argument;
    if(r->abi==2){
        if(!r->startup || !r->startup_bytes || r->startup_bytes>SIZE_MAX-4095){return load_failure(p);}
        uint64_t rounded=(r->startup_bytes+4095)&~UINT64_C(4095),va=65536;int gap=0;
        while(va<p->stack_bottom-4096 && rounded<=p->stack_bottom-4096-va){uint64_t next=va;for(struct page *page=p->pages;page;page=page->next)if(page->address>=va && page->address-va<rounded && page->address+4096>next)next=page->address+4096;if(next==va){gap=1;break;}va=next;}
        if(gap)for(uint64_t off=0;off<rounded;off+=4096){size_t n=off<r->startup_bytes?(r->startup_bytes-off<4096?r->startup_bytes-off:4096):0;if(!add_page(p,va+off,0,(const unsigned char *)r->startup+off,n)){gap=0;break;}}
        if(!gap){return load_failure(p);}p->argument=va;
    }
    p->info.state=PROCESS_READY;flags=lock();p->next=state.head;state.head=p;unlock(flags);return module_result(p->info.id,1);
}
struct kernel_surface_result process_surface_start(MODULE_ARGS){
    MODULE_UNUSED();uint64_t flags=lock();struct process *p=find(a);if(!p || p->info.state!=PROCESS_READY || p->session_closed){unlock(flags);return module_result(0,0);}p->info.state=PROCESS_LOADING;unlock(flags);
    struct thread_request r={.abi=2,.cpu=p->info.cpu,.process_id=p->info.id,.address_space=p->info.address_space,.stack_bytes=32768,.entry=(uintptr_t)kernel_surface_find(state.hub,"process.run"),.major=1,.payload={.arguments={(uintptr_t)p}}};
    /* The entry waits for publication if create wakes its AP immediately. */
    struct kernel_surface_result created=CALL(state.create,&r,0,0);flags=lock();if(!created.status)p->info.thread=created.value;__atomic_store_n(&p->info.state,created.status?PROCESS_READY:PROCESS_RUNNING,__ATOMIC_RELEASE);unlock(flags);return created;
}
/* Cooperative owner request: no forced unwind and no freeing a running task. */
struct kernel_surface_result process_surface_stop(MODULE_ARGS){
    MODULE_UNUSED();uint64_t flags=lock();struct process *p=find(a);if(!p){unlock(flags);return module_result(0,0);}
    if(p->info.state==PROCESS_EXITED || p->info.state==PROCESS_FAULTED || p->info.stop_requested){unlock(flags);return module_result(1,1);}
    if(p->info.state!=PROCESS_RUNNING){unlock(flags);return module_result(0,0);}unlock(flags);
    kernel_surface_call stop=kernel_surface_find(state.hub,"handles.decommission");if(!stop || CALL(stop,a,0,0).status)return module_result(0,0);
    flags=lock();p=find(a);if(p)p->info.stop_requested=1;unlock(flags);return module_result(1,1);
}
struct kernel_surface_result process_surface_run(MODULE_ARGS){
    MODULE_UNUSED();struct process *p=(void *)a;
    if(!p)return module_result(0,0);
    while(__atomic_load_n(&p->info.state,__ATOMIC_ACQUIRE)==PROCESS_LOADING)if(CALL(state.control,THREAD_YIELD,0,0).status)return module_result(0,0);
    uint64_t flags=lock();if(current()!=p || p->info.state!=PROCESS_RUNNING){unlock(flags);return module_result(0,0);}unlock(flags);
    uint64_t saved;__asm__ volatile("pushfq;pop %0;cli":"=r"(saved)::"memory");
    if(CALL(state.entry_stack,p->trap.top,0,0).status)__asm__ volatile("ud2");
    process_enter(p->entry,p->sp,p->argument,&p->saved_rsp);
    CALL(state.entry_stack,0,0,0);__asm__ volatile("push %0;popfq"::"r"(saved):"memory","cc");
    kernel_surface_call io_exit=kernel_surface_find(state.hub,"io.exit");if(io_exit)CALL(io_exit,p->info.id,0,0);
    kernel_surface_call exit=kernel_surface_find(state.hub,"handles.exit");if(exit)CALL(exit,p->info.id,0,0);
    return module_result(p->info.exit_code,1);
}
static void terminate(struct process *p,struct inthandle_frame *f,uint64_t code){
    p->info.exit_code=code;f->rip=(uintptr_t)process_return;f->rsp=p->saved_rsp;f->cs=8;f->ss=16;f->rflags=2;f->rax=code;
}
struct kernel_surface_result process_surface_gate(MODULE_ARGS){
    MODULE_UNUSED();struct inthandle_frame *frame=(void *)a;if(!frame || frame->vector!=128 || (frame->cs&3)!=3)return module_result(0,0);
    uint64_t flags=lock();struct process *p=current();if(!p || !p->saved_rsp){unlock(flags);return module_result(0,0);}
    if(!frame->rax){p->info.state=PROCESS_EXITED;terminate(p,frame,frame->rdi);}
    else if(frame->rax==1){
        unlock(flags);struct cb_system_gate gate;struct kernel_surface_result checked=process_surface_check(p->info.id,frame->rdi,sizeof(gate),1,0,0);
        if(checked.status || process_surface_copy(p->info.id,frame->rdi,(uintptr_t)&gate,sizeof(gate),0,0).status){frame->rax=CB_BAD_REQUEST;return module_result(1,1);}
        kernel_surface_call mediate=kernel_surface_find(state.hub,"handles.gate");
        __asm__ volatile("sti":::"memory");struct kernel_surface_result r=mediate?CALL(mediate,p->info.id,&gate,0):module_result(CB_UNAVAILABLE,1);__asm__ volatile("cli":::"memory");
        gate.status=r.status?CB_RESOURCE_FAILURE:r.value;
        if(process_surface_copy(p->info.id,frame->rdi,(uintptr_t)&gate,sizeof(gate),1,0).status)frame->rax=CB_BAD_REQUEST;else frame->rax=gate.status;
        return module_result(1,1);
    }else frame->rax=CB_BAD_REQUEST;
    unlock(flags);return module_result(1,1);
}
struct kernel_surface_result process_surface_fault(MODULE_ARGS){
    MODULE_UNUSED();struct inthandle_frame *frame=(void *)a;uint64_t flags=lock();struct process *p=current();if(!frame || !p || (frame->cs&3)!=3 || !p->saved_rsp){unlock(flags);return module_result(0,0);}
    uintptr_t cr2=0;if(frame->vector==14)__asm__ volatile("mov %%cr2,%0":"=r"(cr2));
    p->info.state=PROCESS_FAULTED;p->info.fault_vector=frame->vector;p->info.fault_address=cr2;p->info.fault_rip=frame->rip;terminate(p,frame,128+frame->vector);unlock(flags);return module_result(1,1);
}
struct kernel_surface_result process_surface_info(MODULE_ARGS){
    MODULE_UNUSED();if(!b)return module_result(0,0);uint64_t flags=lock();struct process *p=find(a);
    if(p){*(struct process_info *)b=p->info;if(p->info.thread){struct thread_info t;if(!CALL(state.control,THREAD_INFO,p->info.thread,&t).status)((struct process_info *)b)->switches=t.switches;}}
    unlock(flags);return module_result(p!=NULL,p!=NULL);
}
struct kernel_surface_result process_surface_reap(MODULE_ARGS){
    MODULE_UNUSED();uint64_t flags=lock();struct process *p=find(a);if(!p || (p->info.state!=PROCESS_EXITED && p->info.state!=PROCESS_FAULTED && p->info.state!=PROCESS_READY && p->info.state!=PROCESS_CLEANUP)){unlock(flags);return module_result(0,0);}
    if(p->info.thread){struct thread_info t;if(CALL(state.control,THREAD_INFO,p->info.thread,&t).status || t.state!=THREAD_EXITED){unlock(flags);return module_result(0,0);}}
    uint32_t previous=p->info.state;p->info.state=PROCESS_REAPING;unlock(flags);
    if(p->info.thread && CALL(state.control,THREAD_REAP,p->info.thread,0).status){flags=lock();p->info.state=previous;unlock(flags);return module_result(0,0);}p->info.thread=0;
    kernel_surface_call io_exit=kernel_surface_find(state.hub,"io.exit");if(io_exit && CALL(io_exit,p->info.id,0,0).status){flags=lock();p->info.state=previous;unlock(flags);return module_result(0,0);}
    kernel_surface_call handles_exit=kernel_surface_find(state.hub,"handles.exit");if(handles_exit)CALL(handles_exit,p->info.id,0,0);
    if(!cleanup(p)){flags=lock();p->info.state=previous;unlock(flags);return module_result(0,0);}
    flags=lock();struct process **at=&state.head;while(*at!=p)at=&(*at)->next;*at=p->next;unlock(flags);release(p);return module_result(1,1);
}
struct kernel_surface_result process_surface_current(MODULE_ARGS){MODULE_UNUSED();uint64_t flags=lock();struct process *p=current();uint64_t id=p?p->info.id:0;unlock(flags);return module_result(id,1);}
struct kernel_surface_result process_surface_copy(MODULE_ARGS){
    MODULE_UNUSED();if(d>4096 || (!c && d) || b<4096 || b>=MEM_USER_LIMIT || d>MEM_USER_LIMIT-b || e>1)return module_result(0,0);
    uint64_t flags=lock();struct process *p=find(a);if(!p || p->info.state==PROCESS_REAPING){unlock(flags);return module_result(0,0);}
    /* Validate the full range before writing any prefix. Process mappings remain
     * stable under this lock; later handle allocation/release uses the same lock. */
    for(uint64_t off=0;off<d;){struct page *page=p->pages;uint64_t address=(b+off)&~UINT64_C(4095);while(page && page->address!=address)page=page->next;
        if(!page || !page->mapped || (e && !(page->flags&MEM_SPACE_WRITE))){unlock(flags);return module_result(0,0);}uint64_t n=4096-((b+off)&4095);off+=n<d-off?n:d-off;}
    for(uint64_t off=0;off<d;){struct page *page=p->pages;while(page->address!=((b+off)&~UINT64_C(4095)))page=page->next;uint64_t n=4096-((b+off)&4095);if(n>d-off)n=d-off;
        unsigned char *user=(void *)(page->frame.address+((b+off)&4095)),*kernel=(void *)(c+off);for(uint64_t i=0;i<n;++i)if(e)user[i]=kernel[i];else kernel[i]=user[i];off+=n;}
    unlock(flags);return module_result(d,1);
}

struct kernel_surface_result process_surface_check(MODULE_ARGS){
    MODULE_UNUSED();if((b<4096 && c) || b>=MEM_USER_LIMIT || c>MEM_USER_LIMIT-b || d>1 || c>16384)return module_result(0,0);
    uint64_t flags=lock();struct process *p=find(a);int ok=p && (p->info.state==PROCESS_RUNNING || p->info.state==PROCESS_READY);
    for(uint64_t off=0;ok && off<c;){struct page *page=p->pages;uint64_t va=(b+off)&~UINT64_C(4095);while(page && page->address!=va)page=page->next;
        if(!page || !page->mapped || (d && !(page->flags&MEM_SPACE_WRITE))){ok=0;break;}uint64_t n=4096-((b+off)&4095);off+=n<c-off?n:c-off;}
    unlock(flags);return module_result(ok,ok);
}
/* Owner-thread foreground memory primitives; arbitrary user addresses never
 * select another owner's backing. Calls commit pages with short metadata locks. */
struct kernel_surface_result process_surface_memory_free(MODULE_ARGS){
    MODULE_UNUSED();if(!b)return module_result(1,1);uint64_t flags=lock();struct process *p=find(a);
    if(!p || current()!=p || p->info.state!=PROCESS_RUNNING){unlock(flags);return module_result(0,0);}
    struct allocation **at=&p->allocations;while(*at && (*at)->address!=b)at=&(*at)->next;
    if(!*at){unlock(flags);return module_result(0,0);}struct allocation *record=*at;unlock(flags);
    kernel_surface_call unmap=kernel_surface_find(state.hub,"mem.space_unmap");
    for(uint64_t off=0;off<record->bytes;off+=4096){uint64_t va=b+off;flags=lock();struct page **link=&p->pages;while(*link && (*link)->address!=va)link=&(*link)->next;struct page *page=*link;unlock(flags);if(!page)continue;
        if(page->mapped && CALL(unmap,p->info.address_space,a,va).status)return module_result(0,0);
        flags=lock();link=&p->pages;while(*link!=page)link=&(*link)->next;*link=page->next;--p->user_pages;unlock(flags);
        if(CALL(state.pages_free,page->frame.id,a,0).status)__asm__ volatile("ud2");
        release(page);
    }
    flags=lock();at=&p->allocations;while(*at!=record)at=&(*at)->next;*at=record->next;p->memory_next=b;unlock(flags);release(record);return module_result(1,1);
}
struct kernel_surface_result process_surface_memory_alloc(MODULE_ARGS){
    MODULE_UNUSED();if(!b)return module_result(0,1);if(b>UINT64_MAX-4095)return module_result(0,0);uint64_t bytes=(b+4095)&~UINT64_C(4095);
    uint64_t flags=lock();struct process *p=find(a);if(!p || current()!=p || p->info.state!=PROCESS_RUNNING){unlock(flags);return module_result(0,0);}unlock(flags);
    uint64_t start=p->memory_next?p->memory_next:UINT64_C(0x100000000),address=start,limit=p->stack_bottom-4096;int found=0;
    for(unsigned pass=0;pass<2 && !found;++pass){uint64_t end=pass?start:limit;if(pass)address=UINT64_C(0x100000000);
        while(address<end && bytes<=end-address){uint64_t next=address;flags=lock();for(struct page *page=p->pages;page;page=page->next)if(page->address>=address && page->address-address<bytes && page->address+4096>next)next=page->address+4096;unlock(flags);if(next==address){found=1;break;}address=next;}}
    if(!found)return module_result(0,0);
    struct allocation *record=allocate(sizeof(*record));if(!record)return module_result(0,0);
    record->address=address;record->bytes=0;flags=lock();record->next=p->allocations;p->allocations=record;unlock(flags);
    for(uint64_t off=0;off<bytes;off+=4096){record->bytes=off+4096;if(!add_page(p,address+off,MEM_SPACE_WRITE,NULL,0)){(void)process_surface_memory_free(a,address,0,0,0,0);return module_result(0,0);}}
    p->memory_next=address+bytes;return module_result(address,1);
}

struct kernel_surface_result process_surface_sample(MODULE_ARGS){
    MODULE_UNUSED();if(!a || !b || !state.hub)return module_result(0,0);struct cb_process_query query=*(const struct cb_process_query *)a;uint64_t flags=lock();uint64_t ceiling=query.ceiling?query.ceiling:state.serial;
    struct process *selected=NULL;for(struct process *p=state.head;p;p=p->next)if(p->info.id>query.after && p->info.id<=ceiling && p->info.state!=PROCESS_REAPING && (!selected || p->info.id<selected->info.id))selected=p;
    if(!selected){unlock(flags);return module_result(0,1);}struct cb_process_sample out={.process=selected->info.id,.thread=selected->info.thread,.ceiling=ceiling,.user_bytes=selected->user_pages*4096,.state=selected->info.state,.cpu=selected->info.cpu,.name_bytes=selected->name_bytes,.flags=selected->name_truncated?CB_PROCESS_NAME_TRUNCATED:0};
    if(selected->info.stop_requested)out.flags|=CB_PROCESS_STOP_REQUESTED;
    for(unsigned i=0;i<out.name_bytes;++i)out.name[i]=selected->name[i];
    struct thread_account_info time;if(!CALL(state.control,THREAD_ACCOUNTING,out.thread,&time).status){out.timestamp_ns=time.timestamp_ns;out.cpu_ns=time.runtime_ns;out.thread_state=time.state;out.cpus=time.cpus;if(time.flags)out.flags|=CB_PROCESS_CPU_VALID;}
    if(out.thread){struct thread_info thread;if(!CALL(state.control,THREAD_INFO,out.thread,&thread).status)out.switches=thread.switches;}
    *(struct cb_process_sample *)b=out;unlock(flags);return module_result(1,1);
}

static int home_equal(const char *a,const char *b,size_t n){for(size_t i=0;i<n;++i)if(a[i]!=b[i])return 0;return 1;}
struct kernel_surface_result process_surface_credentials(MODULE_ARGS){
    MODULE_UNUSED();if(!state.hub)return module_result(0,0);uint64_t flags=lock();struct process *p=find(a);
    if(!p || p->info.state==PROCESS_REAPING || p->info.state==PROCESS_CLEANUP){unlock(flags);return module_result(0,0);}
    if(b==0){size_t bytes=sizeof(p->credentials)+p->credentials.groups*8;if(!c || d<sizeof(p->credentials)){unlock(flags);return module_result(bytes,0);}*(struct cb_credentials *)c=p->credentials;if(d>=bytes)for(size_t i=0;i<p->credentials.groups;++i)((uint64_t *)(c+sizeof(p->credentials)))[i]=p->groups[i];unlock(flags);return module_result(bytes,1);}
    if(b==3){int yes=p->credentials.gid==c;for(size_t i=0;i<p->credentials.groups;++i)if(p->groups[i]==c)yes=1;unlock(flags);return module_result(yes,1);}
    struct cb_credentials value;const uint64_t *groups;const char *home=NULL;size_t home_bytes=0;
    if(b==2){struct process *parent=find(c);if(!parent || parent->session_closed || p->info.state!=PROCESS_READY || p->info.thread){unlock(flags);return module_result(0,0);}value=parent->credentials;groups=parent->groups;home=parent->home;if(home)while(home[home_bytes])++home_bytes;}
    else if(b==4 && (p->info.state==PROCESS_READY || p->info.state==PROCESS_RUNNING) && c && d>=sizeof(struct cb_identity_home)){struct cb_identity_home request=*(const struct cb_identity_home *)c;value=request.credentials;home_bytes=request.home_bytes;if(value.groups>(d-sizeof(request))/8 || home_bytes>4095 || !home_bytes || home_bytes!=d-sizeof(request)-value.groups*8){unlock(flags);return module_result(0,0);}groups=(const void *)(c+sizeof(request));home=(const void *)(c+sizeof(request)+value.groups*8);if((value.uid==0 && (home_bytes!=12 || !home_equal(home,"/system/root",12))) || (value.uid && (home_bytes<=6 || !home_equal(home,"/home/",6)))){unlock(flags);return module_result(0,0);}for(size_t i=value.uid?6:1;i<home_bytes;++i)if(!home[i] || (value.uid && !((home[i]>='a' && home[i]<='z') || home[i]=='_' || (i>6 && ((home[i]>='0' && home[i]<='9') || home[i]=='-'))))){unlock(flags);return module_result(0,0);}}
    else if(b==1 && (p->info.state==PROCESS_READY || p->info.state==PROCESS_RUNNING) && c && d>=sizeof(value)){value=*(const struct cb_credentials *)c;if(value.groups>(SIZE_MAX-sizeof(value))/8 || d!=sizeof(value)+value.groups*8){unlock(flags);return module_result(0,0);}groups=(const void *)(c+sizeof(value));}
    else {unlock(flags);return module_result(0,0);}
    if(p->credentials.generation==UINT64_MAX){unlock(flags);return module_result(0,0);}
    uint64_t *copy=value.groups?allocate(value.groups*8):NULL;if(value.groups && !copy){unlock(flags);return module_result(0,0);}for(size_t i=0;i<value.groups;++i)copy[i]=groups[i];
    char *new_home=home_bytes?allocate(home_bytes+1):NULL;if(home_bytes && !new_home){release(copy);unlock(flags);return module_result(0,0);}for(size_t i=0;i<home_bytes;++i)new_home[i]=home[i];if(new_home)new_home[home_bytes]=0;char *old_home=p->home;p->home=new_home;
    if(b==2){struct process *parent=find(c);p->parent=c;p->session=parent->session;p->session_parent=parent->session_parent;}

    uint64_t *old=p->groups;value.generation=p->credentials.generation+1;p->credentials=value;p->groups=copy;unlock(flags);release(old_home);release(old);return module_result(1,1);
}

/* Trusted mechanics only. Session policy and lifetime belong to the privileged
 * sessions provider. Never reclaim a running context on logout. */
struct kernel_surface_result process_surface_session(MODULE_ARGS){
    MODULE_UNUSED();if(!state.hub)return module_result(0,0);uint64_t flags=lock();struct process *p=find(a);if(!p && b!=2 && b!=3){unlock(flags);return module_result(0,0);}
    if(b==0 && c && d>=sizeof(struct cb_session)){*(struct cb_session *)c=(struct cb_session){p->session,p->session_parent,p->session,p->credentials.uid};unlock(flags);return module_result(1,1);}
    if(b==1 && !p->session_closed && p->session!=a && p->info.state==PROCESS_RUNNING){p->session_parent=p->session;p->session=a;unlock(flags);return module_result(1,1);}
    if(b==3 && c && d>=sizeof(struct cb_session_status)){struct cb_session_status out={.session={a,p?p->session_parent:0,a,p?p->credentials.uid:0}};for(struct process *q=state.head;q;q=q->next)if(q->session==a){++out.members;if(q->info.id!=a && q->info.state!=PROCESS_EXITED && q->info.state!=PROCESS_FAULTED && q->info.state!=PROCESS_REAPING && q->info.state!=PROCESS_CLEANUP)++out.running;if(q->session_closed)out.closing=1;if(q->session_stop_failed)++out.stop_failures;}*(struct cb_session_status *)c=out;unlock(flags);return module_result(1,1);}
    if(b==2 && a){for(struct process *q=state.head;q;q=q->next)if(q->session==a)q->session_closed=1;unlock(flags);
        kernel_surface_call revoke=kernel_surface_find(state.hub,"handles.revoke"),files=kernel_surface_find(state.hub,"filesystem.revoke");
        uint64_t cursor=0,failed=0;for(;;){flags=lock();uint64_t next=0;for(struct process *q=state.head;q;q=q->next)if(q->session==a && q->info.id>cursor && (!next || q->info.id<next))next=q->info.id;unlock(flags);if(!next)break;
            int bad=!revoke || CALL(revoke,next,0,0).status;if(files)(void)CALL(files,next,0,0);
            if(next!=c && process_surface_stop(next,0,0,0,0,0).status){flags=lock();struct process *q=find(next);if(q && q->info.state!=PROCESS_READY && q->info.state!=PROCESS_EXITED && q->info.state!=PROCESS_FAULTED)bad=1;unlock(flags);}flags=lock();struct process *q=find(next);if(q)q->session_stop_failed=bad;unlock(flags);if(bad)++failed;cursor=next;}return module_result(failed,1);}
    unlock(flags);return module_result(0,0);
}
struct kernel_surface_result process_surface_ancestor(MODULE_ARGS){
    MODULE_UNUSED();if(!state.hub || !a || !b)return module_result(0,0);uint64_t flags=lock();struct process *p=find(b);int yes=p && !p->session_closed && a==b;if(p && p->session_closed)p=NULL;
    while(p && p->parent){if(p->parent==a){yes=1;break;}if(p->parent>=p->info.id)break;p=find(p->parent);}unlock(flags);return module_result(yes,1);
}

struct kernel_surface_result process_surface_home(MODULE_ARGS){
    MODULE_UNUSED();if(!state.hub)return module_result(0,0);uint64_t flags=lock();struct process *p=find(a);if(!p){unlock(flags);return module_result(0,0);}const char *home=p->home?p->home:!p->credentials.uid?"/system/root":NULL;if(!home){unlock(flags);return module_result(0,0);}size_t n=0;while(home[n])++n;if(!b || c<n+1){unlock(flags);return module_result(n+1,0);}for(size_t i=0;i<=n;++i)((char *)b)[i]=home[i];unlock(flags);return module_result(n,1);
}

struct kernel_surface_result process_surface_active(MODULE_ARGS){MODULE_UNUSED();if(!state.hub)return module_result(0,0);uint64_t flags=lock();struct process *p=find(a);int active=p && !p->session_closed;unlock(flags);return module_result(active,1);}
