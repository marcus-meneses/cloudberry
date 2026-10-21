#ifndef CLOUDBERRY_COMMON_HID_REPORT_H
#define CLOUDBERRY_COMMON_HID_REPORT_H
#include <stdint.h>
#include <stddef.h>
enum {HID_REPORT_LIMIT=4096,HID_KEYBOARD=1,HID_MOUSE=2};
struct hid_report_info {uint32_t bits[3][256];uint8_t classes[3][256];uint32_t numbered,input_bytes;};
struct hid_report_item {uint32_t type,id,offset,size,count,flags,classes,collection,parent,collection_type;int64_t minimum,maximum,physical_minimum,physical_maximum;const uint32_t *usages;size_t usage_count;uint32_t usage_min,usage_max;};
typedef void *(*hid_report_alloc)(void *,size_t);
typedef void (*hid_report_free)(void *,void *);
typedef int (*hid_report_visit)(void *,const struct hid_report_item *);
/* Item types: 0 Input, 1 Output, 2 Feature, 3 Collection begin. Collection IDs
 * and parents describe nesting; offsets include constant/unrelated fields.
 * Bounded untrusted descriptor walker. Callback pointers are borrowed for that
 * callback only. Counts/offsets include all collections and padding. */
int hid_report_parse(struct hid_report_info *,const void *,size_t,hid_report_alloc,hid_report_free,void *,hid_report_visit,void *);
uint32_t hid_report_usage(const struct hid_report_item *,uint32_t);
uint32_t hid_report_bits(const unsigned char *,uint32_t,uint32_t);
/* -1 malformed; otherwise class mask, including both for a shared report ID. */
int hid_report_route(const struct hid_report_info *,const void *,size_t);
#endif
