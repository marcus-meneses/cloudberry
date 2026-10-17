#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/cpu.h"
#include <string.h>
#include "../kmodules/shared/clock.h"
#include "../kmodules/shared/clock-platform.h"
#include "../kmodules/shared/mmio.h"
#include "../kmodules/shared/hwenum.h"
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static struct { struct kernel_surfaces hub;struct kernel_surface methods[6];struct kernel_hub_table tables[2]; } fixture;
static struct { struct mmio_table table;struct mmio_region region; } regions;
static struct { struct hwenum_table table;struct hwenum_descriptor descriptor; } hardware;
static uint64_t hpet,ticks,configuration,tsc_step=300;
static unsigned sample,reads;static uint64_t previous_read,last_read;
static bool stalled,drift,no_rtc,no_century;
static uint8_t cmos[128];
static struct clock_cpu cpu;
int clock_platform_configure(const struct kernel_surfaces *hub) {return hub!=NULL;}
struct clock_cpu clock_cpu_detect(void) {return cpu;}
uint64_t clock_cpu_ticks(void) {previous_read=last_read;last_read=ticks+=tsc_step;return last_read;}
uint8_t clock_cmos_read(uint8_t index) {++reads;assert(index!=0xc && !no_rtc);return cmos[index];}
static struct kernel_surface_result map(MODULE_ARGS) {MODULE_UNUSED();return module_result(1,1);}
static struct kernel_surface_result read_mmio(MODULE_ARGS) {
    MODULE_UNUSED();assert(a==0 && c==8);
    if (b==0) return module_result((UINT64_C(100000000)<<32)|(1u<<13),1);
    if (b==16) return module_result(configuration,1);
    assert(b==0xf0);
    if (!stalled) {hpet+=10000;ticks+=drift?3000000+100000*(++sample):3000000;}
    return module_result(hpet,1);
}
static struct kernel_surface_result write_mmio(MODULE_ARGS) {
    MODULE_UNUSED();assert(a==0 && b==16 && c==8 && d==1);configuration=d;return module_result(1,1);
}
static struct kernel_surface_result physical_read(MODULE_ARGS) {
    MODULE_UNUSED();assert(a==0x100000+108 && c==3);
    uint8_t *out=(void *)b;out[0]=no_century?0:0x32;out[1]=no_rtc?(1u<<5):0;out[2]=0;return module_result(3,1);
}
int clock_hpet_read(uintptr_t address,uint64_t *out) {(void)address;struct kernel_surface_result r=CALL(read_mmio,0,0xf0,8);*out=r.value;return !r.status;}
static uintptr_t test_cpu;static void *clock_memory;
static struct kernel_surface_result cpu_call(MODULE_ARGS) {MODULE_UNUSED();assert(a==CPU_CURRENT);return module_result(test_cpu,1);}
static struct kernel_surface_result clock_alloc(MODULE_ARGS) {MODULE_UNUSED();assert(a==1 && b==2 && c==8);clock_memory=calloc(b,c);return module_result((uintptr_t)clock_memory,clock_memory!=NULL);}
int main(int argc,char **argv) {
    const char *mode=argc>1?argv[1]:"tsc";
    bool health_case=!strcmp(mode,"shared-tsc-drift"),smp_case=!strcmp(mode,"shared-tsc") || health_case;
    bool absent=!strcmp(mode,"enumerated"),enumerated=absent || !strcmp(mode,"stalled-enumerated"),fallback=!strcmp(mode,"hpet"),assumed=!strcmp(mode,"no-century");
    no_rtc=!strcmp(mode,"no-rtc");no_century=assumed;stalled=!strcmp(mode,"stalled-enumerated");
    cpu=(struct clock_cpu){.flags=CLOCK_TSC_PRESENT|(fallback?0:CLOCK_TSC_INVARIANT),
        .hz=enumerated?3000000000:0};
    if (enumerated) cpu.flags|=CLOCK_TSC_ENUMERATED;
    fixture.hub=(struct kernel_surfaces){.magic=KERNEL_SURFACES_MAGIC,.major=KERNEL_HUB_ABI,.count=4,
        .header_bytes=offsetof(__typeof__(fixture),methods),.table_count=2,.tables_offset=offsetof(__typeof__(fixture),tables)};
    const char *names[]={"mmio.map","mmio.read","mmio.write","hardware.read"};
    kernel_surface_call calls[]={map,read_mmio,write_mmio,physical_read};
    for (unsigned i=0;i<4;++i) {strcpy(fixture.methods[i].name,names[i]);fixture.methods[i].call=calls[i];fixture.methods[i].major=1;}
    regions.table=(struct mmio_table){.magic=MMIO_TABLE_MAGIC,.major=1,.count=absent?0:1,.entry_bytes=sizeof(struct mmio_region)};
    regions.region.kind=MMIO_HPET;
    hardware.table=(struct hwenum_table){.magic=HWENUM_TABLE_MAGIC,.major=1,.count=1,.entry_bytes=sizeof(struct hwenum_descriptor)};
    hardware.descriptor=(struct hwenum_descriptor){.kind=HWENUM_ACPI_TABLE,.address=0x100000,.length=244,
        .data.acpi={.signature={'F','A','C','P'},.revision=5}};
    fixture.tables[0]=(struct kernel_hub_table){.name=MMIO_TABLE_NAME,.address=(uintptr_t)&regions,.bytes=sizeof(regions)};
    fixture.tables[1]=(struct kernel_hub_table){.name=HWENUM_TABLE_NAME,.address=(uintptr_t)&hardware,.bytes=sizeof(hardware)};
    cmos[0]=0x56;cmos[2]=0x34;cmos[4]=0x12;cmos[7]=0x14;cmos[8]=0x10;cmos[9]=0x26;
    cmos[0xb]=2;cmos[0xd]=0x80;cmos[0x32]=0x20;
    assert(CALL(clock_surface_now_ns,0,0,0).status==KERNEL_SURFACE_FAILED);
    assert(CALL(clock_surface_calibrate,0,0,0).status==KERNEL_SURFACE_FAILED);
    struct kernel_surface_result initialized=CALL(clock_surface_init,&fixture.hub,0,0);
    assert(!initialized.status && initialized.value==(fallback?CLOCK_HPET:CLOCK_TSC));
    struct tsc_diagnostics diagnostics;
    assert(!CALL(clock_surface_tsc_status,&diagnostics,0,0).status);
    assert(diagnostics.reason==(fallback?TSC_REASON_MISSING_INVARIANT:TSC_REASON_NONE));
    assert(configuration==(absent?0:1));
    struct clock_info info;
    assert(!CALL(clock_surface_info,&info,0,0).status && info.calibrations==1);
    assert((bool)(info.flags&CLOCK_RTC_AVAILABLE)==!no_rtc);
    if (!fallback) assert(info.tsc_hz>2999000000 && info.tsc_hz<3001000000);
    if (enumerated) assert(!(info.flags&CLOCK_TSC_CALIBRATED));
    assert(!CALL(clock_surface_init,&fixture.hub,0,0).status);
    assert(CALL(clock_surface_init,0,0,0).status==KERNEL_SURFACE_FAILED);
    uint64_t previous=CALL(clock_surface_now_ns,0,0,0).value;
    for (unsigned i=0;i<10;++i) {ticks+=3000000;hpet+=10000;
        struct kernel_surface_result r=CALL(clock_surface_now_ns,0,0,0);assert(!r.status && r.value>=previous);previous=r.value;}
    assert(!CALL(clock_surface_calibrate,0,0,0).status);
    assert(CALL(clock_surface_now_ns,0,0,0).value>=previous);
    uint64_t ns=CALL(clock_surface_now_ns,0,0,0).value;
    uint64_t us=CALL(clock_surface_now_us,0,0,0).value,ms=CALL(clock_surface_now_ms,0,0,0).value;
    assert(us>=ns/1000 && ms>=us/1000);
    struct clock_calendar date={0};
    if (no_rtc) {assert(!reads && CALL(clock_surface_rtc,&date,0,0).status==KERNEL_SURFACE_FAILED);}
    else {
        assert(!CALL(clock_surface_rtc,&date,0,0).status && (bool)(date.flags&CLOCK_RTC_CENTURY_ASSUMED)==assumed && date.year==2026 && date.month==10 && date.day==14 && date.hour==12 && date.minute==34 && date.second==56);
        cmos[0xb]=0; cmos[4]=0x92; /* 12-hour BCD noon. */
        assert(!CALL(clock_surface_rtc,&date,0,0).status && date.hour==12);
        cmos[4]=0x12;assert(!CALL(clock_surface_rtc,&date,0,0).status && date.hour==0);
        cmos[0xb]=6;cmos[0]=59;cmos[2]=58;cmos[4]=23;cmos[7]=29;cmos[8]=2;cmos[9]=24;cmos[0x32]=20;
        assert(!CALL(clock_surface_rtc,&date,0,0).status && date.year==2024 && date.day==29);
        cmos[9]=25;assert(CALL(clock_surface_rtc,&date,0,0).status==KERNEL_SURFACE_FAILED);
        cmos[9]=24;cmos[0xa]=0x80;assert(CALL(clock_surface_rtc,&date,0,0).status==KERNEL_SURFACE_FAILED);
        cmos[0xa]=0;cmos[0xd]=0;assert(CALL(clock_surface_rtc,&date,0,0).status==KERNEL_SURFACE_FAILED);
    }
    if (absent) {
        previous=CALL(clock_surface_now_ns,0,0,0).value;
        tsc_step=3000000; /* Slow ordered reads make anchoring gaps measurable. */
        assert(!CALL(clock_surface_calibrate,0,0,0).status);
        assert(CALL(clock_surface_now_ns,0,0,0).value-previous==3000000);
        tsc_step=300;
    }
    if (!fallback && !enumerated && !smp_case) {
        previous=CALL(clock_surface_now_ns,0,0,0).value;
        ticks-=600; /* Below previous reading, still above the calibration anchor. */
        assert(!CALL(clock_surface_now_ns,0,0,0).status);
        CALL(clock_surface_info,&info,0,0);
        assert(info.source==CLOCK_HPET && (info.flags&CLOCK_TSC_UNSTABLE));
        assert(!CALL(clock_surface_tsc_status,&diagnostics,0,0).status && diagnostics.reason==TSC_REASON_REGRESSION);
        assert(CALL(clock_surface_now_ns,0,0,0).value>=previous);
        assert(CALL(clock_surface_calibrate,0,0,0).value==CLOCK_TSC);
    }
    if (!enumerated) {
        CALL(clock_surface_info,&info,0,0);uint64_t calibrations=info.calibrations;
        stalled=true;assert(CALL(clock_surface_calibrate,0,0,0).status==KERNEL_SURFACE_FAILED);stalled=false;
        assert(!CALL(clock_surface_tsc_status,&diagnostics,0,0).status && diagnostics.reason==TSC_REASON_REFERENCE_FAILURE);
        CALL(clock_surface_info,&info,0,0);assert(info.calibrations==calibrations);
        if (!fallback && !smp_case) {
            previous=CALL(clock_surface_now_ns,0,0,0).value;drift=true;
            assert(CALL(clock_surface_calibrate,0,0,0).value==CLOCK_HPET);
            assert(CALL(clock_surface_now_ns,0,0,0).value>=previous);
            CALL(clock_surface_info,&info,0,0);assert(info.source==CLOCK_HPET && !(info.flags&CLOCK_TSC_CALIBRATED));
            assert(!CALL(clock_surface_tsc_status,&diagnostics,0,0).status && diagnostics.reason==TSC_REASON_CALIBRATION_JITTER);
        }
    }
    if(enumerated) assert(CALL(clock_surface_shared,0,0,0).status==KERNEL_SURFACE_FAILED);
    else {
        previous=CALL(clock_surface_now_ns,0,0,0).value;
        assert(!CALL(clock_surface_shared,0,0,0).status);
        ticks=0; /* Model a different CPU's unsynchronized counter. */
        assert(CALL(clock_surface_now_ns,0,0,0).value>=previous);
        assert(CALL(clock_surface_calibrate,0,0,0).value==CLOCK_HPET);
        CALL(clock_surface_info,&info,0,0);assert(info.source==CLOCK_HPET);
    }
    if(smp_case) {
        strcpy(fixture.methods[4].name,"cpu.control");fixture.methods[4].call=cpu_call;fixture.methods[4].major=1;
        strcpy(fixture.methods[5].name,"mem.calloc");fixture.methods[5].call=clock_alloc;fixture.methods[5].major=1;fixture.hub.count=6;
        CALL(clock_surface_info,&info,0,0);
        struct tsc_group refinement={.status=TSC_VALID,.retained=10,.target_ns=100000000,.interval_median_ns=100100000,
            .drift_median_ppb=-200000,.drift_mad_ppb=1000,.max_drift_ppm=400,.max_uncertainty_ns=100};
        uint64_t old_hz=info.tsc_hz,old_epoch=info.calibrations;
        previous=CALL(clock_surface_now_ns,0,0,0).value;
        assert(CALL(clock_surface_refine,&refinement,old_epoch-1,old_hz).status);
        test_cpu=1;assert(CALL(clock_surface_refine,&refinement,old_epoch,old_hz).status);test_cpu=0;
        refinement.retained=6;assert(CALL(clock_surface_refine,&refinement,old_epoch,old_hz).status);refinement.retained=10;
        refinement.drift_mad_ppb=100001;assert(CALL(clock_surface_refine,&refinement,old_epoch,old_hz).status);refinement.drift_mad_ppb=1000;
        refinement.drift_median_ppb=INT64_MIN;assert(CALL(clock_surface_refine,&refinement,old_epoch,old_hz).status);refinement.drift_median_ppb=-200000;
        assert(!CALL(clock_surface_refine,&refinement,old_epoch,old_hz).status);
        assert(!CALL(clock_surface_info,&info,0,0).status && info.source==CLOCK_HPET && info.calibrations==old_epoch+1 && info.tsc_hz<old_hz);
        assert(CALL(clock_surface_now_ns,0,0,0).value>=previous);
        assert(CALL(clock_surface_refine,&refinement,old_epoch,old_hz).status);
        refinement.drift_median_ppb=200000;
        old_hz=info.tsc_hz;old_epoch=info.calibrations;
        assert(!CALL(clock_surface_refine,&refinement,old_epoch,old_hz).status);
        assert(!CALL(clock_surface_info,&info,0,0).status && info.tsc_hz>old_hz && info.calibrations==old_epoch+1);
        struct tsc_report proof={.status=TSC_VALID,.cpus=2,.samples=6,.max_offset_ns=100,.max_drift_ppm=1,.max_uncertainty_ns=100,.calibration=info.calibrations};
        assert(CALL(clock_surface_smp_tsc,2,1,0).status);
        --proof.calibration;assert(CALL(clock_surface_smp_tsc,2,1,&proof).status);++proof.calibration;
        assert(!CALL(clock_surface_smp_tsc,2,1,&proof).status);
        uint64_t saved=ticks,anchor=previous_read+(last_read-previous_read)/2;
        test_cpu=1;ticks=anchor-tsc_step-1;assert(!CALL(clock_surface_now_ns,0,0,0).status);
        CALL(clock_surface_info,&info,0,0);assert(info.source==CLOCK_TSC);
        test_cpu=0;ticks=saved;
        previous=CALL(clock_surface_now_ns,0,0,0).value;
        test_cpu=1;ticks-=100;assert(CALL(clock_surface_now_ns,0,0,0).value>=previous);
        CALL(clock_surface_info,&info,0,0);assert(info.source==CLOCK_TSC && !(info.flags&CLOCK_TSC_UNSTABLE));
        test_cpu=0;if(health_case)ticks+=3000000000;else ticks=0;
        assert(!CALL(clock_surface_now_ns,0,0,0).status);
        CALL(clock_surface_info,&info,0,0);assert(info.source==CLOCK_HPET && (info.flags&CLOCK_TSC_UNSTABLE));
        assert(!CALL(clock_surface_shared,0,0,0).status && CALL(clock_surface_smp_tsc,2,1,&proof).status);
        free(clock_memory);
    }
    printf("PASS: clock %s initialization/calibration, continuity, bounded failure, units and RTC decoding/availability\n",mode);
    return 0;
}
