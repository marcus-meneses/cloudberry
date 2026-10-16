#ifndef CLOUDBERRY_USB_DESCRIPTORS_H
#define CLOUDBERRY_USB_DESCRIPTORS_H
#include "usb.h"
struct usb_parsed_interface {struct usb_interface info;size_t first;};
struct usb_configuration {
    unsigned char *raw;size_t bytes,interface_count,endpoint_count;
    uint8_t value,attributes,power,distinct_interfaces;
    struct usb_parsed_interface *interfaces;
    struct usb_endpoint *endpoints;
    unsigned char *configured;
};
struct usb_allocator {void *context;void *(*allocate)(void *,size_t);void (*release)(void *,void *);};
/* Returns 1 valid, 0 malformed/unsupported encoding, -1 allocation failure.
 * Output is unchanged on failure; raw descriptor ownership stays with caller. */
int usb_parse_configuration(unsigned char *,size_t,unsigned,const struct usb_allocator *,struct usb_configuration *);
void usb_free_configuration(const struct usb_allocator *,struct usb_configuration *);
unsigned usb_ep0_packet(unsigned speed,unsigned encoded);
unsigned usb_port_speed(unsigned protocol,unsigned id,const uint32_t *psi,unsigned count);
#endif
