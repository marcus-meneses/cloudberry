#include "../kmodules/shared/clock.h"
#include "../kmodules/shared/smp.h"
#include <stdbool.h>
#define CALL(fn,a,b,c) fn((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static bool failure(unsigned line) {
    const char *s="FAIL clock-boot.c line ";while (*s) __asm__ volatile("outb %0,$0xe9" :: "a"(*s++));
    char digits[10];unsigned n=0;do {digits[n++]=(char)('0'+line%10);line/=10;} while (line);
    while (n) __asm__ volatile("outb %0,$0xe9" :: "a"(digits[--n]));
    __asm__ volatile("outb %0,$0xe9" :: "a"((unsigned char)'\n'));return false;
}
bool kernel_clock_test(const struct kernel_surfaces *hub) {
    kernel_surface_call init=kernel_surface_find_version(hub,"clock.init",1,0),
        calibrate=kernel_surface_find_version(hub,"clock.calibrate",1,0),
        ns=kernel_surface_find_version(hub,"clock.now_ns",1,0),
        us=kernel_surface_find_version(hub,"clock.now_us",1,0),
        ms=kernel_surface_find_version(hub,"clock.now_ms",1,0),
        info_call=kernel_surface_find_version(hub,"clock.info",1,0),
        rtc=kernel_surface_find_version(hub,"clock.rtc",1,0);
    if (!init || !calibrate || !ns || !us || !ms || !info_call || !rtc) return failure(__LINE__);
    struct clock_info before,after;
    if (CALL(info_call,&before,0,0).status || !before.calibrations || !(before.flags&CLOCK_HPET_AVAILABLE) ||
        (before.source!=CLOCK_TSC && before.source!=CLOCK_HPET) || CALL(init,hub,0,0).status) return failure(__LINE__);
    struct kernel_surface_result start=CALL(ns,0,0,0),current=start;
    if (start.status) return failure(__LINE__);
    unsigned poll;
    for (poll=0;poll<1000000 && current.value-start.value<2000000;++poll) {
        current=CALL(ns,0,0,0);if (current.status || current.value<start.value) return failure(__LINE__);
    }
    if (poll==1000000 || CALL(calibrate,0,0,0).status) return failure(__LINE__);
    struct kernel_surface_result later=CALL(ns,0,0,0),micros=CALL(us,0,0,0),millis=CALL(ms,0,0,0);
    if (later.status || later.value<current.value || micros.status || millis.status ||
        micros.value<later.value/1000 || millis.value<micros.value/1000 ||
        CALL(info_call,&after,0,0).status || after.calibrations!=before.calibrations+1) return failure(__LINE__);
    /* Q35's default hardware includes a valid CMOS RTC. */
    struct clock_calendar date;
    if (!(after.flags&CLOCK_RTC_AVAILABLE) || CALL(rtc,&date,0,0).status || date.year<2000 ||
        date.month<1 || date.month>12 || date.day<1 || date.day>31 || date.hour>23 || date.minute>59 || date.second>59)
        return failure(__LINE__);
    kernel_surface_call nmi=kernel_surface_find_version(hub,"inthandle.nmi_mask",1,0),
        cmos=kernel_surface_find_version(hub,"inthandle.cmos_read",1,0);
    if (!nmi || !cmos) return failure(__LINE__);
    struct kernel_surface_result original=CALL(nmi,1,0,0);
    if (original.status || CALL(nmi,2,0,0).value!=1 || CALL(rtc,&date,0,0).status ||
        CALL(nmi,2,0,0).value!=1 || CALL(cmos,12,0,0).status!=KERNEL_SURFACE_FAILED ||
        CALL(nmi,original.value,0,0).status) return failure(__LINE__);
    /* Deliberate recalibration invalidated any earlier SMP proof. Refresh it
     * before thread/device acceptance so an accepted TSC path is actually used. */
    if(before.source==CLOCK_TSC) {
        kernel_surface_call smp_info=kernel_surface_find(hub,"smp.info");struct smp_info topology;
        if(!smp_info || CALL(smp_info,&topology,0,0).status)return failure(__LINE__);
        if(topology.count>1) {
            kernel_surface_call validate=kernel_surface_find(hub,"smp.validate_clock");
            if(!validate)return failure(__LINE__);
            struct kernel_surface_result checked=CALL(validate,0,0,0);
            if(CALL(info_call,&after,0,0).status || (checked.status && after.source!=CLOCK_HPET))return failure(__LINE__);
        }
    }
    const char *source=after.source==CLOCK_TSC?"PASS clock boot: TSC, calibration, monotonic units and RTC\n":
        "PASS clock boot: HPET fallback, calibration, monotonic units and RTC\n";
    while (*source) __asm__ volatile("outb %0,$0xe9" :: "a"(*source++));
    return true;
}
