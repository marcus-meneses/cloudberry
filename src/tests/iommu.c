#include <assert.h>
#include <stdio.h>
#include <string.h>
/* White-box firmware/fault tests share production parsing and register layouts.
 * Privileged entry/activation functions are never called by this host test. */
#include "../kmodules/shared/iommu.c"
#undef state
#define IOMMU_HOST_TEST
#define environment intel_environment
#define u16 intel_u16
#define u32 intel_u32
#define u64 intel_u64
#define backend_route intel_route
#define backend_collect intel_collect
#include "../kmodules/intel/iommu-intel.c"
#undef environment
#undef u16
#undef u32
#undef u64
#undef backend_route
#undef backend_collect
#define environment amd_environment
#define u16 amd_u16
#define u32 amd_u32
#define u64 amd_u64
#define backend_route amd_route
#define backend_collect amd_collect
#include "../kmodules/amd/iommu-amd.c"
#undef environment
#undef u16
#undef u32
#undef u64
#undef backend_route
#undef backend_collect
#define state iommu_state
static struct iommu_backend_env fixture_env;
static struct kernel_surface_result fixture_collect(MODULE_ARGS) {MODULE_UNUSED();if(state.info.backend==IOMMU_INTEL)intel_collect();else amd_collect();return module_result(1,1);}

static unsigned char firmware[256],reserved[2];
static struct iommu_unit units[4];static uint16_t routing[65536];
static struct {struct hwenum_table table;struct hwenum_descriptor entries[2];} hardware;
static void put16(unsigned char *p,uint16_t value) {p[0]=value;p[1]=value>>8;}
static void put64(unsigned char *p,uint64_t value) {for(unsigned i=0;i<8;++i)p[i]=value>>(8*i);}
static void reset(void) {
    fixture_env=(struct iommu_backend_env){.frontend=&iommu_state,.record_fault=record_fault,.read_bytes=read_bytes};intel_environment=amd_environment=&fixture_env;
    memset(&iommu_state,0,sizeof(iommu_state));state.backend_collect=fixture_collect;memset(firmware,0,sizeof(firmware));memset(units,0,sizeof(units));memset(routing,0,sizeof(routing));memset(reserved,0,sizeof(reserved));
    iommu_state.firmware=firmware;iommu_state.units=units;iommu_state.routing=routing;iommu_state.reserved=reserved;iommu_state.hw=&hardware.table;
    memset(&hardware,0,sizeof(hardware));hardware.table.count=1;hardware.entries[0].kind=HWENUM_PCI;hardware.entries[0].data.pci.slot=3;
}
static struct kernel_surface_result mock_cpu(MODULE_ARGS) {MODULE_UNUSED();return module_result(0,1);}
static struct iommu_domain domains[2];
static uint64_t time_ns;
static struct kernel_surface_result mock_clock(MODULE_ARGS) {MODULE_UNUSED();time_ns+=1000000;return module_result(time_ns,1);}
static unsigned char bridge_buses[2]={2,5};
static struct kernel_surface_result bridge_read(MODULE_ARGS) {MODULE_UNUSED();if(a!=0x1019 || c!=2)return module_result(0,0);memcpy((void *)b,bridge_buses,2);return module_result(2,1);}
int main(void) {
    (void)intel_route;(void)amd_route;
    reset();iommu_state.firmware_bytes=64;put16(firmware+50,16);firmware[52]=1;put64(firmware+56,0xfed90000);
    assert(parse_intel() && iommu_state.unit_count==1 && units[0].include_all);
    reset();iommu_state.firmware_bytes=63;put16(firmware+50,15);assert(!parse_intel());
    reset();iommu_state.firmware_bytes=72;put16(firmware+50,24);put64(firmware+56,0xfed90000);
    firmware[64]=1;firmware[65]=8;firmware[70]=3;
    assert(parse_intel() && scope_match(units[0].scopes,units[0].scope_bytes,&hardware.entries[0])==1);
    firmware[65]=7;assert(scope_match(units[0].scopes,8,&hardware.entries[0])<0);
    firmware[65]=8;firmware[64]=2;assert(scope_match(units[0].scopes,8,&hardware.entries[0])<0);
    reset();iommu_state.firmware_bytes=76;firmware[48]=0x10;put16(firmware+50,28);put64(firmware+56,0xfed80000);firmware[72]=1;
    assert(parse_amd() && iommu_state.unit_count==1 && routing[0x18]==1 && routing[65535]==1);
    reset();iommu_state.firmware_bytes=75;firmware[48]=0x10;put16(firmware+50,27);put64(firmware+56,0xfed80000);firmware[72]=1;assert(!parse_amd());
    reset();iommu_state.firmware_bytes=80;firmware[48]=0x20;put16(firmware+50,32);assert(!parse_amd());
    reset();state.firmware_bytes=104;
    for(unsigned at=48;at<104;at+=28) {firmware[at]=0x10;put16(firmware+at+2,28);put64(firmware+at+8,0xfed80000);firmware[at+24]=1;}
    assert(parse_amd() && state.unit_count==1 && routing[0x18]==1);
    put16(firmware+78,27);assert(!parse_amd());
    static _Alignas(4096) uint64_t registers[2048],events[512];struct kernel_surfaces hub={0};struct iommu_info info;
    reset();iommu_state.hub=&hub;iommu_state.cpu=mock_cpu;iommu_state.unit_count=1;units[0].active=1;units[0].registers=(uintptr_t)registers;units[0].events.address=(uintptr_t)events;
    iommu_state.domains=domains;domains[0]=(struct iommu_domain){.owner=99,.requester=0x18,.unit=&units[0]};
    iommu_state.info.backend=IOMMU_AMD;events[0]=0x18|(UINT64_C(2)<<60);events[1]=0x12345000;registers[0x2018/8]=16;
    assert(!iommu_surface_info((uintptr_t)&info,0,0,0,0,0).status && info.faults==1 && info.last_fault_requester==0x18 && info.last_fault_address==0x12345000 && info.fault_reason==2);
    assert(registers[0x2010/8]==16 && domains[0].faulted && iommu_surface_ready(0,99,0,0,0,0).status);
    iommu_state.info=(struct iommu_info){.backend=IOMMU_INTEL};units[0].capability=UINT64_C(16)<<24;registers[0x100/8]=0x12345000;registers[0x108/8]=(UINT64_C(1)<<63)|(UINT64_C(6)<<32)|0x18;
    assert(!iommu_surface_info((uintptr_t)&info,0,0,0,0,0).status && info.faults==1 && info.last_fault_address==0x12345000 && info.fault_reason==6);
    registers[0x34/8]|=UINT64_C(1)<<32; /* FSTS is the high dword. */
    assert(!iommu_surface_info((uintptr_t)&info,0,0,0,0,0).status && info.failed && info.last_error==201);
    registers[0x34/8]=0;state.info=(struct iommu_info){.backend=IOMMU_AMD};registers[0x2020/8]=1;
    assert(!iommu_surface_info((uintptr_t)&info,0,0,0,0,0).status && info.failed && info.last_error==202);registers[0x2020/8]=0;
    iommu_state.clock=mock_clock;iommu_state.control=mock_cpu;uint64_t value=0;
    assert(iommu_wait64((uintptr_t)&value,1,0));assert(!iommu_wait64((uintptr_t)&value,1,1) && time_ns<2000000000);
    reset();hardware.table.count=2;hardware.entries[1]=hardware.entries[0];hardware.entries[1].data.pci.bus=2;hardware.entries[1].data.pci.slot=4;
    hardware.entries[0].data.pci.header_type=1;hardware.entries[0].data.pci.class_code=6;hardware.entries[0].data.pci.subclass=4;hardware.entries[0].address=0x1000;state.read=bridge_read;
    unsigned char path[10]={1,10,0,0,0,0,3,0,4,0};assert(scope_match(path,10,&hardware.entries[1])==1);
    path[0]=2;path[1]=8;assert(scope_match(path,8,&hardware.entries[1])==1);
    bridge_buses[0]=0;assert(scope_match(path,8,&hardware.entries[1])<0);bridge_buses[0]=2;
    static _Alignas(4096) unsigned char config[4096];*(uint16_t *)(config+6)=16;config[0x34]=0x40;config[0x40]=0x10;
    assert(pending_transactions((uintptr_t)config)==0);*(uint16_t *)(config+0x4a)=32;assert(pending_transactions((uintptr_t)config)==1);
    config[0x40]=1;config[0x41]=0x40;assert(pending_transactions((uintptr_t)config)<0);
    static _Alignas(4096) uint64_t tables[4][512];struct iommu_node nodes[4]={0};struct iommu_unit unit={.levels=4,.address_bits=48};
    uint64_t high=UINT64_C(1)<<40;
    for(unsigned i=0;i<4;++i) {nodes[i].page.physical=0x1000+i*4096;nodes[i].page.address=(uintptr_t)tables[i];nodes[i].next=i<3?&nodes[i+1]:NULL;}
    for(unsigned i=0;i<3;++i)tables[i][(high>>(12+(3-i)*9))&511]=nodes[i+1].page.physical|3;
    struct iommu_domain domain={.unit=&unit,.root=nodes,.nodes=nodes};assert(leaf(&domain,high,0)==&tables[3][0]);unit.levels=3;unit.address_bits=39;assert(!leaf(&domain,high,0));
    puts("PASS IOMMU: firmware bounds/scopes, AMD routing, unsupported layouts, Intel/AMD fault decoding and bounded polling");
    return 0;
}
