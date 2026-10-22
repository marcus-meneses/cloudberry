#ifndef CLOUDBERRY_INTERFACE_IO_H
#define CLOUDBERRY_INTERFACE_IO_H
#include "handle.h"
#include "credentials.h"
#include "sessions.h"
#include "registry.h"
#define CB_IO_NAME "/@io"
#define CB_IO_INTERFACE_ID {{0x43,0x42,0x49,0x4f,0x30,0x31,0x46,0x3b,0x91,0xe1,0x40,0x0a,0x1c,0x87,0xc0,0x05}}
enum {CB_IO_KEYBOARD_SUBSCRIBE=1,CB_IO_KEYBOARD_PROTOCOL,CB_IO_KEYBOARD_ACK,CB_IO_KEYBOARD_CLOSE,CB_IO_NOW_NS,CB_IO_PROCESS_LAUNCH,CB_IO_PROCESS_STOP,CB_IO_PROCESS_REAP,CB_IO_PROCESS_RELEASE,CB_IO_DISPLAY_INFO,CB_IO_DISPLAY_FILL,CB_IO_DISPLAY_BLIT,CB_IO_FILESYSTEM,CB_IO_OWNER_WATCH,CB_IO_DISPLAY_COPY,CB_IO_HID_SUBSCRIBE,CB_IO_HID_LIST,CB_IO_HID_OPEN,CB_IO_HID_DESCRIPTOR,CB_IO_HID_START,CB_IO_HID_ACK,CB_IO_HID_CLOSE,CB_IO_HID_GET_FEATURE,CB_IO_HID_SET_FEATURE,CB_IO_PROCESS_SAMPLE,CB_IO_CREDENTIALS,CB_IO_IDENTITY_SET,CB_IO_RANDOM,CB_IO_SESSION,CB_IO_INPUT_ANCESTOR,CB_IO_INPUT_FOCUS_SET,CB_IO_INPUT_FOCUS,CB_IO_IDENTITY_HOME,CB_IO_PROCESS_DATA,CB_IO_CONSOLE_ACCESS,CB_IO_REGISTRY,CB_IO_PROCESS_TERMINATE};
/* credentials: own PID=0 for ordinary callers; identity provider may query any
 * PID. identity_set: target PID then credential snapshot, provider-only.
 * random: requested uint64 byte count (<=64); hardware entropy or fail closed. */
/* session: {pid,operation,exclude-on-close}, provider-only except self query (pid=0,op=0).
 * Operations 4/5 use process as a nonzero UID (revoke/status), sessions only.
 * input_ancestor: {ancestor,descendant}, keyboard only, returns uint64 boolean.
 * input_focus_set: {pid,generation}, keyboard only. input_focus: caller PID,
 * mouse/keyboard only, returns {focus PID,generation,focus SID,caller SID}. */
/* identity_home: PID, cb_credentials, home byte count, groups then canonical
 * home bytes (no NUL). Atomic identity/home replacement; identity provider only. */
struct cb_io_session {uint64_t process,operation,exclude;};
struct cb_io_input_focus {uint64_t process,generation,session,caller_session;};
enum {CB_IO_KEYBOARD_EVENT=1,CB_IO_PROCESS_EVENT=2,CB_IO_OWNER_EXIT=3,CB_IO_HID_EVENT=4};
enum {CB_IO_DEVICE_ADDED=1,CB_IO_REPORT,CB_IO_DEVICE_REMOVED,CB_IO_ERROR};
#include "applications.h"
enum {CB_IO_PROCESS_COMPLETE=1,CB_IO_PROCESS_OWNER_EXIT};
struct cb_io_process_event {uint64_t kind,owner;struct cb_application_status status;};
/* process_launch payload: owner process ID, then cb_application_launch frame. */
struct cb_io_owner_watch {uint64_t process,token,enable;};
struct cb_io_subscription {uint64_t id;};
struct cb_io_protocol {uint64_t id;uint32_t protocol,reserved;};
struct cb_io_keyboard_event {uint64_t subscription,device_id,timestamp_ns;uint32_t kind,status;uint8_t report[8];};
/* Generic HID transport, restricted to catalog-bound class providers. No report
 * key/axis decoding is performed by this bridge. Descriptor collection metadata
 * routes input reports to catalog-bound class consumers; one interface may serve
 * both. Capture size is the descriptor-wide maximum; each consumer ACKs its own
 * copied mailbox. Legacy keyboard operation numbers remain reserved. */
enum {CB_HID_LIMIT=4096,CB_IO_HID_CHANGED=5};
struct cb_io_hid_list {uint64_t subscription,device;uint32_t ordinal,reserved;};
struct cb_io_hid_device {uint64_t device,generation;uint32_t interface,ordinal;uint16_t vendor,product;uint8_t subclass,protocol;uint16_t reserved;};
struct cb_io_hid_open {uint64_t subscription,device,generation;uint32_t interface,reserved;};
struct cb_io_hid_opened {uint64_t connection;uint32_t descriptor_bytes,report_bytes;};
struct cb_io_hid_capture {uint64_t connection;uint32_t bytes,reserved;};
struct cb_io_hid_data {uint64_t connection;uint32_t offset,bytes;};
struct cb_io_hid_feature {uint64_t connection;uint32_t report_id,bytes;};
struct cb_io_hid_event {uint64_t subscription,connection,device,generation,timestamp_ns;uint32_t kind,status,bytes,reserved;};
/* Descriptor/report/feature bytes follow their header, never kernel pointers. */
/* Only installed rendering providers may check a delivery actor. The kernel
 * owns focus/session facts; caller-supplied PID from ordinary apps is denied. */
enum {CB_CONSOLE_TEXT=1,CB_CONSOLE_GRAPHICS,CB_CONSOLE_DISPLAY};
struct cb_io_console_access {uint64_t process;uint32_t layer,reserved;};
#endif
