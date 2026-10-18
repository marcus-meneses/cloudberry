#ifndef CLOUDBERRY_KEYBOARD_HID_H
#define CLOUDBERRY_KEYBOARD_HID_H
#include "../../interfaces/keyboard.h"
struct keyboard_key {uint32_t codepoint[4];uint8_t present,dead,caps,reserved;};
struct keyboard_layout {struct keyboard_key keys[256];};
struct keyboard_hid {uint8_t pressed[256],modifiers,caps;uint16_t repeat_key;uint32_t accent;uint64_t origin[256];};
typedef uint64_t (*keyboard_emit)(void *,uint32_t,uint16_t,uint32_t,uint32_t,uint64_t,uint32_t);
int keyboard_layout_parse(struct keyboard_layout *,const void *,size_t);
int keyboard_report(struct keyboard_hid *,const struct keyboard_layout *,const uint8_t report[8],keyboard_emit,void *);
void keyboard_repeat(struct keyboard_hid *,const struct keyboard_layout *,keyboard_emit,void *);
void keyboard_reset(struct keyboard_hid *,keyboard_emit,void *);
int keyboard_config_layout(const void *,size_t,const char **,size_t *);
#endif
