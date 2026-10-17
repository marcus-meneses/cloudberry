#include "process.h"
#include "process-elf.h"
#include "threads.h"
#include "inthandle.h"
#include "mem-pages.h"
#include "mem-spaces.h"
#include "../../kernel/cpu.h"
struct page {struct page *next;struct mem_pages frame;uint64_t address;uint32_t flags;};
struct process {struct process *next;struct process_info info;struct page *pages;struct mem_stack trap;uint64_t entry,sp,argument,saved_rsp;};
static struct {const struct kernel_surfaces *hub;uintptr_t heap;uint64_t serial;struct process *head;
    kernel_surface_call alloc,free,pages_alloc,pages_free,stack_alloc,stack_free,space_create,space_destroy,space_map,create,control,entry_stack,stat,read,cpu;} state;
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
    page->next=p->pages;p->pages=page;return !r.status;
}
static int cleanup(struct process *p){
    if(p->info.address_space && CALL(state.space_destroy,p->info.address_space,p->info.id,0).status)return 0;
    p->info.address_space=0;
    while(p->pages){struct page *page=p->pages;if(CALL(state.pages_free,page->frame.id,p->info.id,0).status)return 0;p->pages=page->next;release(page);}
    if(p->trap.id && CALL(state.stack_free,p->trap.id,p->info.id,0).status)return 0;
    p->trap.id=0;return 1;
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
    p->entry=h->entry;p->sp=top-8;return 1;
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
    if(!state.hub || !r || r->abi!=1 || !r->path || !r->filesystem)return module_result(0,0);
    struct ext4_stat st;if(CALL(state.stat,r->filesystem,r->path,&st).status || !st.bytes || st.bytes>SIZE_MAX)return module_result(0,0);
    unsigned char *image=allocate(st.bytes);if(!image)return module_result(0,0);
    struct kernel_surface_result got=state.read(r->filesystem,(uintptr_t)r->path,0,(uintptr_t)image,st.bytes,0);
    if(got.status || got.value!=st.bytes){release(image);return module_result(0,0);}
    struct process *p=allocate(sizeof(*p));if(!p){release(image);return module_result(0,0);}
    uint64_t flags=lock();if(state.serial==UINT64_MAX){unlock(flags);release(p);release(image);return module_result(0,0);}p->info.id=++state.serial;p->info.cpu=r->cpu;p->info.state=PROCESS_LOADING;unlock(flags);
    struct mem_space_info space;int ok=!CALL(state.space_create,p->info.id,&space,0).status;if(ok)p->info.address_space=space.id;
    if(ok)ok=elf_load(p,image,st.bytes,r->stack_bytes?r->stack_bytes:65536);
    release(image);
    if(ok)ok=!CALL(state.stack_alloc,65536,p->info.id,&p->trap).status;
    if(!ok){
        if(!cleanup(p)){p->info.state=PROCESS_CLEANUP;flags=lock();p->next=state.head;state.head=p;unlock(flags);return module_result(p->info.id,0);}
        release(p);return module_result(0,0);
    }
    p->argument=r->argument;p->info.state=PROCESS_READY;flags=lock();p->next=state.head;state.head=p;unlock(flags);return module_result(p->info.id,1);
}
struct kernel_surface_result process_surface_start(MODULE_ARGS){
    MODULE_UNUSED();uint64_t flags=lock();struct process *p=find(a);if(!p || p->info.state!=PROCESS_READY){unlock(flags);return module_result(0,0);}p->info.state=PROCESS_LOADING;unlock(flags);
    struct thread_request r={.abi=2,.cpu=p->info.cpu,.process_id=p->info.id,.address_space=p->info.address_space,.stack_bytes=32768,.entry=(uintptr_t)kernel_surface_find(state.hub,"process.run"),.major=1,.payload={.arguments={(uintptr_t)p}}};
    /* The entry waits for publication if create wakes its AP immediately. */
    struct kernel_surface_result created=CALL(state.create,&r,0,0);flags=lock();if(!created.status)p->info.thread=created.value;__atomic_store_n(&p->info.state,created.status?PROCESS_READY:PROCESS_RUNNING,__ATOMIC_RELEASE);unlock(flags);return created;
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
    return module_result(p->info.exit_code,1);
}
static void terminate(struct process *p,struct inthandle_frame *f,uint64_t code){
    p->info.exit_code=code;f->rip=(uintptr_t)process_return;f->rsp=p->saved_rsp;f->cs=8;f->ss=16;f->rflags=2;f->rax=code;
}
struct kernel_surface_result process_surface_gate(MODULE_ARGS){
    MODULE_UNUSED();struct inthandle_frame *frame=(void *)a;if(!frame || frame->vector!=128 || (frame->cs&3)!=3)return module_result(0,0);
    uint64_t flags=lock();struct process *p=current();if(!p || !p->saved_rsp){unlock(flags);return module_result(0,0);}
    if(!frame->rax){p->info.state=PROCESS_EXITED;terminate(p,frame,frame->rdi);}
    else frame->rax=1; /* resource mediation is added through this same gate */
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
        if(!page || (e && !(page->flags&MEM_SPACE_WRITE))){unlock(flags);return module_result(0,0);}uint64_t n=4096-((b+off)&4095);off+=n<d-off?n:d-off;}
    for(uint64_t off=0;off<d;){struct page *page=p->pages;while(page->address!=((b+off)&~UINT64_C(4095)))page=page->next;uint64_t n=4096-((b+off)&4095);if(n>d-off)n=d-off;
        unsigned char *user=(void *)(page->frame.address+((b+off)&4095)),*kernel=(void *)(c+off);for(uint64_t i=0;i<n;++i)if(e)user[i]=kernel[i];else kernel[i]=user[i];off+=n;}
    unlock(flags);return module_result(d,1);
}
