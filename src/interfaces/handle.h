#ifndef CLOUDBERRY_INTERFACE_HANDLE_H
#define CLOUDBERRY_INTERFACE_HANDLE_H
#include <stdint.h>
#include <stddef.h>
/* Contract definitions, not an implemented application runtime. See README.md.
 * Wire records use fixed-width integers, explicit sizes and inline byte payloads;
 * neither kernel pointers nor provider function addresses cross this boundary. */
typedef uint64_t cb_handle_t;
typedef uint32_t cb_status_t;
#define CB_HANDLE_INVALID UINT64_C(0)
struct cb_interface_id {uint8_t bytes[16];};
struct cb_version {uint32_t bytes;uint16_t major,minor;};
struct cb_request_id {uint64_t namespace_id,serial;};
#define CB_HANDLE_INTERFACE_ID {{0x43,0x42,0x48,0x44,0x4c,0x31,0x46,0x3b,0x91,0xe1,0x40,0x0a,0x1c,0x87,0xc0,0x01}}
#define CB_VOW_INTERFACE_ID {{0x43,0x42,0x56,0x4f,0x57,0x31,0x46,0x3b,0x91,0xe1,0x40,0x0a,0x1c,0x87,0xc0,0x02}}
enum {CB_ABI_MAJOR=1,CB_ABI_MINOR=0};
enum {CB_OK,CB_DENIED,CB_UNAVAILABLE,CB_TIMEOUT,CB_REVOKED,CB_BAD_HANDLE,
      CB_BAD_REQUEST,CB_VERSION_MISMATCH,CB_BUFFER_TOO_SMALL,CB_RESOURCE_FAILURE,
      CB_PROVIDER_FAILED,CB_BUSY,CB_INPUT_RESYNC,CB_NAME_IN_USE};
enum {CB_NOT_DISPATCHED,CB_NOT_COMMITTED,CB_COMMITTED,CB_OUTCOME_UNKNOWN};
enum {CB_HANDLE_DESCRIBE=1,CB_HANDLE_RELEASE=2,CB_HANDLE_REQUEST_STATUS=3};
enum {CB_VOW_AWAIT=1};
struct cb_call_header {
    struct cb_version version;
    struct cb_request_id request;
    cb_handle_t handle;
    struct cb_interface_id interface;
    uint32_t operation,flags;
    uint64_t attempt,deadline_ns,payload_bytes;
};
struct cb_reply_header {
    struct cb_version version;
    struct cb_request_id request;
    cb_status_t status;uint32_t outcome;
    uint64_t provider_epoch,result_bytes,error_bytes;
};
enum {CB_CALL_PROVIDER_DELIVERY=1,CB_CALL_SYSTEM_EVENT=2};
/* receive() only: kernel-owned admission context (UTF-8 JSON) precedes the
 * original payload. invoke() rejects this provider-only flag/context from clients.
 * call.version.bytes is sizeof(cb_delivery_header), not sizeof(cb_call_header). */
struct cb_delivery_header {struct cb_call_header call;uint64_t provider_epoch,context_bytes;};
struct cb_error_header {
    struct cb_version version;
    struct cb_request_id request;
    uint32_t outcome,flags;
    uint64_t original_payload_bytes,trace_bytes;
};
/* installation_id is the OS-assigned provider installation identity, not one
 * machine-wide ID shared by every application from the same signing key. */
struct cb_provider_identity {uint8_t signing_key_hash[32],installation_id[16];};
struct cb_describe_request {struct cb_version version;struct cb_interface_id interface;};
/* Concatenated operation records follow, then a UTF-8 JSON object of granted
 * resource facts. Each operation record has inline UTF-8 name/request/reply JSON
 * schemas. Runtime records include only the granted projection. */
struct cb_description {
    struct cb_version version;struct cb_interface_id interface;
    struct cb_provider_identity provider;uint64_t provider_epoch;
    uint16_t interface_major,interface_minor;uint32_t operation_count;
    uint64_t operations_bytes,facts_bytes;
};
struct cb_operation_description {
    struct cb_version version;uint32_t operation,flags;
    uint64_t name_bytes,request_schema_bytes,reply_schema_bytes;
};
/* Name then description follow. Publication supplies no function address:
 * providers receive admitted calls through the broker's delivery queue. */
struct cb_publication {
    struct cb_version version;struct cb_interface_id interface;
    uint16_t interface_major,interface_minor;uint32_t flags;
    uint64_t name_bytes,description_bytes;
};
/* Inline symbolic location/detail strings follow each frame. Never raw PCs or
 * stack pointers. Broker policy filters provider-supplied trace material. */
struct cb_trace_frame {
    struct cb_version version;struct cb_provider_identity provider;
    struct cb_interface_id interface;struct cb_request_id request;
    uint64_t attempt;uint32_t operation,flags;
    uint64_t location_bytes,detail_bytes,provider_epoch;
};
/* Bounds check before interpreting inline payloads. Transport decoders first
 * copy the known header prefix; they must never cast an untrusted user pointer. */
static inline int cb_frame_fits(uint64_t header,uint64_t first,uint64_t second,uint64_t available){
    return header<=available && first<=available-header && second<=available-header-first;
}
#ifdef __cplusplus
extern "C" {
#endif
/* C system-library entry points, implemented in sdk/system.c.
 * The library marshals these local pointers; the kernel authenticates the caller.
 * call/reply buffers include the headers above; reply_bytes reports required size. */
cb_status_t cb_system_get_handle(const char *name,uint64_t name_bytes,const struct cb_interface_id *interface_id,uint16_t major,uint16_t minimum_minor,cb_handle_t *out);
cb_status_t cb_system_invoke(const void *call,uint64_t call_bytes,void *reply,uint64_t reply_capacity,uint64_t *reply_bytes);
cb_status_t cb_system_release(cb_handle_t handle);
cb_status_t cb_system_publish(const void *publication,uint64_t bytes,cb_handle_t *out);
cb_status_t cb_system_receive(void *call,uint64_t capacity,uint64_t *call_bytes,uint64_t deadline_ns);
cb_status_t cb_system_reply(const void *reply,uint64_t bytes);
#ifdef __cplusplus
}
#endif
#endif
