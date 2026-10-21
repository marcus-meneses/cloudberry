#ifndef CLOUDBERRY_INTERFACE_SYSTEM_H
#define CLOUDBERRY_INTERFACE_SYSTEM_H
#include "handle.h"
/* Local x86-64 gate descriptor, copied by the kernel; pointers exist only here,
 * never in provider messages. int80: RAX=1, RDI=&gate. RAX=0 is process exit. */
enum {CB_SYSTEM_ACQUIRE=1,CB_SYSTEM_INVOKE,CB_SYSTEM_RELEASE,CB_SYSTEM_PUBLISH,CB_SYSTEM_RECEIVE,CB_SYSTEM_REPLY,CB_SYSTEM_FORGET,CB_SYSTEM_STANDARD,CB_SYSTEM_SUBMIT,CB_SYSTEM_COLLECT,CB_SYSTEM_WAIT,CB_SYSTEM_ABANDON};
enum {CB_FRAME_LIMIT=16384};
struct cb_system_gate {uint32_t bytes,operation;uint64_t input,input_bytes,output,output_capacity,result_bytes,value,deadline_ns;uint32_t status,reserved;};
struct cb_acquire {struct cb_interface_id interface;uint16_t major,minor;uint32_t reserved;uint64_t name_bytes;};
/* Wait-any observes level readiness without consuming a reply or stream data.
 * Requests are caller-owned submitted provider calls; stream IDs are local caps. */
enum {CB_WAIT_REQUEST=1,CB_WAIT_READ,CB_WAIT_WRITE};
struct cb_wait_item {uint32_t kind,reserved;cb_handle_t handle;struct cb_request_id request;};
/* Abandon is not forced cancellation. A dispatched operation may still commit. */
enum {CB_REQUEST_QUEUED=1,CB_REQUEST_DISPATCHED,CB_REQUEST_COMPLETE};
struct cb_abandon_result {uint32_t phase,outcome;};
#ifdef __cplusplus
extern "C" {
#endif
cb_status_t cb_system_submit(const void *call,uint64_t bytes,struct cb_request_id *request);
/* COLLECT returns transport status; on OK the copied reply owns operation status. */
cb_status_t cb_system_collect(struct cb_request_id request,void *reply,uint64_t capacity,uint64_t *bytes);
cb_status_t cb_system_abandon(struct cb_request_id request,struct cb_abandon_result *result);
cb_status_t cb_system_wait(const struct cb_wait_item *items,uint64_t count,uint64_t deadline_ns,uint64_t *ready);
#ifdef __cplusplus
}
#endif
#endif
