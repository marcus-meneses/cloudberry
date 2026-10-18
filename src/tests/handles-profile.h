#ifndef CLOUDBERRY_TEST_HANDLES_PROFILE_H
#define CLOUDBERRY_TEST_HANDLES_PROFILE_H
#include <stdint.h>
/* Test-only, trusted hub surface; never exposed to applications or candidate. */
enum {HP_ACTORS=8,HP_OPS=8,HP_CPUS=4,HP_PHASES=6};
enum {HP_BEGIN=1,HP_END,HP_READ,HP_LIVE};
enum {HP_CHECK,HP_CAPTURE,HP_BROKER,HP_WAIT,HP_KERNEL,HP_OUTPUT};
struct hp_row {uint64_t count,total_ns,phase_ns[HP_PHASES],max_ns;};
struct hp_locks {uint64_t count,wait_ns,hold_ns,max_wait_ns,max_hold_ns;};
struct hp_report {
    uint64_t actors,pids[HP_ACTORS],allocations,frees,live_blocks,failures,clock_errors;
    uint64_t overhead[32];
    struct hp_row rows[HP_ACTORS][HP_OPS];
    struct hp_locks locks[HP_CPUS];
    uint64_t handoffs,queue_ns,service_ns,resume_ns;
};
#endif
