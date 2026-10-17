#include "space-fault.h"
#include "../kernel/cpu.h"
#include "../kmodules/shared/inthandle.h"
struct __attribute__((packed)) test_gate {uint16_t low,selector;uint8_t ist,flags;uint16_t middle;uint32_t high,reserved;};
struct __attribute__((packed)) test_idtr {uint16_t limit;uintptr_t address;};
_Static_assert(sizeof(struct test_gate)==16 && sizeof(struct test_idtr)==10,"test IDT layout");
static struct space_fault_slot *slots;static size_t count;
extern void space_fault_entry(void) __attribute__((visibility("hidden")));
extern uint64_t space_fault_read(uintptr_t),space_fault_write(uintptr_t,uint64_t),space_fault_execute(uintptr_t);
extern const char space_fault_read_ip[] __attribute__((visibility("hidden")));
extern const char space_fault_read_resume[] __attribute__((visibility("hidden")));
extern const char space_fault_write_ip[] __attribute__((visibility("hidden")));
extern const char space_fault_write_resume[] __attribute__((visibility("hidden")));
extern const char space_fault_execute_resume[] __attribute__((visibility("hidden")));

static struct space_fault_slot *local(void){struct cpu_record *cpu;__asm__ volatile("movq %%gs:0,%0":"=r"(cpu));return slots && cpu->index<count?&slots[cpu->index]:NULL;}
struct kernel_surface_result probe_space_fault_config(MODULE_ARGS){
    MODULE_UNUSED();for(size_t i=0;i<count;++i)if(slots[i].armed)return module_result(0,0);
    if((a==0)!=(b==0) || b>SIZE_MAX/sizeof(*slots))return module_result(0,0);
    slots=(void *)a;count=b;return module_result(1,1);
}
__attribute__((visibility("hidden"))) void space_fault_dispatch(struct inthandle_frame *frame){
    uint64_t cr2,cr3;__asm__ volatile("mov %%cr2,%0;mov %%cr3,%1":"=r"(cr2),"=r"(cr3));struct space_fault_slot *s=local();
    if(!s || !s->armed || frame->vector!=14 || frame->cs!=8 || cr3!=s->root || cr2!=s->address || frame->rip!=s->rip || frame->error!=s->error){
        const char *message="FAIL unexpected address-space probe fault\n";while(*message)__asm__ volatile("outb %0,$0xe9"::"a"(*message++));
        __asm__ volatile("ud2");__builtin_unreachable();
    }
    s->armed=0;++s->seen;frame->rip=s->resume;
}
int space_fault_access(unsigned access,uintptr_t address,uint64_t value,int expect_fault,uint64_t error){
    uint64_t flags;__asm__ volatile("pushfq;pop %0;cli":"=r"(flags)::"memory");
    struct space_fault_slot *s=local();if(!s || s->armed || access>SPACE_EXEC){__asm__ volatile("push %0;popfq"::"r"(flags):"memory","cc");return 0;}
    struct test_idtr idtr;__asm__ volatile("sidt %0":"=m"(idtr));struct test_gate *gate=(void *)(idtr.address+14*sizeof(*gate)),old=*gate;
    if(idtr.limit<15*sizeof(*gate)-1){__asm__ volatile("push %0;popfq"::"r"(flags):"memory","cc");return 0;}
    uint64_t before=s->seen;__asm__ volatile("mov %%cr3,%0":"=r"(s->root));s->address=address;s->error=error;
    s->rip=access==SPACE_READ?(uintptr_t)space_fault_read_ip:access==SPACE_WRITE?(uintptr_t)space_fault_write_ip:address;
    s->resume=access==SPACE_READ?(uintptr_t)space_fault_read_resume:access==SPACE_WRITE?(uintptr_t)space_fault_write_resume:(uintptr_t)space_fault_execute_resume;s->armed=1;
    uintptr_t entry=(uintptr_t)space_fault_entry;*gate=(struct test_gate){.low=entry,.selector=old.selector,.ist=old.ist,.flags=old.flags,.middle=entry>>16,.high=entry>>32};
    __asm__ volatile("lidt %0"::"m"(idtr):"memory");
    uint64_t result=access==SPACE_READ?space_fault_read(address):access==SPACE_WRITE?space_fault_write(address,value):space_fault_execute(address);
    *gate=old;__asm__ volatile("lidt %0"::"m"(idtr):"memory");s->armed=0;
    int ok=s->seen-before==(unsigned)expect_fault && (expect_fault || access!=SPACE_EXEC || result==42);
    __asm__ volatile("push %0;popfq"::"r"(flags):"memory","cc");return ok;
}
