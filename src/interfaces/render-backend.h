#ifndef CLOUDBERRY_INTERFACE_RENDER_BACKEND_H
#define CLOUDBERRY_INTERFACE_RENDER_BACKEND_H
#include "handle.h"
/* Private paired-provider contract. The display provider checks the kernel's
 * current graphics-provider binding; manifest permission alone is insufficient.
 * Applications continue to use /@graphics, not this interface. */
#define CB_RENDER_BACKEND_NAME "/@display.render"
#define CB_RENDER_BACKEND_INTERFACE_ID {{0x43,0x42,0x52,0x45,0x4e,0x31,0x46,0x3b,0x91,0xe1,0x40,0xa,0x1c,0x87,0xc0,0x19}}
enum {CB_RENDER_BACKEND_INFO=1,CB_RENDER_BACKEND_BATCH,CB_RENDER_BACKEND_CREATE,CB_RENDER_BACKEND_RELEASE,CB_RENDER_BACKEND_WRITE,CB_RENDER_BACKEND_COMPOSE,CB_RENDER_BACKEND_PATCH};
enum {CB_RENDER_FILL=1,CB_RENDER_COPY};
struct cb_render_batch {struct cb_version version;uint32_t count,flags;};
struct cb_render_command {uint32_t operation,reserved,x,y,width,height,source_x,source_y,color,reserved2;};
/* BATCH: header and exactly count commands. Reserved fields/flags are zero;
 * FILL uses opaque RGB; COPY is overlap-safe. Validate the whole batch before
 * changing pixels; execute in order and present once. A transport failure can
 * leave a partial batch: outcome UNKNOWN, no automatic replay. No raw VirGL,
 * kernel pointers, arbitrary resource IDs or application ownership bypass. */
/* Cached BGRA surfaces are owned by the calling graphics-provider process.
 * IDs are opaque backend tokens, never native device resource IDs. */
struct cb_render_create {uint32_t width,height;};
struct cb_render_write {uint64_t surface,offset;uint32_t bytes,flags;};
enum {CB_RENDER_WRITE_BEGIN=1,CB_RENDER_WRITE_COMMIT=2};
/* A complete upload starts at offset zero, continues contiguously and commits
 * only at the full backing size. Uncommitted surfaces cannot be composed. */
/* PATCH requires a fully initialized surface. Packed row groups advance offset
 * contiguously within this rectangle; BEGIN/COMMIT bracket the entire update.
 * The texture cannot be composed between chunks. No uninitialized sampling. */
struct cb_render_patch {uint64_t surface,offset;uint32_t x,y,width,height,bytes,flags;};
struct cb_render_layer {uint64_t surface;uint32_t x,y,width,height,source_x,source_y,flags,reserved;};
enum {CB_RENDER_LAYER_ALPHA=1,CB_RENDER_COMPOSE_BEGIN=1,CB_RENDER_COMPOSE_END=2,CB_RENDER_COMPOSE_DAMAGE=4};
/* COMPOSE: cb_render_batch followed by exactly count layers. BEGIN starts a
 * frame with a full-screen opaque base; END presents it. BEGIN|DAMAGE copies
 * the last complete frame into the back target and accepts a clipped opaque
 * base/layers for the damaged rectangle. Unchanged pixels remain intact. Intermediate chunks
 * permit arbitrarily many layers within bounded messages. Validate each chunk
 * before mutation. Public mutations are blocked while a frame is open. */
#endif
