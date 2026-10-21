#ifndef CLOUDBERRY_INTERFACE_CREDENTIALS_H
#define CLOUDBERRY_INTERFACE_CREDENTIALS_H
#include "handle.h"
/* Kernel-owned snapshot. Inline uint64_t supplementary GIDs follow. Generation
 * changes on identity replacement; children receive an independent snapshot. */
struct cb_credentials {uint64_t uid,gid,generation,groups;};
#endif
