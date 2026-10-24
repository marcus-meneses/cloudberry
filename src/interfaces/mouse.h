#ifndef CLOUDBERRY_INTERFACE_MOUSE_H
#define CLOUDBERRY_INTERFACE_MOUSE_H
#include "handle.h"
#define CB_MOUSE_NAME "/@mouse"
#define CB_MOUSE_INTERFACE_ID {{0x43,0x42,0x4d,0x4f,0x55,0x53,0x45,0x31,0x91,0xe1,0x40,0x0a,0x1c,0x87,0xc0,0x12}}
enum {CB_MOUSE_READ=1,CB_MOUSE_STATE,CB_MOUSE_CURSOR_INFO,CB_MOUSE_READ_CURSOR,CB_MOUSE_CURSOR_HIDE,CB_MOUSE_CURSOR_SHOW};
enum {CB_MOUSE_INPUT=1,CB_MOUSE_ADDED,CB_MOUSE_REMOVED,CB_MOUSE_RESET,CB_MOUSE_ERROR};
enum {CB_MOUSE_RELATIVE=1,CB_MOUSE_ABSOLUTE=2};
/* Wheel deltas / resolution are detents; retain the fraction across events.
 * Relative x/y are cumulative device counts, absolute x/y are 0..65535.
 * dx/dy contain relative counts. Buttons 1..64 map to bits 0..63.
 * device_id identifies this interface attachment, not a reusable USB address. */
struct cb_mouse_event {
    struct cb_version version;uint64_t sequence,device_id,timestamp_ns;
    uint32_t kind,flags;int64_t dx,dy,x,y,wheel_x,wheel_y;
    uint64_t buttons,changed;uint32_t resolution_x,resolution_y,status,reserved;
};
/* HIDE/SHOW take empty payloads. Only the keyboard-focused process may hide;
 * its hide is idempotent, owner-scoped and automatically restored on exit. */
struct cb_mouse_cursor {uint32_t x,y,width,height,visible,reserved;};
/* READ_CURSOR uses the same request/header with batch minor 1, and these
 * extended records. Legacy READ remains byte-for-byte unchanged. */
struct cb_mouse_cursor_event {struct cb_mouse_event event;struct cb_mouse_cursor cursor;};
struct cb_mouse_read {struct cb_version version;uint64_t provider_epoch,after_sequence;uint32_t max_events,reserved;};
struct cb_mouse_batch {struct cb_version version;uint64_t provider_epoch,next_sequence;uint32_t count,flags;};
struct cb_mouse_device {uint64_t device_id,usb_device,generation;uint32_t interface,flags;int64_t x,y;uint64_t buttons;};
struct cb_mouse_state {struct cb_version version;uint64_t provider_epoch,last_sequence;uint32_t count,reserved;};
/* STATE is cursor-paginated: payload uint64 index, one cb_mouse_device follows
 * the state header; count is 0 or 1. This does not bound the number of devices. */
#endif
