#ifndef CLOUDBERRY_KEYBOARD_REPORT_H
#define CLOUDBERRY_KEYBOARD_REPORT_H
#include "../../common/hid-report.h"
struct keyboard_report_field {struct keyboard_report_field *next;struct hid_report_item item;uint32_t usages[];};
struct keyboard_descriptor {struct hid_report_info info;struct keyboard_report_field *fields;uint8_t pressed[256][32];hid_report_alloc alloc;hid_report_free free;void *context;};
int keyboard_descriptor_parse(struct keyboard_descriptor *,const void *,size_t,hid_report_alloc,hid_report_free,void *);
void keyboard_descriptor_free(struct keyboard_descriptor *);
/* Aggregates state across report IDs; malformed/rollover input never commits. */
int keyboard_descriptor_decode(struct keyboard_descriptor *,const void *,size_t,uint8_t pressed[256]);
void keyboard_descriptor_reset(struct keyboard_descriptor *);
#endif
