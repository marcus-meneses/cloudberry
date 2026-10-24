#ifndef CLOUDBERRY_GPU_VIRTIO_H
#define CLOUDBERRY_GPU_VIRTIO_H
#include "module.h"
#define GPU_VIRTIO_TABLE "gpu.virtio.devices"
enum {GPU_DISCOVERED=1,GPU_ONLINE,GPU_FAILED,GPU_CLOSED};
enum {GPU_OK,GPU_CONFIG,GPU_OWNERSHIP,GPU_FEATURES,GPU_MEMORY,GPU_IRQ,GPU_TIMEOUT,GPU_PROTOCOL,GPU_DEVICE_ERROR,GPU_CLEANUP};
struct gpu_virtio_info {uint64_t hardware_index,cookie,offered,accepted,commands,interrupts,timeouts,last_fence;uint32_t state,error,scanouts,capsets,resources,contexts,virgl,stage;};
struct gpu_virtio_table {uint32_t bytes;uint16_t major,minor;uint64_t count;const struct gpu_virtio_info *devices;};
struct gpu_virtio_resource {uint64_t cookie;uint32_t id,width,height,reserved;uintptr_t pixels;uint64_t bytes;};
struct gpu_virtio_present {uint64_t cookie;uint32_t resource,scanout,x,y,width,height;};
struct gpu_virtio_capset {uint32_t index,id,version,bytes;};
/* Disk-loaded init(ctx); discover() publishes boot-owned metadata after LIVE.
 * open(hardware_index,out info) may reset GPU firmware queues/scanout; explicit
 * takeover only, never called by normal boot discovery. close(cookie) quiesces
 * DMA and releases resources. The boot release method separately verifies the
 * qualified firmware VGA mode before authorizing fallback.
 * All operations are BSP foreground, one in-flight request per instance, IRQ
 * completion with absolute timeout, failed/uncertain DMA retained until reset.
 * Kernel pointers only. Callers must stop using pixels before dropping them.
 * create(cookie,width,height,out resource), destroy(cookie,id), present(request)
 * with CPU-written B8G8R8X8 pixels. display(cookie,out wire display,capacity).
 * capset(cookie,inout metadata,buffer,capacity); NULL copies required size.
 * context(cookie,0) creates, context(cookie,id) destroys. submit(cookie,context,
 * trusted VirGL dwords,bytes) transports a fenced stream, not an OpenGL API. */
struct kernel_surface_result gpu_virtio_init(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_discover(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_info(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_open(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_close(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_display(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_create(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_destroy(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_present(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_capset(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_context(MODULE_ARGS);
/* VirGL texture foundation: level-0 2D BGRA/BGRX (format1/2), optional CPU
 * backing. texture(cookie,w,h,format,out,host_only); attach(cookie,ctx,res,enable);
 * upload(cookie,ctx,res,optional vg_rect*) copies full backing or a bounded
 * rectangle to a tracked attached texture, preserving full backing stride.
 * Presenting a texture never overwrites it from CPU data; upload is explicit.
 * Broader formats/mips/arrays and OpenGL/Gallium state translation remain above
 * this initial resource contract. No frontend readiness is implied. */
struct kernel_surface_result gpu_virtio_texture(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_attach(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_upload(MODULE_ARGS);
/* readback(cookie,resource,context=0) performs a fenced host-to-guest copy
 * into an attached CPU backing. A nonzero context must own an attachment.
 * Requires VirGL; this observes the GPU resource, not the monitor/scanout. */
struct kernel_surface_result gpu_virtio_readback(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_submit(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_interrupt(MODULE_ARGS);
/* Generic hub gpu.invoke(io_invocation*, kernel-bound display PID) and
 * gpu.exit(PID), implemented by this selected device backend;
 * exit(PID) drains owned devices or retains uncertain DMA and ownership. */
struct kernel_surface_result gpu_virtio_io(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_exit(MODULE_ARGS);

/* Boot selector methods: register-query selection is restored before return;
 * no PCI claim, reset, IRQ, DMA or mode programming occurs during probe. */
struct kernel_surface_result gpu_virtio_probe(MODULE_ARGS);
struct kernel_surface_result gpu_virtio_release(MODULE_ARGS);

#endif
