#ifndef CLOUDBERRY_SDK_RUNTIME_H
#define CLOUDBERRY_SDK_RUNTIME_H
#include "heap.h"
#include "stream.h"
#include "../interfaces/io.h"
/* One instance per user thread (one user thread/process currently). The failure
 * frame is copied unchanged until the next failure: no lossy ID conversion,
 * dropped original payload/trace, hidden retry or implicit provider rebind. */
struct cb_runtime_timer;
struct cb_runtime {struct cb_client client;struct cb_heap heap;cb_handle_t memory,io,filesystem,applications;struct cb_runtime_timer *timers;uint64_t timer_serial;cb_status_t failure;size_t failure_bytes;unsigned char failure_frame[CB_FRAME_LIMIT];};
#ifdef __cplusplus
extern "C" {
#endif
void cb_runtime_init(struct cb_runtime *);
cb_status_t cb_runtime_close(struct cb_runtime *,cb_handle_t);
cb_status_t cb_runtime_now(struct cb_runtime *,uint64_t *);
cb_status_t cb_runtime_utc(struct cb_runtime *,struct cb_io_utc *);
cb_status_t cb_runtime_delay(struct cb_runtime *,uint64_t nanoseconds);
cb_status_t cb_runtime_random(struct cb_runtime *,void *,size_t);
cb_status_t cb_runtime_open(struct cb_runtime *,const char *cwd,const char *path,unsigned mode,cb_handle_t *);
cb_status_t cb_runtime_read(struct cb_runtime *,cb_handle_t,void *,size_t,size_t *,unsigned flags);
cb_status_t cb_runtime_write(struct cb_runtime *,cb_handle_t,const void *,size_t,size_t *,unsigned flags);
cb_status_t cb_runtime_write_all(struct cb_runtime *,cb_handle_t,const void *,size_t);
cb_status_t cb_runtime_load(struct cb_runtime *,const char *,const char *,unsigned char **,size_t *);
cb_status_t cb_runtime_launch(struct cb_runtime *,const char *,size_t,const char *const *,const char *,const cb_handle_t[3],uint64_t *);
cb_status_t cb_runtime_child(struct cb_runtime *,unsigned operation,uint64_t pid,uint64_t deadline,struct cb_application_status *);
/* Async provider calls use owned cb_client requests and existing wait-any;
 * kernel stream I/O uses nonblocking calls plus CB_WAIT_READ/WRITE readiness. */
cb_status_t cb_runtime_submit(struct cb_runtime *,struct cb_client *,cb_handle_t,struct cb_interface_id,unsigned,const void *,size_t);
cb_status_t cb_runtime_collect(struct cb_runtime *,struct cb_client *);
/* One-shot local callbacks; absolute monotonic deadlines. Poll dispatches at
 * most budget callbacks, then returns (ready=UINT64_MAX for timer-only work).
 * Callbacks may add/cancel timers. IDs are not reused during this runtime. */
cb_status_t cb_runtime_timer(struct cb_runtime *,uint64_t deadline,void (*)(void *),void *,uint64_t *id);
cb_status_t cb_runtime_cancel_timer(struct cb_runtime *,uint64_t id);
cb_status_t cb_runtime_poll(struct cb_runtime *,const struct cb_wait_item *,size_t count,uint64_t deadline,size_t budget,uint64_t *ready);
cb_status_t cb_runtime_destroy(struct cb_runtime *);
#ifdef __cplusplus
}
#endif
#endif
