#ifndef CLOUDBERRY_INTERFACE_KEYBOARD_H
#define CLOUDBERRY_INTERFACE_KEYBOARD_H
#include "handle.h"
#define CB_KEYBOARD_NAME "/@keyboard"
#define CB_KEYBOARD_INTERFACE_ID {{0x43,0x42,0x4b,0x45,0x59,0x31,0x46,0x3b,0x91,0xe1,0x40,0x0a,0x1c,0x87,0xc0,0x03}}
enum {CB_KEYBOARD_MAJOR=1,CB_KEYBOARD_MINOR=0};
enum {CB_KEYBOARD_READ=1,CB_KEYBOARD_STATE=2};
enum {CB_KEY_DOWN=1,CB_KEY_UP,CB_KEY_REPEAT,CB_KEY_TEXT,CB_KEY_DEVICE_ADDED,CB_KEY_DEVICE_REMOVED,CB_KEY_RESET};
enum {CB_KEY_SYNTHETIC=1};
struct cb_keyboard_event {
    struct cb_version version;
    uint64_t sequence,device_id,timestamp_ns,origin_sequence;
    uint32_t kind,flags;
    uint16_t usage_page,usage;
    uint32_t codepoint,modifiers,reserved;
};
struct cb_keyboard_read {
    struct cb_version version;
    uint64_t provider_epoch,after_sequence;
    uint32_t max_events,reserved;
};
/* Inline event array follows this header; no process-local pointer is stored. */
struct cb_keyboard_batch {
    struct cb_version version;
    uint64_t provider_epoch,next_sequence;
    uint32_t count,flags;
};
struct cb_keyboard_pressed {uint64_t device_id;uint16_t usage_page,usage;uint32_t reserved;};
/* Inline pressed-key array follows; modifiers are included as HID usages. */
struct cb_keyboard_state {
    struct cb_version version;
    uint64_t provider_epoch,last_sequence;
    uint32_t count,flags;
};
static inline int cb_unicode_scalar(uint32_t value){return value<=0x10ffff && !(value>=0xd800 && value<=0xdfff);}
/* Validate a copied known prefix. The decoder separately verifies that the
 * declared event size and the containing batch fit the received buffer. */
static inline int cb_keyboard_event_valid(const struct cb_keyboard_event *event){
    if(!event || event->version.bytes<sizeof(*event) || event->version.major!=CB_KEYBOARD_MAJOR || !event->sequence ||
       event->reserved || (event->flags&~CB_KEY_SYNTHETIC) || event->kind<CB_KEY_DOWN || event->kind>CB_KEY_RESET)return 0;
    if(!event->device_id && event->kind!=CB_KEY_RESET)return 0;
    if(event->kind==CB_KEY_TEXT)return cb_unicode_scalar(event->codepoint);
    if(event->codepoint)return 0;
    return event->kind>CB_KEY_REPEAT || (event->usage_page && event->usage);
}
#endif
