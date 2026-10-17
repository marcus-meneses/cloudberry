#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/apic.h"
#include "../kmodules/shared/apic-platform.h"
#include "../kmodules/shared/mmio.h"
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static struct {struct kernel_surfaces hub;struct kernel_surface methods[5];struct kernel_hub_table table;} fixture;
static struct {struct mmio_table table;struct mmio_region regions[2];} regions;
static uint32_t registers[64],io[256],io_index,mode;
static uint64_t flags;
static uintptr_t context,owner;
static bool busy=true,fail_map,claimed,pic_masked;
int apic_platform_probe(struct apic_platform *p) {*p=(struct apic_platform){.physical=0xfee00000,.mode=mode};return 1;}
void apic_platform_activate(uintptr_t address,uint32_t value) {assert(value==mode && address==(mode==APIC_XAPIC?0x100000u:0));}
uint32_t apic_register_read(uint32_t offset) {return registers[offset/16];}
void apic_register_write(uint32_t offset,uint32_t value) {assert(offset!=0x320 && offset!=0x380 && offset!=0x3e0 && offset!=0xb0);registers[offset/16]=value;}
uint64_t apic_irq_save(void) {uint64_t previous=flags;flags=0;return previous;}
void apic_irq_restore(uint64_t previous) {flags=previous;}
void apic_mask_pic(void) {pic_masked=true;}
static struct kernel_surface_result claim(MODULE_ARGS) {MODULE_UNUSED();assert(a==255 && b && c && d);if (busy) return module_result(0,0);assert(!claimed);claimed=true;owner=d;context=c;return module_result(1,1);}
static struct kernel_surface_result release(MODULE_ARGS) {MODULE_UNUSED();assert(a==255 && b==owner && claimed);claimed=false;return module_result(1,1);}
static struct kernel_surface_result map(MODULE_ARGS) {MODULE_UNUSED();assert(a<2);return module_result(fail_map?0:0x100000,!fail_map);}
static struct kernel_surface_result read_mmio(MODULE_ARGS) {MODULE_UNUSED();assert(a==1 && b==16 && c==4);return module_result(io[io_index],1);}
static struct kernel_surface_result write_mmio(MODULE_ARGS) {MODULE_UNUSED();assert(a==1 && c==4);if (!b) io_index=d;else {assert(b==16);io[io_index]=d;}return module_result(1,1);}
int main(int argc,char **argv) {
    mode=argc>1 && !strcmp(argv[1],"x2apic")?APIC_X2APIC:APIC_XAPIC;
    fixture.hub=(struct kernel_surfaces){.magic=KERNEL_SURFACES_MAGIC,.major=KERNEL_HUB_ABI,.count=5,.header_bytes=offsetof(__typeof__(fixture),methods),.tables_offset=offsetof(__typeof__(fixture),table),.table_count=1};
    const char *names[]={"inthandle.claim","inthandle.release","mmio.map","mmio.read","mmio.write"};kernel_surface_call calls[]={claim,release,map,read_mmio,write_mmio};
    for (unsigned i=0;i<5;++i) {strcpy(fixture.methods[i].name,names[i]);fixture.methods[i].call=calls[i];fixture.methods[i].major=1;}
    regions.table=(struct mmio_table){.magic=MMIO_TABLE_MAGIC,.major=1,.entry_bytes=sizeof(struct mmio_region),.count=2};
    regions.regions[0]=(struct mmio_region){.kind=MMIO_LOCAL_APIC,.physical=0xfee00000,.bytes=4096};regions.regions[1].kind=MMIO_IO_APIC;
    fixture.table=(struct kernel_hub_table){.name=MMIO_TABLE_NAME,.address=(uintptr_t)&regions,.bytes=sizeof(regions)};
    registers[0x30/16]=5u<<16;registers[0x360/16]=0x400;io[1]=1u<<16;flags=0x200;
    assert(CALL(apic_surface_init,&fixture.hub,0,0).status==KERNEL_SURFACE_FAILED && !claimed && flags==0x200);
    busy=false;fail_map=true;assert(CALL(apic_surface_init,&fixture.hub,0,0).status==KERNEL_SURFACE_FAILED && !claimed);
    fail_map=false;assert(!CALL(apic_surface_init,&fixture.hub,0,0).status && claimed && flags==0x200 && pic_masked);
    assert((io[0x10]&(1u<<16)) && (io[0x12]&(1u<<16)) && registers[0x360/16]==0x400);
    assert(registers[0xf0/16]==0x1ff && (registers[0x350/16]&(1u<<16)));
    struct apic_info value;assert(!CALL(apic_surface_info,&value,0,0).status && value.mode==mode);
    struct inthandle_frame frame={.vector=255};assert(!apic_surface_spurious((uintptr_t)&frame,context,INTHANDLE_CPU_ENTRY,0,0,0).status);
    assert(apic_surface_spurious((uintptr_t)&frame,context,INTHANDLE_DEFERRED,0,0,0).status==KERNEL_SURFACE_FAILED);
    CALL(apic_surface_info,&value,0,0);assert(value.spurious==1);
    pic_masked=false;io[0x10]=0;registers[0xf0/16]=0;registers[0x350/16]=0;
    assert(!CALL(apic_surface_cpu_init,0,0,0).status && !pic_masked && !io[0x10] && claimed);
    assert(registers[0xf0/16]==0x1ff && (registers[0x350/16]&(1u<<16)) && registers[0x360/16]==0x400);
    puts("PASS: APIC controller ownership, claim rollback, input masking, NMI preservation and spurious without EOI");return 0;
}
