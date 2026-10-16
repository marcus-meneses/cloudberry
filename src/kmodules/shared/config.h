#ifndef CLOUDBERRY_CONFIG_H
#define CLOUDBERRY_CONFIG_H
#include "module.h"
enum config_type {CONFIG_STRING=1,CONFIG_BOOL,CONFIG_UINT,CONFIG_GUID};
enum config_error {CONFIG_OK,CONFIG_ARGUMENT,CONFIG_MEMORY,CONFIG_SYNTAX,CONFIG_UNKNOWN,
    CONFIG_DUPLICATE,CONFIG_VALUE,CONFIG_REQUIRED,CONFIG_PUBLICATION,CONFIG_ALREADY_READY};
struct config_guid {uint32_t a;uint16_t b,c;uint8_t d[8];};
/* Immutable, kernel-owned backing until reboot. String bytes include the NUL;
 * bool is uint8_t, uint is uint64_t, GUID uses native fields above (GPT layout).
 * const is a trusted-kernel contract, not heap-page write protection. */
struct config_value {uint16_t major,minor;uint32_t type;uint64_t bytes;const void *data;};
struct config_info {uint64_t count,line;uint32_t ready,error;};
/* init(hub,heap,text,bytes), once on BSP bootstrap, before APs/threads start.
 * Copies all values, atomically publishes config.<key> descriptor references.
 * Failure returns config_error; info(out) gives error and 1-based source line
 * (zero for errors not associated with a line). Failed init may be retried.
 * No raw Limine pointers escape; no mutation/reload surface. */
struct kernel_surface_result config_surface_init(MODULE_ARGS);
struct kernel_surface_result config_surface_info(MODULE_ARGS);
#endif
