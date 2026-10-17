#ifndef CLOUDBERRY_TEST_PROBE_H
#define CLOUDBERRY_TEST_PROBE_H
#include <stdint.h>
struct probe_payload { uint64_t left,right; };
struct probe_thread {uint64_t mode,phase,peer,done,errors,cpu,clock,iterations;uintptr_t address;};
struct probe_irq {uint64_t index,callbacks,notifications,errors;};
struct probe_nvme {uint64_t lba,blocks;uintptr_t buffer;uint64_t done,value,status;};
struct kernel_surfaces;
struct probe_xhci {const struct kernel_surfaces *hub;uint64_t hardware_index,id,done,errors,transfers,interrupts,events,hotplug;};
struct probe_usb_wait {const struct kernel_surfaces *hub;uint64_t id,generation,done,state,status;unsigned char bytes[8];};
struct probe_usb {const struct kernel_surfaces *hub;uint64_t devices,done,errors,hotplug,write;};
struct probe_space {uint64_t root,address,expected,next,phase,command,errors,hold_preemption;};
#endif
