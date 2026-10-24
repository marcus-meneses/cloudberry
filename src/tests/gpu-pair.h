#ifndef CLOUDBERRY_GPU_PAIR_TEST_H
#define CLOUDBERRY_GPU_PAIR_TEST_H
#include <stdint.h>
#define GPU_PAIR_MAGIC UINT64_C(0x4342475055504149)
#define GPU_PAIR_NAME "@gpu-pair-test"
#define GPU_PAIR_ID {{0x43,0x42,0x54,0x45,0x53,0x54,0x47,0x50,0x55,0x50,0x41,0x49,0x52,0,0,1}}
/* Test-only rendezvous in the ordinary app's address space. The observer uses
 * validated process.copy, never a direct user pointer. No production ABI. */
struct gpu_pair_state {uint64_t magic,phase,ack,error;uint32_t width,height,x,y,key,reserved;uint64_t bench_samples,bench_ns,bench_max_ns,bench_times[32];};
#endif
