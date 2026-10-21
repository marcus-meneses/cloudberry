#ifndef CLOUDBERRY_MOUSE_HID_H
#define CLOUDBERRY_MOUSE_HID_H
#include <stdint.h>
#include <stddef.h>
enum {MH_X=1,MH_Y,MH_WHEEL,MH_PAN,MH_BUTTON,MH_BUTTON_ARRAY,MH_MULTIPLIER};
struct mouse_hid_field {uint32_t offset,size,count,kind,usage,collection,report_id,feature,relative;int64_t minimum,maximum,physical_minimum,physical_maximum;uint64_t mask;uint32_t *usages,usage_count,usage_min,usage_max,resolution;};
struct mouse_hid_collection {uint32_t parent;};
struct mouse_hid {struct mouse_hid_field *fields;size_t count,capacity;struct mouse_hid_collection *collections;size_t collections_count,collections_capacity;uint32_t bits[3][256],numbered;uint64_t buttons;int64_t x,y;uint32_t flags;
    void *context;void *(*alloc)(void *,size_t);void (*free)(void *,void *);};
struct mouse_hid_input {int64_t dx,dy,x,y,wheel_x,wheel_y;uint64_t buttons,changed;uint32_t flags,resolution_x,resolution_y;};
int mouse_hid_parse(struct mouse_hid *,const void *,size_t,void *(*)(void *,size_t),void (*)(void *,void *),void *);
void mouse_hid_free(struct mouse_hid *);
/* 1 mouse input, 0 well-formed unrelated report, -1 malformed (state unchanged). */
int mouse_hid_decode(struct mouse_hid *,const void *,size_t,struct mouse_hid_input *);
size_t mouse_hid_feature_bytes(const struct mouse_hid *,unsigned);
/* Prepare only standard Resolution Multiplier fields; preserve all other bits. */
int mouse_hid_feature_prepare(const struct mouse_hid *,unsigned,void *,size_t);
void mouse_hid_feature_apply(struct mouse_hid *,unsigned,const void *,size_t);
#endif
