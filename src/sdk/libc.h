#ifndef CLOUDBERRY_SDK_LIBC_H
#define CLOUDBERRY_SDK_LIBC_H
#include "runtime.h"
/* Static native-process libc binding. No POSIX privilege/syscall bypass. */
#ifdef __cplusplus
extern "C" {
#endif
struct cb_runtime *cb_libc_runtime(void);
int cb_libc_set_cwd(const char *);
#ifdef __cplusplus
}
#endif
#endif
