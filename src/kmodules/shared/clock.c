#include "clock.h"
#include "clock-platform.h"
#include "mmio.h"
#include "hwenum.h"
#include "tsc-validation.h"
#include "../../kernel/cpu.h"
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
#define POLL_LIMIT 2000000u
#define MIN_HZ UINT64_C(1000000)
#define MAX_HZ UINT64_C(10000000000)
static struct {
    const struct kernel_surfaces *hub;
    kernel_surface_call read,write,cpu_control;
    uintptr_t hpet_address;uint64_t *cpu_last;size_t cpu_count;int smp_tsc;
    struct clock_cpu cpu;
    struct clock_info info;uint32_t tsc_reason;
    uint64_t hpet_index,base_ns,hpet_anchor,tsc_anchor,last_ns,last_tsc,health_ns,health_reads;
    uint8_t century;
    int configured,initialized,rtc_allowed,shared;
} state;
/* Exact quotient/remainder conversion avoids 128-bit runtime dependencies. */
static int scale(uint64_t ticks,uint64_t numerator,uint64_t denominator,uint64_t *out) {
    uint64_t q=ticks/denominator,r=ticks%denominator;
    if (q>UINT64_MAX/numerator || (r && numerator>UINT64_MAX/r)) return 0;
    uint64_t whole=q*numerator,fraction=r*numerator/denominator;
    if (fraction>UINT64_MAX-whole) return 0;
    *out=whole+fraction;return 1;
}
static int hpet_ticks(uint64_t *out) {
    if(state.hpet_address)return clock_hpet_read(state.hpet_address,out);
    struct kernel_surface_result r=CALL(state.read,state.hpet_index,0xf0,8);
    if (r.status) return 0;
    *out=r.value;return 1;
}
static int hpet_ns(uint64_t ticks,uint64_t *out) {return scale(ticks,state.info.hpet_period_fs,1000000,out);}
static int now(uint64_t *out) {
    if (!state.initialized) return 0;
    uint64_t ticks,elapsed=0;
    if (state.info.source==CLOCK_TSC) {
        ticks=clock_cpu_ticks();
        uint64_t *previous=&state.last_tsc;
        if(state.smp_tsc) {size_t cpu=CALL(state.cpu_control,CPU_CURRENT,0,0).value;if(cpu>=state.cpu_count)return 0;previous=&state.cpu_last[cpu];}
        if (ticks<*previous || (state.smp_tsc && *previous && ticks==*previous)) {
            state.info.flags|=CLOCK_TSC_UNSTABLE;state.tsc_reason=TSC_REASON_REGRESSION;
            if (!(state.info.flags&CLOCK_HPET_AVAILABLE)) return 0;
            state.info.source=CLOCK_HPET;
        } else {
            *previous=ticks;
            if(ticks>=state.tsc_anchor) {if(!scale(ticks-state.tsc_anchor,1000000000,state.info.tsc_hz,&elapsed))return 0;}
            else if(!state.smp_tsc)return 0; /* A validated AP may initially lag the BSP anchor. */
        }
    }
    if (state.info.source==CLOCK_HPET) {
        if (!hpet_ticks(&ticks) || ticks<state.hpet_anchor || !hpet_ns(ticks-state.hpet_anchor,&elapsed)) return 0;
    }
    if (elapsed>UINT64_MAX-state.base_ns) return 0;
    uint64_t value=state.base_ns+elapsed;
    if (value<state.last_ns) value=state.last_ns;
    if(state.smp_tsc && state.info.source==CLOCK_TSC && (value-state.health_ns>=100000000 || ++state.health_reads>=64)) {
        uint64_t reference,reference_elapsed;
        if(!hpet_ticks(&reference) || reference<state.hpet_anchor || !hpet_ns(reference-state.hpet_anchor,&reference_elapsed) || reference_elapsed>UINT64_MAX-state.base_ns)return 0;
        reference=state.base_ns+reference_elapsed;
        uint64_t difference=reference>value?reference-value:value-reference;
        if(difference>1000000) {state.tsc_reason=TSC_REASON_DRIFT;state.info.flags|=CLOCK_TSC_UNSTABLE;state.info.source=CLOCK_HPET;value=reference<state.last_ns?state.last_ns:reference;}
        state.health_ns=value;state.health_reads=0;
    }
    state.last_ns=value;*out=value;return 1;
}
struct rtc_raw {uint8_t second,minute,hour,day,month,year,century,mode;};
static int rtc_snapshot(struct rtc_raw *r) {
    if (clock_cmos_read(0x0a)&0x80) return 0;
    if (!(clock_cmos_read(0x0d)&0x80)) return 0; /* Battery/time valid. */
    r->second=clock_cmos_read(0);r->minute=clock_cmos_read(2);r->hour=clock_cmos_read(4);
    r->day=clock_cmos_read(7);r->month=clock_cmos_read(8);r->year=clock_cmos_read(9);
    r->century=state.century?clock_cmos_read(state.century):0;r->mode=clock_cmos_read(0x0b);
    return !(clock_cmos_read(0x0a)&0x80) && !(r->mode&0x80); /* Not SET/frozen. */
}
static int number(uint8_t raw,int binary,unsigned *out) {
    if (!binary && ((raw&15)>9 || (raw>>4)>9)) return 0;
    *out=binary?raw:(raw>>4)*10+(raw&15);return 1;
}
static int calendar(struct clock_calendar *out) {
    if (!state.rtc_allowed) return 0;
    struct rtc_raw previous={0},current;
    int have_previous=0;
    for (unsigned attempt=0;attempt<10000;++attempt) {
        if (!rtc_snapshot(&current)) {have_previous=0;continue;}
        if (have_previous && previous.second==current.second && previous.minute==current.minute &&
            previous.hour==current.hour && previous.day==current.day && previous.month==current.month &&
            previous.year==current.year && previous.century==current.century && previous.mode==current.mode) {
            unsigned second,minute,hour,day,month,year,century=20;
            int binary=(current.mode&4)!=0,pm=(current.hour&0x80)!=0;
            if (!number(current.second,binary,&second) || !number(current.minute,binary,&minute) ||
                !number(current.hour&0x7f,binary,&hour) || !number(current.day,binary,&day) ||
                !number(current.month,binary,&month) || !number(current.year,binary,&year) ||
                (state.century && !number(current.century,binary,&century))) return 0;
            if (!(current.mode&2)) {if (hour<1 || hour>12) return 0;hour=hour%12+(pm?12:0);}
            else if (pm) return 0;
            if (year>99 || century<1 || century>99 || second>59 || minute>59 || hour>23 || month<1 || month>12) return 0;
            year+=century*100;
            static const uint8_t days[]={31,28,31,30,31,30,31,31,30,31,30,31};
            unsigned maximum=days[month-1]+(month==2 && year%4==0 && (year%100!=0 || year%400==0));
            if (day<1 || day>maximum) return 0;
            *out=(struct clock_calendar){year,month,day,hour,minute,second,state.century?0:CLOCK_RTC_CENTURY_ASSUMED};return 1;
        }
        previous=current;have_previous=1;
    }
    return 0;
}
static void rtc_detect(const struct kernel_surfaces *hub) {
    const struct kernel_hub_table *ref=kernel_hub_find_table(hub,HWENUM_TABLE_NAME);
    kernel_surface_call read=kernel_surface_find_version(hub,"hardware.read",1,0);
    if (!ref || !read) return;
    if (ref->bytes<sizeof(struct hwenum_table)) return;
    const struct hwenum_table *hw=(const void *)ref->address;
    if (!hw || hw->magic!=HWENUM_TABLE_MAGIC || hw->major!=1 || hw->entry_bytes!=sizeof(hw->entries[0]) ||
        hw->count>(ref->bytes-sizeof(*hw))/sizeof(hw->entries[0])) return;
    for (size_t i=0;i<hw->count;++i) {
        const struct hwenum_descriptor *h=&hw->entries[i];
        if (h->kind!=HWENUM_ACPI_TABLE || h->data.acpi.signature[0]!='F' || h->data.acpi.signature[1]!='A' ||
            h->data.acpi.signature[2]!='C' || h->data.acpi.signature[3]!='P') continue;
        uint8_t data[3]={0};
        if (h->length<109 || CALL(read,h->address+108,data,h->length>=111?3:1).status) return;
        if (h->length>=111 && (data[1]&(1u<<5))) return; /* CMOS RTC not present. */
        if (data[0]>=0x80 || (data[0] && data[0]<0x0e)) return;
        state.century=data[0];state.rtc_allowed=1;
        struct clock_calendar value;
        if (calendar(&value)) state.info.flags|=CLOCK_RTC_AVAILABLE;
        return;
    }
}
struct kernel_surface_result clock_surface_calibrate(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.configured) return module_result(0,0);
    uint64_t frequency=0,hpet=0,tsc=0,base=0;
    if(!(state.cpu.flags&CLOCK_TSC_PRESENT))state.tsc_reason=TSC_REASON_MISSING_TSC;
    else if(!(state.cpu.flags&CLOCK_TSC_INVARIANT))state.tsc_reason=TSC_REASON_MISSING_INVARIANT;
    else if(!state.shared)state.tsc_reason=TSC_REASON_UNCALIBRATED;
    int candidate=!state.shared && (state.cpu.flags&(CLOCK_TSC_PRESENT|CLOCK_TSC_INVARIANT))==(CLOCK_TSC_PRESENT|CLOCK_TSC_INVARIANT);
    if (state.info.flags&CLOCK_HPET_AVAILABLE) {
        uint64_t measurements[3]={0};
        for (unsigned sample=0;sample<3;++sample) {
            uint64_t begin,end=0,first=0,last=0,elapsed;
            if (!hpet_ticks(&begin)) {state.tsc_reason=TSC_REASON_REFERENCE_FAILURE;return module_result(0,0);}
            if (candidate) first=clock_cpu_ticks();
            uint64_t target=UINT64_C(5000000000000)/state.info.hpet_period_fs; /* 5 ms. */
            unsigned poll;
            for (poll=0;poll<POLL_LIMIT;++poll) {
                if (!hpet_ticks(&end) || end<begin) {state.tsc_reason=TSC_REASON_REFERENCE_FAILURE;return module_result(0,0);}
                if (end-begin>=target) break;
                __asm__ volatile("pause");
            }
            if (candidate) last=clock_cpu_ticks();
            if (poll==POLL_LIMIT || !hpet_ns(end-begin,&elapsed) || !elapsed || elapsed>100000000) {state.tsc_reason=TSC_REASON_REFERENCE_FAILURE;return module_result(0,0);}
            if (candidate && last>first && last-first<=UINT64_MAX/1000000000)
                measurements[sample]=(last-first)*1000000000/elapsed;
        }
        uint64_t low=measurements[0],high=low;
        for (unsigned i=1;i<3;++i) {if (measurements[i]<low) low=measurements[i];if (measurements[i]>high) high=measurements[i];}
        if(candidate)state.tsc_reason=TSC_REASON_CALIBRATION_JITTER;
        if (candidate && low>=MIN_HZ && high<=MAX_HZ && high-low<=low/50) {
            frequency=measurements[0]+measurements[1]+measurements[2];frequency/=3;
            if (state.cpu.hz && (state.cpu.hz>frequency?state.cpu.hz-frequency:frequency-state.cpu.hz)>frequency/50) {frequency=0;state.tsc_reason=TSC_REASON_FREQUENCY_MISMATCH;}
        }
    } else if (candidate && state.cpu.hz>=MIN_HZ && state.cpu.hz<=MAX_HZ) frequency=state.cpu.hz;
    if (!frequency && !(state.info.flags&CLOCK_HPET_AVAILABLE)) return module_result(0,0);
    /* Anchor old and new scales to the same observation. Bracket HPET with
     * ordered TSC reads and use their midpoint, rather than dropping the time
     * between an old now() call and new counter reads. No state changes on failure. */
    int need_tsc=frequency || (state.initialized && state.info.source==CLOCK_TSC);
    uint64_t first=need_tsc?clock_cpu_ticks():0;
    if ((state.info.flags&CLOCK_HPET_AVAILABLE) && !hpet_ticks(&hpet)) {state.tsc_reason=TSC_REASON_REFERENCE_FAILURE;return module_result(0,0);}
    uint64_t last=need_tsc?clock_cpu_ticks():0;
    if (need_tsc && (last<first || (state.initialized && first<state.last_tsc))) {state.tsc_reason=TSC_REASON_REGRESSION;return module_result(0,0);}
    tsc=first+(last-first)/2;
    if (state.initialized) {
        uint64_t elapsed;
        if (state.info.source==CLOCK_TSC) {
            if (!scale(tsc-state.tsc_anchor,1000000000,state.info.tsc_hz,&elapsed)) return module_result(0,0);
        } else if (hpet<state.hpet_anchor || !hpet_ns(hpet-state.hpet_anchor,&elapsed)) return module_result(0,0);
        if (elapsed>UINT64_MAX-state.base_ns) return module_result(0,0);
        base=state.base_ns+elapsed;if (base<state.last_ns) base=state.last_ns;
    }
    state.base_ns=base;state.hpet_anchor=hpet;state.tsc_anchor=tsc;state.last_tsc=tsc;state.last_ns=base;
    if(!state.shared)state.info.tsc_hz=frequency;
    if(state.info.tsc_hz)state.tsc_reason=TSC_REASON_NONE;
    state.smp_tsc=0;state.info.source=frequency && !state.shared?CLOCK_TSC:CLOCK_HPET;
    if(!state.shared)state.info.flags&=~CLOCK_TSC_CALIBRATED;
    if (frequency) state.info.flags&=~CLOCK_TSC_UNSTABLE;
    if (frequency && (state.info.flags&CLOCK_HPET_AVAILABLE)) state.info.flags|=CLOCK_TSC_CALIBRATED;
    ++state.info.calibrations;state.initialized=1;
    return module_result(state.info.source,1);
}
struct kernel_surface_result clock_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();const struct kernel_surfaces *hub=(const void *)a;
    if (state.initialized) return module_result(state.info.source,hub==state.hub);
    if (!hub || hub->magic!=KERNEL_SURFACES_MAGIC || hub->major!=KERNEL_HUB_ABI) return module_result(0,0);
    if (!clock_platform_configure(hub)) return module_result(0,0);
    state.configured=0;state.hub=hub;state.cpu=clock_cpu_detect();state.info=(struct clock_info){.flags=state.cpu.flags};
    state.read=kernel_surface_find_version(hub,"mmio.read",1,0);
    state.write=kernel_surface_find_version(hub,"mmio.write",1,0);
    kernel_surface_call map=kernel_surface_find_version(hub,"mmio.map",1,0);
    const struct kernel_hub_table *ref=kernel_hub_find_table(hub,MMIO_TABLE_NAME);
    if (!state.read || !state.write || !map || !ref) return module_result(0,0);
    if (ref->bytes<sizeof(struct mmio_table)) return module_result(0,0);
    const struct mmio_table *table=(const void *)ref->address;
    if (!table || table->magic!=MMIO_TABLE_MAGIC || table->major!=1 || table->entry_bytes!=sizeof(table->entries[0]) ||
        table->count>(ref->bytes-sizeof(*table))/sizeof(table->entries[0])) return module_result(0,0);
    for (size_t i=0;i<table->count;++i) if (table->entries[i].kind==MMIO_HPET) {
        struct kernel_surface_result mapped=CALL(map,i,0,0);if(mapped.status)continue;
        struct kernel_surface_result caps=CALL(state.read,i,0,8),config=CALL(state.read,i,16,8);
        uint64_t period=caps.value>>32;
        if (caps.status || config.status || !(caps.value&(1u<<13)) || !period || period>100000000) continue;
        /* Enable the main counter only. Never reset it or change legacy/IRQ routing. */
        if (!(config.value&1) && state.write(i,16,8,config.value|1,0,0).status) continue;
        state.hpet_address=mapped.value;state.hpet_index=i;state.info.hpet_period_fs=period;state.info.flags|=CLOCK_HPET_AVAILABLE;break;
    }
    state.rtc_allowed=0;state.century=0;rtc_detect(hub);state.configured=1;
    struct kernel_surface_result calibrated=clock_surface_calibrate(0,0,0,0,0,0);
    if (calibrated.status && state.cpu.hz &&
        (state.cpu.flags&(CLOCK_TSC_PRESENT|CLOCK_TSC_INVARIANT))==(CLOCK_TSC_PRESENT|CLOCK_TSC_INVARIANT)) {
        /* A stalled/unreadable reference must not prevent using an independent
         * architecturally enumerated TSC frequency during initial selection. */
        state.info.flags&=~CLOCK_HPET_AVAILABLE;
        calibrated=clock_surface_calibrate(0,0,0,0,0,0);
    }
    return calibrated;
}
struct kernel_surface_result clock_surface_now_ns(MODULE_ARGS) {MODULE_UNUSED();uint64_t value=0;int ok=now(&value);return module_result(value,ok);}
struct kernel_surface_result clock_surface_now_us(MODULE_ARGS) {MODULE_UNUSED();uint64_t value=0;int ok=now(&value);return module_result(value/1000,ok);}
struct kernel_surface_result clock_surface_now_ms(MODULE_ARGS) {MODULE_UNUSED();uint64_t value=0;int ok=now(&value);return module_result(value/1000000,ok);}
struct kernel_surface_result clock_surface_info(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.initialized || !a) return module_result(0,0);
    *(struct clock_info *)a=state.info;return module_result(1,1);
}
struct kernel_surface_result clock_surface_rtc(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.initialized || !a) return module_result(0,0);
    struct clock_calendar value;int ok=calendar(&value);
    if (ok) {state.info.flags|=CLOCK_RTC_AVAILABLE;*(struct clock_calendar *)a=value;}
    else state.info.flags&=~CLOCK_RTC_AVAILABLE;
    return module_result(ok,ok);
}

struct kernel_surface_result clock_surface_shared(MODULE_ARGS) {
    MODULE_UNUSED();
    if(!state.initialized || !(state.info.flags&CLOCK_HPET_AVAILABLE))return module_result(0,0);
    if(state.shared && !state.smp_tsc)return module_result(1,1);
    state.shared=1;
    struct kernel_surface_result r=clock_surface_calibrate(0,0,0,0,0,0);
    if(r.status)state.shared=0;
    return module_result(r.status?0:1,!r.status);
}

struct kernel_surface_result clock_surface_tsc_status(MODULE_ARGS) {
    MODULE_UNUSED();if(!a)return module_result(0,0);
    *(struct tsc_diagnostics *)a=(struct tsc_diagnostics){.reason=state.initialized?state.tsc_reason:TSC_REASON_NOT_INITIALIZED,
        .cpu=UINT32_MAX,.flags=state.info.flags,.source=state.info.source,.hz=state.info.tsc_hz,.enumerated_hz=state.cpu.hz,.calibrations=state.info.calibrations};
    return module_result(1,1);
}
struct kernel_surface_result clock_surface_sample(MODULE_ARGS) {
    MODULE_UNUSED();if(!a || !state.initialized)return module_result(TSC_REASON_NOT_INITIALIZED,0);
    if(!state.hpet_address)return module_result(TSC_REASON_MISSING_HPET,0);
    if(!state.info.tsc_hz)return module_result(state.tsc_reason?state.tsc_reason:TSC_REASON_UNCALIBRATED,0);
    struct clock_cpu cpu=clock_cpu_detect();
    if(!(cpu.flags&CLOCK_TSC_PRESENT))return module_result(TSC_REASON_MISSING_TSC,0);
    if(!(cpu.flags&CLOCK_TSC_INVARIANT))return module_result(TSC_REASON_MISSING_INVARIANT,0);
    if((cpu.flags&CLOCK_TSC_RDTSCP)!=(state.cpu.flags&CLOCK_TSC_RDTSCP))return module_result(TSC_REASON_CAPABILITY_MISMATCH,0);
    uint64_t flags;__asm__ volatile("pushfq;pop %0;cli":"=r"(flags)::"memory");
    uint64_t empty_first=b?clock_cpu_ticks():0,empty_last=b?clock_cpu_ticks():0;
    uint64_t first=clock_cpu_ticks(),hpet=*(volatile uint64_t *)(state.hpet_address+0xf0),last=clock_cpu_ticks();
    __asm__ volatile("push %0;popfq"::"r"(flags):"memory","cc");
    struct clock_sample sample={.flags=cpu.flags};
    if(last<first || (b && (empty_last<empty_first || first<empty_last)))return module_result(TSC_REASON_REGRESSION,0);
    if(!scale(first+(last-first)/2,1000000000,state.info.tsc_hz,&sample.tsc_ns) ||
        !hpet_ns(hpet,&sample.hpet_ns) || !scale(last-first,1000000000,state.info.tsc_hz,&sample.uncertainty_ns))return module_result(TSC_REASON_CONVERSION,0);
    if(b) {struct clock_probe probe={.bracket_ns=sample.uncertainty_ns};
        if(!scale(empty_last-empty_first,1000000000,state.info.tsc_hz,&probe.empty_ns))return module_result(TSC_REASON_CONVERSION,0);
        *(struct clock_probe *)b=probe;
    }
    sample.uncertainty_ns+=state.info.hpet_period_fs/1000000+1;*(struct clock_sample *)a=sample;return module_result(1,1);
}
struct kernel_surface_result clock_surface_smp_tsc(MODULE_ARGS) {
    MODULE_UNUSED();const struct tsc_report *proof=(const void *)c;
    if(!proof || proof->status!=TSC_VALID || proof->cpus!=a || proof->max_offset_ns>10000 || proof->max_drift_ppm>500 || proof->max_uncertainty_ns>50000 || proof->samples<a*3 || proof->calibration!=state.info.calibrations)return module_result(0,0);
    if(!state.shared || !a || !b || !state.info.tsc_hz || (state.info.flags&CLOCK_TSC_UNSTABLE) || state.smp_tsc)return module_result(0,0);
    state.cpu_control=kernel_surface_find_version(state.hub,"cpu.control",1,0);
    kernel_surface_call alloc=kernel_surface_find_version(state.hub,"mem.calloc",1,0);
    if(!state.cpu_control || !alloc || CALL(state.cpu_control,CPU_CURRENT,0,0).value)return module_result(0,0);
    if(state.cpu_last && state.cpu_count!=a) {kernel_surface_call free=kernel_surface_find_version(state.hub,"mem.free",1,0);CALL(free,b,state.cpu_last,0);state.cpu_last=NULL;}
    struct kernel_surface_result memory=state.cpu_last?module_result((uintptr_t)state.cpu_last,1):CALL(alloc,b,a,sizeof(uint64_t));if(memory.status)return memory;
    state.cpu_last=(void *)memory.value;state.cpu_count=a;for(size_t i=0;i<a;++i)state.cpu_last[i]=0;
    uint64_t first=clock_cpu_ticks(),hpet,last,elapsed;
    if(!hpet_ticks(&hpet))return module_result(0,0);
    last=clock_cpu_ticks();
    if(last<first || hpet<state.hpet_anchor || !hpet_ns(hpet-state.hpet_anchor,&elapsed) || elapsed>UINT64_MAX-state.base_ns)return module_result(0,0);
    state.base_ns+=elapsed;if(state.base_ns<state.last_ns)state.base_ns=state.last_ns;
    state.hpet_anchor=hpet;state.tsc_anchor=first+(last-first)/2;state.cpu_last=(void *)memory.value;state.cpu_count=a;state.smp_tsc=1;state.health_ns=state.base_ns;state.health_reads=0;state.info.source=CLOCK_TSC;
    return module_result(1,1);
}

struct kernel_surface_result clock_surface_refine(MODULE_ARGS) {
    MODULE_UNUSED();const struct tsc_group *proof=(const void *)a;
    if(!proof || !state.initialized || !state.shared || state.smp_tsc || state.info.source!=CLOCK_HPET ||
        (state.info.flags&CLOCK_TSC_UNSTABLE) || state.info.calibrations!=b || state.info.tsc_hz!=c)return module_result(0,0);
    kernel_surface_call control=kernel_surface_find_version(state.hub,"cpu.control",1,0);
    if(!control || CALL(control,CPU_CURRENT,0,0).value)return module_result(0,0);
    uint64_t drift=proof->drift_median_ppb<0?UINT64_C(0)-(uint64_t)proof->drift_median_ppb:(uint64_t)proof->drift_median_ppb;
    if((proof->status!=TSC_VALID && proof->status!=TSC_DRIFT) || proof->retained<TSC_MIN_RETAINED ||
        proof->retained>TSC_TRIALS || proof->target_ns<100000000 || proof->interval_median_ns<proof->target_ns ||
        proof->max_offset_ns || proof->max_uncertainty_ns>50000 || proof->drift_mad_ppb>100000 ||
        drift<10000 || drift>20000000 || proof->max_drift_ppm>drift/1000+250)return module_result(0,0);
    if(c>UINT64_MAX/drift)return module_result(0,0);
    uint64_t correction=(c*drift+500000000)/1000000000;
    if(!correction || (proof->drift_median_ppb<0?correction>=c:correction>UINT64_MAX-c))return module_result(0,0);
    uint64_t hz=proof->drift_median_ppb<0?c-correction:c+correction;
    if(hz<MIN_HZ || hz>MAX_HZ || (state.cpu.hz && (state.cpu.hz>hz?state.cpu.hz-hz:hz-state.cpu.hz)>hz/50))return module_result(0,0);
    /* HPET anchor/base stay unchanged. Fresh samples and a new epoch are
     * mandatory before activation; no previous cross-CPU proof is reusable. */
    if(state.info.calibrations==UINT64_MAX)return module_result(0,0);
    state.info.tsc_hz=hz;++state.info.calibrations;state.tsc_reason=TSC_REASON_NONE;
    return module_result(hz,1);
}
