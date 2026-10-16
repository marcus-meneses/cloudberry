#ifndef CLOUDBERRY_KMODULE_CLOCK_H
#define CLOUDBERRY_KMODULE_CLOCK_H
#include "module.h"
#include "tsc-validation.h"
enum clock_source { CLOCK_NONE, CLOCK_TSC, CLOCK_HPET };
enum clock_flags { CLOCK_TSC_PRESENT=1, CLOCK_TSC_INVARIANT=2, CLOCK_TSC_ENUMERATED=4,
    CLOCK_TSC_CALIBRATED=8, CLOCK_HPET_AVAILABLE=16, CLOCK_RTC_AVAILABLE=32, CLOCK_TSC_UNSTABLE=64, CLOCK_TSC_RDTSCP=128 };
struct clock_info {
    uint32_t source, flags;
    uint64_t tsc_hz, hpet_period_fs, calibrations;
};
enum clock_rtc_flags { CLOCK_RTC_CENTURY_ASSUMED=1 };
struct clock_calendar {
    uint16_t year;
    uint8_t month,day,hour,minute,second,flags;
};
/* Calls are serialized through kernel mediation. SMP begins on HPET. sample(out) supplies HPET-referenced brackets;
 * smp_tsc(cpu_count,heap,current_report) requires the successful validation proof.
 * shared()/calibrate() return to HPET and invalidate the old proof.
 * init(hub) -> selected source; initializes hardware and calls calibration.
 * calibrate() -> selected source; preserves elapsed time across source changes.
 * now_ns/us/ms() -> monotonic elapsed time since successful clock initialization.
 * info(out clock_info) -> 1; RTC availability reflects the latest successful probe.
 * rtc(out clock_calendar) -> 1; stable, validated hardware calendar, read-only.
 * Calendar timezone is firmware/user-defined, not asserted to be UTC. With no
 * ACPI century index, years 00..99 mean 2000..2099 and the assumption is flagged.
 * Failure uses result.status; zero timestamps are valid successes.
 * HPET main counter is enabled when needed, never reset; comparators/IRQs are
 * untouched. Without HPET, invariant TSC needs a CPUID-enumerated frequency. */
struct clock_probe {uint64_t empty_ns,bracket_ns;};
/* sample(out, optional probe) measures empty ordered-read and HPET brackets.
 * refine(group, expected epoch, expected Hz) changes only shared HPET-mode calibration. */
struct kernel_surface_result clock_surface_refine(MODULE_ARGS);
struct kernel_surface_result clock_surface_tsc_status(MODULE_ARGS);
struct kernel_surface_result clock_surface_sample(MODULE_ARGS);
struct kernel_surface_result clock_surface_smp_tsc(MODULE_ARGS);
struct kernel_surface_result clock_surface_shared(MODULE_ARGS);
struct kernel_surface_result clock_surface_init(MODULE_ARGS);
struct kernel_surface_result clock_surface_calibrate(MODULE_ARGS);
struct kernel_surface_result clock_surface_now_ns(MODULE_ARGS);
struct kernel_surface_result clock_surface_now_us(MODULE_ARGS);
struct kernel_surface_result clock_surface_now_ms(MODULE_ARGS);
struct kernel_surface_result clock_surface_info(MODULE_ARGS);
struct kernel_surface_result clock_surface_rtc(MODULE_ARGS);
#endif
