#ifndef CLOUDBERRY_INTERFACE_GPU_H
#define CLOUDBERRY_INTERFACE_GPU_H
#include "handle.h"
/* Private display-provider transport over /@io's gpu operation. All fields are
 * values or inline bytes; no DMA, kernel or user addresses cross this ABI.
 * device is a hardware index for INFO/OPEN, an open cookie otherwise.
 * READ/WRITE access CPU backing; READBACK/UPLOAD perform fenced GPU transfers.
 * SUBMIT carries trusted VirGL dwords, NOT a public application OpenGL API. */
enum {CB_GPU_INFO=1,CB_GPU_OPEN,CB_GPU_CLOSE,CB_GPU_CREATE,CB_GPU_TEXTURE,
      CB_GPU_DESTROY,CB_GPU_WRITE,CB_GPU_READ,CB_GPU_PRESENT,CB_GPU_CONTEXT,
      CB_GPU_ATTACH,CB_GPU_UPLOAD,CB_GPU_READBACK,CB_GPU_CAPSET,CB_GPU_SUBMIT,CB_GPU_NEXT,CB_GPU_MODES,CB_GPU_SELECTED};
struct cb_io_gpu_request {
    uint32_t operation,flags;
    uint64_t device;
    uint32_t resource,context,width,height,format,scanout;
    uint64_t offset,bytes;
};
/* CREATE is BGRX; TEXTURE format=1 BGRA or 2 BGRX, flags=1 host-only.
 * WRITE/UPLOAD flags=1 addresses a bounded rectangle: offset is the backing
 * byte offset of its origin, width/height its extent. WRITE payload is tightly
 * packed rows; UPLOAD transfers only that rectangle with the resource's stride.
 * PRESENT uses the full resource. CONTEXT context=0 creates; nonzero destroys.
 * ATTACH flags=1 attaches, flags=0 detaches. CAPSET resource=capset index;
 * bytes=0 queries metadata, otherwise requests data up to that byte count.
 * WRITE/SUBMIT append exactly bytes bytes to the request. READ returns bytes
 * bytes. Other operations have no trailing payload. Unused fields must be zero.
 * OPEN/INFO return cb_io_gpu_info; CREATE/TEXTURE return cb_io_gpu_resource;
 * CONTEXT creation returns uint32 ID; CAPSET returns metadata then capset data. */
struct cb_io_gpu_info {
    uint64_t hardware_index,cookie,offered,accepted,commands,interrupts,timeouts,last_fence;
    uint32_t state,error,scanouts,capsets,resources,contexts,virgl,stage;
};
struct cb_io_gpu_resource {uint32_t id,width,height,format;uint64_t bytes;};
enum {CB_GPU_SCANOUT_LIMIT=16};
struct cb_io_gpu_mode {uint32_t x,y,width,height,enabled,flags;};
struct cb_io_gpu_modes {uint32_t count,reserved;struct cb_io_gpu_mode modes[CB_GPU_SCANOUT_LIMIT];};
/* NEXT: device=UINT64_MAX starts enumeration; otherwise next larger hardware
 * index. SELECTED returns the kernel-qualified boot renderer, or NOT_FOUND.
 * MODES: open cookie, returns fixed modes record; disabled entries remain
 * present. Neither enumeration nor INFO takes over the GPU. */
struct cb_io_gpu_capset {uint32_t index,id,version,bytes;};
#endif
