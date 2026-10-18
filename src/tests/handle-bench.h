#ifndef CLOUDBERRY_TEST_HANDLE_BENCH_H
#define CLOUDBERRY_TEST_HANDLE_BENCH_H
#include <stdint.h>
enum {HB_CASES=8,HB_SAMPLES=48,HB_WARMUP=8};
struct handle_bench_report {uint64_t magic,cases,samples;uint64_t ns[HB_CASES][HB_SAMPLES];};
#define HB_MAGIC UINT64_C(0x434248414e444c45)
#endif
