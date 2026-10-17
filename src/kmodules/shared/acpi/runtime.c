#include "runtime.h"
#include "../pci.h"
#include "../hwenum.h"
#include "../irq.h"
#include "../threads.h"
#include "../../../kernel/cpu.h"
#include <uacpi/kernel_api.h>
const struct kernel_surfaces *acpi_hub;
uintptr_t acpi_heap;
uint64_t acpi_rsdp;
unsigned acpi_log_errors;
static kernel_surface_call alloc_mem,free_mem,map_mem,read_port,write_port,pci_read,pci_write,now,cpu,thread,log_output;
static int busy;
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
int acpi_runtime_setup(const struct kernel_surfaces *hub,uintptr_t heap,uint64_t rsdp) {
    if(!hub || hub->magic!=KERNEL_SURFACES_MAGIC || hub->major!=KERNEL_HUB_ABI || !heap || !rsdp)return 0;
    acpi_hub=hub;acpi_heap=heap;acpi_rsdp=rsdp;
    const char *names[]={"mem.malloc","mem.free","hardware.firmware_map","hardware.port_read","hardware.port_write","pci.firmware_read","pci.firmware_write","clock.now_ns","cpu.control","threads.control","hardware.log"};
    kernel_surface_call *slots[]={&alloc_mem,&free_mem,&map_mem,&read_port,&write_port,&pci_read,&pci_write,&now,&cpu,&thread,&log_output};
    for(unsigned i=0;i<sizeof(slots)/sizeof(slots[0]);++i) {
        *slots[i]=kernel_surface_find(hub,names[i]);if(!*slots[i])return 0;
    }
    return !CALL(cpu,CPU_CURRENT,0,0).value;
}
int acpi_runtime_enter(void) {
    if(!cpu || CALL(cpu,CPU_CURRENT,0,0).value || CALL(cpu,CPU_DEPTH,0,0).value)return 0;
    return !__atomic_exchange_n(&busy,1,__ATOMIC_ACQUIRE);
}
void acpi_runtime_leave(void) {acpi_drain_work();__atomic_store_n(&busy,0,__ATOMIC_RELEASE);}
void *uacpi_kernel_alloc(uacpi_size size) {
    struct kernel_surface_result r=CALL(alloc_mem,acpi_heap,size,0);return r.status?NULL:(void *)r.value;
}
void uacpi_kernel_free(void *p) {if(p)CALL(free_mem,acpi_heap,p,0);}
char *acpi_copy_string(const char *s) {
    if(!s)return NULL;
    size_t len=0;while(s[len])++len;
    char *copy=uacpi_kernel_alloc(len+1);if(copy)for(size_t i=0;i<=len;++i)copy[i]=s[i];return copy;
}
uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out) {*out=acpi_rsdp;return UACPI_STATUS_OK;}
void *uacpi_kernel_map(uacpi_phys_addr address,uacpi_size size) {
    struct kernel_surface_result r=CALL(map_mem,address,size,0);return r.status?UACPI_MAP_FAILED:(void *)r.value;
}
void uacpi_kernel_unmap(void *p,uacpi_size size) {(void)p;(void)size; /* Boot-lifetime mappings. */}
void uacpi_kernel_log(uacpi_log_level level,const uacpi_char *s) {
    if(level==UACPI_LOG_ERROR)++acpi_log_errors;
    size_t len=0;while(s[len])++len;CALL(log_output,s,len,0);
}
struct pci_access {uacpi_pci_address address;};
uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address,uacpi_handle *out) {
    struct pci_firmware_access r={.segment=address.segment,.bus=address.bus,.slot=address.device,.function=address.function,.width=2};
    if(CALL(pci_read,&r,0,0).status || r.value==65535)return UACPI_STATUS_NOT_FOUND;
    struct pci_access *p=uacpi_kernel_alloc(sizeof(*p));if(!p)return UACPI_STATUS_OUT_OF_MEMORY;
    p->address=address;*out=p;return UACPI_STATUS_OK;
}
void uacpi_kernel_pci_device_close(uacpi_handle h) {uacpi_kernel_free(h);}
static uacpi_status pci_access(uacpi_handle h,size_t offset,unsigned width,uint32_t *value,int write) {
    if(!h || offset>4096-width)return UACPI_STATUS_INVALID_ARGUMENT;
    struct pci_access *p=h;struct pci_firmware_access r={.segment=p->address.segment,.bus=p->address.bus,.slot=p->address.device,.function=p->address.function,.offset=offset,.width=width,.value=*value};
    if(CALL(write?pci_write:pci_read,&r,0,0).status)return UACPI_STATUS_DENIED;
    *value=r.value;return UACPI_STATUS_OK;
}
#define PCI_ACCESS(bits,type) \
uacpi_status uacpi_kernel_pci_read##bits(uacpi_handle h,size_t o,type *v) {uint32_t x=0;uacpi_status s=pci_access(h,o,sizeof(*v),&x,0);if(!s)*v=x;return s;} \
uacpi_status uacpi_kernel_pci_write##bits(uacpi_handle h,size_t o,type v) {uint32_t x=v;return pci_access(h,o,sizeof(v),&x,1);}
PCI_ACCESS(8,uacpi_u8)
PCI_ACCESS(16,uacpi_u16)
PCI_ACCESS(32,uacpi_u32)
struct io_access {uint64_t base,size;};
uacpi_status uacpi_kernel_io_map(uacpi_io_addr base,uacpi_size size,uacpi_handle *out) {
    if(!size || base>=65536 || size>65536-base)return UACPI_STATUS_INVALID_ARGUMENT;
    struct io_access *p=uacpi_kernel_alloc(sizeof(*p));if(!p)return UACPI_STATUS_OUT_OF_MEMORY;
    *p=(struct io_access){base,size};*out=p;return UACPI_STATUS_OK;
}
void uacpi_kernel_io_unmap(uacpi_handle h) {uacpi_kernel_free(h);}
static uacpi_status io_access(uacpi_handle h,size_t offset,unsigned width,uint32_t *value,int write) {
    struct io_access *p=h;if(!p || offset>p->size || width>p->size-offset)return UACPI_STATUS_INVALID_ARGUMENT;
    struct kernel_surface_result r=CALL(write?write_port:read_port,p->base+offset,width,*value);
    if(r.status)return UACPI_STATUS_DENIED;
    if(!write)*value=r.value;
    return UACPI_STATUS_OK;
}
#define IO_ACCESS(bits,type) \
uacpi_status uacpi_kernel_io_read##bits(uacpi_handle h,size_t o,type *v) {uint32_t x=0;uacpi_status s=io_access(h,o,sizeof(*v),&x,0);if(!s)*v=x;return s;} \
uacpi_status uacpi_kernel_io_write##bits(uacpi_handle h,size_t o,type v) {uint32_t x=v;return io_access(h,o,sizeof(v),&x,1);}
IO_ACCESS(8,uacpi_u8)
IO_ACCESS(16,uacpi_u16)
IO_ACCESS(32,uacpi_u32)
uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {return CALL(now,0,0,0).value;}
static void wait_until(uint64_t deadline) {
    while(uacpi_kernel_get_nanoseconds_since_boot()<deadline) {
        struct kernel_surface_result current=CALL(thread,THREAD_CURRENT,0,0);
        if(!current.status && current.value)CALL(thread,THREAD_BLOCK_UNTIL,deadline,0);
        else __asm__ volatile("pause");
    }
}
void uacpi_kernel_stall(uacpi_u8 us) {wait_until(uacpi_kernel_get_nanoseconds_since_boot()+(uint64_t)us*1000);}
void uacpi_kernel_sleep(uacpi_u64 ms) {
    uint64_t start=uacpi_kernel_get_nanoseconds_since_boot();
    if(ms>(UINT64_MAX-start)/1000000)ms=(UINT64_MAX-start)/1000000;
    wait_until(start+ms*1000000);
}
uacpi_thread_id uacpi_kernel_get_thread_id(void) {return (uacpi_thread_id)(uintptr_t)(CALL(thread,THREAD_CURRENT,0,0).value+1);}
uacpi_interrupt_state uacpi_kernel_disable_interrupts(void) {uintptr_t flags;__asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");return flags;}
void uacpi_kernel_restore_interrupts(uacpi_interrupt_state flags) {if(flags&512)__asm__ volatile("sti":::"memory");}
struct semaphore {uint32_t units;};
uacpi_handle uacpi_kernel_create_semaphore(uacpi_u32 units) {
    struct semaphore *s=uacpi_kernel_alloc(sizeof(*s));if(s)s->units=units;return s;
}
void uacpi_kernel_free_semaphore(uacpi_handle s) {uacpi_kernel_free(s);}
uacpi_status uacpi_kernel_wait_for_semaphore(uacpi_handle h,uacpi_u16 timeout) {
    struct semaphore *s=h;uint64_t deadline=uacpi_kernel_get_nanoseconds_since_boot()+(uint64_t)timeout*1000000;
    for(;;) {
        uint32_t units=__atomic_load_n(&s->units,__ATOMIC_ACQUIRE);
        if(units && __atomic_compare_exchange_n(&s->units,&units,units-1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return UACPI_STATUS_OK;
        if(!timeout || (timeout!=65535 && uacpi_kernel_get_nanoseconds_since_boot()>=deadline))return UACPI_STATUS_TIMEOUT;
        /* Public AML calls are serialized on BSP. SCI remains masked, so there
         * is no other AML executor to wake; yield to ordinary kernel work. */
        if(CALL(thread,THREAD_CURRENT,0,0).value)CALL(thread,THREAD_YIELD,0,0);
        else __asm__ volatile("pause");
    }
}
void uacpi_kernel_signal_semaphore(uacpi_handle h) {
    struct semaphore *s=h;uint32_t old=__atomic_load_n(&s->units,__ATOMIC_RELAXED);
    while(old!=UINT32_MAX && !__atomic_compare_exchange_n(&s->units,&old,old+1,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED)){}
}
uacpi_handle uacpi_kernel_create_spinlock(void) {return uacpi_kernel_alloc(1);}
void uacpi_kernel_free_spinlock(uacpi_handle h) {uacpi_kernel_free(h);}
uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle h) {(void)h;return uacpi_kernel_disable_interrupts();}
void uacpi_kernel_unlock_spinlock(uacpi_handle h,uacpi_cpu_flags flags) {(void)h;uacpi_kernel_restore_interrupts(flags);}
uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *r) {(void)r;return UACPI_STATUS_UNIMPLEMENTED;}
static int sci_route(uint32_t source,struct irq_request *request) {
    const struct kernel_hub_table *ref=kernel_hub_find_table(acpi_hub,HWENUM_TABLE_NAME);
    kernel_surface_call read=kernel_surface_find(acpi_hub,"hardware.read");
    if(!ref || !read)return 0;
    const struct hwenum_table *hw=(const void *)ref->address;
    int found=0;
    for(size_t i=0;i<hw->count;++i) {
        const struct hwenum_descriptor *entry=&hw->entries[i];const char *sig=entry->data.acpi.signature;
        if(entry->kind!=HWENUM_ACPI_TABLE || sig[0]!='A' || sig[1]!='P' || sig[2]!='I' || sig[3]!='C')continue;
        uint32_t length=0;if(CALL(read,entry->address+4,&length,4).status || length<44)return 0;
        for(uint32_t offset=44;offset<length;) {
            uint8_t record[10];if(length-offset<2 || CALL(read,entry->address+offset,record,2).status || record[1]<2 || record[1]>length-offset)return 0;
            if(record[0]==2) {
                if(record[1]<10 || CALL(read,entry->address+offset,record,10).status)return 0;
                if(!record[2] && record[3]==source) {
                    if(found++)return 0;
                    uint16_t flags=record[8]|((uint16_t)record[9]<<8);unsigned polarity=flags&3,trigger=(flags>>2)&3;
                    if(polarity==2 || trigger==2 || flags&~15u)return 0;
                    request->gsi=record[4]|((uint32_t)record[5]<<8)|((uint32_t)record[6]<<16)|((uint32_t)record[7]<<24);
                    if(polarity)request->active_low=polarity==3;
                    if(trigger)request->level=trigger==3;
                }
            }
            offset+=record[1];
        }
    }
    return 1;
}
static struct {uacpi_interrupt_handler handler;void *context;struct irq_route route;} sci;
uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 gsi,uacpi_interrupt_handler fn,uacpi_handle ctx,uacpi_handle *out) {
    if(sci.handler)return UACPI_STATUS_ALREADY_EXISTS;
    kernel_surface_call route=kernel_surface_find(acpi_hub,"irq.route"),callback=kernel_surface_find(acpi_hub,"acpi.interrupt");
    if(!route || !callback)return UACPI_STATUS_UNIMPLEMENTED;
    struct irq_request r={.kind=IRQ_IOAPIC,.gsi=gsi,.level=1,.active_low=1,.owner=UINT64_C(0x41435049),.callback=callback,.major=1};
    if(!sci_route(gsi,&r))return UACPI_STATUS_INVALID_ARGUMENT;
    if(CALL(route,&r,&sci.route,0).status)return UACPI_STATUS_INTERNAL_ERROR;
    sci.handler=fn;sci.context=ctx;*out=&sci;return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler fn,uacpi_handle h) {
    if(h!=&sci || fn!=sci.handler)return UACPI_STATUS_INVALID_ARGUMENT;
    kernel_surface_call release=kernel_surface_find(acpi_hub,"irq.release");
    if(!release || CALL(release,sci.route.id,sci.route.owner,1).status)return UACPI_STATUS_INTERNAL_ERROR;
    sci.handler=NULL;return UACPI_STATUS_OK;
}
struct kernel_surface_result acpi_surface_interrupt(MODULE_ARGS) {
    MODULE_UNUSED(); /* SCI is deliberately reserved/masked until notification reconciliation exists. */
    return module_result(0,0);
}
struct work {struct work *next;uacpi_work_handler fn;void *ctx;int queued;};
static struct work *work_head,*work_tail;
uacpi_handle uacpi_kernel_create_work_item(void) {
    struct work *w=uacpi_kernel_alloc(sizeof(*w));if(w)*w=(struct work){0};return w;
}
void uacpi_kernel_free_work_item(uacpi_handle h) {uacpi_kernel_free(h);}
void uacpi_kernel_schedule_work(uacpi_work_type type,uacpi_handle h,uacpi_work_handler fn,uacpi_handle ctx) {
    (void)type;struct work *w=h;uacpi_interrupt_state flags=uacpi_kernel_disable_interrupts();
    w->next=NULL;w->fn=fn;w->ctx=ctx;w->queued=1;
    if(work_tail)work_tail->next=w;else work_head=w;work_tail=w;
    uacpi_kernel_restore_interrupts(flags);
}
void acpi_drain_work(void) {
    while(work_head) {
        uacpi_interrupt_state flags=uacpi_kernel_disable_interrupts();struct work *w=work_head;
        work_head=w->next;if(!work_head)work_tail=NULL;w->queued=0;
        uacpi_work_handler fn=w->fn;void *ctx=w->ctx;uacpi_kernel_restore_interrupts(flags);fn(ctx);
    }
}
uacpi_status uacpi_kernel_wait_for_work_completion(void) {acpi_drain_work();return UACPI_STATUS_OK;}
/* Freestanding compiler ABI helpers; uACPI's own string implementation uses
 * these symbols too. Volatile loops prevent recursive builtin substitution. */
void *memcpy(void *dst,const void *src,size_t n) {volatile unsigned char *o=dst;const volatile unsigned char *i=src;while(n--)*o++=*i++;return dst;}
void *memset(void *dst,int value,size_t n) {volatile unsigned char *o=dst;while(n--)*o++=value;return dst;}
void *memmove(void *dst,const void *src,size_t n) {
    volatile unsigned char *o=dst;const volatile unsigned char *i=src;
    if((uintptr_t)dst<(uintptr_t)src)while(n--)*o++=*i++;
    else while(n){--n;o[n]=i[n];}
    return dst;
}
