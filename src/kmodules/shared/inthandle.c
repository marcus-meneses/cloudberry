#include "inthandle.h"
_Static_assert(sizeof(struct inthandle_frame)==176 && offsetof(struct inthandle_frame,vector)==120, "interrupt frame ABI");
struct __attribute__((packed)) idt_entry {
    uint16_t low, selector;
    uint8_t ist, flags;
    uint16_t middle;
    uint32_t high, reserved;
};
struct __attribute__((packed)) descriptor_pointer { uint16_t limit; uint64_t address; };
struct __attribute__((packed)) tss64 {
    uint32_t reserved0;
    uint64_t rsp[3], reserved1, ist[7], reserved2;
    uint16_t reserved3, iomap;
};
struct binding {kernel_surface_call call;uintptr_t context,owner;uint64_t sequence;};
static struct {
    _Alignas(16) struct idt_entry idt[256];
    uint64_t gdt[7];
    struct tss64 tss;
    struct binding callbacks[256];
    uint64_t counts[256];
    _Alignas(16) unsigned char emergency[3][8192];
    kernel_surface_call user_fault,user_return;
    uint8_t cmos_selector;
    int initialized;
} state;
extern const int32_t inthandle_vectors[256] __attribute__((visibility("hidden")));
extern void inthandle_activate(const struct descriptor_pointer *, const struct descriptor_pointer *);
static void print(const char *s) {
    while (*s) __asm__ volatile("outb %0, $0xe9" : : "a"((uint8_t)*s++));
}
static void hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char text[19] = "0x0000000000000000";
    for (unsigned i=0;i<16;++i) { text[17-i]=digits[value&15]; value>>=4; }
    print(text);
}
static void field(const char *name, uint64_t value) { print(name); hex(value); print(" "); }
__attribute__((noreturn)) static void fatal(const struct inthandle_frame *f) {
    uint64_t cr2,cr3;
    __asm__ volatile("mov %%cr2,%0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3,%0" : "=r"(cr3));
    print("inthandle: fatal "); field("vector=",f->vector); field("error=",f->error); print("\n");
    field("rip=",f->rip); field("rsp=",f->rsp); field("flags=",f->rflags); print("\n");
    field("cr2=",cr2); field("cr3=",cr3); field("cs=",f->cs); field("ss=",f->ss); print("\n");
    field("rax=",f->rax); field("rbx=",f->rbx); field("rcx=",f->rcx); field("rdx=",f->rdx); print("\n");
    field("rsi=",f->rsi); field("rdi=",f->rdi); field("rbp=",f->rbp); print("\n");
    field("r8=",f->r8); field("r9=",f->r9); field("r10=",f->r10); field("r11=",f->r11); print("\n");
    field("r12=",f->r12); field("r13=",f->r13); field("r14=",f->r14); field("r15=",f->r15); print("\n");
    print("inthandle: CPU halted\n");
    for (;;) __asm__ volatile("cli; hlt" : : : "memory");
}
/* Foreground writers hold the mediation lock with IRQs disabled. Readers
 * snapshot without that lock, including on other CPUs and in hardware entry. */
static struct binding binding_read(unsigned vector) {
    struct binding *source=&state.callbacks[vector],out;uint64_t before;
    for(;;) {
        before=__atomic_load_n(&source->sequence,__ATOMIC_ACQUIRE);
        if(before&1) {__asm__ volatile("pause");continue;}
        out.call=__atomic_load_n(&source->call,__ATOMIC_RELAXED);
        out.context=__atomic_load_n(&source->context,__ATOMIC_RELAXED);
        out.owner=__atomic_load_n(&source->owner,__ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if(before==__atomic_load_n(&source->sequence,__ATOMIC_RELAXED))return out;
    }
}
static void binding_write(unsigned vector,kernel_surface_call call,uintptr_t context,uintptr_t owner) {
    struct binding *target=&state.callbacks[vector];
    __atomic_add_fetch(&target->sequence,1,__ATOMIC_ACQ_REL);
    __atomic_store_n(&target->call,call,__ATOMIC_RELAXED);
    __atomic_store_n(&target->context,context,__ATOMIC_RELAXED);
    __atomic_store_n(&target->owner,owner,__ATOMIC_RELAXED);
    __atomic_add_fetch(&target->sequence,1,__ATOMIC_RELEASE);
}
void inthandle_dispatch(struct inthandle_frame *frame) {
    if (!frame || frame->vector > 255) {
        print("inthandle: invalid frame\n"); for (;;) __asm__ volatile("cli; hlt");
    }
    __atomic_add_fetch(&state.counts[frame->vector],1,__ATOMIC_RELAXED);
    struct binding callback=binding_read(frame->vector);
    if(frame->vector<32 && (frame->cs&3)==3 && frame->vector!=2 && frame->vector!=8 && frame->vector!=18 && state.user_fault) {
        if(!state.user_fault((uintptr_t)frame,0,0,0,0,0).status)return;
    }
    if (frame->vector < 32 || !callback.call) fatal(frame);
    struct kernel_surface_result r=callback.call((uintptr_t)frame,callback.context,0,0,0,0);
    if (r.status) fatal(frame);
    if((frame->cs&3)==3 && state.user_return && state.user_return((uintptr_t)frame,0,0,0,0,0).status)fatal(frame);
}
static uint64_t irq_save(void) {
    uint64_t flags; __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");return flags;
}
static void irq_restore(uint64_t flags) {__asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory","cc");}
static void cmos_select(uint8_t selector) {
    state.cmos_selector=selector;__asm__ volatile("outb %0,$0x70" :: "a"(selector) : "memory");
}
struct kernel_surface_result inthandle_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();
    if (state.initialized) return module_result(1,1);
    /* Establish ownership explicitly; port 0x70 is not reliably readable. */
    cmos_select(0x80); /* NMI masked until the owned IDT/IST is active. */
    state.gdt[1] = UINT64_C(0x00af9a000000ffff);
    state.gdt[2] = UINT64_C(0x00cf92000000ffff);
    state.gdt[5] = UINT64_C(0x00cff2000000ffff); /* user data, selector 0x2b */
    state.gdt[6] = UINT64_C(0x00affa000000ffff); /* user code, selector 0x33 */
    uint64_t base = (uintptr_t)&state.tss, limit = sizeof(state.tss)-1;
    state.tss.iomap = sizeof(state.tss);
    for (unsigned i=0;i<3;++i) state.tss.ist[i]=(uintptr_t)&state.emergency[i][8192];
    state.gdt[3] = (limit & 0xffff) | ((base & 0xffffff)<<16) | (UINT64_C(0x89)<<40) |
                   (((limit>>16)&15)<<48) | (((base>>24)&255)<<56);
    state.gdt[4] = base>>32;
    for (unsigned i=0;i<256;++i) {
        uintptr_t entry = (uintptr_t)inthandle_vectors + inthandle_vectors[i];
        state.idt[i] = (struct idt_entry){ .low=entry, .selector=8,
            .ist=i==2?1:i==8?2:i==18?3:0, .flags=i==128?0xee:0x8e, .middle=entry>>16, .high=entry>>32 };
    }
    struct descriptor_pointer gdt={sizeof(state.gdt)-1,(uintptr_t)state.gdt};
    struct descriptor_pointer idt={sizeof(state.idt)-1,(uintptr_t)state.idt};
    inthandle_activate(&gdt,&idt);
    state.initialized=1;
    cmos_select(0); /* Kernel policy: enable NMI after its fatal path exists. */
    return module_result(1,1);
}
struct kernel_surface_result inthandle_surface_bind(MODULE_ARGS) {
    MODULE_UNUSED(); if (!state.initialized || a<32 || a>255) return module_result(0,0);
    const struct kernel_surfaces *hub=(const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    kernel_surface_call callback=b?kernel_surface_find_version(hub,(const char *)b,1,0):NULL;
    if (b && !callback) return module_result(0,0);
    uint64_t flags=irq_save();
    const struct kernel_hub_owner *provider=b?kernel_hub_owner(hub,kernel_surface_owner(hub,(const char *)b)):NULL;
    if(provider && !(provider->flags&HUB_OWNER_BOOT_PINNED)){irq_restore(flags);return module_result(0,0);}
    if (binding_read(a).owner) {irq_restore(flags);return module_result(0,0);}
    binding_write(a,callback,c,0);
    irq_restore(flags);return module_result(1,1);
}
struct kernel_surface_result inthandle_surface_check(MODULE_ARGS) {
    MODULE_UNUSED(); return a<=255 ? module_result(__atomic_load_n(&state.counts[a],__ATOMIC_RELAXED),1) : module_result(0,0);
}
struct kernel_surface_result inthandle_surface_dispatch(MODULE_ARGS) {
    MODULE_UNUSED(); if (!state.initialized || !a || ((struct inthandle_frame *)a)->vector>255) return module_result(0,0);
    const struct inthandle_frame *frame=(const void *)a;
    struct binding callback=binding_read(frame->vector);
    if (frame->vector<32 || !callback.call || callback.owner) return module_result(0,0);
    struct inthandle_frame *delivery=(void *)a;
    __atomic_add_fetch(&state.counts[delivery->vector],1,__ATOMIC_RELAXED);
    struct kernel_surface_result r=callback.call((uintptr_t)delivery,callback.context,INTHANDLE_DEFERRED,0,0,0);
    return module_result(r.status?0:1,r.status==KERNEL_SURFACE_OK);
}

struct kernel_surface_result inthandle_surface_is_bound(MODULE_ARGS) {
    MODULE_UNUSED(); if (!state.initialized || a<32 || a>255) return module_result(0,0);
    return module_result(binding_read(a).call!=NULL,1);
}

struct kernel_surface_result inthandle_surface_claim(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.initialized || a<32 || a>255 || !b || !d) return module_result(0,0);
    const struct kernel_surfaces *hub=(const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    kernel_surface_call callback=kernel_surface_find_version(hub,(const char *)b,1,0);
    if (!callback) return module_result(0,0);
    uint64_t flags=irq_save();
    const struct kernel_hub_owner *provider=kernel_hub_owner(hub,kernel_surface_owner(hub,(const char *)b));
    int ok=(!provider || (provider->flags&HUB_OWNER_BOOT_PINNED)) && !binding_read(a).call;
    if (ok) binding_write(a,callback,c,d);
    irq_restore(flags);return module_result(ok,ok);
}
struct kernel_surface_result inthandle_surface_release(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.initialized || a<32 || a>255 || !b) return module_result(0,0);
    uint64_t flags=irq_save();int ok=binding_read(a).owner==b;
    if (ok) binding_write(a,NULL,0,0);
    irq_restore(flags);return module_result(ok,ok);
}
struct kernel_surface_result inthandle_surface_cmos_read(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.initialized || a>127 || a==12) return module_result(0,0);
    uint64_t flags=irq_save();uint8_t previous=state.cmos_selector,value;
    cmos_select((previous&0x80)|(uint8_t)a);
    __asm__ volatile("inb $0x71,%0" : "=a"(value) :: "memory");
    cmos_select(previous);irq_restore(flags);return module_result(value,1);
}
struct kernel_surface_result inthandle_surface_nmi_mask(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.initialized || a>2) return module_result(0,0);
    uint64_t flags=irq_save();unsigned previous=state.cmos_selector>>7;
    if (a<2) cmos_select((state.cmos_selector&0x7f)|(a?0x80:0));
    irq_restore(flags);return module_result(previous,1);
}

/* Size query with a=0. Otherwise a points to zeroed CPU-owned storage and b to
 * three guarded IST tops. The IDT template is immutable after BSP init. */
struct kernel_surface_result inthandle_surface_cpu_init(MODULE_ARGS) {
    MODULE_UNUSED();
    struct cpu_tables { _Alignas(16) struct idt_entry idt[256];uint64_t gdt[7];struct tss64 tss; };
    if(!a) return module_result(sizeof(struct cpu_tables),state.initialized);
    if(!state.initialized || !b || (a&15)) return module_result(0,0);
    struct cpu_tables *cpu=(void *)a;const uintptr_t *tops=(const void *)b;
    for(unsigned i=0;i<256;++i)cpu->idt[i]=state.idt[i];
    cpu->gdt[1]=UINT64_C(0x00af9a000000ffff);cpu->gdt[2]=UINT64_C(0x00cf92000000ffff);
    cpu->gdt[5]=UINT64_C(0x00cff2000000ffff);cpu->gdt[6]=UINT64_C(0x00affa000000ffff);
    uint64_t base=(uintptr_t)&cpu->tss,limit=sizeof(cpu->tss)-1;
    cpu->tss.iomap=sizeof(cpu->tss);
    for(unsigned i=0;i<3;++i) {if(!tops[i])return module_result(0,0);cpu->tss.ist[i]=tops[i];}
    cpu->gdt[3]=(limit&0xffff)|((base&0xffffff)<<16)|(UINT64_C(0x89)<<40)|(((limit>>16)&15)<<48)|(((base>>24)&255)<<56);
    cpu->gdt[4]=base>>32;
    struct descriptor_pointer gdt={sizeof(cpu->gdt)-1,(uintptr_t)cpu->gdt},idt={sizeof(cpu->idt)-1,(uintptr_t)cpu->idt};
    inthandle_activate(&gdt,&idt);return module_result(1,1);
}

/* Kernel-only facilities. RSP0 is CPU-local and set with IF clear before a
 * saved user context is resumed. No untrusted selector/stack is accepted here. */
struct kernel_surface_result inthandle_surface_user_stack(MODULE_ARGS) {
    MODULE_UNUSED();uint64_t flags;__asm__ volatile("pushfq;pop %0":"=r"(flags));
    if(flags&512)return module_result(0,0);
    struct descriptor_pointer gdt;__asm__ volatile("sgdt %0":"=m"(gdt));
    const uint64_t *entries=(const void *)gdt.address;
    uint64_t low=entries[3],high=entries[4];
    uintptr_t base=((low>>16)&0xffffff)|((low>>32)&UINT64_C(0xff000000))|(high<<32);
    ((struct tss64 *)base)->rsp[0]=a;return module_result(1,1);
}
struct kernel_surface_result inthandle_surface_user_fault(MODULE_ARGS) {
    MODULE_UNUSED();if(!a || state.user_fault)return module_result(0,0);
    const struct kernel_surfaces *hub=(const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    const struct kernel_hub_owner *owner=kernel_hub_owner(hub,kernel_surface_owner(hub,(const char *)a));
    if(!owner || !(owner->flags&HUB_OWNER_BOOT_PINNED))return module_result(0,0);
    state.user_fault=kernel_surface_find_version(hub,(const char *)a,1,0);return module_result(1,state.user_fault!=NULL);
}

struct kernel_surface_result inthandle_surface_user_return(MODULE_ARGS){
    MODULE_UNUSED();if(!a || state.user_return)return module_result(0,0);
    const struct kernel_surfaces *hub=(const void *)(uintptr_t)KERNEL_SURFACES_ADDRESS;
    const struct kernel_hub_owner *owner=kernel_hub_owner(hub,kernel_surface_owner(hub,(const char *)a));
    if(!owner || !(owner->flags&HUB_OWNER_BOOT_PINNED))return module_result(0,0);
    state.user_return=kernel_surface_find_version(hub,(const char *)a,1,0);return module_result(1,state.user_return!=NULL);
}
