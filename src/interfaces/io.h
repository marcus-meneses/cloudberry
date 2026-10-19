#ifndef CLOUDBERRY_INTERFACE_IO_H
#define CLOUDBERRY_INTERFACE_IO_H
#include "handle.h"
#define CB_IO_NAME "/@io"
#define CB_IO_INTERFACE_ID {{0x43,0x42,0x49,0x4f,0x30,0x31,0x46,0x3b,0x91,0xe1,0x40,0x0a,0x1c,0x87,0xc0,0x05}}
enum {CB_IO_KEYBOARD_SUBSCRIBE=1,CB_IO_KEYBOARD_PROTOCOL,CB_IO_KEYBOARD_ACK,CB_IO_KEYBOARD_CLOSE,CB_IO_NOW_NS,CB_IO_PROCESS_LAUNCH,CB_IO_PROCESS_STOP,CB_IO_PROCESS_REAP,CB_IO_PROCESS_RELEASE,CB_IO_DISPLAY_INFO,CB_IO_DISPLAY_FILL,CB_IO_DISPLAY_BLIT,CB_IO_FILESYSTEM,CB_IO_OWNER_WATCH,CB_IO_DISPLAY_COPY};
enum {CB_IO_KEYBOARD_EVENT=1,CB_IO_PROCESS_EVENT=2,CB_IO_OWNER_EXIT=3};
enum {CB_IO_DEVICE_ADDED=1,CB_IO_REPORT,CB_IO_DEVICE_REMOVED,CB_IO_ERROR};
#include "applications.h"
enum {CB_IO_PROCESS_COMPLETE=1,CB_IO_PROCESS_OWNER_EXIT};
struct cb_io_process_event {uint64_t kind,owner;struct cb_application_status status;};
/* process_launch payload: owner process ID, then cb_application_launch frame. */
struct cb_io_owner_watch {uint64_t process,token,enable;};
struct cb_io_subscription {uint64_t id;};
struct cb_io_protocol {uint64_t id;uint32_t protocol,reserved;};
struct cb_io_keyboard_event {uint64_t subscription,device_id,timestamp_ns;uint32_t kind,status;uint8_t report[8];};
#endif
