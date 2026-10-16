#ifndef CLOUDBERRY_TSC_VALIDATION_H
#define CLOUDBERRY_TSC_VALIDATION_H
#include <stdint.h>
struct clock_sample {uint64_t tsc_ns,hpet_ns,uncertainty_ns;uint32_t flags,reserved;};
enum tsc_validation_status {TSC_UNCHECKED,TSC_VALID,TSC_UNAVAILABLE,TSC_UNCERTAIN,TSC_OFFSET,TSC_DRIFT,TSC_TIMEOUT};
struct tsc_report {uint32_t status,cpus;uint64_t samples,max_offset_ns,max_drift_ppm,max_uncertainty_ns,calibration;};
enum tsc_rejection_reason {
    TSC_REASON_NONE,TSC_REASON_NOT_INITIALIZED,TSC_REASON_MISSING_TSC,TSC_REASON_MISSING_INVARIANT,
    TSC_REASON_MISSING_HPET,TSC_REASON_UNCALIBRATED,TSC_REASON_CALIBRATION_JITTER,TSC_REASON_FREQUENCY_MISMATCH,
    TSC_REASON_REGRESSION,TSC_REASON_CAPABILITY_MISMATCH,TSC_REASON_REFERENCE_FAILURE,TSC_REASON_CONVERSION,
    TSC_REASON_OFFSET,TSC_REASON_DRIFT,TSC_REASON_UNCERTAINTY,TSC_REASON_TIMEOUT,TSC_REASON_ACTIVATION,
    TSC_REASON_SOURCE,TSC_REASON_SETUP,TSC_REASON_MISSING_METHOD
};
/* New descriptor; existing clock_info/tsc_report binary layouts stay unchanged.
 * Sample failure returns its reason in result.value, without shared mutation. */
struct tsc_diagnostics {uint32_t reason,cpu,flags,source;uint64_t hz,enumerated_hz,samples,calibrations;};
/* Same HPET reference and frequency conversion on every CPU. Measurements with
 * >50us uncertainty fail closed; offset budget 10us, drift budget 500ppm. */
int tsc_validate_pair(const struct clock_sample *,const struct clock_sample *,const struct clock_sample *,const struct clock_sample *,struct tsc_report *);
enum {TSC_TRIALS=10,TSC_WINDOWS=6,TSC_REPLACEMENTS=2,TSC_ATTEMPTS=TSC_WINDOWS*(TSC_REPLACEMENTS+1)+1,TSC_MIN_RETAINED=7,TSC_OBSERVATIONS=TSC_TRIALS*2*TSC_ATTEMPTS};
struct tsc_group {
    uint32_t status,retained,bracket_mask,outlier_mask;
    uint64_t target_ns,interval_median_ns,uncertainty_median_ns;
    int64_t drift_median_ppb,offset_median_ns;
    uint64_t drift_mad_ppb,offset_mad_ns,max_offset_ns,max_drift_ppm,max_uncertainty_ns;
};
void tsc_analyze_group(const struct clock_sample *,const struct clock_sample *,const struct clock_sample *,const struct clock_sample *,int,struct tsc_group *) __attribute__((visibility("hidden")));
struct tsc_exchange {uint64_t before,remote,after,hz,epoch;int64_t offset_ns;uint64_t uncertainty_ns;};
struct tsc_offset_group {uint32_t status,retained,wide_mask,outlier_mask;int64_t median_ns;uint64_t mad_ns,max_offset_ns,max_uncertainty_ns;};
struct tsc_offset_pair {struct tsc_offset_group start,end;};
int tsc_exchange_measure(struct tsc_exchange *) __attribute__((visibility("hidden")));
void tsc_analyze_offsets(const struct tsc_exchange *,struct tsc_offset_group *) __attribute__((visibility("hidden")));
#endif
